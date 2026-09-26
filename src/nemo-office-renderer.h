/* nemo-office-renderer.h
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

#ifndef NEMO_OFFICE_RENDERER_H
#define NEMO_OFFICE_RENDERER_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define NEMO_TYPE_OFFICE_RENDERER (nemo_office_renderer_get_type ())
G_DECLARE_FINAL_TYPE (NemoOfficeRenderer, nemo_office_renderer, NEMO, OFFICE_RENDERER, GObject)

NemoOfficeRenderer *nemo_office_renderer_get_default (void);

/* One request of the nemo-office-preview protocol; the reply is its payload. */
void    nemo_office_renderer_request_async  (NemoOfficeRenderer   *self,
                                             const gchar          *request,
                                             GCancellable         *cancellable,
                                             GAsyncReadyCallback   callback,
                                             gpointer              user_data);
GBytes *nemo_office_renderer_request_finish (NemoOfficeRenderer   *self,
                                             GAsyncResult         *result,
                                             GError              **error);

/* A cached pdf of the document at @path, exported when missing or stale. */
void    nemo_office_renderer_get_pdf_async  (NemoOfficeRenderer   *self,
                                             const gchar          *path,
                                             GCancellable         *cancellable,
                                             GAsyncReadyCallback   callback,
                                             gpointer              user_data);
gchar  *nemo_office_renderer_get_pdf_finish (NemoOfficeRenderer   *self,
                                             GAsyncResult         *result,
                                             GError              **error);

G_END_DECLS

#endif /* NEMO_OFFICE_RENDERER_H */
