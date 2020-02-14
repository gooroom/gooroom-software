/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-popular-tile.h"
#include "gs-star-widget.h"
#include "gs-common.h"
#include "gs-utils.h"

struct _GsPopularTile
{
	GsAppTile	 parent_instance;

	GsApp		*app;
	GtkWidget	*label;
	GtkWidget	*image;
	GtkWidget	*stack;
	GtkWidget	*category;
	guint		 app_state_changed_id;
};

G_DEFINE_TYPE (GsPopularTile, gs_popular_tile, GS_TYPE_APP_TILE)

static GsApp *
gs_popular_tile_get_app (GsAppTile *tile)
{
	return GS_POPULAR_TILE (tile)->app;
}

static gboolean
app_state_changed_idle (gpointer user_data)
{
	GsPopularTile *tile = GS_POPULAR_TILE (user_data);
	AtkObject *accessible;
	g_autofree gchar *name = NULL;

	tile->app_state_changed_id = 0;

	accessible = gtk_widget_get_accessible (GTK_WIDGET (tile));

	switch (gs_app_get_state (tile->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		/* TRANSLATORS: this refers to an app (by name) that is installed */
		name = g_strdup_printf (_("%s (Installed)"),
					gs_app_get_name (tile->app));
		break;
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_INSTALLING:
	default:
		name = g_strdup (gs_app_get_name (tile->app));
		break;
	}

	if (GTK_IS_ACCESSIBLE (accessible)) {
		atk_object_set_name (accessible, name);
		atk_object_set_description (accessible, gs_app_get_summary (tile->app));
	}

	return G_SOURCE_REMOVE;
}

static void
app_state_changed (GsApp *app, GParamSpec *pspec, GsPopularTile *tile)
{
	g_clear_handle_id (&tile->app_state_changed_id, g_source_remove);
	tile->app_state_changed_id = g_idle_add (app_state_changed_idle, tile);
}

static void
gs_popular_tile_set_app (GsAppTile *app_tile, GsApp *app)
{
	GsPopularTile *tile = GS_POPULAR_TILE (app_tile);
	const gchar *css;
	const gchar *group;
	const gchar *desktop_name;
	g_autofree gchar *str = NULL;
    GPtrArray *categories;

	g_return_if_fail (GS_IS_APP (app) || app == NULL);

	if (tile->app != NULL)
		g_signal_handlers_disconnect_by_func (tile->app, app_state_changed, tile);
	g_clear_handle_id (&tile->app_state_changed_id, g_source_remove);

	g_set_object (&tile->app, app);
	if (!app)
		return;
	gtk_stack_set_visible_child_name (GTK_STACK (tile->stack), "content");

	g_signal_connect (tile->app, "notify::state",
		 	  G_CALLBACK (app_state_changed), tile);
	g_signal_connect (tile->app, "notify::name",
			  G_CALLBACK (app_state_changed), tile);
	g_signal_connect (tile->app, "notify::summary",
			  G_CALLBACK (app_state_changed), tile);
	app_state_changed (tile->app, NULL, tile);

	/* perhaps set custom css */
	css = gs_app_get_metadata_item (app, "GnomeSoftware::PopularTile-css");
	gs_utils_widget_set_css (GTK_WIDGET (tile), css);

	if (gs_app_get_pixbuf (tile->app) != NULL) {
		gs_image_set_from_pixbuf (GTK_IMAGE (tile->image), gs_app_get_pixbuf (tile->app));
	} else {
		gtk_image_set_from_icon_name (GTK_IMAGE (tile->image),
					      "application-x-executable",
					      GTK_ICON_SIZE_DIALOG);
	}

	gtk_label_set_label (GTK_LABEL (tile->label), gs_app_get_name (app));

	group = gs_app_get_desktop_group (app);
	if (group == NULL) {
        categories = gs_app_get_categories (app);
        if (categories && 0 == categories->len)
			return;
        group = g_ptr_array_index (categories, 0);
	}

    desktop_name = gs_utils_get_desktop_category_label (group);
	if (desktop_name ==  NULL)
		desktop_name = group;
	str = g_strdup (_(desktop_name));
	gtk_label_set_label (GTK_LABEL (tile->category), str);
}

static void
gs_popular_tile_destroy (GtkWidget *widget)
{
	GsPopularTile *tile = GS_POPULAR_TILE (widget);

	if (tile->app != NULL)
		g_signal_handlers_disconnect_by_func (tile->app, app_state_changed, tile);
	g_clear_handle_id (&tile->app_state_changed_id, g_source_remove);
	g_clear_object (&tile->app);

	GTK_WIDGET_CLASS (gs_popular_tile_parent_class)->destroy (widget);
}

static void
gs_popular_tile_init (GsPopularTile *tile)
{
	gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
	gtk_widget_init_template (GTK_WIDGET (tile));
}

static void
gs_popular_tile_class_init (GsPopularTileClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GsAppTileClass *app_tile_class = GS_APP_TILE_CLASS (klass);

	widget_class->destroy = gs_popular_tile_destroy;

	app_tile_class->set_app = gs_popular_tile_set_app;
	app_tile_class->get_app = gs_popular_tile_get_app;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-popular-tile.ui");

	gtk_widget_class_bind_template_child (widget_class, GsPopularTile, label);
	gtk_widget_class_bind_template_child (widget_class, GsPopularTile, category);
	gtk_widget_class_bind_template_child (widget_class, GsPopularTile, image);
	gtk_widget_class_bind_template_child (widget_class, GsPopularTile, stack);
}

GtkWidget *
gs_popular_tile_new (GsApp *app)
{
	GsPopularTile *tile;

	tile = g_object_new (GS_TYPE_POPULAR_TILE, NULL);
	if (app != NULL)
		gs_app_tile_set_app (GS_APP_TILE (tile), app);

	return GTK_WIDGET (tile);
}

void
gs_popular_tile_show_source (GsPopularTile *tile, gboolean show_source)
{
}
/* vim: set noexpandtab: */
