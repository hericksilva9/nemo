/* nemo-toolbar-layout.c
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

#include "nemo-toolbar-layout.h"

#include "nemo-actions.h"

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#include <libnemo-private/nemo-global-preferences.h>

#define DEBUG_FLAG NEMO_DEBUG_WINDOW
#include <libnemo-private/nemo-debug.h>

#define LAYOUT_FILENAME "toolbar-layout.json"
#define LAYOUT_VERSION 1

typedef struct {
    NemoToolbarItemInfo info;

    /* Read only once, to seed the initial layout from the per-button
     * preferences Nemo used before toolbars became configurable. */
    const gchar *legacy_pref_key;
} CatalogEntry;

/* Order here doubles as the default layout order, which reproduces the
 * fixed toolbar Nemo shipped before this was configurable. */
static const CatalogEntry item_catalog[] = {
    { { NEMO_ACTION_BACK,                 N_("Previous"),              "xsi-go-previous-symbolic",       FALSE }, NEMO_PREFERENCES_SHOW_PREVIOUS_ICON_TOOLBAR },
    { { NEMO_ACTION_FORWARD,              N_("Next"),                  "xsi-go-next-symbolic",           FALSE }, NEMO_PREFERENCES_SHOW_NEXT_ICON_TOOLBAR },
    { { NEMO_ACTION_UP,                   N_("Up"),                    "xsi-go-up-symbolic",             FALSE }, NEMO_PREFERENCES_SHOW_UP_ICON_TOOLBAR },
    { { NEMO_ACTION_RELOAD,               N_("Refresh"),               "xsi-view-refresh-symbolic",      FALSE }, NEMO_PREFERENCES_SHOW_RELOAD_ICON_TOOLBAR },
    { { NEMO_ACTION_HOME,                 N_("Home"),                  "xsi-go-home-symbolic",           FALSE }, NEMO_PREFERENCES_SHOW_HOME_ICON_TOOLBAR },
    { { NEMO_ACTION_COMPUTER,             N_("Computer"),              "xsi-computer-symbolic",          FALSE }, NEMO_PREFERENCES_SHOW_COMPUTER_ICON_TOOLBAR },
    { { NEMO_TOOLBAR_ITEM_PATHBAR,        N_("Path Bar"),              NULL,                             FALSE }, NULL },
    { { NEMO_ACTION_TOGGLE_LOCATION,      N_("Location entry toggle"), "nemo-location-symbolic",         FALSE }, NEMO_PREFERENCES_SHOW_EDIT_ICON_TOOLBAR },
    { { NEMO_ACTION_OPEN_IN_TERMINAL,     N_("Open in terminal"),      "xsi-utilities-terminal-symbolic", FALSE }, NEMO_PREFERENCES_SHOW_OPEN_IN_TERMINAL_TOOLBAR },
    { { NEMO_ACTION_NEW_FOLDER,           N_("New folder"),            "xsi-folder-new-symbolic",        FALSE }, NEMO_PREFERENCES_SHOW_NEW_FOLDER_ICON_TOOLBAR },
    { { NEMO_ACTION_CUT,                  N_("Cut"),                   "xsi-edit-cut-symbolic",          FALSE, TRUE }, NULL },
    { { NEMO_ACTION_COPY,                 N_("Copy"),                  "xsi-edit-copy-symbolic",         FALSE, TRUE }, NULL },
    { { NEMO_ACTION_PASTE,                N_("Paste"),                 "xsi-edit-paste-symbolic",        FALSE, TRUE }, NULL },
    /* The menu item this comes from has no icon of its own. */
    { { NEMO_ACTION_RENAME,               N_("Rename"),                "xsi-document-edit-symbolic",     FALSE, TRUE }, NULL },
    { { NEMO_ACTION_SEARCH,               N_("Search"),                "xsi-edit-find-symbolic",         TRUE  }, NEMO_PREFERENCES_SHOW_SEARCH_ICON_TOOLBAR },
    { { NEMO_ACTION_SHOW_THUMBNAILS,      N_("Show Thumbnails"),       "xsi-preview-symbolic",           TRUE  }, NEMO_PREFERENCES_SHOW_SHOW_THUMBNAILS_TOOLBAR },
    { { NEMO_ACTION_SHOW_HIDE_EXTRA_PANE, N_("Extra Pane"),            "xsi-view-dual-symbolic",         TRUE  }, NEMO_PREFERENCES_SHOW_TOGGLE_EXTRA_PANE_TOOLBAR },
    { { NEMO_ACTION_ICON_VIEW,            N_("Icon view"),             "xsi-view-grid-symbolic",         TRUE  }, NEMO_PREFERENCES_SHOW_ICON_VIEW_ICON_TOOLBAR },
    { { NEMO_ACTION_LIST_VIEW,            N_("List view"),             "xsi-view-list-symbolic",         TRUE  }, NEMO_PREFERENCES_SHOW_LIST_VIEW_ICON_TOOLBAR },
    { { NEMO_ACTION_COMPACT_VIEW,         N_("Compact view"),          "xsi-view-compact-symbolic",      TRUE  }, NEMO_PREFERENCES_SHOW_COMPACT_VIEW_ICON_TOOLBAR },
};

struct _NemoToolbarLayout {
    GObject parent_instance;

    GList        *bars;
    GFileMonitor *monitor;
};

G_DEFINE_TYPE (NemoToolbarLayout, nemo_toolbar_layout, G_TYPE_OBJECT)

enum {
    CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

NemoToolbarBar *
nemo_toolbar_bar_new (void)
{
    NemoToolbarBar *bar;

    bar = g_new0 (NemoToolbarBar, 1);
    bar->visible = TRUE;

    return bar;
}

void
nemo_toolbar_bar_free (NemoToolbarBar *bar)
{
    g_list_free_full (bar->items, g_free);
    g_free (bar);
}

void
nemo_toolbar_bars_free (GList *bars)
{
    g_list_free_full (bars, (GDestroyNotify) nemo_toolbar_bar_free);
}

gboolean
nemo_toolbar_layout_id_is_action (const gchar *id)
{
    return id != NULL && g_str_has_prefix (id, NEMO_TOOLBAR_ACTION_PREFIX);
}

const gchar *
nemo_toolbar_layout_action_uuid (const gchar *id)
{
    g_return_val_if_fail (nemo_toolbar_layout_id_is_action (id), NULL);

    return id + sizeof (NEMO_TOOLBAR_ACTION_PREFIX) - 1;
}

const NemoToolbarItemInfo *
nemo_toolbar_layout_lookup_item (const gchar *id)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (item_catalog); i++) {
        if (g_strcmp0 (item_catalog[i].info.id, id) == 0) {
            return &item_catalog[i].info;
        }
    }

    return NULL;
}

static GList *
build_default_bars (void)
{
    NemoToolbarBar *bar;
    guint i;

    bar = nemo_toolbar_bar_new ();

    for (i = 0; i < G_N_ELEMENTS (item_catalog); i++) {
        const CatalogEntry *entry = &item_catalog[i];

        /* The default reproduces the toolbar Nemo used to ship, so items it
         * never had are left for the user to add. */
        if (entry->info.from_view) {
            continue;
        }

        if (entry->legacy_pref_key != NULL &&
            !g_settings_get_boolean (nemo_preferences, entry->legacy_pref_key)) {
            continue;
        }

        bar->items = g_list_prepend (bar->items, g_strdup (entry->info.id));
    }

    bar->items = g_list_reverse (bar->items);

    return g_list_prepend (NULL, bar);
}

static GList *
bars_from_json_root (JsonNode *root)
{
    JsonObject *object;
    JsonArray *bar_array;
    GList *bars = NULL;
    guint i, n_bars;

    if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root)) {
        return NULL;
    }

    object = json_node_get_object (root);

    if (!json_object_has_member (object, "bars")) {
        return NULL;
    }

    bar_array = json_object_get_array_member (object, "bars");
    n_bars = bar_array != NULL ? json_array_get_length (bar_array) : 0;

    for (i = 0; i < n_bars; i++) {
        JsonObject *bar_object;
        JsonArray *item_array;
        NemoToolbarBar *bar;
        guint j, n_items;

        bar_object = json_array_get_object_element (bar_array, i);

        if (bar_object == NULL) {
            continue;
        }

        bar = nemo_toolbar_bar_new ();

        if (json_object_has_member (bar_object, "visible")) {
            bar->visible = json_object_get_boolean_member (bar_object, "visible");
        }

        item_array = json_object_has_member (bar_object, "items") ?
                     json_object_get_array_member (bar_object, "items") : NULL;
        n_items = item_array != NULL ? json_array_get_length (item_array) : 0;

        for (j = 0; j < n_items; j++) {
            const gchar *id = json_array_get_string_element (item_array, j);

            /* A user action is kept even when it cannot be resolved right now:
             * its defining file may simply not be installed at the moment. */
            if (!nemo_toolbar_layout_id_is_action (id) &&
                nemo_toolbar_layout_lookup_item (id) == NULL) {
                DEBUG ("Unknown toolbar item '%s', skipping.", id);
                continue;
            }

            bar->items = g_list_prepend (bar->items, g_strdup (id));
        }

        bar->items = g_list_reverse (bar->items);
        bars = g_list_prepend (bars, bar);
    }

    return g_list_reverse (bars);
}

/* The path bar is one reparented widget, so exactly one bar may own it.
 * Returns TRUE when the layout had to be corrected. */
static gboolean
ensure_single_pathbar (GList *bars)
{
    gboolean seen = FALSE;
    gboolean modified = FALSE;
    GList *l;

    for (l = bars; l != NULL; l = l->next) {
        NemoToolbarBar *bar = l->data;
        GList *item = bar->items;

        while (item != NULL) {
            GList *next = item->next;

            if (g_strcmp0 (item->data, NEMO_TOOLBAR_ITEM_PATHBAR) == 0) {
                if (seen) {
                    g_free (item->data);
                    bar->items = g_list_delete_link (bar->items, item);
                    modified = TRUE;
                } else {
                    seen = TRUE;
                }
            }

            item = next;
        }
    }

    if (!seen && bars != NULL) {
        NemoToolbarBar *first = bars->data;

        first->items = g_list_prepend (first->items, g_strdup (NEMO_TOOLBAR_ITEM_PATHBAR));
        modified = TRUE;
    }

    return modified;
}

static void
save_bars (GList *bars)
{
    JsonBuilder *builder;
    JsonGenerator *generator;
    JsonNode *root;
    GList *l;
    GError *error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *data = NULL;

    builder = json_builder_new ();
    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "version");
    json_builder_add_int_value (builder, LAYOUT_VERSION);

    json_builder_set_member_name (builder, "bars");
    json_builder_begin_array (builder);

    for (l = bars; l != NULL; l = l->next) {
        NemoToolbarBar *bar = l->data;
        GList *item;

        json_builder_begin_object (builder);

        json_builder_set_member_name (builder, "visible");
        json_builder_add_boolean_value (builder, bar->visible);

        json_builder_set_member_name (builder, "items");
        json_builder_begin_array (builder);

        for (item = bar->items; item != NULL; item = item->next) {
            json_builder_add_string_value (builder, item->data);
        }

        json_builder_end_array (builder);
        json_builder_end_object (builder);
    }

    json_builder_end_array (builder);
    json_builder_end_object (builder);

    root = json_builder_get_root (builder);
    generator = json_generator_new ();
    json_generator_set_root (generator, root);
    json_generator_set_pretty (generator, TRUE);
    data = json_generator_to_data (generator, NULL);

    dir = g_build_filename (g_get_user_config_dir (), "nemo", NULL);
    g_mkdir_with_parents (dir, 0700);
    path = g_build_filename (dir, LAYOUT_FILENAME, NULL);

    if (!g_file_set_contents (path, data, -1, &error)) {
        g_warning ("Could not save toolbar layout: %s", error->message);
        g_clear_error (&error);
    }

    json_node_free (root);
    g_object_unref (generator);
    g_object_unref (builder);
}

static GList *
load_bars (void)
{
    JsonParser *parser;
    GList *bars = NULL;
    GError *error = NULL;
    g_autofree gchar *path = NULL;

    path = g_build_filename (g_get_user_config_dir (), "nemo", LAYOUT_FILENAME, NULL);
    parser = json_parser_new ();

    if (json_parser_load_from_file (parser, path, &error)) {
        bars = bars_from_json_root (json_parser_get_root (parser));
    } else {
        if (error->code != G_FILE_ERROR_NOENT) {
            g_warning ("Error loading toolbar layout file: %s", error->message);
        }

        g_clear_error (&error);
    }

    g_object_unref (parser);

    return bars;
}

static void
reload (NemoToolbarLayout *layout)
{
    GList *bars;

    bars = load_bars ();

    if (bars == NULL) {
        bars = build_default_bars ();
        save_bars (bars);
    } else if (ensure_single_pathbar (bars)) {
        save_bars (bars);
    }

    nemo_toolbar_bars_free (layout->bars);
    layout->bars = bars;
}

static void
config_dir_changed (GFileMonitor      *monitor,
                    GFile             *file,
                    GFile             *other_file,
                    GFileMonitorEvent  event_type,
                    gpointer           user_data)
{
    NemoToolbarLayout *layout = NEMO_TOOLBAR_LAYOUT (user_data);

    g_autofree gchar *basename = g_file_get_basename (file);
    g_autofree gchar *other_name = NULL;

    if (other_file != NULL) {
        other_name = g_file_get_basename (other_file);
    }

    if (g_strcmp0 (basename, LAYOUT_FILENAME) != 0 &&
        g_strcmp0 (other_name, LAYOUT_FILENAME) != 0) {
        return;
    }

    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_RENAMED:
        reload (layout);
        g_signal_emit (layout, signals[CHANGED], 0);
        break;
    default:
        break;
    }
}

GList *
nemo_toolbar_layout_get_bars (NemoToolbarLayout *layout)
{
    return layout->bars;
}

GList *
nemo_toolbar_layout_copy_bars (NemoToolbarLayout *layout)
{
    GList *copy = NULL;
    GList *l, *item;

    for (l = layout->bars; l != NULL; l = l->next) {
        NemoToolbarBar *source = l->data;
        NemoToolbarBar *bar;

        bar = nemo_toolbar_bar_new ();
        bar->visible = source->visible;

        for (item = source->items; item != NULL; item = item->next) {
            bar->items = g_list_prepend (bar->items, g_strdup (item->data));
        }

        bar->items = g_list_reverse (bar->items);
        copy = g_list_prepend (copy, bar);
    }

    return g_list_reverse (copy);
}

void
nemo_toolbar_layout_set_bars (NemoToolbarLayout *layout,
                              GList             *bars)
{
    ensure_single_pathbar (bars);
    save_bars (bars);

    nemo_toolbar_bars_free (layout->bars);
    layout->bars = bars;

    g_signal_emit (layout, signals[CHANGED], 0);
}

guint
nemo_toolbar_layout_get_n_items (void)
{
    return G_N_ELEMENTS (item_catalog);
}

const NemoToolbarItemInfo *
nemo_toolbar_layout_get_item (guint index)
{
    g_return_val_if_fail (index < G_N_ELEMENTS (item_catalog), NULL);

    return &item_catalog[index].info;
}

NemoToolbarLayout *
nemo_toolbar_layout_get_default (void)
{
    static NemoToolbarLayout *singleton = NULL;

    if (singleton == NULL) {
        singleton = g_object_new (NEMO_TYPE_TOOLBAR_LAYOUT, NULL);
    }

    return singleton;
}

static void
nemo_toolbar_layout_finalize (GObject *object)
{
    NemoToolbarLayout *layout = NEMO_TOOLBAR_LAYOUT (object);

    g_clear_object (&layout->monitor);
    nemo_toolbar_bars_free (layout->bars);

    G_OBJECT_CLASS (nemo_toolbar_layout_parent_class)->finalize (object);
}

static void
nemo_toolbar_layout_class_init (NemoToolbarLayoutClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = nemo_toolbar_layout_finalize;

    signals[CHANGED] = g_signal_new ("changed",
                                     G_TYPE_FROM_CLASS (klass),
                                     G_SIGNAL_RUN_LAST,
                                     0, NULL, NULL, NULL,
                                     G_TYPE_NONE, 0);
}

static void
nemo_toolbar_layout_init (NemoToolbarLayout *layout)
{
    GFile *dir;
    g_autofree gchar *path = NULL;

    reload (layout);

    path = g_build_filename (g_get_user_config_dir (), "nemo", NULL);
    dir = g_file_new_for_path (path);
    layout->monitor = g_file_monitor_directory (dir, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
    g_object_unref (dir);

    if (layout->monitor != NULL) {
        g_signal_connect (layout->monitor, "changed", G_CALLBACK (config_dir_changed), layout);
    }
}
