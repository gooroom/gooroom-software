/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include "gs-feature-tile-small.h"
#include "gs-common.h"
#include "gs-css.h"

struct _GsFeatureTileSmall
{
	GsAppTile	 parent_instance;

	GsApp		*app;
	GtkWidget	*stack;
	GtkWidget	*box;
	GtkWidget	*title;
    guint        app_state_changed_id;
};

G_DEFINE_TYPE (GsFeatureTileSmall, gs_feature_tile_small, GS_TYPE_APP_TILE)

static GsApp *
gs_feature_tile_small_get_app (GsAppTile *tile)
{
	return GS_FEATURE_TILE_SMALL (tile)->app;
}

static gboolean
app_state_changed_idle (gpointer user_data)
{
	GsFeatureTileSmall *tile = GS_FEATURE_TILE_SMALL (user_data);
	AtkObject *accessible;
	const gchar *markup;
	g_autofree gchar *name = NULL;
	g_autoptr(GsCss) css = NULL;

	tile->app_state_changed_id = 0;

	/* update text */
	gtk_label_set_label (GTK_LABEL (tile->title), gs_app_get_name (tile->app));

	/* perhaps set custom css */
	markup = gs_app_get_metadata_item (tile->app, "GnomeSoftware::FeatureTile-css");
    css = gs_css_new ();
	if (markup != NULL)
		gs_css_parse (css, markup, NULL);

	gs_utils_widget_set_css (tile->box,
				 gs_css_get_markup_for_id (css, "tile"));
	
    accessible = gtk_widget_get_accessible (GTK_WIDGET (tile));

	switch (gs_app_get_state (tile->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (tile->app),
					_("Installed"));
		break;
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_INSTALLING:
	default:
		name = g_strdup (gs_app_get_name (tile->app));
		break;
	}

	if (GTK_IS_ACCESSIBLE (accessible) && name != NULL) {
		atk_object_set_name (accessible, name);
		atk_object_set_description (accessible, gs_app_get_summary (tile->app));
	}

	return G_SOURCE_REMOVE;
}

static void
app_state_changed (GsApp *app, GParamSpec *pspec, GsFeatureTileSmall *tile)
{
	g_clear_handle_id (&tile->app_state_changed_id, g_source_remove);
	tile->app_state_changed_id = g_idle_add (app_state_changed_idle, tile);
}

static void
gs_feature_tile_small_set_app (GsAppTile *app_tile, GsApp *app)
{
	GsFeatureTileSmall *tile = GS_FEATURE_TILE_SMALL (app_tile);
	g_autoptr(GString) data = NULL;

	g_return_if_fail (GS_IS_APP (app) || app == NULL);

	g_set_object (&tile->app, app);
	if (app == NULL)
		return;
	
	gtk_stack_set_visible_child_name (GTK_STACK (tile->stack), "content");

	g_signal_connect (tile->app, "notify::state",
			  G_CALLBACK (app_state_changed), tile);
	g_signal_connect (tile->app, "notify::name",
			  G_CALLBACK (app_state_changed), tile);
	app_state_changed (tile->app, NULL, tile);
}

static void
gs_feature_tile_small_destroy (GtkWidget *widget)
{
	GsFeatureTileSmall *tile = GS_FEATURE_TILE_SMALL (widget);

    if (tile->app != NULL)
        g_signal_handlers_disconnect_by_func (tile->app, app_state_changed, tile);
    g_clear_handle_id (&tile->app_state_changed_id, g_source_remove);
    g_clear_object (&tile->app);

	GTK_WIDGET_CLASS (gs_feature_tile_small_parent_class)->destroy (widget);
}

static void
gs_feature_tile_small_init (GsFeatureTileSmall *tile)
{
	gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
	gtk_widget_init_template (GTK_WIDGET (tile));
}

static void
gs_feature_tile_small_class_init (GsFeatureTileSmallClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GsAppTileClass *app_tile_class = GS_APP_TILE_CLASS (klass);

	widget_class->destroy = gs_feature_tile_small_destroy;

	app_tile_class->set_app = gs_feature_tile_small_set_app;
	app_tile_class->get_app = gs_feature_tile_small_get_app;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-feature-tile-small.ui");

	gtk_widget_class_bind_template_child (widget_class, GsFeatureTileSmall, stack);
	gtk_widget_class_bind_template_child (widget_class, GsFeatureTileSmall, box);
	gtk_widget_class_bind_template_child (widget_class, GsFeatureTileSmall, title);
}

GtkWidget *
gs_feature_tile_small_new (GsApp *app)
{
	GsFeatureTileSmall *tile;
	tile = g_object_new (GS_TYPE_FEATURE_TILE_SMALL,
			     "vexpand", FALSE,
			     NULL);
	if (app != NULL)
		gs_app_tile_set_app (GS_APP_TILE (tile), app);
	return GTK_WIDGET (tile);
}

/* vim: set noexpandtab: */
