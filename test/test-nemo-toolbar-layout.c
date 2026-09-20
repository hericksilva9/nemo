/* test-nemo-toolbar-layout.c
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

#include <src/nemo-actions.h>
#include <src/nemo-toolbar-layout.h>

#include <libnemo-private/nemo-global-preferences.h>

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

/* The layout is a process-wide singleton reading one file, so these run in
 * order against one config directory: test_migration first, while that file
 * still does not exist, then the tests that write it. meson gives us a
 * throwaway XDG_CONFIG_HOME and the memory GSettings backend. */

#define RELOAD_TIMEOUT_MS 10000

static gchar *
layout_path (void)
{
    return g_build_filename (g_get_user_config_dir (), "nemo", "toolbar-layout.json", NULL);
}

static NemoToolbarBar *
bar_with (const gchar *first_item, ...)
{
    NemoToolbarBar *bar;
    const gchar *item;
    va_list args;

    bar = nemo_toolbar_bar_new ();

    va_start (args, first_item);

    for (item = first_item; item != NULL; item = va_arg (args, const gchar *)) {
        bar->items = g_list_append (bar->items, g_strdup (item));
    }

    va_end (args);

    return bar;
}

static void
assert_items (GList *bars, guint index, const gchar * const *expected)
{
    NemoToolbarBar *bar;
    GList *item;
    guint i;

    bar = g_list_nth_data (bars, index);
    g_assert_nonnull (bar);

    item = bar->items;

    for (i = 0; expected[i] != NULL; i++) {
        g_assert_nonnull (item);
        g_assert_cmpstr (item->data, ==, expected[i]);
        item = item->next;
    }

    g_assert_null (item);
}

static void
test_catalog (void)
{
    GHashTable *seen;
    guint i, n;

    n = nemo_toolbar_layout_get_n_items ();
    g_assert_cmpuint (n, >, 0);

    seen = g_hash_table_new (g_str_hash, g_str_equal);

    for (i = 0; i < n; i++) {
        const NemoToolbarItemInfo *info = nemo_toolbar_layout_get_item (i);

        g_assert_nonnull (info);
        g_assert_nonnull (info->id);
        g_assert_nonnull (info->label);

        /* A saved id becomes a widget by way of this lookup, so it has to
         * land back on the entry it came from. */
        g_assert_true (nemo_toolbar_layout_lookup_item (info->id) == info);

        /* Ids are what the layout file stores; a duplicate would make one of
         * the two entries unreachable. */
        g_assert_false (g_hash_table_contains (seen, info->id));
        g_hash_table_add (seen, (gpointer) info->id);

        /* Every item is a button drawn from its action's icon, except the
         * path bar, which is a widget and deliberately has none. */
        if (g_strcmp0 (info->id, NEMO_TOOLBAR_ITEM_PATHBAR) == 0) {
            g_assert_null (info->icon_name);
        } else {
            g_assert_nonnull (info->icon_name);
        }
    }

    g_hash_table_destroy (seen);

    g_assert_nonnull (nemo_toolbar_layout_lookup_item (NEMO_TOOLBAR_ITEM_PATHBAR));
    g_assert_null (nemo_toolbar_layout_lookup_item ("No Such Button"));
    g_assert_null (nemo_toolbar_layout_lookup_item ("action:1234"));
}

static void
test_action_ids (void)
{
    g_assert_true (nemo_toolbar_layout_id_is_action ("action:1234-abcd"));
    g_assert_cmpstr (nemo_toolbar_layout_action_uuid ("action:1234-abcd"), ==, "1234-abcd");

    g_assert_false (nemo_toolbar_layout_id_is_action (NEMO_ACTION_BACK));
    g_assert_false (nemo_toolbar_layout_id_is_action (NEMO_TOOLBAR_ITEM_PATHBAR));
    g_assert_false (nemo_toolbar_layout_id_is_action (NULL));
}

static void
test_migration (void)
{
    /* main() left only Home and Search turned on; the path bar has no key of
     * its own and always comes along, in catalog order. */
    const gchar * const expected[] = {
        NEMO_ACTION_HOME, NEMO_TOOLBAR_ITEM_PATHBAR, NEMO_ACTION_SEARCH, NULL
    };
    g_autofree gchar *path = layout_path ();
    GList *bars;

    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));

    bars = nemo_toolbar_layout_get_bars (nemo_toolbar_layout_get_default ());

    g_assert_cmpuint (g_list_length (bars), ==, 1);
    assert_items (bars, 0, expected);

    /* The seeding happens once: it is written out so the keys are never read
     * again. */
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
}

/* Copy and friends act on the files a view is showing, not on the window, and
 * were never toolbar buttons before, so they wait in the catalog for the user
 * to place them. */
static void
test_view_items_are_opt_in (void)
{
    GList *bars;
    guint i, n, n_from_view = 0;

    bars = nemo_toolbar_layout_get_bars (nemo_toolbar_layout_get_default ());
    n = nemo_toolbar_layout_get_n_items ();

    for (i = 0; i < n; i++) {
        const NemoToolbarItemInfo *info = nemo_toolbar_layout_get_item (i);
        GList *bar;

        if (!info->from_view) {
            continue;
        }

        n_from_view++;

        /* The button is built from the catalog icon, not from the action,
         * which may not name one. */
        g_assert_nonnull (info->icon_name);
        g_assert_false (info->is_toggle);

        for (bar = bars; bar != NULL; bar = bar->next) {
            NemoToolbarBar *b = bar->data;

            g_assert_null (g_list_find_custom (b->items, info->id, (GCompareFunc) g_strcmp0));
        }
    }

    g_assert_cmpuint (n_from_view, >, 0);
    g_assert_true (nemo_toolbar_layout_lookup_item (NEMO_ACTION_COPY)->from_view);
    g_assert_false (nemo_toolbar_layout_lookup_item (NEMO_ACTION_HOME)->from_view);
}

static void
test_second_pathbar_is_dropped (void)
{
    const gchar * const expected_first[] = { NEMO_ACTION_BACK, NEMO_TOOLBAR_ITEM_PATHBAR, NULL };
    const gchar * const expected_second[] = { NEMO_ACTION_HOME, NULL };
    NemoToolbarLayout *layout = nemo_toolbar_layout_get_default ();
    GList *bars = NULL;

    /* One widget cannot sit on two bars at once. */
    bars = g_list_append (bars, bar_with (NEMO_ACTION_BACK, NEMO_TOOLBAR_ITEM_PATHBAR, NULL));
    bars = g_list_append (bars, bar_with (NEMO_ACTION_HOME, NEMO_TOOLBAR_ITEM_PATHBAR, NULL));

    nemo_toolbar_layout_set_bars (layout, bars);

    bars = nemo_toolbar_layout_get_bars (layout);
    g_assert_cmpuint (g_list_length (bars), ==, 2);
    assert_items (bars, 0, expected_first);
    assert_items (bars, 1, expected_second);
}

static void
test_missing_pathbar_is_restored (void)
{
    const gchar * const expected[] = { NEMO_TOOLBAR_ITEM_PATHBAR, NEMO_ACTION_UP, NULL };
    NemoToolbarLayout *layout = nemo_toolbar_layout_get_default ();
    GList *bars = NULL;

    /* Losing it would leave no way to type a location, so it comes back on
     * the first bar. */
    bars = g_list_append (bars, bar_with (NEMO_ACTION_UP, NULL));

    nemo_toolbar_layout_set_bars (layout, bars);

    bars = nemo_toolbar_layout_get_bars (layout);
    g_assert_cmpuint (g_list_length (bars), ==, 1);
    assert_items (bars, 0, expected);
}

static void
test_saved_file_round_trips (void)
{
    NemoToolbarLayout *layout = nemo_toolbar_layout_get_default ();
    g_autofree gchar *path = layout_path ();
    JsonParser *parser;
    JsonObject *root;
    JsonArray *saved_bars;
    JsonObject *saved;
    NemoToolbarBar *hidden;
    GList *bars = NULL;

    bars = g_list_append (bars, bar_with (NEMO_TOOLBAR_ITEM_PATHBAR, NEMO_ACTION_BACK, NULL));

    hidden = bar_with (NEMO_ACTION_ICON_VIEW, "action:1234-abcd", NULL);
    hidden->visible = FALSE;
    bars = g_list_append (bars, hidden);

    nemo_toolbar_layout_set_bars (layout, bars);

    parser = json_parser_new ();
    g_assert_true (json_parser_load_from_file (parser, path, NULL));

    root = json_node_get_object (json_parser_get_root (parser));
    g_assert_cmpint (json_object_get_int_member (root, "version"), ==, 1);

    saved_bars = json_object_get_array_member (root, "bars");
    g_assert_cmpuint (json_array_get_length (saved_bars), ==, 2);

    saved = json_array_get_object_element (saved_bars, 0);
    g_assert_true (json_object_get_boolean_member (saved, "visible"));
    g_assert_cmpstr (json_array_get_string_element (json_object_get_array_member (saved, "items"), 0),
                     ==, NEMO_TOOLBAR_ITEM_PATHBAR);

    saved = json_array_get_object_element (saved_bars, 1);
    g_assert_false (json_object_get_boolean_member (saved, "visible"));

    /* A user action is stored by uuid, whether or not its file is installed. */
    g_assert_cmpstr (json_array_get_string_element (json_object_get_array_member (saved, "items"), 1),
                     ==, "action:1234-abcd");

    g_object_unref (parser);
}

typedef struct {
    NemoToolbarLayout   *layout;
    const gchar * const *expected;
    GMainLoop           *loop;
    gboolean             matched;
} ReloadWait;

static gboolean
bars_match (GList *bars, const gchar * const *expected)
{
    NemoToolbarBar *bar;
    GList *item;
    guint i;

    if (g_list_length (bars) != 1) {
        return FALSE;
    }

    bar = bars->data;
    item = bar->items;

    for (i = 0; expected[i] != NULL; i++) {
        if (item == NULL || g_strcmp0 (item->data, expected[i]) != 0) {
            return FALSE;
        }

        item = item->next;
    }

    return item == NULL;
}

static void
layout_changed (NemoToolbarLayout *layout,
                gpointer           user_data)
{
    ReloadWait *wait = user_data;

    /* Writes from the earlier tests can still be in flight, so wait for the
     * reload that carries the content this test wrote. */
    if (bars_match (nemo_toolbar_layout_get_bars (layout), wait->expected)) {
        wait->matched = TRUE;
        g_main_loop_quit (wait->loop);
    }
}

static gboolean
give_up (gpointer user_data)
{
    ReloadWait *wait = user_data;

    g_main_loop_quit (wait->loop);

    return G_SOURCE_REMOVE;
}

static void
test_edited_file_is_reloaded (void)
{
    /* "No Such Button" is gone; an action whose file is not installed stays,
     * so it returns when the file does. */
    const gchar * const expected[] = {
        NEMO_ACTION_BACK, "action:1234-abcd", NEMO_TOOLBAR_ITEM_PATHBAR, NULL
    };
    const gchar *edited =
        "{\"version\": 1, \"bars\": [{\"visible\": true, \"items\": ["
        "\"Back\", \"No Such Button\", \"action:1234-abcd\", \"__pathbar__\"]}]}";
    g_autofree gchar *path = layout_path ();
    ReloadWait wait = { 0 };
    gulong handler;
    guint timeout;

    wait.layout = nemo_toolbar_layout_get_default ();
    wait.expected = expected;
    wait.loop = g_main_loop_new (NULL, FALSE);

    handler = g_signal_connect (wait.layout, "changed", G_CALLBACK (layout_changed), &wait);
    timeout = g_timeout_add (RELOAD_TIMEOUT_MS, give_up, &wait);

    g_assert_true (g_file_set_contents (path, edited, -1, NULL));

    g_main_loop_run (wait.loop);

    g_assert_true (wait.matched);

    g_source_remove (timeout);
    g_signal_handler_disconnect (wait.layout, handler);
    g_main_loop_unref (wait.loop);
}

/* Leave only the two keys test_migration expects, so the seeded layout is a
 * fact about the keys and not about whatever the schema defaults happen to
 * be. */
static void
seed_legacy_preferences (void)
{
    gchar **keys;
    guint i;

    nemo_preferences = g_settings_new ("org.nemo.preferences");

    keys = g_settings_list_keys (nemo_preferences);

    for (i = 0; keys[i] != NULL; i++) {
        if (g_str_has_prefix (keys[i], "show-") && g_str_has_suffix (keys[i], "-toolbar")) {
            g_settings_set_boolean (nemo_preferences, keys[i], FALSE);
        }
    }

    g_strfreev (keys);

    g_settings_set_boolean (nemo_preferences, "show-home-icon-toolbar", TRUE);
    g_settings_set_boolean (nemo_preferences, "show-search-icon-toolbar", TRUE);
}

int
main (int argc, char *argv[])
{
    g_autofree gchar *path = NULL;

    g_test_init (&argc, &argv, NULL);

    /* The config directory survives between runs; test_migration needs the
     * layout file gone. */
    path = layout_path ();
    g_remove (path);

    seed_legacy_preferences ();

    g_test_add_func ("/toolbar-layout/catalog", test_catalog);
    g_test_add_func ("/toolbar-layout/action-ids", test_action_ids);
    g_test_add_func ("/toolbar-layout/migration", test_migration);
    g_test_add_func ("/toolbar-layout/view-items-are-opt-in", test_view_items_are_opt_in);
    g_test_add_func ("/toolbar-layout/second-pathbar-is-dropped", test_second_pathbar_is_dropped);
    g_test_add_func ("/toolbar-layout/missing-pathbar-is-restored", test_missing_pathbar_is_restored);
    g_test_add_func ("/toolbar-layout/saved-file-round-trips", test_saved_file_round_trips);
    g_test_add_func ("/toolbar-layout/edited-file-is-reloaded", test_edited_file_is_reloaded);

    return g_test_run ();
}
