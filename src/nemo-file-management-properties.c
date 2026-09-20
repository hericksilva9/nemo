/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-file-management-properties.c - Functions to create and show the nemo preference dialog.

   Copyright (C) 2002 Jan Arne Petersen

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: Jan Arne Petersen <jpetersen@uni-bonn.de>
*/

#include <config.h>

#include "nemo-file-management-properties.h"
#include "nemo-toolbar-layout.h"

#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include <glib/gi18n.h>

#include <eel/eel-glib-extensions.h>

#include <libnemo-private/nemo-column-chooser.h>
#include <libnemo-private/nemo-column-utilities.h>
#include <libnemo-private/nemo-action-manager.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-module.h>

#include "nemo-plugin-manager.h"
#include "nemo-template-config-widget.h"
#include "nemo-actions.h"

/* string enum preferences */
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DEFAULT_VIEW_WIDGET "default_view_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ICON_VIEW_ZOOM_WIDGET "icon_view_zoom_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_COMPACT_VIEW_ZOOM_WIDGET "compact_view_zoom_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_LIST_VIEW_ZOOM_WIDGET "list_view_zoom_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SORT_ORDER_WIDGET "sort_order_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET "date_format_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FONT_CHOICE_WIDGET "date_font_choice_combobox"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_IMAGE_WIDGET "preview_image_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_FOLDER_WIDGET "preview_folder_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SIZE_PREFIXES_WIDGET "size_prefixes_combobox"

/* bool preferences */
#define NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_VIEW_WIDGET "inherit_view_checkbox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_REVERSE_SORT_WIDGET "reverse_sort_checkbox"
#define NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN "quick_renames_with_pause_in_between"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_FAVORITES_FIRST_WIDGET "sort_favorites_first_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_FOLDERS_FIRST_WIDGET "sort_folders_first_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_COMPACT_LAYOUT_WIDGET "compact_layout_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_LABELS_BESIDE_ICONS_WIDGET "labels_beside_icons_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ALL_COLUMNS_SAME_WIDTH "all_columns_same_width_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ALWAYS_USE_BROWSER_WIDGET "always_use_browser_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_MOVE_WIDGET "trash_confirm_move_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_WIDGET "trash_confirm_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_DELETE_WIDGET "trash_delete_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SWAP_TRASH_DELETE "swap_trash_binding_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_OPEN_NEW_WINDOW_WIDGET "new_window_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TREE_VIEW_FOLDERS_WIDGET "treeview_folders_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_LIST_VIEW_EXPANDERS_WIDGET "list_view_show_expanders_checkbutton"


#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_FULL_PATH_IN_TITLE_BARS_WIDGET "show_full_path_in_title_bars_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_CLOSE_DEVICE_VIEW_ON_EJECT_WIDGET "close_device_view_on_eject_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOMOUNT_MEDIA_WIDGET "media_automount_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOOPEN_MEDIA_WIDGET "media_autoopen_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_AUTORUN_MEDIA_WIDGET "media_autorun_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DETECT_CONTENT_MEDIA_WIDGET "media_detect_content_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_ADVANCED_PERMISSIONS_WIDGET "show_advanced_permissions_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_START_WITH_DUAL_PANE_WIDGET "start_with_dual_pane_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_RESTORE_TABS_ON_STARTUP_WIDGET "restore_tabs_on_startup_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_IGNORE_VIEW_METADATA_WIDGET "ignore_view_metadata_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_BOOKMARKS_IN_TO_MENUS_WIDGET "bookmarks_in_to_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_PLACES_IN_TO_MENUS_WIDGET "places_in_to_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_SHOW_THUMBNAILS_WIDGET "inherit_show_thumbnails_checkbutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_BULK_RENAME_WIDGET "bulk_rename_entry"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET "tooltips_on_icon_view_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET "tooltips_on_list_view_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_DESKTOP_WIDGET "tooltips_on_desktop_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FILE_TYPE_WIDGET "tt_show_file_type_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_MOD_DATE_WIDGET "tt_show_modified_date_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_ACCESS_DATE_WIDGET "tt_show_accessed_date_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_CREATED_DATE_WIDGET "tt_show_created_date_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FULL_PATH_WIDGET "tt_show_full_path_checkbutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_SKIP_FILE_OP_QUEUE_WIDGET "skip_file_op_queue_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_CLICK_DBL_PARENT_FOLDER_WIDGET "click_double_parent_folder_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_EXPAND_ROW_ON_DND_DWELL_WIDGET "expand_row_on_dnd_dwell_checkbutton"

/* int enums */
#define NEMO_FILE_MANAGEMENT_PROPERTIES_THUMBNAIL_LIMIT_WIDGET "preview_image_size_combobox"

#define W(s) (gtk_builder_get_object (builder, s))

#define TOOLBAR_PADDING 20

static const char * const default_view_values[] = {
	"icon-view",
	"list-view",
	"compact-view",
	NULL
};

static const char * const zoom_values[] = {
	"smallest",
	"smaller",
	"small",
	"standard",
	"large",
	"larger",
	"largest",
	NULL
};

static const char * const sort_order_values[] = {
	"name",
	"size",
	"type",
    "detailed_type",
	"mtime",
	"atime",
	"trash-time",
	NULL
};

static const char * const date_format_values[] = {
	"locale",
	"iso",
	"informal",
	NULL
};

static const char * const date_font_choice_values[] = {
    "auto-mono",
    "system-mono",
    "no-mono",
    NULL
};

static const char * const preview_image_values[] = {
    "always",
    "local-only",
    "never",
    NULL
};

static const char * const preview_folder_values[] = {
	"always",
	"local-only",
	"never",
	NULL
};

static const char * const click_behavior_components[] = {
	"single_click_radiobutton",
	"double_click_radiobutton",
	NULL
};

static const char * const click_behavior_values[] = {
	"single",
	"double",
	NULL
};

static const char * const executable_text_components[] = {
	"scripts_execute_radiobutton",
	"scripts_view_radiobutton",
	"scripts_confirm_radiobutton",
	NULL
};

static const char * const executable_text_values[] = {
	"launch",
	"display",
	"ask",
	NULL
};

static const char * const size_prefixes_values[] = {
	"base-10",
	"base-10-full",
	"base-2",
	"base-2-full",
	NULL
};

static const guint64 thumbnail_limit_values[] = {
	102400,
	512000,
	1048576,
	3145728,
	5242880,
	10485760,
	104857600,
	1073741824,
	2147483648U,
	4294967295U,
	8589934592U,
	17179869184U,
	34359738368U,
	68719476736U
};

static const char * const icon_captions_components[] = {
	"captions_0_combobox",
	"captions_1_combobox",
	"captions_2_combobox",
	NULL
};

static GtkWidget *preferences_dialog = NULL;

static void
nemo_file_management_properties_size_group_create (GtkBuilder *builder,
						       char *prefix,
						       int items)
{
	GtkSizeGroup *size_group;
	int i;
	char *item_name;
	GtkWidget *widget;

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	for (i = 0; i < items; i++) {	
		item_name = g_strdup_printf ("%s_%d", prefix, i);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, item_name));
		gtk_size_group_add_widget (size_group, widget);
		g_free (item_name);
	}
	g_object_unref (G_OBJECT (size_group));
}

static void
columns_changed_callback (NemoColumnChooser *chooser,
			  gpointer callback_data)
{
	char **visible_columns;
	char **column_order;

	nemo_column_chooser_get_settings (NEMO_COLUMN_CHOOSER (chooser),
					      &visible_columns,
					      &column_order);

	g_settings_set_strv (nemo_list_view_preferences,
			     NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS,
			     (const char * const *)visible_columns);
	g_settings_set_strv (nemo_list_view_preferences,
			     NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER,
			     (const char * const *)column_order);

	g_strfreev (visible_columns);
	g_strfreev (column_order);
}

static void
free_column_names_array (GPtrArray *column_names)
{
	g_ptr_array_foreach (column_names, (GFunc) g_free, NULL);
	g_ptr_array_free (column_names, TRUE);
}

static void
create_icon_caption_combo_box_items (GtkComboBoxText *combo_box,
			             GList *columns)
{
	GList *l;
	GPtrArray *column_names;

	column_names = g_ptr_array_new ();

	/* Translators: this is referred to captions under icons. */
	gtk_combo_box_text_append_text (combo_box, _("None"));
	g_ptr_array_add (column_names, g_strdup ("none"));

	for (l = columns; l != NULL; l = l->next) {
		NemoColumn *column;
		char *name;
		char *label;

		column = NEMO_COLUMN (l->data);

		g_object_get (G_OBJECT (column), 
			      "name", &name, "label", &label, 
			      NULL);

		/* Don't show name here, it doesn't make sense */
		if (!strcmp (name, "name")) {
			g_free (name);
			g_free (label);
			continue;
		}

		gtk_combo_box_text_append_text (combo_box, label);
		g_ptr_array_add (column_names, name);

		g_free (label);
	}
	g_object_set_data_full (G_OBJECT (combo_box), "column_names",
			        column_names,
			        (GDestroyNotify) free_column_names_array);
}

static void
icon_captions_changed_callback (GtkComboBox *combo_box,
				gpointer user_data)
{
	GPtrArray *captions;
	GtkBuilder *builder;
	guint i;

	builder = GTK_BUILDER (user_data);

	captions = g_ptr_array_new ();

	for (i = 0; icon_captions_components[i] != NULL; i++) {
		int active;
		GPtrArray *column_names;
		char *name;
		GtkWidget *c_box;

		c_box = GTK_WIDGET (gtk_builder_get_object
					(builder, icon_captions_components[i]));
		active = gtk_combo_box_get_active (GTK_COMBO_BOX (c_box));

		column_names = g_object_get_data (G_OBJECT (c_box),
						  "column_names");

		name = g_ptr_array_index (column_names, active);
		g_ptr_array_add (captions, name);
	}
	g_ptr_array_add (captions, NULL);

	g_settings_set_strv (nemo_icon_view_preferences,
			     NEMO_PREFERENCES_ICON_VIEW_CAPTIONS,
			     (const char **)captions->pdata);
	g_ptr_array_free (captions, TRUE);
}

static void
update_caption_combo_box (GtkBuilder *builder,
			  const char *combo_box_name,
			  const char *name)
{
	GtkWidget *combo_box;
	guint i;
	GPtrArray *column_names;

	combo_box = GTK_WIDGET (gtk_builder_get_object (builder, combo_box_name));

	g_signal_handlers_block_by_func
		(combo_box,
		 G_CALLBACK (icon_captions_changed_callback),
		 builder);

	column_names = g_object_get_data (G_OBJECT (combo_box), 
					  "column_names");

	for (i = 0; i < column_names->len; ++i) {
		if (!strcmp (name, g_ptr_array_index (column_names, i))) {
			gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), i);
			break;
		}
	}

	g_signal_handlers_unblock_by_func
		(combo_box,
		 G_CALLBACK (icon_captions_changed_callback),
		 builder);
}

static void
update_icon_captions_from_settings (GtkBuilder *builder)
{
	char **captions;
	int i, j;

	captions = g_settings_get_strv (nemo_icon_view_preferences, NEMO_PREFERENCES_ICON_VIEW_CAPTIONS);
	if (captions == NULL)
		return;

	for (i = 0, j = 0; 
	     icon_captions_components[i] != NULL;
	     i++) {
		char *data;

		if (captions[j]) {
			data = captions[j];
			++j;
		} else {
			data = (char *)"none";
		}

		update_caption_combo_box (builder, 
					  icon_captions_components[i],
					  data);
	}

	g_strfreev (captions);
}

static void
nemo_file_management_properties_dialog_setup_icon_caption_page (GtkBuilder *builder)
{
	GList *columns;
	int i;
	gboolean writable;

	writable = g_settings_is_writable (nemo_icon_view_preferences,
					   NEMO_PREFERENCES_ICON_VIEW_CAPTIONS);

	columns = nemo_get_common_columns ();

	for (i = 0; icon_captions_components[i] != NULL; i++) {
		GtkWidget *combo_box;

		combo_box = GTK_WIDGET (gtk_builder_get_object (builder,
								icon_captions_components[i]));

		create_icon_caption_combo_box_items (GTK_COMBO_BOX_TEXT (combo_box), columns);
		gtk_widget_set_sensitive (combo_box, writable);

		g_signal_connect (combo_box, "changed",
				  G_CALLBACK (icon_captions_changed_callback),
				  builder);
	}

	nemo_column_list_free (columns);

	update_icon_captions_from_settings (builder);
}

static void
nemo_file_management_properties_dialog_setup_plugin_page (GtkBuilder *builder)
{
    GtkWidget *box;

    box = GTK_WIDGET (gtk_builder_get_object (builder, "plugin_box"));

    gtk_box_pack_start (GTK_BOX (box),
                        GTK_WIDGET (nemo_plugin_manager_new ()),
                        TRUE, TRUE, 0);
}

static void
nemo_file_management_properties_dialog_setup_templates_page (GtkBuilder *builder)
{
    GtkWidget *box;

    box = GTK_WIDGET (gtk_builder_get_object (builder, "templates_box"));

    gtk_box_pack_start (GTK_BOX (box),
                        GTK_WIDGET (nemo_template_config_widget_new ()),
                        TRUE, TRUE, 0);
}

static void
create_date_format_menu (GtkBuilder *builder)
{
	GtkComboBoxText *combo_box;
	gchar *date_string;
	GDateTime *now;

	combo_box = GTK_COMBO_BOX_TEXT
		(gtk_builder_get_object (builder,
					 NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET));

	now = g_date_time_new_now_local ();

	date_string = g_date_time_format (now, "%c");
	gtk_combo_box_text_append_text (combo_box, date_string);
	g_free (date_string);

	date_string = g_date_time_format (now, "%Y-%m-%d %H:%M:%S");
	gtk_combo_box_text_append_text (combo_box, date_string);
	g_free (date_string);

	gtk_combo_box_text_append_text (combo_box, _("Yesterday"));

	g_date_time_unref (now);
}

static void
set_columns_from_settings (NemoColumnChooser *chooser)
{
	char **visible_columns;
	char **column_order;

	visible_columns = g_settings_get_strv (nemo_list_view_preferences,
					       NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS);
	column_order = g_settings_get_strv (nemo_list_view_preferences,
					    NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER);

	nemo_column_chooser_set_settings (NEMO_COLUMN_CHOOSER (chooser),
					      visible_columns,
					      column_order);

	g_strfreev (visible_columns);
	g_strfreev (column_order);
}

static void
use_default_callback (NemoColumnChooser *chooser,
		      gpointer user_data)
{
	g_settings_reset (nemo_list_view_preferences,
			  NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS);
	g_settings_reset (nemo_list_view_preferences,
			  NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER);
	set_columns_from_settings (chooser);
}

static void
nemo_file_management_properties_dialog_setup_list_column_page (GtkBuilder *builder)
{
	GtkWidget *chooser;
	GtkWidget *box;

	chooser = nemo_column_chooser_new (NULL);

	set_columns_from_settings (NEMO_COLUMN_CHOOSER (chooser));

	g_signal_connect (chooser, "changed",
			  G_CALLBACK (columns_changed_callback), chooser);
	g_signal_connect (chooser, "use_default",
			  G_CALLBACK (use_default_callback), chooser);

	gtk_widget_show (chooser);
	box = GTK_WIDGET (gtk_builder_get_object (builder, "list_columns_vbox"));

	gtk_box_pack_start (GTK_BOX (box), chooser, TRUE, TRUE, 0);
}

static void
bind_builder_bool (GtkBuilder *builder,
		   GSettings *settings,
		   const char *widget_name,
		   const char *prefs)
{
	g_settings_bind (settings, prefs,
			 gtk_builder_get_object (builder, widget_name),
			 "active", G_SETTINGS_BIND_DEFAULT);
}

static void
bind_builder_bool_inverted (GtkBuilder *builder,
			    GSettings *settings,
			    const char *widget_name,
			    const char *prefs)
{
	g_settings_bind (settings, prefs,
			 gtk_builder_get_object (builder, widget_name),
			 "active", G_SETTINGS_BIND_INVERT_BOOLEAN);
}

static void
bind_builder_string_entry (GtkBuilder *builder,
                            GSettings *settings,
                           const char *widget_name,
                           const char *prefs)
{
    g_settings_bind (settings, prefs,
                     gtk_builder_get_object (builder, widget_name),
                     "text", G_SETTINGS_BIND_DEFAULT);
}

static gboolean
enum_get_mapping (GValue             *value,
		  GVariant           *variant,
		  gpointer            user_data)
{
	const char **enum_values = user_data;
	const char *str;
	int i;

	str = g_variant_get_string (variant, NULL);
	for (i = 0; enum_values[i] != NULL; i++) {
		if (strcmp (enum_values[i], str) == 0) {
			g_value_set_int (value, i);
			return TRUE;
		}
	}

	return FALSE;
}

static GVariant *
enum_set_mapping (const GValue       *value,
		  const GVariantType *expected_type,
		  gpointer            user_data)
{
	const char **enum_values = user_data;

	return g_variant_new_string (enum_values[g_value_get_int (value)]);
}

static void
bind_builder_enum (GtkBuilder *builder,
		   GSettings *settings,
		   const char *widget_name,
		   const char *prefs,
		   const char **enum_values)
{
	g_settings_bind_with_mapping (settings, prefs,
				      gtk_builder_get_object (builder, widget_name),
				      "active", G_SETTINGS_BIND_DEFAULT,
				      enum_get_mapping,
				      enum_set_mapping,
				      enum_values, NULL);
}


typedef struct {
	const guint64 *values;
	int n_values;
} UIntEnumBinding;

static gboolean
uint_enum_get_mapping (GValue             *value,
		       GVariant           *variant,
		       gpointer            user_data)
{
	UIntEnumBinding *binding = user_data;
	guint64 v;
	int i;

	v = g_variant_get_uint64 (variant);
	for (i = 0; i < binding->n_values; i++) {
		if (binding->values[i] >= v) {
			g_value_set_int (value, i);
			return TRUE;
		}
	}

	return FALSE;
}

static GVariant *
uint_enum_set_mapping (const GValue       *value,
		       const GVariantType *expected_type,
		       gpointer            user_data)
{
	UIntEnumBinding *binding = user_data;

	return g_variant_new_uint64 (binding->values[g_value_get_int (value)]);
}

static void
bind_builder_uint_enum (GtkBuilder *builder,
			GSettings *settings,
			const char *widget_name,
			const char *prefs,
			const guint64 *values,
			int n_values)
{
	UIntEnumBinding *binding;

	binding = g_new (UIntEnumBinding, 1);
	binding->values = values;
	binding->n_values = n_values;

	g_settings_bind_with_mapping (settings, prefs,
				      gtk_builder_get_object (builder, widget_name),
				      "active", G_SETTINGS_BIND_DEFAULT,
				      uint_enum_get_mapping,
				      uint_enum_set_mapping,
				      binding, g_free);
}

static GVariant *
radio_mapping_set (const GValue *gvalue,
		   const GVariantType *expected_type,
		   gpointer user_data)
{
	const gchar *widget_value = user_data;
	GVariant *retval = NULL;

	if (g_value_get_boolean (gvalue)) {
		retval = g_variant_new_string (widget_value);
	}

	return retval;
}

static gboolean
radio_mapping_get (GValue *gvalue,
		   GVariant *variant,
		   gpointer user_data)
{
	const gchar *widget_value = user_data;
	const gchar *value;

	value = g_variant_get_string (variant, NULL);

	if (g_strcmp0 (value, widget_value) == 0) {
		g_value_set_boolean (gvalue, TRUE);
	} else {
		g_value_set_boolean (gvalue, FALSE);
	}

	return TRUE;
}

static void
bind_builder_radio (GtkBuilder *builder,
		    GSettings *settings,
		    const char **widget_names,
		    const char *prefs,
		    const char **values)
{
	GtkWidget *button;
	int i;

	for (i = 0; widget_names[i] != NULL; i++) {
		button = GTK_WIDGET (gtk_builder_get_object (builder, widget_names[i]));

		g_settings_bind_with_mapping (settings, prefs,
					      button, "active",
					      G_SETTINGS_BIND_DEFAULT,
					      radio_mapping_get, radio_mapping_set,
					      (gpointer) values[i], NULL);
	}
}

static void
setup_configurable_menu_items (GtkBuilder *builder)
{
    gint i;

    for (i = 0; i < CONFIGURABLE_MENU_ITEM_COUNT; i++) {
        if (CONFIGURABLE_MENU_ITEM_INFO[i].config_widget_name == NULL) {
            continue;
        }

        bind_builder_bool (builder,
                           nemo_menu_config_preferences,
                           CONFIGURABLE_MENU_ITEM_INFO[i].config_widget_name,
                           CONFIGURABLE_MENU_ITEM_INFO[i].settings_key);
    }
}

static void
setup_tooltip_items (GtkBuilder *builder)
{
    gboolean enabled = FALSE;

    enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET))) ||
              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_DESKTOP_WIDGET))) ||
              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET)));

    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FILE_TYPE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_MOD_DATE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_CREATED_DATE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_ACCESS_DATE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FULL_PATH_WIDGET)), enabled);
}

static void
connect_tooltip_items (GtkBuilder *builder)
{
    GtkToggleButton *w;

    w = GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET));
    g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_tooltip_items), builder);

    w = GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET));
    g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_tooltip_items), builder);

    w = GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_DESKTOP_WIDGET));
    g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_tooltip_items), builder);

}

/* When single click radio button is selected, checkbox for quick renames should get unselected and disable to avoid annoying features */
static void
setup_quick_renames (GtkBuilder *builder)
{
	gboolean enabled = FALSE;
	enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (click_behavior_components[1])));
	if(enabled==FALSE){
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(W (NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN)), FALSE);
	}
	gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN)), enabled);
}

static void
connect_quick_renames (GtkBuilder *builder)
{
	GtkRadioButton *w;
	w=GTK_RADIO_BUTTON(W(click_behavior_components[0]));
 		g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_quick_renames), builder);

	w=GTK_RADIO_BUTTON(W(click_behavior_components[1]));
		g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_quick_renames), builder);
}

static void
on_dialog_destroy (GtkWidget *widget,
                   gpointer   user_data)
{
    GtkBuilder *builder = GTK_BUILDER (user_data);

    g_object_unref (builder);
}

static void
on_date_format_combo_changed (GtkComboBox *widget,
                              gpointer     user_data)
{
    GtkBuilder *builder = GTK_BUILDER (user_data);
    gint active = gtk_combo_box_get_active (widget);

    switch (active) {
        case NEMO_DATE_FORMAT_LOCALE:
        case NEMO_DATE_FORMAT_ISO:
            gtk_widget_set_sensitive (GTK_WIDGET (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FONT_CHOICE_WIDGET)), TRUE);
            break;
        case NEMO_DATE_FORMAT_INFORMAL:
        default:
            gtk_widget_set_sensitive (GTK_WIDGET (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FONT_CHOICE_WIDGET)), FALSE);
            break;
    }
}

static void
set_gtk_filechooser_sort_first (GObject *object,
				GParamSpec *pspec)
{
	g_settings_set_boolean (gtk_filechooser_preferences,
				NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST,
				gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object)));
}

/* Toolbar page: edits the bar layout held by NemoToolbarLayout. Bars and the
 * buttons on them are shown as a tree and rearranged by dragging; every change
 * is written straight through, so open windows restyle as you drop. */

enum {
    ROW_BAR,
    ROW_AVAILABLE,
    ROW_ITEM
};

enum {
    COL_KIND,
    COL_ICON,
    COL_LABEL,
    COL_ID,
    COL_VISIBLE,
    COL_SHOW_CHECK,
    N_TOOLBAR_COLS
};

typedef struct {
    GtkTreeStore      *store;
    GtkWidget         *view;
    NemoActionManager *actions;
    guint              commit_id;
    gboolean           updating;
} ToolbarPage;

static void toolbar_page_fill (ToolbarPage *page);

static void
toolbar_page_free (gpointer data)
{
    ToolbarPage *page = data;

    if (page->commit_id != 0) {
        g_source_remove (page->commit_id);
    }

    g_clear_object (&page->actions);
    g_clear_object (&page->store);
    g_free (page);
}

static void
toolbar_page_collect_items (GtkTreeModel   *model,
                            GtkTreeIter    *iter,
                            NemoToolbarBar *bar)
{
    do {
        GtkTreeIter child;
        gchar *id = NULL;
        gint kind;

        gtk_tree_model_get (model, iter, COL_KIND, &kind, COL_ID, &id, -1);

        if (kind == ROW_ITEM && id != NULL) {
            bar->items = g_list_append (bar->items, id);
        } else {
            g_free (id);
        }

        /* A drop can leave one button nested under another; flatten it out. */
        if (gtk_tree_model_iter_children (model, &child, iter)) {
            toolbar_page_collect_items (model, &child, bar);
        }
    } while (gtk_tree_model_iter_next (model, iter));
}

static GList *
toolbar_page_harvest (ToolbarPage *page)
{
    GtkTreeModel *model = GTK_TREE_MODEL (page->store);
    GList *bars = NULL;
    GtkTreeIter top;
    gboolean ok;

    for (ok = gtk_tree_model_get_iter_first (model, &top);
         ok;
         ok = gtk_tree_model_iter_next (model, &top)) {
        NemoToolbarBar *bar;
        GtkTreeIter child;
        gboolean visible;
        gint kind;

        gtk_tree_model_get (model, &top, COL_KIND, &kind, COL_VISIBLE, &visible, -1);

        if (kind != ROW_BAR) {
            continue;
        }

        bar = nemo_toolbar_bar_new ();
        bar->visible = visible;

        if (gtk_tree_model_iter_children (model, &child, &top)) {
            toolbar_page_collect_items (model, &child, bar);
        }

        bars = g_list_prepend (bars, bar);
    }

    return g_list_reverse (bars);
}

static void
toolbar_page_commit (ToolbarPage *page)
{
    GList *bars;

    if (page->updating) {
        return;
    }

    bars = toolbar_page_harvest (page);

    if (bars == NULL) {
        bars = g_list_prepend (NULL, nemo_toolbar_bar_new ());
    }

    nemo_toolbar_layout_set_bars (nemo_toolbar_layout_get_default (), bars);
    toolbar_page_fill (page);
}

static void
toolbar_page_append_item (ToolbarPage *page,
                          GtkTreeIter *parent,
                          const gchar *id)
{
    GtkTreeIter iter;
    const gchar *icon = NULL;
    g_autofree gchar *label = NULL;

    if (nemo_toolbar_layout_id_is_action (id)) {
        const gchar *uuid = nemo_toolbar_layout_action_uuid (id);
        NemoAction *action;

        action = nemo_action_manager_get_action (page->actions, uuid);

        /* An action whose file is not installed right now still keeps its
         * place, so it comes back when the file does. */
        if (action != NULL) {
            icon = gtk_action_get_icon_name (GTK_ACTION (action));
            label = g_strdup (nemo_action_get_orig_label (action));
        } else {
            label = g_strdup (uuid);
        }
    } else {
        const NemoToolbarItemInfo *info = nemo_toolbar_layout_lookup_item (id);

        icon = info->icon_name;
        label = g_strdup (_(info->label));
    }

    gtk_tree_store_append (page->store, &iter, parent);
    gtk_tree_store_set (page->store, &iter,
                        COL_KIND, ROW_ITEM,
                        COL_ICON, icon,
                        COL_LABEL, label,
                        COL_ID, id,
                        -1);
}

static void
toolbar_page_fill (ToolbarPage *page)
{
    GHashTable *used;
    GList *bars, *l, *item;
    GtkTreeIter top;
    guint i, n_items;
    gint index = 1;

    page->updating = TRUE;

    gtk_tree_store_clear (page->store);
    used = g_hash_table_new (g_str_hash, g_str_equal);
    bars = nemo_toolbar_layout_get_bars (nemo_toolbar_layout_get_default ());

    for (l = bars; l != NULL; l = l->next) {
        NemoToolbarBar *bar = l->data;
        g_autofree gchar *name = g_strdup_printf (_("Toolbar %d"), index++);

        gtk_tree_store_append (page->store, &top, NULL);
        gtk_tree_store_set (page->store, &top,
                            COL_KIND, ROW_BAR,
                            COL_LABEL, name,
                            COL_VISIBLE, bar->visible,
                            COL_SHOW_CHECK, TRUE,
                            -1);

        for (item = bar->items; item != NULL; item = item->next) {
            g_hash_table_add (used, item->data);
            toolbar_page_append_item (page, &top, item->data);
        }
    }

    gtk_tree_store_append (page->store, &top, NULL);
    gtk_tree_store_set (page->store, &top,
                        COL_KIND, ROW_AVAILABLE,
                        COL_LABEL, _("Not on any toolbar"),
                        -1);

    n_items = nemo_toolbar_layout_get_n_items ();

    for (i = 0; i < n_items; i++) {
        const NemoToolbarItemInfo *info = nemo_toolbar_layout_get_item (i);

        if (!g_hash_table_contains (used, info->id)) {
            toolbar_page_append_item (page, &top, info->id);
        }
    }

    for (l = nemo_action_manager_list_actions (page->actions); l != NULL; l = l->next) {
        g_autofree gchar *id = g_strconcat (NEMO_TOOLBAR_ACTION_PREFIX,
                                            NEMO_ACTION (l->data)->uuid, NULL);

        if (!g_hash_table_contains (used, id)) {
            toolbar_page_append_item (page, &top, id);
        }
    }

    g_hash_table_destroy (used);
    gtk_tree_view_expand_all (GTK_TREE_VIEW (page->view));

    page->updating = FALSE;
}

static gboolean
toolbar_page_commit_idle (gpointer data)
{
    ToolbarPage *page = data;

    page->commit_id = 0;
    toolbar_page_commit (page);

    return G_SOURCE_REMOVE;
}

static void
toolbar_page_drag_end (GtkWidget      *widget,
                       GdkDragContext *context,
                       ToolbarPage    *page)
{
    /* Let the tree finish applying the drop before reading it back. */
    if (page->commit_id == 0) {
        page->commit_id = g_idle_add (toolbar_page_commit_idle, page);
    }
}

static void
toolbar_page_visible_toggled (GtkCellRendererToggle *renderer,
                              gchar                 *path,
                              ToolbarPage           *page)
{
    GtkTreeIter iter;
    gboolean visible;

    if (!gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (page->store), &iter, path)) {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (page->store), &iter, COL_VISIBLE, &visible, -1);
    gtk_tree_store_set (page->store, &iter, COL_VISIBLE, !visible, -1);

    toolbar_page_commit (page);
}

static void
toolbar_page_add_bar (GtkButton   *button,
                      ToolbarPage *page)
{
    GtkTreeIter iter;

    gtk_tree_store_append (page->store, &iter, NULL);
    gtk_tree_store_set (page->store, &iter,
                        COL_KIND, ROW_BAR,
                        COL_VISIBLE, TRUE,
                        COL_SHOW_CHECK, TRUE,
                        -1);

    toolbar_page_commit (page);
}

static void
toolbar_page_remove_bar (GtkButton   *button,
                         ToolbarPage *page)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter, parent;
    gint kind;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (page->view));

    if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
        return;
    }

    gtk_tree_model_get (model, &iter, COL_KIND, &kind, -1);

    /* Having one of its buttons selected is a fair way to mean the toolbar. */
    if (kind == ROW_ITEM && gtk_tree_model_iter_parent (model, &parent, &iter)) {
        iter = parent;
        gtk_tree_model_get (model, &iter, COL_KIND, &kind, -1);
    }

    if (kind != ROW_BAR) {
        return;
    }

    gtk_tree_store_remove (page->store, &iter);
    toolbar_page_commit (page);
}

static void
setup_toolbar_page (GtkBuilder *builder)
{
    ToolbarPage *page;
    GtkWidget *box, *scrolled, *controls, *add, *remove, *hint;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;

    box = GTK_WIDGET (gtk_builder_get_object (builder, "toolbar_layout_box"));

    page = g_new0 (ToolbarPage, 1);

    /* Actions load in the background, so the list is refreshed when they land. */
    page->actions = nemo_action_manager_new ();
    g_signal_connect_swapped (page->actions, "changed",
                              G_CALLBACK (toolbar_page_fill), page);

    page->store = gtk_tree_store_new (N_TOOLBAR_COLS,
                                      G_TYPE_INT,
                                      G_TYPE_STRING,
                                      G_TYPE_STRING,
                                      G_TYPE_STRING,
                                      G_TYPE_BOOLEAN,
                                      G_TYPE_BOOLEAN);

    page->view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (page->store));
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (page->view), FALSE);
    gtk_tree_view_set_reorderable (GTK_TREE_VIEW (page->view), TRUE);

    column = gtk_tree_view_column_new ();

    renderer = gtk_cell_renderer_toggle_new ();
    gtk_tree_view_column_pack_start (column, renderer, FALSE);
    gtk_tree_view_column_add_attribute (column, renderer, "active", COL_VISIBLE);
    gtk_tree_view_column_add_attribute (column, renderer, "visible", COL_SHOW_CHECK);
    g_signal_connect (renderer, "toggled", G_CALLBACK (toolbar_page_visible_toggled), page);

    renderer = gtk_cell_renderer_pixbuf_new ();
    gtk_tree_view_column_pack_start (column, renderer, FALSE);
    gtk_tree_view_column_add_attribute (column, renderer, "icon-name", COL_ICON);

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (column, renderer, TRUE);
    gtk_tree_view_column_add_attribute (column, renderer, "text", COL_LABEL);

    gtk_tree_view_append_column (GTK_TREE_VIEW (page->view), column);

    g_signal_connect (page->view, "drag-end", G_CALLBACK (toolbar_page_drag_end), page);

    hint = gtk_label_new (_("Drag buttons to reorder them or to move them between toolbars."));
    gtk_label_set_xalign (GTK_LABEL (hint), 0);
    gtk_style_context_add_class (gtk_widget_get_style_context (hint), GTK_STYLE_CLASS_DIM_LABEL);

    scrolled = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled), GTK_SHADOW_IN);
    gtk_widget_set_vexpand (scrolled, TRUE);
    gtk_container_add (GTK_CONTAINER (scrolled), page->view);

    controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    add = gtk_button_new_with_label (_("Add Toolbar"));
    remove = gtk_button_new_with_label (_("Remove Toolbar"));
    gtk_box_pack_start (GTK_BOX (controls), add, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (controls), remove, FALSE, FALSE, 0);

    g_signal_connect (add, "clicked", G_CALLBACK (toolbar_page_add_bar), page);
    g_signal_connect (remove, "clicked", G_CALLBACK (toolbar_page_remove_bar), page);

    gtk_box_pack_start (GTK_BOX (box), hint, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), scrolled, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (box), controls, FALSE, FALSE, 0);

    g_object_set_data_full (G_OBJECT (box), "nemo-toolbar-page", page, toolbar_page_free);

    toolbar_page_fill (page);
    gtk_widget_show_all (box);
}

static  void
nemo_file_management_properties_dialog_setup (GtkBuilder  *builder,
                                              GtkWindow   *window,
                                              const gchar *initial_page)
{
	GtkWidget *dialog;

	/* setup UI */
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"views_label",
							       5);
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"captions_label",
							       3);
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"preview_label",
							       3);
	create_date_format_menu (builder);


	setup_toolbar_page (builder);

	/* setup preferences */
	bind_builder_bool (builder, nemo_icon_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_LABELS_BESIDE_ICONS_WIDGET,
			   NEMO_PREFERENCES_ICON_VIEW_LABELS_BESIDE_ICONS);
	bind_builder_bool (builder, nemo_compact_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_ALL_COLUMNS_SAME_WIDTH,
			   NEMO_PREFERENCES_COMPACT_VIEW_ALL_COLUMNS_SAME_WIDTH);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_FOLDERS_FIRST_WIDGET,
			   NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST);
	g_signal_connect (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_FOLDERS_FIRST_WIDGET),
                          "notify::active",
                          G_CALLBACK (set_gtk_filechooser_sort_first), NULL);
	bind_builder_bool(builder, nemo_preferences,
			    NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN,
			    NEMO_PREFERENCES_CLICK_TO_RENAME);
	bind_builder_bool_inverted (builder, nemo_preferences,
				    NEMO_FILE_MANAGEMENT_PROPERTIES_ALWAYS_USE_BROWSER_WIDGET,
				    NEMO_PREFERENCES_ALWAYS_USE_BROWSER);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_MOVE_WIDGET,
			   NEMO_PREFERENCES_CONFIRM_MOVE_TO_TRASH);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_WIDGET,
			   NEMO_PREFERENCES_CONFIRM_TRASH);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_DELETE_WIDGET,
			   NEMO_PREFERENCES_ENABLE_DELETE);
    bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_SWAP_TRASH_DELETE,
               NEMO_PREFERENCES_SWAP_TRASH_DELETE);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_FULL_PATH_IN_TITLE_BARS_WIDGET,
			   NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES);
	bind_builder_bool (builder, nemo_tree_sidebar_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TREE_VIEW_FOLDERS_WIDGET,
			   NEMO_PREFERENCES_TREE_SHOW_ONLY_DIRECTORIES);
  bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_VIEW_WIDGET,
			   NEMO_PREFERENCES_INHERIT_FOLDER_VIEWER);
  bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_REVERSE_SORT_WIDGET,
               NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);
  bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_FAVORITES_FIRST_WIDGET,
               NEMO_PREFERENCES_SORT_FAVORITES_FIRST);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_DEFAULT_VIEW_WIDGET,
			   NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER,
			   (const char **) default_view_values);
	bind_builder_enum (builder, nemo_icon_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_ICON_VIEW_ZOOM_WIDGET,
			   NEMO_PREFERENCES_ICON_VIEW_DEFAULT_ZOOM_LEVEL,
			   (const char **) zoom_values);
	bind_builder_enum (builder, nemo_compact_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_COMPACT_VIEW_ZOOM_WIDGET,
			   NEMO_PREFERENCES_COMPACT_VIEW_DEFAULT_ZOOM_LEVEL,
			   (const char **) zoom_values);
	bind_builder_enum (builder, nemo_list_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_LIST_VIEW_ZOOM_WIDGET,
			   NEMO_PREFERENCES_LIST_VIEW_DEFAULT_ZOOM_LEVEL,
			   (const char **) zoom_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SORT_ORDER_WIDGET,
			   NEMO_PREFERENCES_DEFAULT_SORT_ORDER,
			   (const char **) sort_order_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_IMAGE_WIDGET,
			   NEMO_PREFERENCES_SHOW_IMAGE_FILE_THUMBNAILS,
			   (const char **) preview_image_values);
    bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_SHOW_THUMBNAILS_WIDGET,
               NEMO_PREFERENCES_INHERIT_SHOW_THUMBNAILS);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_FOLDER_WIDGET,
			   NEMO_PREFERENCES_SHOW_DIRECTORY_ITEM_COUNTS,
			   (const char **) preview_folder_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SIZE_PREFIXES_WIDGET,
			   NEMO_PREFERENCES_SIZE_PREFIXES,
			   (const char **) size_prefixes_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET,
			   NEMO_PREFERENCES_DATE_FORMAT,
			   (const char **) date_format_values);
    bind_builder_enum (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FONT_CHOICE_WIDGET,
               NEMO_PREFERENCES_DATE_FONT_CHOICE,
               (const char **) date_font_choice_values);
	bind_builder_radio (builder, nemo_preferences,
			    (const char **) click_behavior_components,
			    NEMO_PREFERENCES_CLICK_POLICY,
			    (const char **) click_behavior_values);
	bind_builder_radio (builder, nemo_preferences,
			    (const char **) executable_text_components,
			    NEMO_PREFERENCES_EXECUTABLE_TEXT_ACTIVATION,
			    (const char **) executable_text_values);

	bind_builder_uint_enum (builder, nemo_preferences,
				NEMO_FILE_MANAGEMENT_PROPERTIES_THUMBNAIL_LIMIT_WIDGET,
				NEMO_PREFERENCES_IMAGE_FILE_THUMBNAIL_LIMIT,
				thumbnail_limit_values,
				G_N_ELEMENTS (thumbnail_limit_values));

    bind_builder_bool (builder, gnome_media_handling_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOMOUNT_MEDIA_WIDGET,
               GNOME_DESKTOP_MEDIA_HANDLING_AUTOMOUNT);

    bind_builder_bool (builder, gnome_media_handling_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOOPEN_MEDIA_WIDGET,
               GNOME_DESKTOP_MEDIA_HANDLING_AUTOMOUNT_OPEN);

    bind_builder_bool_inverted (builder, gnome_media_handling_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_AUTORUN_MEDIA_WIDGET,
               GNOME_DESKTOP_MEDIA_HANDLING_AUTORUN);

    bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_DETECT_CONTENT_MEDIA_WIDGET,
               NEMO_PREFERENCES_MEDIA_HANDLING_DETECT_CONTENT);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_CLOSE_DEVICE_VIEW_ON_EJECT_WIDGET,
                       NEMO_PREFERENCES_CLOSE_DEVICE_VIEW_ON_EJECT);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_ADVANCED_PERMISSIONS_WIDGET,
                       NEMO_PREFERENCES_SHOW_ADVANCED_PERMISSIONS);

    bind_builder_string_entry (builder, nemo_preferences,
                         NEMO_FILE_MANAGEMENT_PROPERTIES_BULK_RENAME_WIDGET,
                         NEMO_PREFERENCES_BULK_RENAME_TOOL);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_START_WITH_DUAL_PANE_WIDGET,
                       NEMO_PREFERENCES_START_WITH_DUAL_PANE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_RESTORE_TABS_ON_STARTUP_WIDGET,
                       NEMO_PREFERENCES_RESTORE_TABS_ON_STARTUP);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_IGNORE_VIEW_METADATA_WIDGET,
                       NEMO_PREFERENCES_IGNORE_VIEW_METADATA);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_BOOKMARKS_IN_TO_MENUS_WIDGET,
                       NEMO_PREFERENCES_SHOW_BOOKMARKS_IN_TO_MENUS);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_PLACES_IN_TO_MENUS_WIDGET,
                       NEMO_PREFERENCES_SHOW_PLACES_IN_TO_MENUS);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_DESKTOP_WIDGET,
                       NEMO_PREFERENCES_TOOLTIPS_DESKTOP);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET,
                       NEMO_PREFERENCES_TOOLTIPS_ICON_VIEW);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET,
                       NEMO_PREFERENCES_TOOLTIPS_LIST_VIEW);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FILE_TYPE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_FILE_TYPE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_MOD_DATE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_MOD_DATE);
    
    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_ACCESS_DATE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_ACCESS_DATE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_CREATED_DATE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_CREATED_DATE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FULL_PATH_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_FULL_PATH);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_SKIP_FILE_OP_QUEUE_WIDGET,
                       NEMO_PREFERENCES_NEVER_QUEUE_FILE_OPS);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_CLICK_DBL_PARENT_FOLDER_WIDGET,
                       NEMO_PREFERENCES_CLICK_DOUBLE_PARENT_FOLDER);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_EXPAND_ROW_ON_DND_DWELL_WIDGET,
                       NEMO_PREFERENCES_EXPAND_ROW_ON_DND_DWELL);

    bind_builder_bool (builder, nemo_list_view_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_LIST_VIEW_EXPANDERS_WIDGET,
                       NEMO_PREFERENCES_LIST_VIEW_ENABLE_EXPANSION);

    setup_tooltip_items (builder);
    connect_tooltip_items (builder);

    /* to make checkbox for quickrenames get disabled when single click is selected */ 
    setup_quick_renames(builder);
    connect_quick_renames(builder);

    g_signal_connect (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET), "changed",
                      G_CALLBACK (on_date_format_combo_changed), builder);

    on_date_format_combo_changed (GTK_COMBO_BOX (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET)),
                                  builder);

	nemo_file_management_properties_dialog_setup_icon_caption_page (builder);
	nemo_file_management_properties_dialog_setup_list_column_page (builder);
    nemo_file_management_properties_dialog_setup_plugin_page (builder);
    nemo_file_management_properties_dialog_setup_templates_page (builder);


    setup_configurable_menu_items (builder);

    dialog = GTK_WIDGET (gtk_builder_get_object (builder, "file_management_dialog"));

	g_signal_connect (dialog, "delete-event",
			  G_CALLBACK (gtk_widget_destroy), NULL);

    g_signal_connect (dialog, "destroy",
                      G_CALLBACK (on_dialog_destroy), builder);

	gtk_window_set_icon_name (GTK_WINDOW (dialog), "folder");

	if (window) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), window);
	}

	preferences_dialog = dialog;
	g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &preferences_dialog);

    if (initial_page != NULL) {
        GtkStack *stack;

        stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));

        gtk_stack_set_visible_child_name (stack, initial_page);
    }

	gtk_widget_show (dialog);
}

void
nemo_file_management_properties_dialog_show (GtkWindow   *window,
                                             const gchar *initial_page)
{
	GtkBuilder *builder;

	if (preferences_dialog != NULL) {
		gtk_window_present (GTK_WINDOW (preferences_dialog));
		return;
	}

	builder = gtk_builder_new ();
    gtk_builder_set_translation_domain (builder, GETTEXT_PACKAGE);
	gtk_builder_add_from_resource (builder,
				       "/org/nemo/nemo-file-management-properties.glade",
				       NULL);

	nemo_file_management_properties_dialog_setup (builder, window, initial_page);
}
