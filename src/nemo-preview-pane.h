/* nemo-preview-pane.h
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

#ifndef NEMO_PREVIEW_PANE_H
#define NEMO_PREVIEW_PANE_H

#include <gtk/gtk.h>

#include "nemo-view.h"

#define NEMO_TYPE_PREVIEW_PANE nemo_preview_pane_get_type ()

G_DECLARE_FINAL_TYPE (NemoPreviewPane, nemo_preview_pane, NEMO, PREVIEW_PANE, GtkBox)

GtkWidget *nemo_preview_pane_new      (void);

/* Follow the selection of @view, or of nothing when @view is NULL. */
void       nemo_preview_pane_set_view (NemoPreviewPane *self,
                                       NemoView        *view);

#endif /* NEMO_PREVIEW_PANE_H */
