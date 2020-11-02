/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2020 Gooroom <gooroom@gooroom.kr>
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

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#ifdef HAVE_GSPELL
#include <gspell/gspell.h>
#endif

#include "gs-category.h"
#include "gs-common.h"
#include "gs-consent-page.h"

struct _GsConsentPage
{
	GsPage       parent_instance;

	GsPluginLoader  *plugin_loader;
    GtkBuilder  *builder;
	GsShell     *shell;

	GtkWidget	*stack_consent;
	GtkWidget	*spinner_consent;

	GtkWidget	*consent_button;

	GSettings   *settings;
};

G_DEFINE_TYPE (GsConsentPage, gs_consent_page, GS_TYPE_PAGE)

static void
gs_consent_page_done_button_cb (GtkWidget *widget, GsConsentPage *self)
{
    g_autofree gchar *name;
    GPtrArray *find_categories;
	GsCategory *ca, *parent;

    name = g_strdup ("nonfree");
    find_categories = g_ptr_array_new ();
    g_ptr_array_add (find_categories, name);

    g_settings_set_boolean (self->settings, "user-consent", TRUE);

    ca = gs_shell_get_category  (self->shell, find_categories);
    parent = gs_category_get_parent (ca);
    gs_shell_change_mode (self->shell, GS_SHELL_MODE_NONFREE, parent, FALSE);

}

static void
gs_consent_page_init (GsConsentPage *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
    self->settings = g_settings_new ("kr.gooroom.software");

    g_signal_connect (self->consent_button, "clicked",
              G_CALLBACK (gs_consent_page_done_button_cb), self);

	gtk_stack_set_visible_child_name (GTK_STACK (self->stack_consent), "ready");
}

static void
gs_consent_page_dispose (GObject *object)
{
    GsConsentPage *self = GS_CONSENT_PAGE (object);
	g_object_unref (self->settings);

    g_clear_object (&self->builder);
    g_clear_object (&self->plugin_loader);

	G_OBJECT_CLASS (gs_consent_page_parent_class)->dispose (object);
}

static gboolean
gs_consent_page_setup (GsPage *page,
                       GsShell *shell,
                       GsPluginLoader *plugin_loader,
                       GtkBuilder *builder,
                       GCancellable *cancellable,
                       GError **error)
{
    GsConsentPage *self = GS_CONSENT_PAGE (page);

    self->plugin_loader = g_object_ref (plugin_loader);
    self->builder = g_object_ref (builder);
    self->shell = shell;
    return TRUE;
}

static void
gs_consent_page_class_init (GsConsentPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_consent_page_dispose;
    page_class->setup = gs_consent_page_setup;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-consent-page.ui");

	gtk_widget_class_bind_template_child (widget_class, GsConsentPage, stack_consent);
	gtk_widget_class_bind_template_child (widget_class, GsConsentPage, spinner_consent);

	gtk_widget_class_bind_template_child (widget_class, GsConsentPage, consent_button);
}

GsConsentPage *
gs_consent_page_new (void)
{
    GsConsentPage *self;
	self = g_object_new (GS_TYPE_CONSENT_PAGE, NULL);
	return GS_CONSENT_PAGE (self);
}

/* vim: set noexpandtab: */
