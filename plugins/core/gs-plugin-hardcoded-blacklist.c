/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <fnmatch.h>
#include <gnome-software.h>

/*
 * SECTION:
 * Blacklists some applications based on a hardcoded list.
 */

struct GsPluginData {
	GSettings *settings;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = NULL;
	priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->settings = g_settings_new ("kr.gooroom.software");
	/* need ID */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->settings);
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	guint i;
	gboolean user_consent;
	GsPluginData *priv = NULL;
	const gchar *app_globs[] = {
		"freeciv-server.desktop",
		"links.desktop",
		"nm-connection-editor.desktop",
		"plank.desktop",
		"*release-notes*.desktop",
		"*Release_Notes*.desktop",
		"Rodent-*.desktop",
		"rygel-preferences.desktop",
		"system-config-keyboard.desktop",
		"tracker-preferences.desktop",
		"Uninstall*.desktop",
		"wine-*.desktop",
		NULL };

	/* not set yet */
	if (gs_app_get_id (app) == NULL)
		return TRUE;
#ifdef USE_GOOROOM
	user_consent = FALSE;
	priv = gs_plugin_get_data (plugin);
	user_consent  = g_settings_get_boolean (priv->settings, "user-consent");
	if (g_strcmp0 (gs_app_get_origin(app), "debian-stable-non-free") == 0)
	{
	    if (user_consent)
		{
		    gs_app_remove_category (app, "Blacklisted");
		}
		else
		{
		    gs_app_add_category (app, "Blacklisted");
		}
		return TRUE;
	}
#endif
	/* search */
	for (i = 0; app_globs[i] != NULL; i++) {
		if (fnmatch (app_globs[i], gs_app_get_id (app), 0) == 0) {
			gs_app_add_category (app, "Blacklisted");
			break;
		}
	}

	return TRUE;
}
