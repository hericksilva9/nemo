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

#include <glib/gi18n.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>

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

    NemoView *view;
    NemoFile *file;
    GCancellable *cancellable;
    guint update_id;
    gboolean showing_icon;
    gint load_width;
    gint load_height;
};

G_DEFINE_TYPE (NemoPreviewPane, nemo_preview_pane, GTK_TYPE_BOX)

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

static void
text_loaded_cb (GObject      *source,
                GAsyncResult *res,
                gpointer      user_data)
{
    NemoPreviewPane *self = user_data;
    GBytes *bytes;
    const gchar *data, *end;
    gsize length;

    bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (source), res, NULL);

    if (bytes == NULL) {
        return;
    }

    data = g_bytes_get_data (bytes, &length);

    /* Only a character cut in half at the read limit may be invalid; anything
     * else means the file isn't really text, so the icon stays. */
    if (g_utf8_validate (data, length, &end) || (data + length) - end < 4) {
        gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->text_view)),
                                  data, end - data);
        gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "text");
        self->showing_icon = FALSE;
    }

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
    GtkWidget *scrolled, *grid;
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

    self->text_view = gtk_text_view_new ();
    gtk_text_view_set_editable (GTK_TEXT_VIEW (self->text_view), FALSE);
    gtk_text_view_set_monospace (GTK_TEXT_VIEW (self->text_view), TRUE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (self->text_view), GTK_WRAP_WORD_CHAR);

    scrolled = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (scrolled), self->text_view);
    gtk_stack_add_named (GTK_STACK (self->stack), scrolled, "text");

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
    self->modified_label = add_info_row (GTK_GRID (grid), 3, _("Modified"), NULL);

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

    G_OBJECT_CLASS (nemo_preview_pane_parent_class)->dispose (object);
}

static void
nemo_preview_pane_class_init (NemoPreviewPaneClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->dispose = nemo_preview_pane_dispose;
}

GtkWidget *
nemo_preview_pane_new (void)
{
    return g_object_new (NEMO_TYPE_PREVIEW_PANE, NULL);
}
