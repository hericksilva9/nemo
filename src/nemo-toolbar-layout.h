/* nemo-toolbar-layout.h
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

#ifndef NEMO_TOOLBAR_LAYOUT_H
#define NEMO_TOOLBAR_LAYOUT_H

#include <glib-object.h>

#define NEMO_TYPE_TOOLBAR_LAYOUT nemo_toolbar_layout_get_type ()

G_DECLARE_FINAL_TYPE (NemoToolbarLayout, nemo_toolbar_layout, NEMO, TOOLBAR_LAYOUT, GObject)

/* The path bar and the location entry share a single widget that gets reparented
 * between rows, so the layout addresses it by this id instead of an action name. */
#define NEMO_TOOLBAR_ITEM_PATHBAR "__pathbar__"

/* User actions come and go with the files that define them, so they are not in
 * the catalog; the layout carries them as this prefix plus the action's uuid. */
#define NEMO_TOOLBAR_ACTION_PREFIX "action:"

typedef struct {
    const gchar *id;
    const gchar *label;
    const gchar *icon_name;
    gboolean     is_toggle;
} NemoToolbarItemInfo;

typedef struct {
    gboolean  visible;
    GList    *items;
} NemoToolbarBar;

NemoToolbarBar            *nemo_toolbar_bar_new            (void);
void                       nemo_toolbar_bar_free           (NemoToolbarBar    *bar);
void                       nemo_toolbar_bars_free          (GList             *bars);

NemoToolbarLayout         *nemo_toolbar_layout_get_default (void);
GList                     *nemo_toolbar_layout_get_bars    (NemoToolbarLayout *layout);
GList                     *nemo_toolbar_layout_copy_bars   (NemoToolbarLayout *layout);
void                       nemo_toolbar_layout_set_bars    (NemoToolbarLayout *layout,
                                                            GList             *bars);
gboolean                   nemo_toolbar_layout_id_is_action (const gchar      *id);
const gchar               *nemo_toolbar_layout_action_uuid   (const gchar      *id);

guint                      nemo_toolbar_layout_get_n_items (void);
const NemoToolbarItemInfo *nemo_toolbar_layout_get_item     (guint              index);
const NemoToolbarItemInfo *nemo_toolbar_layout_lookup_item  (const gchar       *id);

#endif /* NEMO_TOOLBAR_LAYOUT_H */
