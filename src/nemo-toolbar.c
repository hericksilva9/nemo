/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * Nemo
 *
 * Copyright (C) 2011, Red Hat, Inc.
 *
 * Nemo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nemo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 * Author: Cosimo Cecchi <cosimoc@redhat.com>
 *
 */

#include <config.h>

#include "nemo-toolbar.h"

#include "nemo-location-bar.h"
#include "nemo-pathbar.h"
#include "nemo-toolbar-layout.h"
#include "nemo-view.h"
#include "nemo-window-private.h"
#include "nemo-actions.h"
#include "nemo-file-utilities.h"
#include <glib/gi18n.h>
#include <libnemo-private/nemo-action-manager.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-ui-utilities.h>

#define ROW_VISIBLE_KEY "nemo-toolbar-row-visible"

struct _NemoToolbarPriv {
	GtkActionGroup *action_group;
	GtkUIManager *ui_manager;

    GList *rows;
    GtkSizeGroup *row_sizes;

    GtkWidget *pathbar_holder;
	GtkWidget *path_bar;
	GtkWidget *location_bar;
    GtkWidget *root_bar;
    GtkWidget *stack;

    NemoToolbarLayout *layout;

    /* The toolbar keeps action objects of its own. The ones a view holds are
     * hidden whenever it rebuilds its menus (nemo-action-manager.c), so a
     * button watching those would blink out with every menu refresh. */
    NemoActionManager *action_manager;
    NemoView *action_view; /* weak: whichever tab is on top */

    /* Buttons for the actions that belong to a view rather than to the
     * window, kept so they can be rebound when the view underneath changes. */
    GList *view_buttons;

	gboolean show_main_bar;
	gboolean show_location_entry;
    gboolean show_root_bar;
};

enum {
	PROP_ACTION_GROUP = 1,
	PROP_SHOW_LOCATION_ENTRY,
	PROP_SHOW_MAIN_BAR,
	NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

enum {
    CHECK_ADMIN_LOCATION,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (NemoToolbar, nemo_toolbar, GTK_TYPE_BOX);

static void toolbar_forget_action_view (NemoToolbar *self);
static void toolbar_sync_action_states (NemoToolbar *self);
static void toolbar_sync_view_state (NemoToolbar *self);

static void
nemo_toolbar_update_root_state (NemoToolbar *self)
{
    gboolean is_admin_uri;

    g_signal_emit (self, signals[CHECK_ADMIN_LOCATION], 0, &is_admin_uri);

    if ((is_admin_uri ||
         (nemo_user_is_root () && !nemo_treating_root_as_normal())) &&
         g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_ROOT_WARNING)) {
        if (self->priv->show_root_bar != TRUE) {
            self->priv->show_root_bar = TRUE;
        }
    } else {
        self->priv->show_root_bar = FALSE;
    }
}

static void
toolbar_update_appearance (NemoToolbar *self)
{
    GList *l;

    nemo_toolbar_update_root_state (self);

    for (l = self->priv->rows; l != NULL; l = l->next) {
        gboolean bar_visible;

        bar_visible = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (l->data), ROW_VISIBLE_KEY));
        gtk_widget_set_visible (GTK_WIDGET (l->data),
                                self->priv->show_main_bar && bar_visible);
    }

    if (self->priv->show_location_entry) {
        gtk_stack_set_visible_child_name (GTK_STACK (self->priv->stack), "location_bar");
    } else {
        gtk_stack_set_visible_child_name (GTK_STACK (self->priv->stack), "path_bar");
    }

    gtk_widget_set_visible (self->priv->root_bar,
                self->priv->show_root_bar);
}

static void
setup_root_info_bar (NemoToolbar *self) {

    GtkWidget *root_bar = gtk_info_bar_new ();
    gtk_info_bar_set_message_type (GTK_INFO_BAR (root_bar), GTK_MESSAGE_ERROR);
    GtkWidget *content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (root_bar));

    GtkWidget *label = gtk_label_new (_("Elevated Privileges"));
    gtk_widget_show (label);
    gtk_container_add (GTK_CONTAINER (content_area), label);

    self->priv->root_bar = root_bar;
    gtk_box_pack_start (GTK_BOX (self), self->priv->root_bar, TRUE, TRUE, 0);
}

static GtkWidget *
toolbar_button_for_action (GtkAction *action,
                           gboolean   create_toggle)
{
    GtkWidget *button;
    GtkWidget *image;

    if (create_toggle)
    {
        button = gtk_toggle_button_new ();
    } else {
        button = gtk_button_new ();
    }

    image = gtk_image_new ();

    gtk_button_set_image (GTK_BUTTON (button), image);
    gtk_activatable_set_related_action (GTK_ACTIVATABLE (button), action);
    gtk_button_set_label (GTK_BUTTON (button), NULL);
    gtk_widget_set_tooltip_text (button, gtk_action_get_tooltip (action));
    gtk_widget_set_can_focus (button, FALSE);
    gtk_style_context_add_class (gtk_widget_get_style_context (button), GTK_STYLE_CLASS_FLAT);

    return button;
}

static GtkWidget *
toolbar_create_toolbutton (NemoToolbar *self,
                gboolean create_toggle,
                const gchar *name)
{
    return toolbar_button_for_action (gtk_action_group_get_action (self->priv->action_group, name),
                                      create_toggle);
}

static GtkWindow *
toolbar_get_window (NemoToolbar *self)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));

    return GTK_IS_WINDOW (toplevel) ? GTK_WINDOW (toplevel) : NULL;
}

#define TOOLBAR_ACTION_KEY "nemo-toolbar-action"

static void
toolbar_action_button_clicked (GtkButton   *button,
                               NemoToolbar *self)
{
    NemoAction *action = g_object_get_data (G_OBJECT (button), TOOLBAR_ACTION_KEY);
    GList *selection;

    if (self->priv->action_view == NULL) {
        return;
    }

    selection = nemo_view_get_selection (self->priv->action_view);

    nemo_action_activate (action, selection,
                          nemo_view_get_directory_as_file (self->priv->action_view),
                          toolbar_get_window (self));

    nemo_file_list_free (selection);
}

/* An action says "does not apply here" by turning invisible, which suits the
 * context menu it was designed for. A toolbar button that vanishes shifts
 * every button beside it, so the state is shown the way the built-in buttons
 * show it: greyed out, in place. */
static void
toolbar_action_notify_visible (NemoAction *action,
                               GParamSpec *pspec,
                               GtkWidget  *button)
{
    gtk_widget_set_sensitive (button, gtk_action_is_visible (GTK_ACTION (action)));
}

/* Whether an action applies depends on what is selected, which only the view
 * knows, so its state is recomputed here rather than read off the view. */
static void
toolbar_sync_action_states (NemoToolbar *self)
{
    GList *selection, *l;
    NemoFile *parent;
    GtkWindow *window;

    if (self->priv->action_view == NULL) {
        return;
    }

    window = toolbar_get_window (self);

    if (window == NULL) {
        return;
    }

    selection = nemo_view_get_selection (self->priv->action_view);
    parent = nemo_view_get_directory_as_file (self->priv->action_view);

    for (l = nemo_action_manager_list_actions (self->priv->action_manager); l != NULL; l = l->next) {
        nemo_action_update_display_state (NEMO_ACTION (l->data), selection, parent, FALSE, window);
    }

    nemo_file_list_free (selection);
}

static GtkWidget *
toolbar_create_action_button (NemoToolbar *self,
                              const gchar *id)
{
    NemoAction *action;
    GtkWidget *button;
    GIcon *icon;

    action = nemo_action_manager_get_action (self->priv->action_manager,
                                             nemo_toolbar_layout_action_uuid (id));

    if (action == NULL) {
        return NULL;
    }

    /* Deliberately not bound with gtk_activatable_set_related_action: that
     * syncs the action's visibility onto the button, and its label over the
     * icon. Only the state and the activation are wanted. */
    button = gtk_button_new ();
    icon = gtk_action_get_gicon (GTK_ACTION (action));

    if (icon != NULL) {
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_BUTTON));
    }

    gtk_widget_set_tooltip_text (button, gtk_action_get_tooltip (GTK_ACTION (action)));
    gtk_widget_set_can_focus (button, FALSE);
    gtk_style_context_add_class (gtk_widget_get_style_context (button), GTK_STYLE_CLASS_FLAT);

    g_object_set_data (G_OBJECT (button), TOOLBAR_ACTION_KEY, action);
    gtk_widget_set_sensitive (button, gtk_action_is_visible (GTK_ACTION (action)));

    g_signal_connect_object (action, "notify::visible",
                             G_CALLBACK (toolbar_action_notify_visible), button, 0);
    g_signal_connect (button, "clicked",
                      G_CALLBACK (toolbar_action_button_clicked), self);

    return button;
}

#define VIEW_ACTION_NAME_KEY "nemo-toolbar-view-action-name"
#define VIEW_ACTION_KEY "nemo-toolbar-view-action"

/* Same reasoning as the user actions above: an action that does not apply to
 * the current location turns invisible, so it is shown greyed out in place
 * rather than left to disappear and shift the rest of the row. */
static void
toolbar_view_action_state_changed (GtkAction  *action,
                                   GParamSpec *pspec,
                                   GtkWidget  *button)
{
    gtk_widget_set_sensitive (button,
                              gtk_action_is_sensitive (action) &&
                              gtk_action_is_visible (action));
}

/* The action a view button drives is owned by whichever view is on top, and is
 * a different object after every tab switch, so the button keeps only its name
 * and looks the action up again each time the view changes. */
static void
toolbar_bind_view_button (NemoToolbar *self,
                          GtkWidget   *button)
{
    const gchar *name;
    GtkAction *action = NULL;
    GtkAction *bound;

    name = g_object_get_data (G_OBJECT (button), VIEW_ACTION_NAME_KEY);
    bound = g_object_get_data (G_OBJECT (button), VIEW_ACTION_KEY);

    if (self->priv->action_view != NULL) {
        action = nemo_view_get_action (self->priv->action_view, name);
    }

    if (action == bound) {
        if (action != NULL) {
            toolbar_view_action_state_changed (action, NULL, button);
        }

        return;
    }

    if (bound != NULL) {
        g_signal_handlers_disconnect_by_func (bound, toolbar_view_action_state_changed, button);
    }

    g_object_set_data_full (G_OBJECT (button), VIEW_ACTION_KEY,
                            action != NULL ? g_object_ref (action) : NULL,
                            g_object_unref);

    /* No view is active, or its menus are not merged: nothing to act on. */
    if (action == NULL) {
        gtk_widget_set_sensitive (button, FALSE);
        return;
    }

    gtk_widget_set_tooltip_text (button, gtk_action_get_tooltip (action));

    g_signal_connect_object (action, "notify::sensitive",
                             G_CALLBACK (toolbar_view_action_state_changed), button, 0);
    g_signal_connect_object (action, "notify::visible",
                             G_CALLBACK (toolbar_view_action_state_changed), button, 0);

    toolbar_view_action_state_changed (action, NULL, button);
}

static void
toolbar_bind_view_buttons (NemoToolbar *self)
{
    GList *l;

    for (l = self->priv->view_buttons; l != NULL; l = l->next) {
        toolbar_bind_view_button (self, l->data);
    }
}

static void
toolbar_view_button_clicked (GtkButton   *button,
                             NemoToolbar *self)
{
    GtkAction *action = g_object_get_data (G_OBJECT (button), VIEW_ACTION_KEY);

    if (action != NULL) {
        gtk_action_activate (action);
    }
}

static GtkWidget *
toolbar_create_view_button (NemoToolbar               *self,
                            const NemoToolbarItemInfo *info)
{
    GtkWidget *button;

    /* Deliberately not bound with gtk_activatable_set_related_action: that
     * would tie the button to one view's action for good, and would hide it
     * whenever the action goes invisible. */
    button = gtk_button_new ();
    gtk_button_set_image (GTK_BUTTON (button),
                          gtk_image_new_from_icon_name (info->icon_name, GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text (button, _(info->label));
    gtk_widget_set_can_focus (button, FALSE);
    gtk_widget_set_sensitive (button, FALSE);
    gtk_style_context_add_class (gtk_widget_get_style_context (button), GTK_STYLE_CLASS_FLAT);

    g_object_set_data (G_OBJECT (button), VIEW_ACTION_NAME_KEY, (gpointer) info->id);
    g_signal_connect (button, "clicked",
                      G_CALLBACK (toolbar_view_button_clicked), self);

    self->priv->view_buttons = g_list_prepend (self->priv->view_buttons, button);
    toolbar_bind_view_button (self, button);

    return button;
}

/* Consecutive buttons share one tool item so they keep the tight 2px spacing
 * of the original toolbar, with a 6px gap against the path bar. */
static void
flush_button_box (GtkWidget  *row,
                  GtkWidget **box,
                  gboolean    margin_left,
                  gboolean    margin_right)
{
    GtkToolItem *tool_box;

    if (*box == NULL) {
        return;
    }

    tool_box = gtk_tool_item_new ();
    gtk_container_add (GTK_CONTAINER (tool_box), *box);
    gtk_container_add (GTK_CONTAINER (row), GTK_WIDGET (tool_box));
    gtk_widget_show_all (GTK_WIDGET (tool_box));

    if (margin_left) {
        gtk_widget_set_margin_left (GTK_WIDGET (tool_box), 6);
    }

    if (margin_right) {
        gtk_widget_set_margin_right (GTK_WIDGET (tool_box), 6);
    }

    *box = NULL;
}

static void
add_pathbar_item (NemoToolbar *self,
                  GtkWidget   *row)
{
    GtkToolItem *tool_box;

    tool_box = gtk_tool_item_new ();
    gtk_tool_item_set_expand (tool_box, TRUE);
    gtk_container_add (GTK_CONTAINER (tool_box), self->priv->pathbar_holder);
    gtk_container_add (GTK_CONTAINER (row), GTK_WIDGET (tool_box));
    gtk_widget_show (GTK_WIDGET (tool_box));
}

static GtkWidget *
build_row (NemoToolbar    *self,
           NemoToolbarBar *bar)
{
    GtkWidget *row;
    GtkWidget *box = NULL;
    gboolean after_pathbar = FALSE;
    GList *l;

    row = gtk_toolbar_new ();
    gtk_style_context_add_class (gtk_widget_get_style_context (row),
                                 GTK_STYLE_CLASS_PRIMARY_TOOLBAR);

    for (l = bar->items; l != NULL; l = l->next) {
        const NemoToolbarItemInfo *info;

        if (g_strcmp0 (l->data, NEMO_TOOLBAR_ITEM_PATHBAR) == 0) {
            flush_button_box (row, &box, after_pathbar, TRUE);
            add_pathbar_item (self, row);
            after_pathbar = TRUE;
            continue;
        }

        if (nemo_toolbar_layout_id_is_action (l->data)) {
            GtkWidget *button = toolbar_create_action_button (self, l->data);

            if (button == NULL) {
                continue;
            }

            if (box == NULL) {
                box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
            }

            gtk_container_add (GTK_CONTAINER (box), button);
            continue;
        }

        info = nemo_toolbar_layout_lookup_item (l->data);

        if (box == NULL) {
            box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
        }

        gtk_container_add (GTK_CONTAINER (box),
                           info->from_view ? toolbar_create_view_button (self, info)
                                           : toolbar_create_toolbutton (self, info->is_toggle, info->id));
    }

    flush_button_box (row, &box, after_pathbar, FALSE);

    g_object_set_data (G_OBJECT (row), ROW_VISIBLE_KEY, GINT_TO_POINTER (bar->visible));

    return row;
}

static void
rebuild_rows (NemoToolbar *self)
{
    GtkWidget *parent;
    GList *bars, *l;

    /* The path bar is reparented rather than recreated, so the handlers the
     * window pane connected to it survive a layout change. */
    parent = gtk_widget_get_parent (self->priv->pathbar_holder);

    if (parent != NULL) {
        gtk_container_remove (GTK_CONTAINER (parent), self->priv->pathbar_holder);
    }

    g_list_free_full (self->priv->rows, (GDestroyNotify) gtk_widget_destroy);
    self->priv->rows = NULL;

    /* Destroyed along with the rows that held them. */
    g_list_free (self->priv->view_buttons);
    self->priv->view_buttons = NULL;

    /* An empty toolbar has nothing to give it height, so it would come up as a
     * sliver until its first button lands. The bar holding the path bar is
     * never empty, so there is always a populated row to take the height from. */
    g_clear_object (&self->priv->row_sizes);
    self->priv->row_sizes = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);

    bars = nemo_toolbar_layout_get_bars (self->priv->layout);

    for (l = bars; l != NULL; l = l->next) {
        GtkWidget *row;

        row = build_row (self, l->data);
        gtk_box_pack_start (GTK_BOX (self), row, TRUE, TRUE, 0);
        gtk_size_group_add_widget (self->priv->row_sizes, row);
        self->priv->rows = g_list_append (self->priv->rows, row);
    }

    /* Keep the privilege warning underneath every row. */
    gtk_box_reorder_child (GTK_BOX (self), self->priv->root_bar, -1);

    toolbar_update_appearance (self);

    /* Fresh action objects start out with no idea of the current selection. */
    toolbar_sync_action_states (self);
}

static void
nemo_toolbar_constructed (GObject *obj)
{
	NemoToolbar *self = NEMO_TOOLBAR (obj);
    GtkWidget *hbox;

	G_OBJECT_CLASS (nemo_toolbar_parent_class)->constructed (obj);

	gtk_style_context_set_junction_sides (gtk_widget_get_style_context (GTK_WIDGET (self)),
					      GTK_JUNCTION_BOTTOM);

    self->priv->show_location_entry = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_LOCATION_ENTRY);

	/* add the UI */
	self->priv->ui_manager = gtk_ui_manager_new ();
	gtk_ui_manager_insert_action_group (self->priv->ui_manager, self->priv->action_group, 0);

    /* Container to hold the location and pathbars */
    self->priv->stack = gtk_stack_new();
    gtk_stack_set_transition_type (GTK_STACK (self->priv->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration (GTK_STACK (self->priv->stack), 150);

    /* Regular Path Bar */
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (self->priv->stack), TRUE, TRUE, 0);

    self->priv->path_bar = g_object_new (NEMO_TYPE_PATH_BAR, NULL);
    gtk_stack_add_named(GTK_STACK (self->priv->stack), GTK_WIDGET (self->priv->path_bar), "path_bar");

    /* Entry-Like Location Bar */
    self->priv->location_bar = nemo_location_bar_new ();
    gtk_stack_add_named(GTK_STACK (self->priv->stack), GTK_WIDGET (self->priv->location_bar), "location_bar");
    gtk_widget_show_all (hbox);

    self->priv->pathbar_holder = g_object_ref_sink (hbox);

    setup_root_info_bar (self);

    self->priv->action_manager = nemo_action_manager_new ();

    /* Actions load in the background, and editing one rebuilds the objects. */
    g_signal_connect_swapped (self->priv->action_manager, "changed",
                              G_CALLBACK (rebuild_rows), self);

    self->priv->layout = nemo_toolbar_layout_get_default ();

    g_signal_connect_object (self->priv->layout, "changed",
                             G_CALLBACK (rebuild_rows), self,
                             G_CONNECT_SWAPPED);

    rebuild_rows (self);

    g_signal_connect_swapped (nemo_preferences,
                  "changed",
                  G_CALLBACK (toolbar_update_appearance), self);
}

static void
nemo_toolbar_init (NemoToolbar *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NEMO_TYPE_TOOLBAR,
						  NemoToolbarPriv);
	self->priv->show_main_bar = TRUE;
}

static void
nemo_toolbar_get_property (GObject *object,
			       guint property_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	NemoToolbar *self = NEMO_TOOLBAR (object);

	switch (property_id) {
	case PROP_SHOW_LOCATION_ENTRY:
		g_value_set_boolean (value, self->priv->show_location_entry);
		break;
	case PROP_SHOW_MAIN_BAR:
		g_value_set_boolean (value, self->priv->show_main_bar);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
nemo_toolbar_set_property (GObject *object,
			       guint property_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	NemoToolbar *self = NEMO_TOOLBAR (object);

	switch (property_id) {
	case PROP_ACTION_GROUP:
		self->priv->action_group = g_value_dup_object (value);
		break;
	case PROP_SHOW_LOCATION_ENTRY:
		nemo_toolbar_set_show_location_entry (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_MAIN_BAR:
		nemo_toolbar_set_show_main_bar (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
nemo_toolbar_dispose (GObject *obj)
{
	NemoToolbar *self = NEMO_TOOLBAR (obj);

	toolbar_forget_action_view (self);

	g_clear_object (&self->priv->action_manager);
	g_clear_object (&self->priv->action_group);
	g_clear_object (&self->priv->pathbar_holder);
	g_clear_object (&self->priv->row_sizes);

	g_list_free (self->priv->rows);
	self->priv->rows = NULL;

	g_list_free (self->priv->view_buttons);
	self->priv->view_buttons = NULL;

	g_signal_handlers_disconnect_by_func (nemo_preferences,
					      toolbar_update_appearance, self);

	G_OBJECT_CLASS (nemo_toolbar_parent_class)->dispose (obj);
}

static void
nemo_toolbar_class_init (NemoToolbarClass *klass)
{
	GObjectClass *oclass;

	oclass = G_OBJECT_CLASS (klass);
	oclass->get_property = nemo_toolbar_get_property;
	oclass->set_property = nemo_toolbar_set_property;
	oclass->constructed = nemo_toolbar_constructed;
	oclass->dispose = nemo_toolbar_dispose;

	properties[PROP_ACTION_GROUP] =
		g_param_spec_object ("action-group",
				     "The action group",
				     "The action group to get actions from",
				     GTK_TYPE_ACTION_GROUP,
				     G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS);
	properties[PROP_SHOW_LOCATION_ENTRY] =
		g_param_spec_boolean ("show-location-entry",
				      "Whether to show the location entry",
				      "Whether to show the location entry instead of the pathbar",
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	properties[PROP_SHOW_MAIN_BAR] =
		g_param_spec_boolean ("show-main-bar",
				      "Whether to show the main bar",
				      "Whether to show the main toolbar",
				      TRUE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    signals[CHECK_ADMIN_LOCATION] =
        g_signal_new ("check-admin-location",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_BOOLEAN, 0);

	g_type_class_add_private (klass, sizeof (NemoToolbarClass));
	g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);
}

GtkWidget *
nemo_toolbar_new (GtkActionGroup *action_group)
{
	return g_object_new (NEMO_TYPE_TOOLBAR,
			     "action-group", action_group,
			     "orientation", GTK_ORIENTATION_VERTICAL,
			     NULL);
}

GtkWidget *
nemo_toolbar_get_path_bar (NemoToolbar *self)
{
	return self->priv->path_bar;
}

GtkWidget *
nemo_toolbar_get_location_bar (NemoToolbar *self)
{
	return self->priv->location_bar;
}

gboolean
nemo_toolbar_get_show_location_entry (NemoToolbar *self)
{
	return self->priv->show_location_entry;
}

void
nemo_toolbar_set_show_main_bar (NemoToolbar *self,
				    gboolean show_main_bar)
{
	if (show_main_bar != self->priv->show_main_bar) {
		self->priv->show_main_bar = show_main_bar;
		toolbar_update_appearance (self);

		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_MAIN_BAR]);
	}
}

void
nemo_toolbar_set_show_location_entry (NemoToolbar *self,
					  gboolean show_location_entry)
{
	if (show_location_entry != self->priv->show_location_entry) {
		self->priv->show_location_entry = show_location_entry;
		toolbar_update_appearance (self);

		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_LOCATION_ENTRY]);
	}
}

void
nemo_toolbar_update_for_location (NemoToolbar *self)
{
    toolbar_update_appearance (self);
}

/* Everything on the toolbar that depends on what the view is showing. */
static void
toolbar_sync_view_state (NemoToolbar *self)
{
    toolbar_bind_view_buttons (self);
    toolbar_sync_action_states (self);
}

static void
toolbar_forget_action_view (NemoToolbar *self)
{
    if (self->priv->action_view == NULL) {
        return;
    }

    g_signal_handlers_disconnect_by_func (self->priv->action_view, toolbar_sync_view_state, self);
    g_object_remove_weak_pointer (G_OBJECT (self->priv->action_view),
                                  (gpointer *) &self->priv->action_view);
    self->priv->action_view = NULL;
}

void
nemo_toolbar_set_action_view (NemoToolbar *self,
                              NemoView    *view)
{
    if (view == self->priv->action_view) {
        /* Called again for the same view once its menus are merged, which is
         * when its actions become available to look up. */
        toolbar_sync_view_state (self);
        return;
    }

    toolbar_forget_action_view (self);

    if (view != NULL) {
        self->priv->action_view = view;
        g_object_add_weak_pointer (G_OBJECT (view), (gpointer *) &self->priv->action_view);

        g_signal_connect_swapped (view, "selection-changed",
                                  G_CALLBACK (toolbar_sync_view_state), self);
    }

    toolbar_sync_view_state (self);
}
