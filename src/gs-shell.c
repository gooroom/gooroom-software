/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2018-2019 Gooroom <gooroom@gooroom.kr>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

#include "gs-common.h"
#include "gs-shell.h"
#include "gs-details-page.h"
#include "gs-installed-page.h"
#include "gs-moderate-page.h"
#include "gs-loading-page.h"
#include "gs-search-page.h"
#include "gs-overview-page.h"
#include "gs-updates-page.h"
#include "gs-category-page.h"
#include "gs-extras-page.h"
#include "gs-repos-dialog.h"
#include "gs-prefs-dialog.h"
#include "gs-update-dialog.h"
#include "gs-update-monitor.h"
#include "gs-utils.h"
#include "gs-category.h"
#include "gs-review-dialog.h"

static const gchar *page_name[] = {
	"unknown",
	"overview",
	"installed",
	"search",
	"updates",
	"details",
	"category",
	"extras",
	"moderate",
	"loading",
	"category",
	"category",
	"consent",
};

typedef struct {
	GsShellMode	 mode;
	GtkWidget	*focus;
	GsApp		*app;
	GsCategory	*category;
	gchar		*search;
} BackEntry;

typedef struct
{
	gboolean		 ignore_primary_buttons;
	GCancellable		*cancellable;
	GsPluginLoader		*plugin_loader;
	GsShellMode		 mode;
	GHashTable		*pages;
	GtkWidget		*header_start_widget;
	GtkWidget		*header_end_widget;
	GtkBuilder		*builder;
	GtkWindow		*main_window;
	GQueue			*back_entry_stack;
	GPtrArray		*modal_dialogs;
	gulong			 search_changed_id;
	gchar			*events_info_uri;
	gboolean		 in_mode_change;
	GsPage			*page_last;
	gboolean		 search_clean;

	GtkWidget		*category_menu;
	GSettings		*settings;

	gboolean		 enable_group;
} GsShellPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsShell, gs_shell, G_TYPE_OBJECT)

enum {
	SIGNAL_LOADED,
	SIGNAL_LAST
};

static void save_back_entry (GsShell *shell);
static guint signals [SIGNAL_LAST] = { 0 };
static void
gs_popup_menu_cb (GtkMenuItem *menuItem, gpointer user_data)
{
	GsCategory *category;
	category = GS_CATEGORY (g_object_get_data (G_OBJECT (menuItem), "gnome-software::category-value"));
	save_back_entry (user_data);

	gs_shell_change_mode (user_data, GS_SHELL_MODE_CATEGORY, category, FALSE);
}

static void
add_category (gpointer key, gpointer value, gpointer user_data)
{
	const gchar *name;
	GtkStyleContext *style_context;
	GtkWidget *menu_item = NULL;
	GsShellPrivate *priv = gs_shell_get_instance_private (user_data);
	const gchar *id = gs_category_get_id (value);

	if (g_strcmp0 (id, "addons") == 0)
		return;

	if (g_strcmp0 (id, "nonfree") == 0) // Add non-free package category from gooroom
		return;

	if (g_strcmp0 (id, "group") == 0) // Add group package category from gooroom
		return;

	name = gs_category_get_name (value);
	menu_item = gtk_menu_item_new_with_label (name);
	style_context = gtk_widget_get_style_context (menu_item);
	gtk_style_context_remove_class (style_context, GTK_STYLE_CLASS_MENUITEM);
	gtk_style_context_add_class (style_context, "main_category_menu_item");
	gtk_widget_set_size_request (menu_item, 170, 34);

	g_object_set_data (G_OBJECT (menu_item), "gnome-software::category-id", name);
	g_object_set_data (G_OBJECT (menu_item), "gnome-software::category-value", value);
	g_signal_connect (menu_item, "activate", G_CALLBACK (gs_popup_menu_cb), user_data);
	gtk_menu_shell_append (GTK_MENU_SHELL (priv->category_menu), menu_item);
	gtk_widget_show (menu_item);
}

static void
gs_category_button_cb (GtkWidget *button, GsShell *shell)
{
	GsPage *tmp_page;
	GHashTable *categories;
	g_autoptr(GList) children = NULL;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gboolean is_active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	if (!is_active) {
		GtkWidget *img_widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_category_right_arrow_image"));
		gtk_widget_hide (img_widget);
		return;
	}

	children = gtk_container_get_children (GTK_CONTAINER (priv->category_menu));
	for (GList *l = children; l != NULL; l = l->next)
		gtk_container_remove (GTK_CONTAINER (priv->category_menu), GTK_WIDGET (l->data));

	tmp_page = GS_PAGE (g_hash_table_lookup (priv->pages, "overview"));
	if (tmp_page)
	{
		categories = gs_overview_page_get_categories (GS_OVERVIEW_PAGE(tmp_page));
		g_hash_table_foreach (categories, (GHFunc)add_category, shell);
	}
}

static void
gs_category_button_activate_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	gboolean is_active;
	GsShellPrivate *priv = gs_shell_get_instance_private (user_data);
	GtkWidget *img_widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_category_right_arrow_image"));

	if (event->type == GDK_ENTER_NOTIFY)
	{
		gtk_widget_show (img_widget);
		gs_utils_widget_set_css (GTK_WIDGET (widget), g_strdup ("background-color:rgba(201,201,201,0.6)"));
	}
	else
	{
		is_active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(widget)); 
		if (!is_active)
		{
			gtk_widget_hide (img_widget);
			gs_utils_widget_set_css (GTK_WIDGET (widget), g_strdup ("background-color:rgba(255,255,255,1.0)"));
		}
	}
}

static void
modal_dialog_unmapped_cb (GtkWidget *dialog,
                          GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_debug ("modal dialog %p unmapped", dialog);
	g_ptr_array_remove (priv->modal_dialogs, dialog);
}

static GsCategory *
gs_shell_find_main_category (GList *categories, const gchar* name)
{
	const gchar *desktop;
	GList *li;
	GsCategory *category;
	GPtrArray *children;
	GsCategory *subcategory;
	GPtrArray *desktop_groups;

	for (li = categories; li != NULL; li=li->next) {
		category = GS_CATEGORY(li->data);
		children = gs_category_get_children (category);

		for (guint i = 0; i < children->len; i++) {
			subcategory = g_ptr_array_index (children, i);
			desktop_groups = gs_category_get_desktop_groups (subcategory);
			desktop =  g_ptr_array_index (desktop_groups, 0);
			if (g_strcmp0 (desktop, name) == 0) {
				return subcategory;
			}
		}
	}
	return NULL;
}

GsCategory *
gs_shell_get_category  (GsShell *shell,
                        GPtrArray   *find_categories)
{
	const gchar *category_name, *main_category_name;
	GList *list;
	GsPage *tmp_page;
	GHashTable *categories;
	GsCategory *find_category = NULL;
	GsCategory *main_category = NULL;

	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	tmp_page = GS_PAGE (g_hash_table_lookup (priv->pages, "overview"));
	if (tmp_page == NULL)
		return NULL;

	categories = gs_overview_page_get_categories (GS_OVERVIEW_PAGE(tmp_page));
	list = g_hash_table_get_values (categories);

	if (1 < find_categories->len) {
		for (guint i = 0; i < find_categories->len; i++) {
			category_name = g_ptr_array_index (find_categories, i);
			main_category = gs_shell_find_main_category (list, category_name);
			if (main_category) {
				main_category_name = category_name;
				break;
			}
		}
		for (guint i = 0; i < find_categories->len; i++) {
			category_name = g_ptr_array_index (find_categories, i);
			if (g_strcmp0 (main_category_name, category_name) == 0)
				continue;

			category_name = g_strdup_printf ("%s::%s",main_category_name, category_name);
			find_category = gs_shell_find_main_category (list, category_name);
		}
	}
	else {
		category_name = g_ptr_array_index (find_categories, 0);
		find_category = gs_shell_find_main_category (list, category_name);
	}

	if (find_category)
		return find_category;

	return main_category;
}

void
gs_shell_modal_dialog_present (GsShell *shell, GtkDialog *dialog)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWindow *parent;

	/* show new modal on top of old modal */
	if (priv->modal_dialogs->len > 0) {
		parent = g_ptr_array_index (priv->modal_dialogs,
					    priv->modal_dialogs->len - 1);
		g_debug ("using old modal %p as parent", parent);
	} else {
		parent = priv->main_window;
		g_debug ("using main window");
	}
	gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

	/* add to stack, transfer ownership to here */
	g_ptr_array_add (priv->modal_dialogs, dialog);
	g_signal_connect (GTK_WIDGET (dialog), "unmap",
	                  G_CALLBACK (modal_dialog_unmapped_cb), shell);

	/* present the new one */
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_present (GTK_WINDOW (dialog));
}

gboolean
gs_shell_is_active (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	return gtk_window_is_active (priv->main_window);
}

GtkWindow *
gs_shell_get_window (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	return priv->main_window;
}

void
gs_shell_activate (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gtk_window_present (priv->main_window);
}

#if 1
static void
gs_shell_set_header_start_widget (GsShell *shell, GtkWidget *widget)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *old_widget;
	GtkWidget *header;

	old_widget = priv->header_start_widget;
	header = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));

	if (priv->header_start_widget == widget)
		return;

	if (widget != NULL) {
		g_object_ref (widget);
		gtk_header_bar_pack_start (GTK_HEADER_BAR (header), widget);
	}

	priv->header_start_widget = widget;

	if (old_widget != NULL) {
		gtk_container_remove (GTK_CONTAINER (header), old_widget);
		g_object_unref (old_widget);
	}
}

static void
gs_shell_set_header_end_widget (GsShell *shell, GtkWidget *widget)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *old_widget;
	GtkWidget *header;

	old_widget = priv->header_end_widget;
	header = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));

	if (priv->header_end_widget == widget)
		return;

	if (widget != NULL) {
		g_object_ref (widget);
		gtk_header_bar_pack_end (GTK_HEADER_BAR (header), widget);
	}

	priv->header_end_widget = widget;

	if (old_widget != NULL) {
		gtk_container_remove (GTK_CONTAINER (header), old_widget);
		g_object_unref (old_widget);
	}
}

#endif

static void
free_back_entry (BackEntry *entry)
{
	if (entry->focus != NULL)
		g_object_remove_weak_pointer (G_OBJECT (entry->focus),
		                              (gpointer *) &entry->focus);
	g_clear_object (&entry->category);
	g_clear_object (&entry->app);
	g_free (entry->search);
	g_free (entry);
}

static void
gs_shell_clean_back_entry_stack (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	BackEntry *entry;

	while ((entry = g_queue_pop_head (priv->back_entry_stack)) != NULL) {
		free_back_entry (entry);
	}
}

void
gs_shell_change_mode (GsShell *shell,
		      GsShellMode mode,
		      gpointer data,
		      gboolean scroll_up)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GsApp *app;
	GsPage *page;
	GtkWidget *widget;
	const gchar *search_msg;

	if (priv->ignore_primary_buttons)
		return;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));
	gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (widget), TRUE);

	/* hide all mode specific header widgets here, they will be shown in the
	 * refresh functions
	 */
	if (mode == GS_SHELL_MODE_LOADING)
	{
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "title_box"));
		gtk_widget_hide (widget);
	}
	else
	{
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "title_box"));
		gtk_widget_show (widget);
	}

	priv->in_mode_change = TRUE;
	/* set the window title back to default */
	gtk_window_set_title (priv->main_window, g_get_application_name ());

	/* update main buttons according to mode */
	priv->ignore_primary_buttons = TRUE;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_all"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_OVERVIEW);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_INSTALLED);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_UPDATES);

	gtk_widget_set_visible (widget, gs_plugin_loader_get_allow_updates (priv->plugin_loader) ||
					mode == GS_SHELL_MODE_UPDATES);

	if (!scroll_up) { /* menu popup active */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_category"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_CATEGORY);
	}

	if (priv->enable_group) {
		/* Add group widget from Gooroom*/
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_group"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_GROUP);
		/* Add non-free widget from Gooroom*/
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_consent"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_CONSENT);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_NONFREE);
	}
	else {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_group"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_consent"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_separator"));
		gtk_widget_hide (widget);
	}

	priv->ignore_primary_buttons = FALSE;

	/* switch page */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_main"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), page_name[mode]);

	/* do action for mode */
	priv->mode = mode;
	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
		//gs_shell_clean_back_entry_stack (shell);
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "overview"));
		break;
	case GS_SHELL_MODE_INSTALLED:
		//gs_shell_clean_back_entry_stack (shell);
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "installed"));
		break;
	case GS_SHELL_MODE_MODERATE:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "moderate"));
		break;
	case GS_SHELL_MODE_LOADING:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "loading"));
		break;
	case GS_SHELL_MODE_SEARCH:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "search"));
		gs_search_page_set_text (GS_SEARCH_PAGE (page), data);
		break;
	case GS_SHELL_MODE_UPDATES:
		//gs_shell_clean_back_entry_stack (shell);
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "updates"));
		break;
	case GS_SHELL_MODE_GROUP: //Add group mode
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "category"));
		if (data == NULL)
		{
			gs_category_page_set_category (GS_CATEGORY_PAGE (page), NULL);
		}
		else
		{
			gs_category_page_set_category (GS_CATEGORY_PAGE (page), GS_CATEGORY (data));
		}
		break;
	case GS_SHELL_MODE_CONSENT: //Add user-consent mode
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "consent"));
		break;
	case GS_SHELL_MODE_NONFREE: //Add non-free mode
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "category"));
		gs_category_page_set_category (GS_CATEGORY_PAGE (page),
		                               GS_CATEGORY (data));
		break;
	case GS_SHELL_MODE_DETAILS:
		app = GS_APP (data);
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "details"));
		if (gs_app_get_local_file (app) != NULL) {
			gs_details_page_set_local_file (GS_DETAILS_PAGE (page),
			                                gs_app_get_local_file (app));
		} else if (gs_app_get_metadata_item (app, "GnomeSoftware::from-url") != NULL) {
			gs_details_page_set_url (GS_DETAILS_PAGE (page),
			                         gs_app_get_metadata_item (app, "GnomeSoftware::from-url"));
		} else {
			gs_details_page_set_app (GS_DETAILS_PAGE (page), data);
		}
		break;
	case GS_SHELL_MODE_CATEGORY:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "category"));
		gs_category_page_set_category (GS_CATEGORY_PAGE (page),
		                               GS_CATEGORY (data));
		break;
	case GS_SHELL_MODE_EXTRAS:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "extras"));
		break;
	default:
		g_assert_not_reached ();
	}

	/* show the back button if needed */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	gtk_widget_set_sensitive (widget,
				mode != GS_SHELL_MODE_SEARCH &&
				!g_queue_is_empty (priv->back_entry_stack));

	priv->in_mode_change = TRUE;

	if (priv->page_last)
		gs_page_switch_from (priv->page_last);
	g_set_object (&priv->page_last, page);
	gs_page_switch_to (page, scroll_up);
	priv->in_mode_change = FALSE;

	/* update header bar widgets */
	widget = gs_page_get_header_start_widget (page);
	gs_shell_set_header_start_widget (shell, widget);

	widget = gs_page_get_header_end_widget (page);
	gs_shell_set_header_end_widget (shell, widget);

	/* destroy any existing modals */
	if (priv->modal_dialogs != NULL) {
		gsize i = 0;
		/* block signal emission of 'unmapped' since that will
		 * call g_ptr_array_remove_index. The unmapped signal may
		 * be emitted whilst running unref handlers for
		 * g_ptr_array_set_size */
		for (i = 0; i < priv->modal_dialogs->len; ++i) {
			GtkWidget *dialog = g_ptr_array_index (priv->modal_dialogs, i);
			g_signal_handlers_disconnect_by_func (dialog,
							      modal_dialog_unmapped_cb,
							      shell);
		}
		g_ptr_array_set_size (priv->modal_dialogs, 0);
	}

	priv->in_mode_change = TRUE;
	if (mode == GS_SHELL_MODE_UPDATES ||
		mode == GS_SHELL_MODE_DETAILS ||
		mode == GS_SHELL_MODE_INSTALLED ||
		mode == GS_SHELL_MODE_GROUP ||
		mode == GS_SHELL_MODE_NONFREE ||
		mode == GS_SHELL_MODE_CATEGORY ||
		mode == GS_SHELL_MODE_OVERVIEW) {

		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		search_msg = gtk_entry_get_text (GTK_ENTRY (widget));
		if (g_utf8_strlen(search_msg, -1) > 0 ) {
			priv->search_clean = TRUE;
			gtk_entry_reset_im_context (GTK_ENTRY (widget));
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
			gtk_entry_set_text (GTK_ENTRY (widget), "");
		}
	}
	priv->in_mode_change = FALSE;
}

static void
gs_group_chamge_mode (GsShell *shell)
{
	gchar *name;
	GsCategory *ca = NULL;
	GsCategory *parent = NULL;
	GPtrArray *find_categories;

	name = g_strdup ("Group");

	find_categories = g_ptr_array_new ();
	g_ptr_array_add (find_categories, name);

	ca = gs_shell_get_category  (shell, find_categories);

	if (ca == NULL)
	{
		gs_shell_change_mode (shell, GS_SHELL_MODE_GROUP, NULL, FALSE);
		return;
	}

	parent = gs_category_get_parent (ca);
	gs_shell_change_mode (shell, GS_SHELL_MODE_GROUP, parent, FALSE);
}

static void
gs_consent_chamge_mode (GsShell *shell)
{
	gchar *name;
	GsCategory *ca = NULL;
	GsCategory *parent = NULL;
	GPtrArray *find_categories;

	name = g_strdup ("nonfree");

	find_categories = g_ptr_array_new ();
	g_ptr_array_add (find_categories, name);

	ca = gs_shell_get_category  (shell, find_categories);
	parent = gs_category_get_parent (ca);
	gs_shell_change_mode (shell, GS_SHELL_MODE_NONFREE, parent, FALSE);
}

static void
gs_overview_page_button_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellMode mode;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	if (priv->ignore_primary_buttons)
		return;

	mode = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
						   "gnome-software::overview-mode"));
	save_back_entry (shell);

	if (mode == GS_SHELL_MODE_GROUP)
	{
		gs_group_chamge_mode (shell);
	}
	else if (mode == GS_SHELL_MODE_CONSENT)
	{
		gboolean user_consent;
		user_consent  = g_settings_get_boolean (priv->settings, "user-consent");

		if (user_consent)
		{
			gs_consent_chamge_mode (shell);
			return;
		}

		gs_shell_change_mode (shell, mode, NULL, TRUE);
	}
	else
	{
		gs_shell_change_mode (shell, mode, NULL, TRUE);
	}
}

static void
save_back_entry (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	BackEntry *entry;
	GsPage *page;
#if 0
	/* never go back to a details page */
	if (priv->mode == GS_SHELL_MODE_DETAILS) {
		g_debug ("ignoring back entry for details");
		return;
	}
#endif
	entry = g_new0 (BackEntry, 1);
	entry->mode = priv->mode;

	entry->focus = gtk_window_get_focus (priv->main_window);
	if (entry->focus != NULL)
		g_object_add_weak_pointer (G_OBJECT (entry->focus),
					   (gpointer *) &entry->focus);

	switch (priv->mode) {
	case GS_SHELL_MODE_GROUP:
	case GS_SHELL_MODE_NONFREE:
	case GS_SHELL_MODE_CATEGORY:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "category"));
		entry->category = gs_category_page_get_category (GS_CATEGORY_PAGE (page));
		g_object_ref (entry->category);
		g_debug ("pushing back entry for %s with %s",
			 page_name[entry->mode],
			 gs_category_get_id (entry->category));
		break;
	case GS_SHELL_MODE_SEARCH:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "search"));
		entry->search = g_strdup (gs_search_page_get_text (GS_SEARCH_PAGE (page)));
		g_debug ("pushing back entry for %s with %s",
			 page_name[entry->mode], entry->search);
		break;
	case GS_SHELL_MODE_DETAILS:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "details"));
		entry->app = gs_details_page_get_app (GS_DETAILS_PAGE (page));
		g_debug ("pushing back entry for %s", page_name[entry->mode]);
		break;
	default:
		g_debug ("pushing back entry for %s\n", page_name[entry->mode]);
		break;
	}

	g_queue_push_head (priv->back_entry_stack, entry);
}

static void
gs_shell_plugin_events_sources_cb (GtkWidget *widget, GsShell *shell)
{
	gs_shell_show_sources (shell);
}

static void
gs_shell_plugin_events_no_space_cb (GtkWidget *widget, GsShell *shell)
{
	g_autoptr(GError) error = NULL;
	if (!g_spawn_command_line_async ("baobab", &error))
		g_warning ("failed to exec baobab: %s", error->message);
}

static void
gs_shell_plugin_events_network_settings_cb (GtkWidget *widget, GsShell *shell)
{
	g_autoptr(GError) error = NULL;
	if (!g_spawn_command_line_async ("gnome-control-center network", &error))
		g_warning ("failed to exec gnome-control-center: %s", error->message);
}

static void
gs_shell_plugin_events_more_info_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_autoptr(GError) error = NULL;
	if (!g_app_info_launch_default_for_uri (priv->events_info_uri, NULL, &error)) {
		g_warning ("failed to launch URI %s: %s",
			   priv->events_info_uri, error->message);
	}
}

static void
gs_shell_plugin_events_restart_required_cb (GtkWidget *widget, GsShell *shell)
{
	g_autoptr(GError) error = NULL;
	if (!g_spawn_command_line_async (LIBEXECDIR "/gooroom-software-restarter", &error))
		g_warning ("failed to restart: %s", error->message);
}

static void
gs_shell_go_back (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	BackEntry *entry;
	GtkWidget *widget;

	g_return_if_fail (!g_queue_is_empty (priv->back_entry_stack));

	entry = g_queue_pop_head (priv->back_entry_stack);

	switch (entry->mode) {
	case GS_SHELL_MODE_UNKNOWN:
	case GS_SHELL_MODE_LOADING:
		/* happens when using --search, --details, --install, etc. options */
		g_debug ("popping back entry for %s", page_name[entry->mode]);
		gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, FALSE);
		break;
	case GS_SHELL_MODE_GROUP:
	case GS_SHELL_MODE_NONFREE:
	case GS_SHELL_MODE_CATEGORY:
		g_debug ("popping back entry for %s with %s",
			 page_name[entry->mode],
			 gs_category_get_id (entry->category));
		gs_shell_change_mode (shell, entry->mode, entry->category, TRUE);
		break;
	case GS_SHELL_MODE_DETAILS:
		g_debug ("popping back entry for %s with %p",
			 page_name[entry->mode], entry->app);
		gs_shell_change_mode (shell, entry->mode, entry->app, FALSE);
		break;
	case GS_SHELL_MODE_SEARCH:
		g_debug ("popping back entry for %s with %s",
			 page_name[entry->mode], entry->search);

		/* set the text in the entry and move cursor to the end */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		g_signal_handler_block (widget, priv->search_changed_id);
		gtk_entry_set_text (GTK_ENTRY (widget), entry->search);
		gtk_editable_set_position (GTK_EDITABLE (widget), -1);
		g_signal_handler_unblock (widget, priv->search_changed_id);

		/* set the mode directly */
		gs_shell_change_mode (shell, entry->mode,
				      (gpointer) entry->search, FALSE);
		break;
	default:
		g_debug ("popping back entry for %s", page_name[entry->mode]);
		gs_shell_change_mode (shell, entry->mode, NULL, FALSE);
		break;
	}

	if (entry->focus != NULL)
		gtk_widget_grab_focus (entry->focus);

	free_back_entry (entry);
}

static void
gs_shell_main_button_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *popover;
	popover = GTK_WIDGET (gtk_builder_get_object (priv->builder, "main_popover"));
	gtk_popover_set_relative_to (GTK_POPOVER (popover), GTK_WIDGET (widget));
	gtk_popover_popup (GTK_POPOVER (popover));
}

static void
gs_shell_back_button_cb (GtkWidget *widget, GsShell *shell)
{
	gs_shell_go_back (shell);
}

static void
gs_shell_reload_cb (GsPluginLoader *plugin_loader, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_autoptr(GList) keys = g_hash_table_get_keys (priv->pages);
	for (GList *l = keys; l != NULL; l = l->next) {
		GsPage *page = GS_PAGE (g_hash_table_lookup (priv->pages, l->data));
		gs_page_reload (page);
	}
}

static void
initial_refresh_done (GsLoadingPage *loading_page, gpointer data)
{
	GsPage *page;
	GsShell *shell = data;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	g_signal_handlers_disconnect_by_func (loading_page, initial_refresh_done, data);

	page = GS_PAGE (gtk_builder_get_object (priv->builder, "updates_page"));
	gs_page_reload (page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "installed_page"));
	gs_page_reload (page);

	g_signal_emit (shell, signals[SIGNAL_LOADED], 0);

	/* go to OVERVIEW, unless the "loading" callbacks changed mode already */
	if (priv->mode == GS_SHELL_MODE_LOADING)
		gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, TRUE);

	/* now that we're finished with the loading page, connect the reload signal handler */
	g_signal_connect (priv->plugin_loader, "reload",
	                  G_CALLBACK (gs_shell_reload_cb), shell);
}

static void
search_changed_handler (GObject *entry, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const gchar *text;

	if (priv->search_clean) {
		priv->search_clean = FALSE;
		return;
	}

	priv->search_clean = FALSE;
	text = gtk_entry_get_text (GTK_ENTRY (entry));
	if (gs_shell_get_mode (shell) != GS_SHELL_MODE_SEARCH) {
		save_back_entry (shell);
		gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH,
					      (gpointer) text, TRUE);
	} else {
		GsPage *page = GS_PAGE (g_hash_table_lookup (priv->pages, "search"));
		gs_search_page_set_text (GS_SEARCH_PAGE (page), text);
		gs_page_switch_to (page, TRUE);
	}
}

static gboolean
window_key_press_event (GtkWidget *win, GdkEventKey *event, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GdkKeymap *keymap;
	GdkModifierType state;
	gboolean is_rtl;
	GtkWidget *button;

	button = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	if (!gtk_widget_is_sensitive (button))
		return GDK_EVENT_PROPAGATE;

	state = event->state;
	keymap = gdk_keymap_get_for_display (gtk_widget_get_display (win));
	gdk_keymap_add_virtual_modifiers (keymap, &state);
	state = state & gtk_accelerator_get_default_mod_mask ();
	is_rtl = gtk_widget_get_direction (button) == GTK_TEXT_DIR_RTL;

	if ((!is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Left) ||
	    (is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Right) ||
	    event->keyval == GDK_KEY_Back) {
		gtk_widget_activate (button);
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static gboolean
window_button_press_event (GtkWidget *win, GdkEventButton *event, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *button;

	/* Mouse hardware back button is 8 */
	if (event->button != 8)
		return GDK_EVENT_PROPAGATE;

	button = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	if (!gtk_widget_is_sensitive (button))
		return GDK_EVENT_PROPAGATE;

	gtk_widget_activate (button);
	return GDK_EVENT_STOP;
}

static gboolean
main_window_closed_cb (GtkWidget *dialog, GdkEvent *event, gpointer user_data)
{
	GsShell *shell = user_data;

	/* hide any notifications */
	g_application_withdraw_notification (g_application_get_default (),
					     "installed");
	g_application_withdraw_notification (g_application_get_default (),
					     "install-resources");

	gs_shell_clean_back_entry_stack (shell);
	gtk_widget_hide (dialog);
	return TRUE;
}

static void
gs_shell_main_window_mapped_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gs_plugin_loader_set_scale (priv->plugin_loader,
				    (guint) gtk_widget_get_scale_factor (widget));
}

static void
gs_shell_main_window_realized_cb (GtkWidget *widget, GsShell *shell)
{

	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	/* adapt the window for low resolution screens */
	if (gs_utils_is_low_resolution (GTK_WIDGET (priv->main_window))) {
		    GtkWidget *buttonbox = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));

			gtk_container_child_set (GTK_CONTAINER (buttonbox),
					     GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_all")),
					     "non-homogeneous", TRUE,
					     NULL);
			gtk_container_child_set (GTK_CONTAINER (buttonbox),
					     GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed")),
					     "non-homogeneous", TRUE,
					     NULL);
			gtk_container_child_set (GTK_CONTAINER (buttonbox),
					     GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates")),
					     "non-homogeneous", TRUE,
					     NULL);
			gtk_container_child_set (GTK_CONTAINER (buttonbox),
					     GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_category")),
					     "non-homogeneous", TRUE,
					     NULL);
	}
}

static void
gs_shell_allow_updates_notify_cb (GsPluginLoader *plugin_loader,
				    GParamSpec *pspec,
				    GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_widget_set_visible (widget, gs_plugin_loader_get_allow_updates (plugin_loader) ||
					priv->mode == GS_SHELL_MODE_UPDATES);
}

static gboolean
gs_shell_has_disk_examination_app (void)
{
	g_autofree gchar *baobab = g_find_program_in_path ("baobab");
	return (baobab != NULL);
}

static void
gs_shell_show_event_app_notify (GsShell *shell,
				const gchar *title,
				GsShellEventButtons buttons)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;

	/* set visible */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notification_event"));
	gtk_revealer_set_reveal_child (GTK_REVEALER (widget), TRUE);

	/* sources button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_sources"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_SOURCES) > 0);

	/* no-space button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_no_space"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_NO_SPACE) > 0 &&
					gs_shell_has_disk_examination_app());

	/* network settings button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_network_settings"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS) > 0);

	/* restart button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_restart_required"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_RESTART_REQUIRED) > 0);

	/* more-info button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_more_info"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_MORE_INFO) > 0);

	/* dismiss button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_dismiss"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_RESTART_REQUIRED) == 0);

	/* set title */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_events"));
	gtk_label_set_markup (GTK_LABEL (widget), title);
	gtk_widget_set_visible (widget, title != NULL);
}

static gchar *
gs_shell_get_title_from_origin (GsApp *app)
{
	/* get a title, falling back */
	if (gs_app_get_origin_hostname (app) != NULL) {
		/* TRANSLATORS: this is part of the in-app notification,
		 * where the %s is the truncated hostname, e.g.
		 * 'alt.fedoraproject.org' */
		return g_strdup_printf (_("“%s”"), gs_app_get_origin_hostname (app));
	}
	if (gs_app_get_origin (app) != NULL) {
		/* TRANSLATORS: this is part of the in-app notification,
		 * where the %s is the origin id, e.g. 'fedora' */
		return g_strdup_printf (_("“%s”"), gs_app_get_origin (app));
	}
	return g_strdup_printf ("“%s”", gs_app_get_id (app));
}

/* return a name for the app, using quotes if the name is more than one word */
static gchar *
gs_shell_get_title_from_app (GsApp *app)
{
	const gchar *tmp = gs_app_get_name (app);
	if (tmp != NULL) {
		if (g_strstr_len (tmp, -1, " ") != NULL) {
			/* TRANSLATORS: this is part of the in-app notification,
			 * where the %s is a multi-word localised app name
			 * e.g. 'Getting things GNOME!" */
			return g_strdup_printf (_("“%s”"), tmp);
		}
		return g_strdup (tmp);
	}
	return g_strdup_printf (_("“%s”"), gs_app_get_id (app));
}

static gboolean
gs_shell_show_detailed_error (GsShell *shell, const GError *error)
{
	if (error->code == GS_PLUGIN_ERROR_FAILED)
		return TRUE;
	if (error->code == GS_PLUGIN_ERROR_DOWNLOAD_FAILED)
		return TRUE;
	return FALSE;
}

static gchar *
get_first_line (const gchar *str)
{
	g_auto(GStrv) lines = NULL;

	lines = g_strsplit (str, "\n", 2);
	if (lines != NULL && g_strv_length (lines) != 0)
		return g_strdup (lines[0]);

	return NULL;
}

static void
gs_shell_append_detailed_error (GsShell *shell, GString *str, const GError *error)
{
	g_autofree gchar *first_line = get_first_line (error->message);
	if (first_line != NULL) {
		g_autofree gchar *escaped = g_markup_escape_text (first_line, -1);
		g_string_append_printf (str, ":\n%s", escaped);
	}
}

static gboolean
gs_shell_show_event_refresh (GsShell *shell, GsPluginEvent *event)
{
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	GsPluginAction action = gs_plugin_event_get_action (event);
	g_autofree gchar *str_origin = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			if (gs_app_get_bundle_kind (origin) == AS_BUNDLE_KIND_CABINET) {
				/* TRANSLATORS: failure text for the in-app notification,
				 * where the %s is the source (e.g. "alt.fedoraproject.org") */
				g_string_append_printf (str, _("Unable to download "
							       "firmware updates from %s"),
							str_origin);
			} else {
				/* TRANSLATORS: failure text for the in-app notification,
				 * where the %s is the source (e.g. "alt.fedoraproject.org") */
				g_string_append_printf (str, _("Unable to download updates from %s"),
							str_origin);
				if (gs_app_get_management_plugin (origin) != NULL)
					buttons |= GS_SHELL_EVENT_BUTTON_SOURCES;
			}
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("Unable to download updates"));
		}
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to download updates: "
				       "internet access was required but wasn’t available"));
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the source (e.g. "alt.fedoraproject.org") */
			g_string_append_printf (str, _("Unable to download updates "
					         "from %s: not enough disk space"),
					       str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("Unable to download updates: "
					        "not enough disk space"));
		}
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to download updates: "
				        "authentication was required"));
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to download updates: "
				        "authentication was invalid"));
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to download updates: you do not have"
				        " permission to install software"));
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		if (action == GS_PLUGIN_ACTION_DOWNLOAD) {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("Unable to download updates"));
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("Unable to get list of updates"));
		}
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* add extra debugging for debug builds */
	if (gs_shell_show_detailed_error (shell, error))
		gs_shell_append_detailed_error (shell, str, error);

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_purchase (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	const GError *error = gs_plugin_event_get_error (event);
	g_autofree gchar *str_app = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	str_app = gs_shell_get_title_from_app (app);
	switch (error->code) {
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to purchase %s: "
					       "authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to purchase %s: "
					       "authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_PURCHASE_NOT_SETUP:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to purchase %s: "
					       "no payment method setup"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_PURCHASE_DECLINED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to purchase %s: "
					       "payment was declined"),
					str_app);
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to purchase %s"), str_app);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add extra debugging for debug builds */
	if (gs_shell_show_detailed_error (shell, error))
		gs_shell_append_detailed_error (shell, str, error);

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, GS_SHELL_EVENT_BUTTON_NONE);
	return TRUE;
}

static gboolean
gs_shell_show_event_install (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autofree gchar *msg = NULL;
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	str_app = gs_shell_get_title_from_app (app);
	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the first %s is the application name (e.g. "GIMP") and
			 * the second %s is the origin, e.g. "Fedora Project [fedoraproject.org]"  */
			g_string_append_printf (str, _("Unable to install %s as "
						       "download failed from %s"),
					       str_app, str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to install %s "
						       "as download failed"),
						str_app);
		}
		break;
	case GS_PLUGIN_ERROR_NOT_SUPPORTED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the first %s is the application name (e.g. "GIMP")
			 * and the second %s is the name of the runtime, e.g.
			 * "GNOME SDK [flatpak.gnome.org]" */
			g_string_append_printf (str, _("Unable to install %s as "
						       "runtime %s not available"),
					       str_app, str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to install %s "
						       "as not supported"),
						str_app);
		}
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to install: internet access was "
				        "required but wasn’t available"));
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_INVALID_FORMAT:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to install: the application has an invalid format"));
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to install %s: "
					       "not enough disk space"),
					str_app);
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to install %s: "
					       "authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to install %s: "
					       "authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to install %s: "
					       "you do not have permission to "
					       "install software"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_ACCOUNT_SUSPENDED:
	case GS_PLUGIN_ERROR_ACCOUNT_DEACTIVATED:
		if (origin != NULL) {
			const gchar *url_homepage;

			/* TRANSLATORS: failure text for the in-app notification,
			 * the %s is the name of the authentication service,
			 * e.g. "Ubuntu One" */
			g_string_append_printf (str, _("Your %s account has been suspended."),
						gs_app_get_name (origin));
			g_string_append (str, " ");
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("It is not possible to install "
						"software until this has been resolved."));
			url_homepage = gs_app_get_url (origin, AS_URL_KIND_HOMEPAGE);
			if (url_homepage != NULL) {
				g_autofree gchar *url = NULL;
				url = g_strdup_printf ("<a href=\"%s\">%s</a>",
						       url_homepage,
						       url_homepage);
				/* TRANSLATORS: failure text for the in-app notification,
				 * where the %s is the clickable link (e.g.
				 * "http://example.com/what-did-i-do-wrong/") */
				msg = g_strdup_printf (_("For more information, visit %s."),
							url);
				g_string_append_printf (str, " %s", msg);
			}
		}
		break;
	case GS_PLUGIN_ERROR_AC_POWER_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "Dell XPS 13") */
		g_string_append_printf (str, _("Unable to install %s: "
					       "AC power is required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to install %s"), str_app);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* add extra debugging for debug builds */
	if (gs_shell_show_detailed_error (shell, error))
		gs_shell_append_detailed_error (shell, str, error);

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_update (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	str_app = gs_shell_get_title_from_app (app);
	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the first %s is the app name (e.g. "GIMP") and
			 * the second %s is the origin, e.g. "Fedora" or
			 * "Fedora Project [fedoraproject.org]" */
			g_string_append_printf (str, _("Unable to update %s from %s"),
					       str_app, str_origin);
			buttons = TRUE;
		} else {
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to update %s as download failed"),
						str_app);
		}
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to update: "
					"internet access was required but "
					"wasn’t available"));
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to update %s: "
					       "not enough disk space"),
					str_app);
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to update %s: "
					       "authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to update %s: "
					       "authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to update %s: "
					       "you do not have permission to "
					       "update software"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AC_POWER_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "Dell XPS 13") */
		g_string_append_printf (str, _("Unable to update %s: "
					       "AC power is required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to update %s"), str_app);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* add extra debugging for debug builds */
	if (gs_shell_show_detailed_error (shell, error))
		gs_shell_append_detailed_error (shell, str, error);

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_upgrade (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;

	str_app = g_strdup_printf ("%s %s", gs_app_get_name (app), gs_app_get_version (app));
	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the first %s is the distro name (e.g. "Fedora 25") and
			 * the second %s is the origin, e.g. "Fedora Project [fedoraproject.org]" */
			g_string_append_printf (str, _("Unable to upgrade to %s from %s"),
					       str_app, str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the app name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to upgrade to %s "
						       "as download failed"),
						str_app);
		}
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to upgrade: "
					"internet access was required but "
					"wasn’t available"));
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "not enough disk space"),
					str_app);
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "you do not have permission to upgrade"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AC_POWER_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "AC power is required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s"), str_app);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* add extra debugging for debug builds */
	if (gs_shell_show_detailed_error (shell, error))
		gs_shell_append_detailed_error (shell, str, error);

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_remove (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);
	g_autofree gchar *str_app = NULL;

	str_app = gs_shell_get_title_from_app (app);
	switch (error->code) {
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s: authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s: authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s: you do not have"
					       " permission to remove software"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AC_POWER_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s: "
					       "AC power is required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s"), str_app);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* add extra debugging for debug builds */
	if (gs_shell_show_detailed_error (shell, error))
		gs_shell_append_detailed_error (shell, str, error);

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_launch (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;

	switch (error->code) {
	case GS_PLUGIN_ERROR_NOT_SUPPORTED:
		if (app != NULL && origin != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the first %s is the application name (e.g. "GIMP")
			 * and the second %s is the name of the runtime, e.g.
			 * "GNOME SDK [flatpak.gnome.org]" */
			g_string_append_printf (str, _("Unable to launch %s: %s is not installed"),
					        str_app,
					        str_origin);
		}
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Not enough disk space — free up some space "
					"and try again"));
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		/* TRANSLATORS: we failed to get a proper error code */
		g_string_append (str, _("Sorry, something went wrong"));
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* add extra debugging for debug builds */
	if (gs_shell_show_detailed_error (shell, error))
		gs_shell_append_detailed_error (shell, str, error);

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_file_to_app (GsShell *shell, GsPluginEvent *event)
{
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);

	switch (error->code) {
	case GS_PLUGIN_ERROR_NOT_SUPPORTED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Failed to install file: not supported"));
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Failed to install file: authentication failed"));
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Not enough disk space — free up some space "
					"and try again"));
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		/* TRANSLATORS: we failed to get a proper error code */
		g_string_append (str, _("Sorry, something went wrong"));
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add extra debugging for debug builds */
	if (gs_shell_show_detailed_error (shell, error))
		gs_shell_append_detailed_error (shell, str, error);

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_url_to_app (GsShell *shell, GsPluginEvent *event)
{
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);

	switch (error->code) {
	case GS_PLUGIN_ERROR_NOT_SUPPORTED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Failed to install: not supported"));
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Failed to install: authentication failed"));
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Not enough disk space — free up some space "
					"and try again"));
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		/* TRANSLATORS: we failed to get a proper error code */
		g_string_append (str, _("Sorry, something went wrong"));
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add extra debugging for debug builds */
	if (gs_shell_show_detailed_error (shell, error))
		g_string_append_printf (str, "\n%s", error->message);

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_fallback (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;

	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * the %s is the origin, e.g. "Fedora" or
			 * "Fedora Project [fedoraproject.org]" */
			g_string_append_printf (str, _("Unable to contact %s"),
					        str_origin);
		}
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Not enough disk space — free up some space "
					"and try again"));
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_RESTART_REQUIRED:
		if (app != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("%s needs to be restarted "
						       "to use new plugins."),
						str_app);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("This application needs to be "
						"restarted to use new plugins."));
		}
		buttons |= GS_SHELL_EVENT_BUTTON_RESTART_REQUIRED;
		break;
	case GS_PLUGIN_ERROR_AC_POWER_REQUIRED:
		/* TRANSLATORS: need to be connected to the AC power */
		g_string_append (str, _("AC power is required"));
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		/* TRANSLATORS: we failed to get a proper error code */
		g_string_append (str, _("Sorry, something went wrong"));
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* add extra debugging for debug builds */
	if (gs_shell_show_detailed_error (shell, error))
		gs_shell_append_detailed_error (shell, str, error);

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event (GsShell *shell, GsPluginEvent *event)
{
	const GError *error;
	GsPluginAction action;

	/* get error */
	error = gs_plugin_event_get_error (event);
	if (error == NULL)
		return FALSE;

	/* name and shame the plugin */
	if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_TIMED_OUT)) {
		gs_shell_show_event_app_notify (shell, error->message,
						GS_SHELL_EVENT_BUTTON_NONE);
		return TRUE;
	}

	/* split up the events by action */
	action = gs_plugin_event_get_action (event);
	switch (action) {
	case GS_PLUGIN_ACTION_REFRESH:
	case GS_PLUGIN_ACTION_DOWNLOAD:
		return gs_shell_show_event_refresh (shell, event);
	case GS_PLUGIN_ACTION_PURCHASE:
		return gs_shell_show_event_purchase (shell, event);
	case GS_PLUGIN_ACTION_INSTALL:
		return gs_shell_show_event_install (shell, event);
	case GS_PLUGIN_ACTION_UPDATE:
		return gs_shell_show_event_update (shell, event);
	case GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD:
		return gs_shell_show_event_upgrade (shell, event);
	case GS_PLUGIN_ACTION_REMOVE:
		return gs_shell_show_event_remove (shell, event);
	case GS_PLUGIN_ACTION_LAUNCH:
		return gs_shell_show_event_launch (shell, event);
	case GS_PLUGIN_ACTION_FILE_TO_APP:
		return gs_shell_show_event_file_to_app (shell, event);
	case GS_PLUGIN_ACTION_URL_TO_APP:
		return gs_shell_show_event_url_to_app (shell, event);
	default:
		break;
	}

	/* capture some warnings every time */
	return gs_shell_show_event_fallback (shell, event);
}

static void
gs_shell_rescan_events (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;
	g_autoptr(GsPluginEvent) event = NULL;

	/* find the first active event and show it */
	event = gs_plugin_loader_get_event_default (priv->plugin_loader);
	if (event != NULL) {
		if (!gs_shell_show_event (shell, event)) {
			GsPluginAction action = gs_plugin_event_get_action (event);
			const GError *error = gs_plugin_event_get_error (event);
			if (error != NULL &&
			    !g_error_matches (error,
					      GS_PLUGIN_ERROR,
					      GS_PLUGIN_ERROR_CANCELLED)) {
				g_warning ("not handling error %s for action %s: %s",
					   gs_plugin_error_to_string (error->code),
					   gs_plugin_action_to_string (action),
					   error->message);
			}
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INVALID);
			return;
		}
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_VISIBLE);
		return;
	}

	/* nothing to show */
	g_debug ("no events to show");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notification_event"));
	gtk_revealer_set_reveal_child (GTK_REVEALER (widget), FALSE);
}

static void
gs_shell_events_notify_cb (GsPluginLoader *plugin_loader,
			   GParamSpec *pspec,
			   GsShell *shell)
{
	gs_shell_rescan_events (shell);
}

static void
gs_shell_plugin_event_dismissed_cb (GtkButton *button, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	guint i;
	g_autoptr(GPtrArray) events = NULL;

	/* mark any events currently showing as invalid */
	events = gs_plugin_loader_get_events (priv->plugin_loader);
	for (i = 0; i < events->len; i++) {
		GsPluginEvent *event = g_ptr_array_index (events, i);
		if (gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_VISIBLE)) {
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INVALID);
			gs_plugin_event_remove_flag (event, GS_PLUGIN_EVENT_FLAG_VISIBLE);
		}
	}

	/* show the next event */
	gs_shell_rescan_events (shell);
}

static void
gs_shell_setup_pages (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_autoptr(GList) keys = g_hash_table_get_keys (priv->pages);
	for (GList *l = keys; l != NULL; l = l->next) {
		g_autoptr(GError) error = NULL;
		GsPage *page = GS_PAGE (g_hash_table_lookup (priv->pages, l->data));
		if (!gs_page_setup (page, shell,
				    priv->plugin_loader,
				    priv->builder,
				    priv->cancellable,
				    &error)) {
			g_warning ("Failed to setup panel: %s", error->message);
		}
	}
}

void
gs_shell_setup (GsShell *shell, GsPluginLoader *plugin_loader, GCancellable *cancellable)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;
	GtkStyleContext *style_context;
	GsPage *page;
	g_return_if_fail (GS_IS_SHELL (shell));

	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect_object (priv->plugin_loader, "notify::events",
				 G_CALLBACK (gs_shell_events_notify_cb),
				 shell, 0);
	g_signal_connect_object (priv->plugin_loader, "notify::allow-updates",
				 G_CALLBACK (gs_shell_allow_updates_notify_cb),
				 shell, 0);
    priv->cancellable = g_object_ref (cancellable);

	/* get UI */
	priv->builder = gtk_builder_new_from_resource ("/org/gnome/Software/gooroom-software.ui");
	priv->main_window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
	g_signal_connect (priv->main_window, "map",
			  G_CALLBACK (gs_shell_main_window_mapped_cb), shell);
	g_signal_connect (priv->main_window, "realize",
			  G_CALLBACK (gs_shell_main_window_realized_cb), shell);

	g_signal_connect (priv->main_window, "delete-event",
			  G_CALLBACK (main_window_closed_cb), shell);

	/* fix up the header bar */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));
	if (gs_utils_is_current_desktop ("Unity")) {
		style_context = gtk_widget_get_style_context (widget);
		gtk_style_context_remove_class (style_context, GTK_STYLE_CLASS_TITLEBAR);
		gtk_style_context_add_class (style_context, GTK_STYLE_CLASS_PRIMARY_TOOLBAR);
		gtk_header_bar_set_decoration_layout (GTK_HEADER_BAR (widget), "");
	} else {
		g_object_ref (widget);
		gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (widget)), widget);
		gtk_window_set_titlebar (GTK_WINDOW (priv->main_window), widget);
		g_object_unref (widget);
	}

	/* global keynav */
	g_signal_connect_after (priv->main_window, "key_press_event",
				G_CALLBACK (window_key_press_event), shell);
	/* mouse hardware back button */
	g_signal_connect_after (priv->main_window, "button_press_event",
				G_CALLBACK (window_button_press_event), shell);

	/* setup buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_main"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_main_button_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_back_button_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_all"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_SHELL_MODE_OVERVIEW));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_overview_page_button_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_SHELL_MODE_INSTALLED));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_overview_page_button_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_SHELL_MODE_UPDATES));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_overview_page_button_cb), shell);

	if (priv->enable_group)
	{
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_group"));
		g_object_set_data (G_OBJECT (widget),
						"gnome-software::overview-mode",
						GINT_TO_POINTER (GS_SHELL_MODE_GROUP));
						g_signal_connect (widget, "clicked",
						G_CALLBACK (gs_overview_page_button_cb), shell);

		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_consent"));
		g_object_set_data (G_OBJECT (widget),
						"gnome-software::overview-mode",
						GINT_TO_POINTER (GS_SHELL_MODE_CONSENT));
						g_signal_connect (widget, "clicked",
						G_CALLBACK (gs_overview_page_button_cb), shell);
	}
	/* category menu button */
	widget = gtk_menu_new ();
	style_context = gtk_widget_get_style_context (widget);
	gtk_style_context_remove_class (style_context, GTK_STYLE_CLASS_MENU);
	gtk_style_context_remove_class (style_context, GTK_STYLE_CLASS_MENUBAR);
	gtk_style_context_add_class (style_context, "main_category_menu");
	gtk_widget_set_size_request (widget, 216, 280);
	priv->category_menu = widget;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_category"));
	gtk_menu_button_set_popup ((GtkMenuButton*)widget, priv->category_menu);
	gtk_menu_button_set_direction ((GtkMenuButton*)widget, GTK_ARROW_RIGHT);
	gtk_widget_set_sensitive (widget, TRUE);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_category_button_cb), shell);
	g_signal_connect (widget, "enter-notify-event",
			  G_CALLBACK (gs_category_button_activate_cb), shell);
	g_signal_connect (widget, "leave-notify-event",
			  G_CALLBACK (gs_category_button_activate_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_category_right_arrow_image"));
	gtk_widget_hide (widget);
	/* set up in-app notification controls */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_dismiss"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_event_dismissed_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_sources"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_events_sources_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_no_space"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_events_no_space_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_network_settings"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_events_network_settings_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_more_info"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_events_more_info_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_restart_required"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_events_restart_required_cb), shell);

	/* add pages to hash */
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "overview_page"));
	g_hash_table_insert (priv->pages, g_strdup ("overview"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "updates_page"));
	g_hash_table_insert (priv->pages, g_strdup ("updates"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "installed_page"));
	g_hash_table_insert (priv->pages, g_strdup ("installed"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "moderate_page"));
	g_hash_table_insert (priv->pages, g_strdup ("moderate"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "loading_page"));
	g_hash_table_insert (priv->pages, g_strdup ("loading"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "search_page"));
	g_hash_table_insert (priv->pages, g_strdup ("search"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "details_page"));
	g_hash_table_insert (priv->pages, g_strdup ("details"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "category_page"));
	g_hash_table_insert (priv->pages, g_strdup ("category"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "extras_page"));
	g_hash_table_insert (priv->pages, g_strdup ("extras"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "consent_page"));
	g_hash_table_insert (priv->pages, g_strdup ("consent"), page);
	gs_shell_setup_pages (shell);

	/* set up search */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	priv->search_changed_id =
		g_signal_connect (widget, "search-changed",
				  G_CALLBACK (search_changed_handler), shell);

	/* load content */
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "loading_page"));
	g_signal_connect (page, "refreshed",
			  G_CALLBACK (initial_refresh_done), shell);

	/* coldplug */
	gs_shell_rescan_events (shell);

	/* show loading page, which triggers the initial refresh */
	gs_shell_change_mode (shell, GS_SHELL_MODE_LOADING, NULL, TRUE);
}

void
gs_shell_set_mode (GsShell *shell, GsShellMode mode)
{
	gs_shell_change_mode (shell, mode, NULL, TRUE);
}

GsShellMode
gs_shell_get_mode (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	return priv->mode;
}

const gchar *
gs_shell_get_mode_string (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	return page_name[priv->mode];
}

void
gs_shell_install (GsShell *shell, GsApp *app, GsShellInteraction interaction)
{
	GsPage *page;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_DETAILS,
			      (gpointer) app, TRUE);
	page = GS_PAGE (g_hash_table_lookup (priv->pages, "details"));
	gs_page_install_app (page, app, interaction, priv->cancellable);
}

void
gs_shell_show_installed_updates (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *dialog;

	dialog = gs_update_dialog_new (priv->plugin_loader);
	gs_update_dialog_show_installed_updates (GS_UPDATE_DIALOG (dialog));

	gtk_window_set_transient_for (GTK_WINDOW (dialog), priv->main_window);
	gtk_window_present (GTK_WINDOW (dialog));
}

void
gs_shell_show_sources (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *dialog;

	/* use if available */
	if (g_spawn_command_line_async ("software-properties-gtk", NULL))
		return;

	dialog = gs_repos_dialog_new (priv->main_window, priv->plugin_loader);
	gs_shell_modal_dialog_present (shell, GTK_DIALOG (dialog));

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), dialog);
}

void
gs_shell_show_prefs (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *dialog;

	dialog = gs_prefs_dialog_new (priv->main_window, priv->plugin_loader);
	gs_shell_modal_dialog_present (shell, GTK_DIALOG (dialog));

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), dialog);
}

void
gs_shell_show_app (GsShell *shell, GsApp *app)
{
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_DETAILS, app, TRUE);
	gs_shell_activate (shell);
}

void
gs_shell_show_category (GsShell *shell, GsCategory *category)
{
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_CATEGORY, category, TRUE);
}

void gs_shell_show_extras_search (GsShell *shell, const gchar *mode, gchar **resources)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GsPage *page;

	page = GS_PAGE (gtk_builder_get_object (priv->builder, "extras_page"));

	save_back_entry (shell);
	gs_extras_page_search (GS_EXTRAS_PAGE (page), mode, resources);
	gs_shell_change_mode (shell, GS_SHELL_MODE_EXTRAS, NULL, TRUE);
	gs_shell_activate (shell);
}

void
gs_shell_show_search (GsShell *shell, const gchar *search)
{
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH,
			      (gpointer) search, TRUE);
}

void
gs_shell_show_local_file (GsShell *shell, GFile *file)
{
	g_autoptr(GsApp) app = gs_app_new (NULL);
	save_back_entry (shell);
	gs_app_set_local_file (app, file);
	gs_shell_change_mode (shell, GS_SHELL_MODE_DETAILS,
			      (gpointer) app, TRUE);
	gs_shell_activate (shell);
}

void
gs_shell_show_search_result (GsShell *shell, const gchar *id, const gchar *search)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GsPage *page;

	save_back_entry (shell);
	page = GS_PAGE (g_hash_table_lookup (priv->pages, "search"));
	gs_search_page_set_appid_to_show (GS_SEARCH_PAGE (page), id);
	gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH,
			      (gpointer) search, TRUE);
}

void
gs_shell_show_uri (GsShell *shell, const gchar *url)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_autoptr(GError) error = NULL;

	if (!gtk_show_uri_on_window (priv->main_window,
	                             url,
	                             GDK_CURRENT_TIME,
	                             &error)) {
		g_warning ("failed to show URI %s: %s",
		           url, error->message);
	}
}

void
gs_shell_notify(GsShell *shell,
				const gchar *title,
				GsShellEventButtons buttons)
{
	gs_shell_show_event_app_notify (shell, title, buttons);
}

static void
gs_shell_dispose (GObject *object)
{
	GsShell *shell = GS_SHELL (object);
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	if (priv->back_entry_stack != NULL) {
		g_queue_free_full (priv->back_entry_stack, (GDestroyNotify) free_back_entry);
		priv->back_entry_stack = NULL;
	}
	g_clear_object (&priv->builder);
	g_clear_object (&priv->cancellable);
	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->header_start_widget);
	g_clear_object (&priv->header_end_widget);
	g_clear_object (&priv->page_last);
	g_clear_pointer (&priv->pages, g_hash_table_unref);
	g_clear_pointer (&priv->events_info_uri, g_free);
	g_clear_pointer (&priv->modal_dialogs, g_ptr_array_unref);

	g_clear_object (&priv->category_menu);

	g_object_unref (priv->settings);

	G_OBJECT_CLASS (gs_shell_parent_class)->dispose (object);
}

static void
gs_shell_class_init (GsShellClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_shell_dispose;

	signals [SIGNAL_LOADED] =
		g_signal_new ("loaded",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsShellClass, loaded),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
gs_shell_init (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	priv->pages = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	priv->back_entry_stack = g_queue_new ();
	priv->ignore_primary_buttons = FALSE;
	priv->modal_dialogs = g_ptr_array_new_with_free_func ((GDestroyNotify) gtk_widget_destroy);
	priv->search_clean = FALSE;
	priv->settings = g_settings_new ("kr.gooroom.software");
	priv->enable_group = gs_utils_check_gooroom_version (GOOROOM_VERSION) || gs_utils_check_gooroom_version (HGOOROOM_VERSION);
}

GsShell *
gs_shell_new (void)
{
	GsShell *shell;
	shell = g_object_new (GS_TYPE_SHELL, NULL);
	return GS_SHELL (shell);
}

/* vim: set noexpandtab: */
