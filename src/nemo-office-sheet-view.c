/* nemo-office-sheet-view.c - A spreadsheet drawn by LibreOffice in tiles.
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

#include "nemo-office-sheet-view.h"
#include "nemo-office-renderer.h"

#include <stdlib.h>
#include <string.h>

/* Matches DOCTYPE_SPREADSHEET in nemo-office-preview.c. */
#define DOCTYPE_SPREADSHEET 1

/* LibreOffice measures in twips, 1440 to the inch; 96 dpi makes it 15 to a
 * pixel, which is the sheet at 100%. */
#define TWIPS_PER_PIXEL 15
#define TILE_SIZE 256
#define MAX_TILES 256

/* Past this GTK can't size the area, and a preview has shown enough. */
#define MAX_AREA_SIZE 30000

struct _NemoOfficeSheetView {
    GtkBox parent_instance;

    GtkWidget *sheet_combo;
    GtkWidget *area;

    gchar *path;
    gint part;
    gint width;
    gint height;

    /* Tile position to its surface; NULL while LibreOffice is drawing it. */
    GHashTable *tiles;
    GCancellable *cancellable;
};

G_DEFINE_TYPE (NemoOfficeSheetView, nemo_office_sheet_view, GTK_TYPE_BOX)

typedef struct {
    NemoOfficeSheetView *self;
    guint64 key;
} TileRequest;

static guint64
tile_key (gint col,
          gint row)
{
    return ((guint64) col << 32) | (guint32) row;
}

/* Drops what was drawn and whatever is still being drawn. */
static void
reset_tiles (NemoOfficeSheetView *self)
{
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
    self->cancellable = g_cancellable_new ();

    g_hash_table_remove_all (self->tiles);
    gtk_widget_queue_draw (self->area);
}

static void
tile_ready_cb (GObject      *source,
               GAsyncResult *res,
               gpointer      user_data)
{
    TileRequest *request = user_data;
    NemoOfficeSheetView *self = request->self;
    cairo_surface_t *surface;
    const guint8 *data;
    GBytes *pixels;
    gsize size;
    gint scale, side, stride, row;

    pixels = nemo_office_renderer_request_finish (NEMO_OFFICE_RENDERER (source), res, NULL);

    /* Also NULL once cancelled, when the tile belongs to a sheet no longer
     * shown and self may be gone. */
    if (pixels == NULL) {
        g_free (request);
        return;
    }

    scale = gtk_widget_get_scale_factor (self->area);
    side = TILE_SIZE * scale;
    data = g_bytes_get_data (pixels, &size);

    if (size != (gsize) side * side * 4) {
        g_hash_table_remove (self->tiles, &request->key);
        g_bytes_unref (pixels);
        g_free (request);
        return;
    }

    /* BGRA from LibreOffice is cairo's ARGB32 on little endian machines. */
    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, side, side);
    stride = cairo_image_surface_get_stride (surface);

    cairo_surface_flush (surface);

    for (row = 0; row < side; row++) {
        memcpy (cairo_image_surface_get_data (surface) + row * stride,
                data + row * side * 4, side * 4);
    }

    cairo_surface_mark_dirty (surface);
    cairo_surface_set_device_scale (surface, scale, scale);

    g_hash_table_replace (self->tiles, g_memdup2 (&request->key, sizeof (guint64)), surface);
    gtk_widget_queue_draw (self->area);

    g_bytes_unref (pixels);
    g_free (request);
}

static void
request_tile (NemoOfficeSheetView *self,
              gint                 col,
              gint                 row)
{
    TileRequest *request;
    gchar *text;
    gint side, twips;

    request = g_new (TileRequest, 1);
    request->self = self;
    request->key = tile_key (col, row);

    /* A placeholder, so the tile is asked for once. */
    g_hash_table_insert (self->tiles, g_memdup2 (&request->key, sizeof (guint64)), NULL);

    side = TILE_SIZE * gtk_widget_get_scale_factor (self->area);
    twips = TILE_SIZE * TWIPS_PER_PIXEL;

    text = g_strdup_printf ("tile\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
                            self->path, self->part, side, side,
                            col * twips, row * twips, twips, twips);
    nemo_office_renderer_request_async (nemo_office_renderer_get_default (), text,
                                        self->cancellable, tile_ready_cb, request);
    g_free (text);
}

static gboolean
area_draw_cb (GtkWidget           *area,
              cairo_t             *cr,
              NemoOfficeSheetView *self)
{
    GdkRectangle clip;
    gint col, row;

    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_paint (cr);

    if (self->path == NULL || !gdk_cairo_get_clip_rectangle (cr, &clip)) {
        return TRUE;
    }

    /* Scrolling far keeps asking for tiles; forget the old ones rather than
     * hold them all. */
    if (g_hash_table_size (self->tiles) > MAX_TILES) {
        g_hash_table_remove_all (self->tiles);
    }

    for (row = clip.y / TILE_SIZE; row <= (clip.y + clip.height - 1) / TILE_SIZE; row++) {
        for (col = clip.x / TILE_SIZE; col <= (clip.x + clip.width - 1) / TILE_SIZE; col++) {
            guint64 key = tile_key (col, row);
            cairo_surface_t *surface;

            if (!g_hash_table_lookup_extended (self->tiles, &key, NULL, (gpointer *) &surface)) {
                request_tile (self, col, row);
            } else if (surface != NULL) {
                cairo_set_source_surface (cr, surface, col * TILE_SIZE, row * TILE_SIZE);
                cairo_paint (cr);
            }
        }
    }

    return TRUE;
}

static gchar *
reply_to_string (GBytes *reply)
{
    gsize size;
    const gchar *data = g_bytes_get_data (reply, &size);

    return g_strndup (data, size);
}

/* Takes a reply to "size": width and height in twips. */
static void
set_sheet_size (NemoOfficeSheetView *self,
                GBytes              *reply)
{
    gchar **lines, *text;

    text = reply_to_string (reply);
    lines = g_strsplit (text, "\n", -1);
    g_free (text);

    if (g_strv_length (lines) >= 2) {
        self->width = MIN (atol (lines[0]) / TWIPS_PER_PIXEL, MAX_AREA_SIZE);
        self->height = MIN (atol (lines[1]) / TWIPS_PER_PIXEL, MAX_AREA_SIZE);
    }

    g_strfreev (lines);

    gtk_widget_set_size_request (self->area, self->width, self->height);
    reset_tiles (self);
}

static void
load_size_cb (GObject      *source,
              GAsyncResult *res,
              gpointer      user_data)
{
    GTask *task = user_data;
    NemoOfficeSheetView *self = g_task_get_source_object (task);
    GError *error = NULL;
    GBytes *reply;

    reply = nemo_office_renderer_request_finish (NEMO_OFFICE_RENDERER (source), res, &error);

    if (reply == NULL) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    set_sheet_size (self, reply);
    g_bytes_unref (reply);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
request_size (NemoOfficeSheetView *self,
              GCancellable        *cancellable,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
    gchar *text;

    text = g_strdup_printf ("size\t%s\t%d", self->path, self->part);
    nemo_office_renderer_request_async (nemo_office_renderer_get_default (), text,
                                        cancellable, callback, user_data);
    g_free (text);
}

static void
load_info_cb (GObject      *source,
              GAsyncResult *res,
              gpointer      user_data)
{
    GTask *task = user_data;
    NemoOfficeSheetView *self = g_task_get_source_object (task);
    GError *error = NULL;
    GBytes *reply;
    gchar **lines, *text;
    guint n_lines, i;
    gint parts;

    reply = nemo_office_renderer_request_finish (NEMO_OFFICE_RENDERER (source), res, &error);

    if (reply == NULL) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    text = reply_to_string (reply);
    lines = g_strsplit (text, "\n", -1);
    n_lines = g_strv_length (lines);
    g_free (text);
    g_bytes_unref (reply);

    /* type, parts, current part, then a line per part name. */
    parts = n_lines >= 3 ? atoi (lines[1]) : 0;

    if (parts <= 0 || atoi (lines[0]) != DOCTYPE_SPREADSHEET || n_lines < 3 + (guint) parts) {
        g_strfreev (lines);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Not a spreadsheet");
        g_object_unref (task);
        return;
    }

    self->part = CLAMP (atoi (lines[2]), 0, parts - 1);

    g_signal_handlers_block_matched (self->sheet_combo, G_SIGNAL_MATCH_DATA,
                                     0, 0, NULL, NULL, self);
    gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (self->sheet_combo));

    for (i = 0; i < (guint) parts; i++) {
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (self->sheet_combo), lines[3 + i]);
    }

    gtk_combo_box_set_active (GTK_COMBO_BOX (self->sheet_combo), self->part);
    g_signal_handlers_unblock_matched (self->sheet_combo, G_SIGNAL_MATCH_DATA,
                                       0, 0, NULL, NULL, self);

    /* A single sheet needs no picker. */
    gtk_widget_set_visible (self->sheet_combo, parts > 1);
    g_strfreev (lines);

    request_size (self, g_task_get_cancellable (task), load_size_cb, task);
}

void
nemo_office_sheet_view_load_async (NemoOfficeSheetView *self,
                                   const gchar         *path,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
    GTask *task;
    gchar *text;

    nemo_office_sheet_view_clear (self);
    self->path = g_strdup (path);

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_source_tag (task, nemo_office_sheet_view_load_async);

    text = g_strconcat ("info\t", path, NULL);
    nemo_office_renderer_request_async (nemo_office_renderer_get_default (), text,
                                        cancellable, load_info_cb, task);
    g_free (text);
}

gboolean
nemo_office_sheet_view_load_finish (NemoOfficeSheetView  *self,
                                    GAsyncResult         *result,
                                    GError              **error)
{
    g_return_val_if_fail (g_task_is_valid (result, self), FALSE);

    return g_task_propagate_boolean (G_TASK (result), error);
}

void
nemo_office_sheet_view_clear (NemoOfficeSheetView *self)
{
    g_clear_pointer (&self->path, g_free);
    reset_tiles (self);
}

static void
part_size_cb (GObject      *source,
              GAsyncResult *res,
              gpointer      user_data)
{
    NemoOfficeSheetView *self = user_data;
    GBytes *reply;

    reply = nemo_office_renderer_request_finish (NEMO_OFFICE_RENDERER (source), res, NULL);

    /* Also NULL once cancelled, when self may be gone. */
    if (reply == NULL) {
        return;
    }

    set_sheet_size (self, reply);
    g_bytes_unref (reply);
}

static void
sheet_changed_cb (GtkComboBox         *combo,
                  NemoOfficeSheetView *self)
{
    gint part = gtk_combo_box_get_active (combo);

    if (part < 0 || part == self->part || self->path == NULL) {
        return;
    }

    self->part = part;
    reset_tiles (self);
    request_size (self, self->cancellable, part_size_cb, self);
}

static void
scale_changed_cb (NemoOfficeSheetView *self)
{
    reset_tiles (self);
}

static void
nemo_office_sheet_view_init (NemoOfficeSheetView *self)
{
    GtkWidget *scrolled;

    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing (GTK_BOX (self), 6);

    self->tiles = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free,
                                         (GDestroyNotify) cairo_surface_destroy);
    self->cancellable = g_cancellable_new ();

    self->sheet_combo = gtk_combo_box_text_new ();
    gtk_widget_set_no_show_all (self->sheet_combo, TRUE);
    g_signal_connect (self->sheet_combo, "changed", G_CALLBACK (sheet_changed_cb), self);
    gtk_box_pack_start (GTK_BOX (self), self->sheet_combo, FALSE, FALSE, 0);

    self->area = gtk_drawing_area_new ();
    gtk_widget_set_halign (self->area, GTK_ALIGN_START);
    gtk_widget_set_valign (self->area, GTK_ALIGN_START);
    g_signal_connect (self->area, "draw", G_CALLBACK (area_draw_cb), self);
    g_signal_connect_swapped (self->area, "notify::scale-factor",
                              G_CALLBACK (scale_changed_cb), self);

    scrolled = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (scrolled), self->area);
    gtk_box_pack_start (GTK_BOX (self), scrolled, TRUE, TRUE, 0);
    gtk_widget_show_all (scrolled);
}

static void
nemo_office_sheet_view_dispose (GObject *object)
{
    NemoOfficeSheetView *self = NEMO_OFFICE_SHEET_VIEW (object);

    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
    g_clear_pointer (&self->tiles, g_hash_table_unref);
    g_clear_pointer (&self->path, g_free);

    G_OBJECT_CLASS (nemo_office_sheet_view_parent_class)->dispose (object);
}

static void
nemo_office_sheet_view_class_init (NemoOfficeSheetViewClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = nemo_office_sheet_view_dispose;
}

GtkWidget *
nemo_office_sheet_view_new (void)
{
    return g_object_new (NEMO_TYPE_OFFICE_SHEET_VIEW, NULL);
}
