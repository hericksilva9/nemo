/* nemo-office-sheet-view.h
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

#ifndef NEMO_OFFICE_SHEET_VIEW_H
#define NEMO_OFFICE_SHEET_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NEMO_TYPE_OFFICE_SHEET_VIEW (nemo_office_sheet_view_get_type ())
G_DECLARE_FINAL_TYPE (NemoOfficeSheetView, nemo_office_sheet_view, NEMO, OFFICE_SHEET_VIEW, GtkBox)

GtkWidget *nemo_office_sheet_view_new         (void);

/* Shows the spreadsheet at @path, drawn by LibreOffice a tile at a time. */
void       nemo_office_sheet_view_load_async  (NemoOfficeSheetView  *self,
                                               const gchar          *path,
                                               GCancellable         *cancellable,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data);
gboolean   nemo_office_sheet_view_load_finish (NemoOfficeSheetView  *self,
                                               GAsyncResult         *result,
                                               GError              **error);

void       nemo_office_sheet_view_clear       (NemoOfficeSheetView  *self);

G_END_DECLS

#endif /* NEMO_OFFICE_SHEET_VIEW_H */
