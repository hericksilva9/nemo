/* nemo-preview-pane.c
 *
 * Copyright (C) 2026 Linux Mint
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "nemo-preview-pane.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gst/gst.h>
#include <gtksourceview/gtksource.h>
#include <xreader-document.h>
#include <xreader-view.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-icon-info.h>

#define TEXT_PREVIEW_BYTES (64 * 1024)
#define MAX_ICON_SIZE 256

#define FILE_ATTRIBUTES (NEMO_FILE_ATTRIBUTE_INFO | \
                         NEMO_FILE_ATTRIBUTE_DIRECTORY_ITEM_COUNT)

struct _NemoPreviewPane {
    GtkBox parent_instance;

    GtkWidget *stack;
    GtkWidget *image;
    GtkWidget *text_view;
    GtkWidget *message;
    GtkWidget *info_box;
    GtkWidget *name_label;
    GtkWidget *type_label;
    GtkWidget *size_label;
    GtkWidget *modified_label;
    GtkWidget *dimensions_title;
    GtkWidget *dimensions_label;
    GtkWidget *pages_title;
    GtkWidget *pages_label;
    GtkWidget *document_view;
    GtkWidget *media_box;
    GtkWidget *video_frame;
    GtkWidget *video_widget;
    GtkWidget *audio_image;
    GtkWidget *play_button;
    GtkWidget *seek_scale;
    GtkWidget *time_label;

    NemoView *view;
    NemoFile *file;
    GCancellable *cancellable;
    guint update_id;
    gboolean showing_icon;
    gint load_width;
    gint load_height;
    EvJob *document_job;

    GstElement *player;
    guint bus_watch_id;
    guint position_id;
    gboolean media_is_video;
    gboolean gl_confirmed;
    gboolean gl_failed;
};

G_DEFINE_TYPE (NemoPreviewPane, nemo_preview_pane, GTK_TYPE_BOX)

/* Mime types the document backends can open, filled on first use. */
static GHashTable *document_types = NULL;

static gboolean
is_document_type (const gchar *mime_type)
{
    if (document_types == NULL) {
        GList *infos, *l;

        document_types = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        infos = ev_backends_manager_get_all_types_info ();

        for (l = infos; l != NULL; l = l->next) {
            EvTypeInfo *info = l->data;
            gint i;

            for (i = 0; info->mime_types[i] != NULL; i++) {
                g_hash_table_add (document_types, g_strdup (info->mime_types[i]));
            }
        }

        /* The infos belong to the backends manager. */
        g_list_free (infos);
    }

    /* Images already have their own preview, and epub needs a web view the
     * backend brings in on its own. */
    return !g_str_has_prefix (mime_type, "image/") &&
           g_strcmp0 (mime_type, "application/epub+zip") != 0 &&
           g_hash_table_contains (document_types, mime_type);
}

static void
get_preview_area (NemoPreviewPane *self,
                  gint            *width,
                  gint            *height)
{
    *width = MAX (gtk_widget_get_allocated_width (self->stack), 128);
    *height = MAX (gtk_widget_get_allocated_height (self->stack), 128);
}

static void
set_image_pixbuf (NemoPreviewPane *self,
                  GdkPixbuf       *pixbuf)
{
    cairo_surface_t *surface;

    surface = gdk_cairo_surface_create_from_pixbuf (pixbuf,
                                                    gtk_widget_get_scale_factor (GTK_WIDGET (self)),
                                                    NULL);
    gtk_image_set_from_surface (GTK_IMAGE (self->image), surface);
    cairo_surface_destroy (surface);

    gtk_stack_set_visible_child (GTK_STACK (self->stack), self->image);
}

static void
show_icon (NemoPreviewPane *self)
{
    GdkPixbuf *pixbuf;
    gint width, height, size;

    get_preview_area (self, &width, &height);
    size = MIN (MIN (width, height), MAX_ICON_SIZE);

    /* Thumbnails come from the same thumbnailers the views use, which covers
     * documents and videos without any renderer of our own. */
    pixbuf = nemo_file_get_icon_pixbuf (self->file, size, TRUE,
                                        gtk_widget_get_scale_factor (GTK_WIDGET (self)),
                                        NEMO_FILE_ICON_FLAGS_USE_THUMBNAILS);
    if (pixbuf != NULL) {
        set_image_pixbuf (self, pixbuf);
        g_object_unref (pixbuf);
    }

    self->showing_icon = TRUE;
}

static void
show_message (NemoPreviewPane *self,
              const gchar     *message)
{
    gtk_label_set_text (GTK_LABEL (self->message), message);
    gtk_stack_set_visible_child (GTK_STACK (self->stack), self->message);
    gtk_widget_hide (self->info_box);
}

static void
update_info (NemoPreviewPane *self)
{
    gchar *text;

    text = nemo_file_get_display_name (self->file);
    gtk_label_set_text (GTK_LABEL (self->name_label), text);
    g_free (text);

    text = nemo_file_get_string_attribute (self->file, "detailed_type");
    gtk_label_set_text (GTK_LABEL (self->type_label), text != NULL ? text : "");
    g_free (text);

    /* For folders this is the item count. */
    text = nemo_file_get_string_attribute (self->file, "size");
    gtk_label_set_text (GTK_LABEL (self->size_label), text != NULL ? text : "");
    g_free (text);

    text = nemo_file_get_string_attribute (self->file, "date_modified");
    gtk_label_set_text (GTK_LABEL (self->modified_label), text != NULL ? text : "");
    g_free (text);

    gtk_widget_show (self->info_box);
}

static void
image_loaded_cb (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    NemoPreviewPane *self = user_data;
    GdkPixbuf *pixbuf, *oriented;
    GError *error = NULL;

    pixbuf = gdk_pixbuf_new_from_stream_finish (res, &error);

    if (pixbuf == NULL) {
        /* On cancellation self may already be gone. Otherwise the format is
         * one we can't load, and the icon already shown stays. */
        g_error_free (error);
        return;
    }

    oriented = gdk_pixbuf_apply_embedded_orientation (pixbuf);
    set_image_pixbuf (self, oriented);
    self->showing_icon = FALSE;

    g_object_unref (oriented);
    g_object_unref (pixbuf);
}

static void
image_read_cb (GObject      *source,
               GAsyncResult *res,
               gpointer      user_data)
{
    NemoPreviewPane *self = user_data;
    GFileInputStream *stream;

    stream = g_file_read_finish (G_FILE (source), res, NULL);

    if (stream == NULL) {
        return;
    }

    gdk_pixbuf_new_from_stream_at_scale_async (G_INPUT_STREAM (stream),
                                               self->load_width, self->load_height, TRUE,
                                               self->cancellable,
                                               image_loaded_cb, self);
    g_object_unref (stream);
}

static void
load_image (NemoPreviewPane *self,
            gint             image_width,
            gint             image_height)
{
    GFile *location;
    gint width, height, scale;

    get_preview_area (self, &width, &height);
    scale = gtk_widget_get_scale_factor (GTK_WIDGET (self));

    /* Fit the pane, but never blow a small image up past its own size. */
    self->load_width = width * scale;
    self->load_height = height * scale;

    if (image_width > 0 && image_height > 0 &&
        image_width <= self->load_width && image_height <= self->load_height) {
        self->load_width = image_width;
        self->load_height = image_height;
    }

    location = nemo_file_get_location (self->file);
    g_file_read_async (location, G_PRIORITY_DEFAULT, self->cancellable, image_read_cb, self);
    g_object_unref (location);
}

static void
image_info_cb (GObject      *source,
               GAsyncResult *res,
               gpointer      user_data)
{
    NemoPreviewPane *self = user_data;
    GdkPixbufFormat *format;
    gint width, height;
    gchar *text;

    format = gdk_pixbuf_get_file_info_finish (res, &width, &height, NULL);

    if (format == NULL) {
        return;
    }

    text = g_strdup_printf ("%d \u00d7 %d", width, height);
    gtk_label_set_text (GTK_LABEL (self->dimensions_label), text);
    g_free (text);

    gtk_widget_show (self->dimensions_title);
    gtk_widget_show (self->dimensions_label);

    load_image (self, width, height);
}

static void
start_image (NemoPreviewPane *self)
{
    GFile *location;
    gchar *path;

    location = nemo_file_get_location (self->file);
    path = g_file_get_path (location);

    /* The header gives the size without decoding the image. Without a local
     * path there is no reading it on its own, so the image is just loaded. */
    if (path != NULL) {
        gdk_pixbuf_get_file_info_async (path, self->cancellable, image_info_cb, self);
    } else {
        load_image (self, 0, 0);
    }

    g_free (path);
    g_object_unref (location);
}

static GtkSourceLanguage *
guess_language (NemoFile *file)
{
    GtkSourceLanguage *language;
    gchar *name, *mime_type;

    name = nemo_file_get_display_name (file);
    mime_type = nemo_file_get_mime_type (file);

    language = gtk_source_language_manager_guess_language (gtk_source_language_manager_get_default (),
                                                           name, mime_type);

    g_free (name);
    g_free (mime_type);

    return language;
}

/* Colors code the way the text editor does, when it is installed. */
static GtkSourceStyleScheme *
get_style_scheme (void)
{
    GtkSourceStyleSchemeManager *manager;
    GtkSourceStyleScheme *scheme = NULL;
    GSettingsSchema *schema;
    gboolean prefer_dark = FALSE;
    gchar *theme = NULL;

    manager = gtk_source_style_scheme_manager_get_default ();

    schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
                                              "org.x.editor.preferences.editor", TRUE);
    if (schema != NULL) {
        GSettings *settings;
        gchar *id;

        settings = g_settings_new_full (schema, NULL, NULL);
        id = g_settings_get_string (settings, "scheme");
        scheme = gtk_source_style_scheme_manager_get_scheme (manager, id);

        g_free (id);
        g_object_unref (settings);
        g_settings_schema_unref (schema);
    }

    if (scheme != NULL) {
        return scheme;
    }

    g_object_get (gtk_settings_get_default (),
                  "gtk-application-prefer-dark-theme", &prefer_dark,
                  "gtk-theme-name", &theme,
                  NULL);

    if (theme != NULL) {
        gchar *lower = g_ascii_strdown (theme, -1);

        prefer_dark |= g_strrstr (lower, "dark") != NULL;
        g_free (lower);
    }

    g_free (theme);

    return gtk_source_style_scheme_manager_get_scheme (manager,
                                                       prefer_dark ? "oblivion" : "classic");
}

static void
text_loaded_cb (GObject      *source,
                GAsyncResult *res,
                gpointer      user_data)
{
    NemoPreviewPane *self = user_data;
    GBytes *bytes;
    const gchar *data, *end;
    gchar *converted = NULL;
    GtkTextBuffer *buffer;
    gsize length;

    bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (source), res, NULL);

    if (bytes == NULL) {
        return;
    }

    data = g_bytes_get_data (bytes, &length);

    /* A character cut in half at the read limit is the only invalid UTF-8
     * allowed through.  Other than that, text files older than UTF-8 (bank
     * statements, say) are nearly always Windows-1252, so those are read as
     * that; a NUL byte means it isn't text at all, and the icon stays. */
    if (!g_utf8_validate (data, length, &end) && (data + length) - end >= 4) {
        if (memchr (data, '\0', length) == NULL) {
            converted = g_convert (data, length, "UTF-8", "WINDOWS-1252", NULL, NULL, NULL);
        }

        if (converted == NULL) {
            g_bytes_unref (bytes);
            return;
        }

        data = converted;
        end = converted + strlen (converted);
    }

    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->text_view));
    gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (buffer), guess_language (self->file));
    gtk_text_buffer_set_text (buffer, data, end - data);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "text");
    self->showing_icon = FALSE;

    g_free (converted);
    g_bytes_unref (bytes);
}

static void
text_read_cb (GObject      *source,
              GAsyncResult *res,
              gpointer      user_data)
{
    NemoPreviewPane *self = user_data;
    GFileInputStream *stream;

    stream = g_file_read_finish (G_FILE (source), res, NULL);

    if (stream == NULL) {
        return;
    }

    g_input_stream_read_bytes_async (G_INPUT_STREAM (stream), TEXT_PREVIEW_BYTES,
                                     G_PRIORITY_DEFAULT, self->cancellable,
                                     text_loaded_cb, self);
    g_object_unref (stream);
}

static void
clear_document_job (NemoPreviewPane *self)
{
    if (self->document_job == NULL) {
        return;
    }

    g_signal_handlers_disconnect_by_data (self->document_job, self);
    ev_job_cancel (self->document_job);
    g_clear_object (&self->document_job);
}

static void
document_loaded_cb (EvJob           *job,
                    NemoPreviewPane *self)
{
    EvDocumentModel *model;
    gchar *text;

    if (ev_job_is_failed (job)) {
        clear_document_job (self);
        return;
    }

    model = ev_document_model_new_with_document (job->document);
    ev_document_model_set_sizing_mode (model, EV_SIZING_FIT_WIDTH);
    ev_document_model_set_continuous (model, TRUE);
    ev_view_set_model (EV_VIEW (self->document_view), model);
    g_object_unref (model);

    text = g_strdup_printf ("%d", ev_document_get_n_pages (job->document));
    gtk_label_set_text (GTK_LABEL (self->pages_label), text);
    g_free (text);

    gtk_widget_show (self->pages_title);
    gtk_widget_show (self->pages_label);

    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "document");
    self->showing_icon = FALSE;

    clear_document_job (self);
}

static void
start_document (NemoPreviewPane *self)
{
    gchar *uri;

    uri = nemo_file_get_uri (self->file);
    self->document_job = ev_job_load_new (uri);
    g_free (uri);

    g_signal_connect (self->document_job, "finished",
                      G_CALLBACK (document_loaded_cb), self);
    ev_job_scheduler_push_job (self->document_job, EV_JOB_PRIORITY_NONE);
}

static void
set_media_playing (NemoPreviewPane *self,
                   gboolean         playing);

static gchar *
format_time (gint64 nanoseconds)
{
    gint64 seconds = MAX (nanoseconds, 0) / GST_SECOND;

    if (seconds >= 3600) {
        return g_strdup_printf ("%d:%02d:%02d", (gint) (seconds / 3600),
                                (gint) (seconds / 60 % 60), (gint) (seconds % 60));
    }

    return g_strdup_printf ("%d:%02d", (gint) (seconds / 60), (gint) (seconds % 60));
}

static gboolean
update_media_position (gpointer user_data)
{
    NemoPreviewPane *self = user_data;
    gint64 position = 0, duration = 0;
    gchar *position_text, *duration_text, *text;

    gst_element_query_position (self->player, GST_FORMAT_TIME, &position);
    gst_element_query_duration (self->player, GST_FORMAT_TIME, &duration);

    if (duration > 0) {
        gtk_range_set_range (GTK_RANGE (self->seek_scale), 0, (gdouble) duration / GST_SECOND);
    }

    gtk_range_set_value (GTK_RANGE (self->seek_scale), (gdouble) position / GST_SECOND);

    position_text = format_time (position);
    duration_text = format_time (duration);
    text = g_strdup_printf ("%s / %s", position_text, duration_text);
    gtk_label_set_text (GTK_LABEL (self->time_label), text);

    g_free (text);
    g_free (position_text);
    g_free (duration_text);

    return G_SOURCE_CONTINUE;
}

static void
set_media_playing (NemoPreviewPane *self,
                   gboolean         playing)
{
    gtk_button_set_image (GTK_BUTTON (self->play_button),
                          gtk_image_new_from_icon_name (playing ? "xsi-media-playback-pause-symbolic"
                                                                : "xsi-media-playback-start-symbolic",
                                                        GTK_ICON_SIZE_BUTTON));

    g_clear_handle_id (&self->position_id, g_source_remove);

    if (playing) {
        self->position_id = g_timeout_add (250, update_media_position, self);
    }

    gst_element_set_state (self->player, playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
}

static void
stop_media (NemoPreviewPane *self)
{
    if (self->player == NULL) {
        return;
    }

    g_clear_handle_id (&self->position_id, g_source_remove);
    gst_element_set_state (self->player, GST_STATE_NULL);
}

static void
show_media (NemoPreviewPane *self)
{
    if (self->media_is_video) {
        gtk_widget_show (self->video_frame);
        gtk_widget_hide (self->audio_image);
    } else {
        GdkPixbuf *pixbuf;

        pixbuf = nemo_file_get_icon_pixbuf (self->file, NEMO_ICON_SIZE_LARGER, TRUE,
                                            gtk_widget_get_scale_factor (GTK_WIDGET (self)),
                                            NEMO_FILE_ICON_FLAGS_USE_THUMBNAILS);
        gtk_image_set_from_pixbuf (GTK_IMAGE (self->audio_image), pixbuf);
        g_clear_object (&pixbuf);

        gtk_widget_hide (self->video_frame);
        gtk_widget_show (self->audio_image);
    }

    update_media_position (self);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "media");
    self->showing_icon = FALSE;
}

static void start_media (NemoPreviewPane *self,
                         gboolean         is_video);

/* The frame is kept to the video's own shape, so what surrounds the picture
 * is the pane rather than bars the sink would paint black. */
static void
update_video_aspect (NemoPreviewPane *self)
{
    GstPad *pad = NULL;
    GstCaps *caps;
    GstStructure *structure;
    gint width, height, par_n = 1, par_d = 1;

    g_signal_emit_by_name (self->player, "get-video-pad", 0, &pad);

    if (pad == NULL) {
        return;
    }

    caps = gst_pad_get_current_caps (pad);

    if (caps != NULL) {
        structure = gst_caps_get_structure (caps, 0);

        if (gst_structure_get_int (structure, "width", &width) &&
            gst_structure_get_int (structure, "height", &height) &&
            width > 0 && height > 0) {
            gst_structure_get_fraction (structure, "pixel-aspect-ratio", &par_n, &par_d);
            gtk_aspect_frame_set (GTK_ASPECT_FRAME (self->video_frame), 0.5, 0.5,
                                  (gfloat) width * par_n / (height * par_d), FALSE);
        }

        gst_caps_unref (caps);
    }

    gst_object_unref (pad);
}

/* Only errors from the GL elements say anything about GL: a file that
 * fails to decode fails the same with either sink. */
static gboolean
is_gl_element (GstObject *object)
{
    GstElementFactory *factory;

    if (!GST_IS_ELEMENT (object)) {
        return FALSE;
    }

    factory = gst_element_get_factory (GST_ELEMENT (object));

    return factory != NULL &&
           (g_str_has_prefix (GST_OBJECT_NAME (factory), "gl") ||
            g_str_has_prefix (GST_OBJECT_NAME (factory), "gtkgl"));
}

static void
destroy_player (NemoPreviewPane *self)
{
    if (self->player == NULL) {
        return;
    }

    stop_media (self);
    g_clear_handle_id (&self->bus_watch_id, g_source_remove);
    gtk_container_remove (GTK_CONTAINER (self->video_frame), self->video_widget);
    self->video_widget = NULL;
    g_clear_pointer (&self->player, gst_object_unref);
}

static gboolean
player_bus_cb (GstBus     *bus,
               GstMessage *message,
               gpointer    user_data)
{
    NemoPreviewPane *self = user_data;

    if (GST_MESSAGE_SRC (message) != GST_OBJECT (self->player) &&
        GST_MESSAGE_TYPE (message) != GST_MESSAGE_ERROR) {
        return G_SOURCE_CONTINUE;
    }

    switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ASYNC_DONE:
            self->gl_confirmed = TRUE;

            if (self->media_is_video) {
                update_video_aspect (self);
            }

            /* The first frame is ready: until then the thumbnail stays. */
            if (self->file != NULL && self->showing_icon) {
                show_media (self);
            }
            break;
        case GST_MESSAGE_EOS:
            set_media_playing (self, FALSE);
            gst_element_seek_simple (self->player, GST_FORMAT_TIME,
                                     GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 0);
            update_media_position (self);
            break;
        case GST_MESSAGE_ERROR:
            /* A GL sink that has never shown a frame most likely has no GL to
             * work with, so the file is retried with the plain sink. */
            if (!self->gl_confirmed && !self->gl_failed && self->file != NULL &&
                is_gl_element (GST_MESSAGE_SRC (message))) {
                self->gl_failed = TRUE;
                destroy_player (self);
                start_media (self, self->media_is_video);
                break;
            }

            /* No decoder for it, most likely; the icon stays. */
            stop_media (self);
            if (self->file != NULL && !self->showing_icon) {
                show_icon (self);
            }
            break;
        default:
            break;
    }

    return G_SOURCE_CONTINUE;
}

static gboolean
ensure_player (NemoPreviewPane *self)
{
    GstElement *sink, *widget_sink;
    GstBus *bus;

    if (self->player != NULL) {
        return TRUE;
    }

    self->player = gst_element_factory_make ("playbin", NULL);

    if (self->player == NULL) {
        return FALSE;
    }

    gst_object_ref_sink (self->player);

    /* Scaling on the GPU costs a fraction of doing it per frame on the CPU. */
    sink = NULL;
    widget_sink = NULL;

    if (!self->gl_failed) {
        widget_sink = gst_element_factory_make ("gtkglsink", NULL);
        sink = gst_element_factory_make ("glsinkbin", NULL);

        if (widget_sink != NULL && sink != NULL) {
            g_object_set (sink, "sink", widget_sink, NULL);
        } else {
            g_clear_pointer (&widget_sink, gst_object_unref);
            g_clear_pointer (&sink, gst_object_unref);
        }
    }

    if (sink == NULL) {
        self->gl_confirmed = TRUE;
        sink = widget_sink = gst_element_factory_make ("gtksink", NULL);
    } else {
        self->gl_confirmed = FALSE;
    }

    if (sink == NULL) {
        g_clear_pointer (&self->player, gst_object_unref);
        return FALSE;
    }

    g_object_get (widget_sink, "widget", &self->video_widget, NULL);
    gtk_container_add (GTK_CONTAINER (self->video_frame), self->video_widget);
    gtk_widget_show (self->video_widget);
    g_object_unref (self->video_widget);

    g_object_set (self->player, "video-sink", sink, NULL);

    bus = gst_element_get_bus (self->player);
    self->bus_watch_id = gst_bus_add_watch (bus, player_bus_cb, self);
    gst_object_unref (bus);

    return TRUE;
}

static void
start_media (NemoPreviewPane *self,
             gboolean         is_video)
{
    gchar *uri;

    if (!ensure_player (self)) {
        return;
    }

    self->media_is_video = is_video;

    uri = nemo_file_get_uri (self->file);
    g_object_set (self->player, "uri", uri, NULL);
    g_free (uri);

    gtk_range_set_range (GTK_RANGE (self->seek_scale), 0, 1);
    set_media_playing (self, FALSE);
}

static void
play_button_clicked_cb (NemoPreviewPane *self)
{
    GstState state;

    gst_element_get_state (self->player, &state, NULL, 0);
    set_media_playing (self, state != GST_STATE_PLAYING);
}

static gboolean
seek_scale_change_value_cb (GtkRange        *range,
                            GtkScrollType    scroll,
                            gdouble          value,
                            NemoPreviewPane *self)
{
    gst_element_seek_simple (self->player, GST_FORMAT_TIME,
                             GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                             (gint64) (value * GST_SECOND));

    return FALSE;
}

static void
file_ready_cb (NemoFile *file,
               gpointer  user_data)
{
    NemoPreviewPane *self = user_data;
    gchar *mime_type;

    update_info (self);

    /* The icon stands in until the real preview, if any, has loaded. */
    show_icon (self);

    if (nemo_file_is_directory (file)) {
        return;
    }

    mime_type = nemo_file_get_mime_type (file);

    if (g_str_has_prefix (mime_type, "image/")) {
        start_image (self);
    } else if (g_str_has_prefix (mime_type, "video/") ||
               g_str_has_prefix (mime_type, "audio/")) {
        start_media (self, g_str_has_prefix (mime_type, "video/"));
    } else if (is_document_type (mime_type)) {
        start_document (self);
    } else if (g_content_type_is_a (mime_type, "text/plain")) {
        GFile *location = nemo_file_get_location (file);

        g_file_read_async (location, G_PRIORITY_DEFAULT, self->cancellable, text_read_cb, self);
        g_object_unref (location);
    }

    g_free (mime_type);
}

static void
file_changed_cb (NemoFile        *file,
                 NemoPreviewPane *self)
{
    if (!nemo_file_check_if_ready (file, FILE_ATTRIBUTES)) {
        return;
    }

    update_info (self);

    /* Picks up a thumbnail that finished after the file was selected. */
    if (self->showing_icon) {
        show_icon (self);
    }
}

static void
clear_file (NemoPreviewPane *self)
{
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
    clear_document_job (self);
    stop_media (self);

    if (self->file == NULL) {
        return;
    }

    nemo_file_cancel_call_when_ready (self->file, file_ready_cb, self);
    nemo_file_monitor_remove (self->file, self);
    g_signal_handlers_disconnect_by_func (self->file, file_changed_cb, self);
    g_clear_pointer (&self->file, nemo_file_unref);
}

static void
set_file (NemoPreviewPane *self,
          NemoFile        *file)
{
    clear_file (self);

    self->file = nemo_file_ref (file);
    self->cancellable = g_cancellable_new ();
    self->showing_icon = FALSE;

    /* Only known for images, once their header has been read. */
    gtk_widget_hide (self->dimensions_title);
    gtk_widget_hide (self->dimensions_label);
    gtk_widget_hide (self->pages_title);
    gtk_widget_hide (self->pages_label);

    nemo_file_monitor_add (file, self, FILE_ATTRIBUTES | NEMO_FILE_ATTRIBUTE_THUMBNAIL);
    g_signal_connect (file, "changed", G_CALLBACK (file_changed_cb), self);
    nemo_file_call_when_ready (file, FILE_ATTRIBUTES, file_ready_cb, self);
}

static gboolean
update_cb (gpointer user_data)
{
    NemoPreviewPane *self = user_data;
    GList *selection = NULL;
    guint n_selected;

    self->update_id = 0;

    /* A hidden pane lets go of its file instead of keeping it loaded. */
    if (self->view != NULL && gtk_widget_get_visible (GTK_WIDGET (self))) {
        selection = nemo_view_get_selection (self->view);
    }

    n_selected = g_list_length (selection);

    if (n_selected == 1) {
        if (selection->data != self->file) {
            set_file (self, selection->data);
        }
    } else {
        clear_file (self);

        if (n_selected == 0) {
            show_message (self, _("No file selected"));
        } else {
            gchar *message;

            message = g_strdup_printf (ngettext ("%u item selected",
                                                 "%u items selected",
                                                 n_selected),
                                       n_selected);
            show_message (self, message);
            g_free (message);
        }
    }

    nemo_file_list_free (selection);

    return G_SOURCE_REMOVE;
}

static void
schedule_update (NemoPreviewPane *self)
{
    if (self->update_id != 0) {
        g_source_remove (self->update_id);
    }

    /* No delay of our own: the view already holds selection-displayed back
     * until the selection settles, e.g. while an arrow key is held. */
    self->update_id = g_idle_add (update_cb, self);
}

static void
forget_view (NemoPreviewPane *self)
{
    if (self->view == NULL) {
        return;
    }

    g_signal_handlers_disconnect_by_func (self->view, schedule_update, self);
    g_object_remove_weak_pointer (G_OBJECT (self->view), (gpointer *) &self->view);
    self->view = NULL;
}

void
nemo_preview_pane_set_view (NemoPreviewPane *self,
                            NemoView        *view)
{
    g_return_if_fail (NEMO_IS_PREVIEW_PANE (self));

    if (view == self->view) {
        return;
    }

    forget_view (self);

    if (view != NULL) {
        self->view = view;
        g_object_add_weak_pointer (G_OBJECT (view), (gpointer *) &self->view);

        g_signal_connect_swapped (view, "selection-displayed",
                                  G_CALLBACK (schedule_update), self);
        g_signal_connect_swapped (view, "begin-loading",
                                  G_CALLBACK (schedule_update), self);
    }

    schedule_update (self);
}

static GtkWidget *
add_info_row (GtkGrid     *grid,
              gint         row,
              const gchar *title,
              GtkWidget  **title_label)
{
    GtkWidget *label, *value;

    label = gtk_label_new (title);
    gtk_label_set_xalign (GTK_LABEL (label), 1.0);
    gtk_label_set_yalign (GTK_LABEL (label), 0.0);
    gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
    gtk_grid_attach (grid, label, 0, row, 1, 1);

    if (title_label != NULL) {
        *title_label = label;
    }

    value = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (value), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (value), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (value), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_selectable (GTK_LABEL (value), TRUE);
    gtk_widget_set_hexpand (value, TRUE);
    gtk_grid_attach (grid, value, 1, row, 1, 1);

    return value;
}

static void
nemo_preview_pane_init (NemoPreviewPane *self)
{
    GtkWidget *scrolled, *grid, *controls;
    PangoAttrList *attrs;

    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing (GTK_BOX (self), 12);
    gtk_container_set_border_width (GTK_CONTAINER (self), 12);
    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                                 "nemo-preview-pane");

    self->stack = gtk_stack_new ();
    gtk_widget_set_vexpand (self->stack, TRUE);
    gtk_box_pack_start (GTK_BOX (self), self->stack, TRUE, TRUE, 0);

    self->image = gtk_image_new ();
    gtk_stack_add_named (GTK_STACK (self->stack), self->image, "image");

    self->text_view = gtk_source_view_new ();
    gtk_source_buffer_set_highlight_matching_brackets (GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->text_view))),
                                                      FALSE);
    gtk_source_buffer_set_style_scheme (GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->text_view))),
                                        get_style_scheme ());
    gtk_text_view_set_editable (GTK_TEXT_VIEW (self->text_view), FALSE);
    gtk_text_view_set_monospace (GTK_TEXT_VIEW (self->text_view), TRUE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (self->text_view), GTK_WRAP_WORD_CHAR);

    scrolled = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (scrolled), self->text_view);
    gtk_stack_add_named (GTK_STACK (self->stack), scrolled, "text");

    self->document_view = ev_view_new ();
    scrolled = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (scrolled), self->document_view);
    gtk_stack_add_named (GTK_STACK (self->stack), scrolled, "document");

    /* The video widget comes from the player, which is only made once a
     * media file is first selected. */
    self->media_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_stack_add_named (GTK_STACK (self->stack), self->media_box, "media");

    self->video_frame = gtk_aspect_frame_new (NULL, 0.5, 0.5, 16.0 / 9.0, FALSE);
    gtk_frame_set_shadow_type (GTK_FRAME (self->video_frame), GTK_SHADOW_NONE);
    gtk_widget_set_vexpand (self->video_frame, TRUE);
    gtk_box_pack_start (GTK_BOX (self->media_box), self->video_frame, TRUE, TRUE, 0);

    self->audio_image = gtk_image_new ();
    gtk_widget_set_vexpand (self->audio_image, TRUE);
    gtk_box_pack_start (GTK_BOX (self->media_box), self->audio_image, TRUE, TRUE, 0);

    controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_end (GTK_BOX (self->media_box), controls, FALSE, FALSE, 0);

    self->play_button = gtk_button_new ();
    gtk_button_set_relief (GTK_BUTTON (self->play_button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text (self->play_button, _("Play or pause"));
    /* Clicking the controls must leave the keyboard with the file view, so
     * the arrow keys keep moving through files. */
    gtk_widget_set_can_focus (self->play_button, FALSE);
    g_signal_connect_swapped (self->play_button, "clicked",
                              G_CALLBACK (play_button_clicked_cb), self);
    gtk_box_pack_start (GTK_BOX (controls), self->play_button, FALSE, FALSE, 0);

    self->seek_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 1, 1);
    gtk_scale_set_draw_value (GTK_SCALE (self->seek_scale), FALSE);
    gtk_widget_set_can_focus (self->seek_scale, FALSE);
    g_signal_connect (self->seek_scale, "change-value",
                      G_CALLBACK (seek_scale_change_value_cb), self);
    gtk_box_pack_start (GTK_BOX (controls), self->seek_scale, TRUE, TRUE, 0);

    self->time_label = gtk_label_new (NULL);
    gtk_style_context_add_class (gtk_widget_get_style_context (self->time_label), "dim-label");
    gtk_box_pack_start (GTK_BOX (controls), self->time_label, FALSE, FALSE, 0);

    self->message = gtk_label_new (NULL);
    gtk_label_set_line_wrap (GTK_LABEL (self->message), TRUE);
    gtk_style_context_add_class (gtk_widget_get_style_context (self->message), "dim-label");
    gtk_stack_add_named (GTK_STACK (self->stack), self->message, "message");

    self->info_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start (GTK_BOX (self), self->info_box, FALSE, FALSE, 0);

    self->name_label = gtk_label_new (NULL);
    gtk_label_set_line_wrap (GTK_LABEL (self->name_label), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (self->name_label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_selectable (GTK_LABEL (self->name_label), TRUE);
    gtk_label_set_justify (GTK_LABEL (self->name_label), GTK_JUSTIFY_CENTER);
    attrs = pango_attr_list_new ();
    pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes (GTK_LABEL (self->name_label), attrs);
    pango_attr_list_unref (attrs);
    gtk_box_pack_start (GTK_BOX (self->info_box), self->name_label, FALSE, FALSE, 0);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 4);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
    gtk_box_pack_start (GTK_BOX (self->info_box), grid, FALSE, FALSE, 0);

    self->type_label = add_info_row (GTK_GRID (grid), 0, _("Type"), NULL);
    self->size_label = add_info_row (GTK_GRID (grid), 1, _("Size"), NULL);
    self->dimensions_label = add_info_row (GTK_GRID (grid), 2, _("Dimensions"),
                                           &self->dimensions_title);
    self->pages_label = add_info_row (GTK_GRID (grid), 3, _("Pages"), &self->pages_title);
    self->modified_label = add_info_row (GTK_GRID (grid), 4, _("Modified"), NULL);

    gtk_widget_show_all (GTK_WIDGET (self));
    gtk_widget_set_no_show_all (GTK_WIDGET (self), TRUE);

    show_message (self, _("No file selected"));

    g_signal_connect (self, "notify::visible", G_CALLBACK (schedule_update), NULL);
}

static void
nemo_preview_pane_dispose (GObject *object)
{
    NemoPreviewPane *self = NEMO_PREVIEW_PANE (object);

    g_clear_handle_id (&self->update_id, g_source_remove);
    forget_view (self);
    clear_file (self);

    destroy_player (self);

    G_OBJECT_CLASS (nemo_preview_pane_parent_class)->dispose (object);
}

static void
nemo_preview_pane_class_init (NemoPreviewPaneClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->dispose = nemo_preview_pane_dispose;

    ev_init ();
    gst_init (NULL, NULL);
}

GtkWidget *
nemo_preview_pane_new (void)
{
    return g_object_new (NEMO_TYPE_PREVIEW_PANE, NULL);
}
