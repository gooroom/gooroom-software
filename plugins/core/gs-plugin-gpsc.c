/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#include <config.h>

#include <fnmatch.h>
#include <gnome-software.h>
#include <glib/gi18n.h>
#include <json-glib/json-glib.h>

/*
 * SECTION:
 * Blacklists some applications based on a hardcoded list.
 */

struct GsPluginData {
    GSettings *settings;
    GPtrArray *popularList;
    GPtrArray *featuredList;
    GPtrArray *unexposedList;
};

static gchar*
gs_plugin_gpsc_token_parser (const gchar *filename, GError **error)
{
    JsonNode *json_root;
    JsonObject *json_item;
    JsonNode *json_node;
    g_autoptr(JsonParser) json_parser = NULL;
    json_parser = json_parser_new ();

    if (!json_parser_load_from_file (json_parser, filename, error))
    {
        gs_utils_error_convert_json_glib (error);
        return NULL;
    }

    json_root = json_parser_get_root (json_parser);
    if (json_root == NULL)
        goto error;

    if (json_node_get_node_type (json_root) != JSON_NODE_OBJECT)
        goto error;

    json_item = json_node_get_object (json_root);
    if (json_item == NULL)
        goto error;

    if (json_object_has_member (json_item, "data"))
    {
        json_item = json_object_get_object_member (json_item, "data");
        if (json_item == NULL)
            goto error;;

        json_item = json_object_get_object_member (json_item, "loginInfo");
        if (json_item == NULL)
            goto error;

        json_node = json_object_get_member (json_item, "login_token");
        if (json_node != NULL)
            return  g_strdup (json_node_get_string (json_node));
    }

error :
    g_set_error_literal (error,
                        GS_PLUGIN_ERROR,
                        GS_PLUGIN_ERROR_FAILED,
                        "");
    g_debug ("error, gs_plugin_gpsc_token_parser");
    return NULL;
}

static gboolean
gs_plugin_gpsc_data_parser (GsPlugin *plugin, const gchar *data, GError **error)
{
    JsonNode *json_root;
    JsonObject *json_item;
    g_autoptr(JsonParser) json_parser = NULL;
    GsPluginData *priv = gs_plugin_get_data (plugin);
    json_parser = json_parser_new ();

    if (!json_parser_load_from_data (json_parser, data, strlen (data), error))
    {
        gs_utils_error_convert_json_glib (error);
        return FALSE;
    }

    json_root = json_parser_get_root (json_parser);
    if (json_root == NULL)
        goto error;

    if (json_node_get_node_type (json_root) != JSON_NODE_OBJECT)
        goto error;

    json_item = json_node_get_object (json_root);
    if (json_item == NULL)
        goto error;

    if (json_object_has_member (json_item, "unexposedList"))
    {
        guint i;
        gchar* unexposed_id;
        JsonArray *array = json_object_get_array_member (json_item, "unexposedList");

        for (i = 0; i < json_array_get_length (array); i++)
        {
            JsonNode *node = json_array_get_element (array, i);
            if (node == NULL)
                continue;

            unexposed_id = g_strdup (json_node_get_string (node));
            g_debug ("unexposed id : %s", unexposed_id);
            g_ptr_array_add (priv->unexposedList, unexposed_id);
        }
    }

    if (json_object_has_member (json_item, "featuredList"))
    {
        guint i;
        gchar* featured_id;
        JsonArray *array = json_object_get_array_member (json_item, "featuredList");

        for (i = 0; i < json_array_get_length (array); i++)
        {
            JsonNode *node = json_array_get_element (array, i);
            if (node == NULL)
                continue;

            featured_id = g_strdup (json_node_get_string (node));
            g_debug ("featured id : %s", featured_id);
            g_ptr_array_add (priv->featuredList, featured_id);
        }
    }

    if (json_object_has_member (json_item, "popularList"))
    {
        guint i;
        gchar* popular_id;
        JsonArray *array = json_object_get_array_member (json_item, "popularList");

        for (i = 0; i < json_array_get_length (array); i++)
        {
            JsonNode *node = json_array_get_element (array, i);
            if (node == NULL)
                continue;

            popular_id = g_strdup (json_node_get_string (node));
            g_debug ("popular id : %s", popular_id);
            g_ptr_array_add (priv->popularList, popular_id);
        }
    }

   return TRUE;

error:
    g_set_error_literal (error,
                        GS_PLUGIN_ERROR,
                        GS_PLUGIN_ERROR_FAILED,
                        "");
    g_debug ("error, gs_plugin_gpsc_data_parser");
    return FALSE;
}

static void
gs_plugin_repository_initialize (GsPlugin *plugin)
{
    g_autofree gchar *uri = NULL;
    g_autofree gchar *cmd = NULL;
    g_autoptr(GError) error = NULL;
    GsPluginData *priv = gs_plugin_get_data (plugin);

    if (!gs_plugin_get_network_available (plugin)) {
        g_warning ("no network connection is available");
        return;
    }
    //Flathub Repository for Runtime
    uri = g_settings_get_string (priv->settings, "software-runtime-remote");
    cmd = g_strdup_printf ("flatpak --user remote-add --if-not-exists flathub %s", uri);
    if (!g_spawn_command_line_sync (cmd, NULL, NULL, NULL, &error))
        g_warning ("failed to setting flathub repository : %s", error->message);

    //Gooroom Repository
    uri = g_settings_get_string (priv->settings, "software-remote");
    cmd = g_strdup_printf ("flatpak --user remote-add --if-not-exists gooroom %s", uri);
    if (!g_spawn_command_line_sync (cmd, NULL, NULL, NULL, &error)) {
        g_warning ("failed to setting gooroom repository : %s", error->message);
    }
    else {
        g_debug ("gs_plugin_repository_initialize : update flatpak");
        cmd = g_strdup ("flatpak --user update --appstream gooroom");
        if (!g_spawn_command_line_async (cmd, &error))
            g_warning ("failed to flatpak update : %s", error->message);
    }
}

static void
gs_plugin_session_initialize (GsPlugin *plugin)
{
    guint status_code;
    const gchar *res_data = NULL;
    g_autofree gchar *uri = NULL;
    g_autofree gchar *data = NULL;
    g_autofree gchar *client_id = NULL;
    g_autofree gchar *client_id_path = NULL;
    g_autofree gchar *access_token = NULL;
    g_autofree gchar *access_token_path = NULL;
    g_autoptr(SoupMessage) msg = NULL;
    g_autoptr(GError) error = NULL;
    GsPluginData *priv = gs_plugin_get_data (plugin);

    GKeyFile *keyfile = g_key_file_new ();
    client_id_path= g_build_filename ("/etc/gooroom/gooroom-client-server-register/", "gcsr.conf", NULL);
    if (g_key_file_load_from_file (keyfile, client_id_path, G_KEY_FILE_NONE, &error))
       client_id = g_key_file_get_string (keyfile, "certificate", "client_name", NULL);
    else
        client_id = g_strdup ("");

    access_token_path = g_strdup_printf ("/var/run/user/%u/gooroom/.grm-user", getuid());
    if (g_file_test (access_token_path, G_FILE_TEST_EXISTS))
        access_token = g_strdup_printf ("Bearer %s", gs_plugin_gpsc_token_parser (access_token_path, &error));
    else
        access_token = g_strdup ("");

    g_debug ("client id is %s", client_id);
    g_debug ("access token is %s", access_token);

    uri = g_settings_get_string (priv->settings, "software-server");
    msg = soup_message_new (SOUP_METHOD_GET, uri);

    soup_message_headers_append (msg->request_headers, "client-Id", client_id);
    soup_message_headers_append (msg->request_headers, "authorization", access_token);
    soup_message_headers_append (msg->request_headers, "Accept", "application/vnd.v1.anonymous+json");

    status_code = soup_session_send_message (gs_plugin_get_soup_session (plugin), msg);
    if (status_code != SOUP_STATUS_OK) {
        g_set_error_literal (&error,
                            GS_PLUGIN_ERROR,
                            GS_PLUGIN_ERROR_FAILED,
                            "status code invalid");

        g_debug ("error, status_code_invalid");
        return;
    }

    res_data = msg->response_body->data;
    g_debug ("response data : %s", res_data);
    if (res_data != NULL)
        gs_plugin_gpsc_data_parser (plugin, res_data, &error);
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
    GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
    priv->settings = g_settings_new ("kr.gooroom.software");
    priv->unexposedList = g_ptr_array_new ();
    priv->featuredList = g_ptr_array_new ();
    priv->popularList = g_ptr_array_new ();

    gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");

    /* flatpak repository */
    gs_plugin_repository_initialize (plugin);
    /* http session */
    gs_plugin_session_initialize (plugin);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
    GsPluginData *priv = gs_plugin_get_data (plugin);
    if (priv->unexposedList != NULL)
        g_ptr_array_unref (priv->unexposedList);
    if (priv->featuredList != NULL)
        g_ptr_array_unref (priv->featuredList);
    if (priv->popularList != NULL)
        g_ptr_array_unref (priv->popularList);
}

gboolean
gs_plugin_add_editor_featured (GsPlugin *plugin,
                GsAppList *list,
                GCancellable *cancellable,
                GError **error)
{
    guint i;
    GsPluginData *priv = gs_plugin_get_data (plugin);

    if (priv->featuredList == NULL)
        return FALSE;

    for (i = 0; i < priv->featuredList->len; i++)
    {
        g_autoptr (GsApp) app = NULL;
        const gchar *data = g_ptr_array_index (priv->featuredList, i);
        app = gs_plugin_cache_lookup (plugin, data);
        if (app != NULL)
        {
            gs_app_list_add (list, app);
            continue;
        }

        app = gs_app_new (data);
        gs_app_add_kudo (app, GS_APP_KUDO_FEATURED);
        gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
        gs_app_set_metadata (app, "GnomeSoftware::Creator",
                     gs_plugin_get_name (plugin));

        gs_app_list_add (list, app);
        gs_plugin_cache_add (plugin, data, app);
    }

    for (i = 0; i < priv->featuredList->len; i++)
    {
        g_autoptr (GsApp) app = NULL;
        const gchar *data = g_ptr_array_index (priv->featuredList, i);
        const gchar *desktop = g_strdup_printf ("%s.desktop", data);
        app = gs_plugin_cache_lookup (plugin, desktop);
        if (app != NULL)
        {
            gs_app_list_add (list, app);
            continue;
        }

        app = gs_app_new (desktop);
        gs_app_add_kudo (app, GS_APP_KUDO_FEATURED);
        gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
        gs_app_set_metadata (app, "GnomeSoftware::Creator",
                     gs_plugin_get_name (plugin));

        gs_app_list_add (list, app);
        gs_plugin_cache_add (plugin, desktop, app);
    }
    return TRUE;
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
                GsAppList *list,
                GCancellable *cancellable,
                GError **error)
{
    guint i;
    GsPluginData *priv = gs_plugin_get_data (plugin);

    if (priv->popularList == NULL)
        return FALSE;

    for (i = 0; i < priv->popularList->len; i++)
    {
        g_autoptr (GsApp) app = NULL;
        const gchar *data = g_ptr_array_index (priv->popularList, i);
        app = gs_plugin_cache_lookup (plugin, data);
        if (app != NULL)
        {
            gs_app_list_add (list, app);
            continue;
        }

        app = gs_app_new (data);
        gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
        gs_app_set_metadata (app, "GnomeSoftware::Creator",
                     gs_plugin_get_name (plugin));
        gs_app_list_add (list, app);
        gs_plugin_cache_add (plugin, data, app);
    }

    for (i = 0; i < priv->popularList->len; i++)
    {
        g_autoptr (GsApp) app = NULL;
        const gchar *data = g_ptr_array_index (priv->popularList, i);
        const gchar *desktop = g_strdup_printf ("%s.desktop", data);
        app = gs_plugin_cache_lookup (plugin, desktop);
        if (app != NULL)
        {
            gs_app_list_add (list, app);
            continue;
        }

        app = gs_app_new (desktop);
        gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
        gs_app_set_metadata (app, "GnomeSoftware::Creator",
                     gs_plugin_get_name (plugin));
        gs_app_list_add (list, app);
        gs_plugin_cache_add (plugin, desktop, app);
    }
#if 0
    if (priv->featuredList == NULL)
        return FALSE;

    for (i = 0; i < priv->featuredList->len; i++)
    {
        g_autoptr (GsApp) app = NULL;
        const gchar *data = g_ptr_array_index (priv->featuredList, i);
        app = gs_plugin_cache_lookup (plugin, data);
        if (app != NULL)
        {
            gs_app_list_add (list, app);
            continue;
        }

        app = gs_app_new (data);
        gs_app_add_kudo (app, GS_APP_KUDO_FEATURED);
        gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
        gs_app_set_metadata (app, "GnomeSoftware::Creator",
                     gs_plugin_get_name (plugin));

        gs_app_list_add (list, app);
        gs_plugin_cache_add (plugin, data, app);
    }

    for (i = 0; i < priv->featuredList->len; i++)
    {
        g_autoptr (GsApp) app = NULL;
        const gchar *data = g_ptr_array_index (priv->featuredList, i);
        const gchar *desktop = g_strdup_printf ("%s.desktop", data);
        app = gs_plugin_cache_lookup (plugin, desktop);
        if (app != NULL)
        {
            gs_app_list_add (list, app);
            continue;
        }

        app = gs_app_new (desktop);
        gs_app_add_kudo (app, GS_APP_KUDO_FEATURED);
        gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
        gs_app_set_metadata (app, "GnomeSoftware::Creator",
                     gs_plugin_get_name (plugin));

        gs_app_list_add (list, app);
        gs_plugin_cache_add (plugin, desktop, app);
    }
#endif
    return TRUE;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
    GsPluginData *priv = gs_plugin_get_data (plugin);

    if (gs_app_get_id (app) == NULL)
        return TRUE;

    if (priv->unexposedList != NULL)
    {
        guint i;
        for (i = 0; i < priv->unexposedList->len; i++)
        {
            const gchar *data = g_ptr_array_index (priv->unexposedList, i);
            if (g_strcmp0 (data, gs_app_get_id (app)) == 0)
            {
                gs_app_add_category (app, "Blacklisted");
                g_debug ("Blacklisted is %s", gs_app_get_id (app));
                break;
            }
        }
        for (i = 0; i < priv->unexposedList->len; i++)
        {
            const gchar *data = g_ptr_array_index (priv->unexposedList, i);
            const gchar *desktop = g_strdup_printf ("%s.desktop", data);
            if (g_strcmp0 (desktop, gs_app_get_id (app)) == 0)
            {
                gs_app_add_category (app, "Blacklisted");
                g_debug ("Blacklisted is %s", gs_app_get_id (app));
                break;
            }
        }

    }

    if (priv->featuredList != NULL)
    {
        guint i;
        for (i = 0; i < priv->featuredList->len; i++)
        {
            const gchar *data = g_ptr_array_index (priv->featuredList, i);
            if (g_strcmp0 (data, gs_app_get_id (app)) != 0 )
                continue;

            gs_app_add_kudo (app, GS_APP_KUDO_FEATURED);
            break;
        }
    }

    return TRUE;
}
