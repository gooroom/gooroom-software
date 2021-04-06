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

#include <locale.h>
#include <string.h>
#include <glib/gi18n.h>

#include "gs-common.h"
#include "gs-content-rating.h"

#include "gs-details-page.h"
#include "gs-app-addon-row.h"
#include "gs-auth-dialog.h"
#include "gs-history-dialog.h"
#include "gs-screenshot-image.h"
#include "gs-star-widget.h"
#include "gs-review-histogram.h"
#include "gs-review-dialog.h"
#include "gs-review-row.h"
#include "gs-popular-tile.h"
#include "gs-hiding-box.h"

#include "gs-utils.h"

/* the number of reviews to show before clicking the 'More Reviews' button */
#define N_TILES             		4
#define SHOW_NR_REVIEWS_INITIAL		4
#define HIDINGBOX_DEFAULT_SIZE_WIDTH    725 
#define HIDINGBOX_DEFAULT_SIZE_HEIGHT   200 

static void gs_details_page_refresh_all (GsDetailsPage *self);

typedef enum {
	GS_DETAILS_PAGE_STATE_LOADING,
	GS_DETAILS_PAGE_STATE_READY,
	GS_DETAILS_PAGE_STATE_FAILED
} GsDetailsPageState;

struct _GsDetailsPage
{
	GsPage			 parent_instance;

	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GCancellable		*app_cancellable;
	GsApp			*app;
	GsShell			*shell;
	SoupSession		*session;
	gboolean		 enable_reviews;
	gboolean		 show_all_reviews;
	GSettings		*settings;

	GtkWidget		*application_details_icon;
	GtkWidget		*application_details_category;
	GtkWidget		*application_details_developer;
	GtkWidget		*application_details_title;
	GtkWidget		*box_addons;
	GtkWidget		*box_details;
	GtkWidget		*box_details_description;
	GtkWidget		*box_progress;
	GtkWidget		*box_progress2;
	GtkWidget		*box_details_screenshot;
	GtkWidget		*box_details_screenshot_thumbnails;
	GtkWidget		*box_details_license_list;
	GtkWidget		*button_details_launch;
	GtkWidget		*button_details_add_shortcut;
	GtkWidget		*button_details_remove_shortcut;
	GtkWidget		*button_install;
	GtkWidget		*button_cancel;
	GtkWidget		*button_more_reviews;
	GtkWidget		*infobar_details_app_norepo;
	GtkWidget		*infobar_details_app_repo;
	GtkWidget		*infobar_details_package_baseos;
	GtkWidget		*infobar_details_repo;
	GtkWidget		*label_progress_percentage;
	GtkWidget		*label_progress_status;
	GtkWidget		*label_addons_uninstalled_app;
	GtkWidget		*label_details_category_title;
	GtkWidget		*label_details_category_value;
	GtkWidget		*label_details_developer_title;
	GtkWidget		*label_details_developer_value;
	GtkWidget		*button_details_license_free;
	GtkWidget		*button_details_license_nonfree;
	GtkWidget		*button_details_license_unknown;
	GtkWidget		*label_details_size_installed_title;
	GtkWidget		*label_details_size_installed_value;
	GtkWidget		*label_details_size_download_title;
	GtkWidget		*label_details_size_download_value;
	GtkWidget		*label_details_updated_title;
	GtkWidget		*label_details_updated_value;
	GtkWidget		*label_details_channel_title;
	GtkWidget		*label_details_channel_value;
	GtkWidget		*label_failed;
	GtkWidget		*label_license_nonfree_details;
	GtkWidget		*label_licenses_intro;
	GtkWidget		*list_box_addons;
	GtkWidget		*box_reviews;
	GtkWidget		*box_details_screenshot_fallback;
	GtkWidget		*histogram;
	GtkWidget		*button_review;
	GtkWidget		*list_box_reviews;
	GtkWidget		*scrolledwindow_details;
    GtkWidget		*spinner_details;
	GtkWidget		*stack_details;
	GtkWidget		*progressbar_top;
	GtkWidget		*popover_license_free;
	GtkWidget		*popover_license_nonfree;
	GtkWidget		*popover_license_unknown;
	GtkWidget		*popover_content_rating;
	GtkWidget		*label_content_rating_title;
	GtkWidget		*label_content_rating_message;
	GtkWidget		*label_content_rating_none;
    GtkWidget       *box_similar;
    GtkWidget       *similar_heading;
    GtkWidget       *similar_more;

    GtkWidget       *stack;
};

G_DEFINE_TYPE (GsDetailsPage, gs_details_page, GS_TYPE_PAGE)

static void
gs_details_page_set_state (GsDetailsPage *self,
                           GsDetailsPageState state)
{
	/* spinner */
	switch (state) {
	case GS_DETAILS_PAGE_STATE_LOADING:
		gs_start_spinner (GTK_SPINNER (self->spinner_details));
		gtk_widget_show (self->spinner_details);
		break;
	case GS_DETAILS_PAGE_STATE_READY:
	case GS_DETAILS_PAGE_STATE_FAILED:
		gs_stop_spinner (GTK_SPINNER (self->spinner_details));
		gtk_widget_hide (self->spinner_details);
		break;
	default:
		g_assert_not_reached ();
	}

	/* stack */
	switch (state) {
	case GS_DETAILS_PAGE_STATE_LOADING:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_details), "spinner");
		break;
	case GS_DETAILS_PAGE_STATE_READY:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_details), "ready");
		break;
	case GS_DETAILS_PAGE_STATE_FAILED:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_details), "failed");
		break;
	default:
		g_assert_not_reached ();
	}
}

static gboolean
app_has_pending_action (GsApp *app)
{
	/* sanitize the pending state change by verifying we're in one of the
	 * expected states */
	if (gs_app_get_state (app) != AS_APP_STATE_AVAILABLE &&
	    gs_app_get_state (app) != AS_APP_STATE_UPDATABLE_LIVE &&
	    gs_app_get_state (app) != AS_APP_STATE_UPDATABLE &&
	    gs_app_get_state (app) != AS_APP_STATE_QUEUED_FOR_INSTALL)
		return FALSE;

	return (gs_app_get_pending_action (app) != GS_PLUGIN_ACTION_UNKNOWN) ||
	       (gs_app_get_state (app) == AS_APP_STATE_QUEUED_FOR_INSTALL);
}

static void
gs_details_page_switch_to (GsPage *page, gboolean scroll_up)
{
	GsDetailsPage *self = GS_DETAILS_PAGE (page);
	AsAppState state;
	GsPrice *price;
	g_autofree gchar *text = NULL;
	GtkAdjustment *adj;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_DETAILS) {
		g_warning ("Called switch_to(details) when in mode %s",
			   gs_shell_get_mode_string (self->shell));
		return;
	}

    /* not set, perhaps file-to-app */
	if (self->app == NULL)
		return;

	state = gs_app_get_state (self->app);

	/* install button */
	switch (state) {
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_UNKNOWN: // Add AS_APP_STATE_UNKNOWN from Gooroom
	case AS_APP_STATE_AVAILABLE_LOCAL:
		gtk_widget_set_visible (self->button_install, TRUE);
		gtk_style_context_add_class (gtk_widget_get_style_context (self->button_install), "suggested-action");
		/* TRANSLATORS: button text in the header when an application
		 * can be installed */
		gtk_button_set_label (GTK_BUTTON (self->button_install), _("_Install"));
		break;
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (self->button_install, FALSE);
		break;
	case AS_APP_STATE_PURCHASABLE:
		gtk_widget_set_visible (self->button_install, TRUE);
		gtk_style_context_add_class (gtk_widget_get_style_context (self->button_install), "suggested-action");
		price = gs_app_get_price (self->app);
		text = gs_price_to_string (price);
		gtk_button_set_label (GTK_BUTTON (self->button_install), text);
		break;
	case AS_APP_STATE_PURCHASING:
		gtk_widget_set_visible (self->button_install, FALSE);
		break;
	//case AS_APP_STATE_UNKNOWN: // Remove AS_APP_STATE_UNKONWN from Gooroom
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
	case AS_APP_STATE_UPDATABLE_LIVE:
		gtk_widget_set_visible (self->button_install, FALSE);
		break;
#if 0
	case AS_APP_STATE_UPDATABLE_LIVE:
		GtkStyleContext *sc;
		gtk_widget_set_visible (self->button_install, TRUE);
		sc = gtk_widget_get_style_context (self->button_install);
		if (gs_app_get_kind (self->app) == AS_APP_KIND_FIRMWARE) {
			/* TRANSLATORS: button text in the header when firmware
			 * can be live-installed */
			gtk_button_set_label (GTK_BUTTON (self->button_install), _("_Install"));
			gtk_style_context_add_class (sc, "suggested-action");
		} else {
			/* TRANSLATORS: button text in the header when an application
			 * can be live-updated */
			gtk_button_set_label (GTK_BUTTON (self->button_install), _("_Update"));
			gtk_style_context_remove_class (sc, "suggested-action");
		}
		break;
#endif
	case AS_APP_STATE_UNAVAILABLE:
		if (gs_app_get_url (self->app, AS_URL_KIND_MISSING) != NULL) {
			gtk_widget_set_visible (self->button_install, FALSE);
		} else {
			gtk_widget_set_visible (self->button_install, TRUE);
			/* TRANSLATORS: this is a button that allows the apps to
			 * be installed.
			 * The ellipsis indicates that further steps are required,
			 * e.g. enabling software repositories or the like */
			gtk_button_set_label (GTK_BUTTON (self->button_install), _("_Install…"));
		}
		break;
	default:
		g_warning ("App unexpectedly in state %s",
			   as_app_state_to_string (state));
		g_assert_not_reached ();
	}

	/* launch button */
	switch (gs_app_get_state (self->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		if (!gs_app_has_quirk (self->app, AS_APP_QUIRK_NOT_LAUNCHABLE) &&
		    !gs_app_has_category (self->app, "Group")) { //Add group category check from Gooroom
			gtk_widget_set_visible (self->button_details_launch, TRUE);
		} else {
			gtk_widget_set_visible (self->button_details_launch, FALSE);
		}
		break;
	default:
		gtk_widget_set_visible (self->button_details_launch, FALSE);
		break;
	}

	if (gs_app_get_kind (self->app) == AS_APP_KIND_SHELL_EXTENSION) {
		gtk_button_set_label (GTK_BUTTON (self->button_details_launch),
		                      /* TRANSLATORS: A label for a button to show the settings for
		                         the selected shell extension. */
		                      _("Extension Settings"));
	} else {
		gtk_button_set_label (GTK_BUTTON (self->button_details_launch),
		                      /* TRANSLATORS: A label for a button to execute the selected
		                         application. */
		                      _("_Launch"));
	}

	/* don't show the launch and shortcut buttons if the app doesn't have a desktop ID */
	if (gs_app_get_id (self->app) == NULL) {
		gtk_widget_set_visible (self->button_details_launch, FALSE);
	}

	if (app_has_pending_action (self->app)) {
		gtk_widget_set_visible (self->button_install, FALSE);
		gtk_widget_set_visible (self->button_details_launch, FALSE);
	}

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_details));
	gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));

	gs_grab_focus_when_mapped (self->scrolledwindow_details);
}

static void
gs_details_page_refresh_progress (GsDetailsPage *self)
{
	guint percentage;
	AsAppState state;

	/* cancel button */
	state = gs_app_get_state (self->app);
	switch (state) {
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (self->button_cancel, TRUE);
		/* If the app is installing, the user can only cancel it if
		 * 1) They haven't already, and
		 * 2) the plugin hasn't said that they can't, for example if a
		 *    package manager has already gone 'too far'
		 */
		gtk_widget_set_sensitive (self->button_cancel,
					  !g_cancellable_is_cancelled (self->app_cancellable) &&
					   gs_app_get_allow_cancel (self->app));
		break;
	default:
		gtk_widget_set_visible (self->button_cancel, FALSE);
		break;
	}
	if (app_has_pending_action (self->app)) {
		gtk_widget_set_visible (self->button_cancel, TRUE);
		gtk_widget_set_sensitive (self->button_cancel,
					  !g_cancellable_is_cancelled (self->app_cancellable) &&
					  gs_app_get_allow_cancel (self->app));
	}

	/* progress status label */
	switch (state) {
	case AS_APP_STATE_REMOVING:
		gtk_widget_set_visible (self->label_progress_status, TRUE);
		gtk_label_set_label (GTK_LABEL (self->label_progress_status),
				     _("Removing…"));
		break;
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (self->label_progress_status, TRUE);
		gtk_label_set_label (GTK_LABEL (self->label_progress_status),
				     _("Installing"));
		break;
	default:
		gtk_widget_set_visible (self->label_progress_status, FALSE);
		break;
	}
	if (app_has_pending_action (self->app)) {
		GsPluginAction action = gs_app_get_pending_action (self->app);
		gtk_widget_set_visible (self->label_progress_status, TRUE);
		switch (action) {
		case GS_PLUGIN_ACTION_INSTALL:
			gtk_label_set_label (GTK_LABEL (self->label_progress_status),
					     /* TRANSLATORS: This is a label on top of the app's progress
					      * bar to inform the user that the app should be installed soon */
					     _("Pending installation…"));
			break;
		case GS_PLUGIN_ACTION_UPDATE:
		case GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD:
			gtk_label_set_label (GTK_LABEL (self->label_progress_status),
					     /* TRANSLATORS: This is a label on top of the app's progress
					      * bar to inform the user that the app should be updated soon */
					     _("Pending update…"));
			break;
		default:
			gtk_widget_set_visible (self->label_progress_status, FALSE);
			break;
		}
	}

	/* percentage bar */
	switch (state) {
	case AS_APP_STATE_INSTALLING:
		percentage = gs_app_get_progress (self->app);
		if (percentage <= 100) {
			g_autofree gchar *str = g_strdup_printf ("%u%%", percentage);
			gtk_label_set_label (GTK_LABEL (self->label_progress_percentage), str);
			gtk_widget_set_visible (self->label_progress_percentage, TRUE);
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progressbar_top),
						       (gdouble) percentage / 100.f);
			gtk_widget_set_visible (self->progressbar_top, TRUE);
			break;
		}
		/* FALLTHROUGH */
	default:
		gtk_widget_set_visible (self->label_progress_percentage, FALSE);
		gtk_widget_set_visible (self->progressbar_top, FALSE);
		break;
	}
	if (app_has_pending_action (self->app)) {
		gtk_widget_set_visible (self->progressbar_top, TRUE);
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progressbar_top), 0);
	}
	
    /* progress box */
	switch (state) {
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (self->box_progress, TRUE);
		break;
	default:
		gtk_widget_set_visible (self->box_progress, FALSE);
		break;
	}
	if (app_has_pending_action (self->app))
		gtk_widget_set_visible (self->box_progress, TRUE);
}

static gboolean
gs_details_page_refresh_progress_idle (gpointer user_data)
{
	GsDetailsPage *self = GS_DETAILS_PAGE (user_data);
	gs_details_page_refresh_progress (self);
	g_object_unref (self);
	return G_SOURCE_REMOVE;
}

static void
gs_details_page_progress_changed_cb (GsApp *app,
                                     GParamSpec *pspec,
                                     GsDetailsPage *self)
{
	g_idle_add (gs_details_page_refresh_progress_idle, g_object_ref (self));
}

static gboolean
gs_details_page_allow_cancel_changed_idle (gpointer user_data)
{
	GsDetailsPage *self = GS_DETAILS_PAGE (user_data);
	gtk_widget_set_sensitive (self->button_cancel,
				  gs_app_get_allow_cancel (self->app));
	g_object_unref (self);
	return G_SOURCE_REMOVE;
}

static void
gs_details_page_allow_cancel_changed_cb (GsApp *app,
                                                    GParamSpec *pspec,
                                                    GsDetailsPage *self)
{
	g_idle_add (gs_details_page_allow_cancel_changed_idle,
		    g_object_ref (self));
}

static gboolean
gs_details_page_switch_to_idle (gpointer user_data)
{
	GsDetailsPage *self = GS_DETAILS_PAGE (user_data);

	if (gs_shell_get_mode (self->shell) == GS_SHELL_MODE_DETAILS)
		gs_page_switch_to (GS_PAGE (self), TRUE);

	/* update widgets */
	gs_details_page_refresh_all (self);

	g_object_unref (self);
	return G_SOURCE_REMOVE;
}

static void
gs_details_page_notify_state_changed_cb (GsApp *app,
                                         GParamSpec *pspec,
                                         GsDetailsPage *self)
{
	g_idle_add (gs_details_page_switch_to_idle, g_object_ref (self));
}

static void
gs_details_page_refresh_screenshots (GsDetailsPage *self)
{
    guint i;
	GPtrArray *screenshots;
	AsScreenshot *ss;
	GtkWidget *ssimg;

	/* fallback warning */
	screenshots = gs_app_get_screenshots (self->app);
	switch (gs_app_get_kind (self->app)) {
	case AS_APP_KIND_GENERIC:
	case AS_APP_KIND_CODEC:
	case AS_APP_KIND_ADDON:
	case AS_APP_KIND_SOURCE:
	case AS_APP_KIND_FIRMWARE:
	case AS_APP_KIND_DRIVER:
	case AS_APP_KIND_INPUT_METHOD:
	case AS_APP_KIND_LOCALIZATION:
	case AS_APP_KIND_RUNTIME:
		gtk_widget_set_visible (self->box_details_screenshot_fallback, FALSE);
		break;
	default:
		gtk_widget_set_visible (self->box_details_screenshot_fallback,
					FALSE);
		break;
	}

    /* set screenshots */
    gtk_widget_set_visible (self->stack, TRUE);
    if (screenshots->len == 0) {
        gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "screenshot_empty");
		return;
	}
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "screenshot");
    gs_container_remove_all (GTK_CONTAINER (self->box_details_screenshot_thumbnails));
	/* set all the thumbnails */
	for (i = 0; i < screenshots->len; i++) {
		ss = g_ptr_array_index (screenshots, i);
		ssimg = gs_screenshot_image_new (self->session);
		gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg), ss);
		gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg),
					      300,
					      187.5);
		gs_screenshot_image_load_async (GS_SCREENSHOT_IMAGE (ssimg), NULL);
	    gtk_box_pack_start (GTK_BOX (self->box_details_screenshot_thumbnails), ssimg, FALSE, FALSE, 0);
		gtk_widget_set_visible (ssimg, TRUE);
	}
}

static void
gs_details_page_set_description (GsDetailsPage *self, const gchar *tmp)
{
	GtkStyleContext *style_context;
	GtkWidget *para;
	g_auto(GStrv) split = NULL;

	/* does the description exist? */
	gtk_widget_set_visible (self->box_details_description, tmp != NULL);
	if (tmp == NULL)
		return;

	/* add each paragraph as a new GtkLabel which lets us get the 24px
	 * paragraph spacing */
	gs_container_remove_all (GTK_CONTAINER (self->box_details_description));

#ifdef USE_GOOROOM
	para = gtk_label_new (tmp);
	gtk_label_set_line_wrap (GTK_LABEL (para), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (para), 40);
	gtk_label_set_selectable (GTK_LABEL (para), FALSE);
	gtk_widget_set_visible (para, TRUE);
	gtk_widget_set_can_focus (para, FALSE);
	g_object_set (para,
		      "xalign", 0.0,
		      NULL);

	/* add style class for theming */
	style_context = gtk_widget_get_style_context (para);
	gtk_style_context_add_class (style_context,
				     "application-details-description");

	gtk_box_pack_start (GTK_BOX (self->box_details_description), para, FALSE, FALSE, 0);
#else
	guint i;
	split = g_strsplit (tmp, "\n\n", -1);
	for (i = 0; split[i] != NULL; i++) {
		para = gtk_label_new (split[i]);
		gtk_label_set_line_wrap (GTK_LABEL (para), TRUE);
		gtk_label_set_max_width_chars (GTK_LABEL (para), 40);
		gtk_label_set_selectable (GTK_LABEL (para), TRUE);
		gtk_widget_set_visible (para, TRUE);
		gtk_widget_set_can_focus (para, FALSE);
		g_object_set (para,
			      "xalign", 0.0,
			      NULL);

		/* add style class for theming */
		style_context = gtk_widget_get_style_context (para);
		gtk_style_context_add_class (style_context,
					     "application-details-description");

		gtk_box_pack_start (GTK_BOX (self->box_details_description), para, FALSE, FALSE, 0);
	}
#endif
	/* show the webapp warning */
	if (gs_app_get_kind (self->app) == AS_APP_KIND_WEB_APP) {
		GtkWidget *label;
		/* TRANSLATORS: this is the warning box */
		label = gtk_label_new (_("This application can only be used when there is an active internet connection."));
		gtk_widget_set_visible (label, TRUE);
		gtk_label_set_xalign (GTK_LABEL (label), 0.f);
		gtk_label_set_selectable (GTK_LABEL (label), FALSE);
		gtk_style_context_add_class (gtk_widget_get_style_context (label),
					     "application-details-webapp-warning");
		gtk_box_pack_start (GTK_BOX (self->box_details_description),
				    label, FALSE, FALSE, 0);
	}
}
#if 0
static void
gs_details_page_set_sensitive (GtkWidget *widget, gboolean is_active)
{
}
#endif
static gboolean
gs_details_page_history_cb (GtkLabel *label,
                            gchar *uri,
                            GsDetailsPage *self)
{
	GtkWidget *dialog;

	dialog = gs_history_dialog_new ();
	gs_history_dialog_set_app (GS_HISTORY_DIALOG (dialog), self->app);
	gs_shell_modal_dialog_present (self->shell, GTK_DIALOG (dialog));

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), dialog);

	return TRUE;
}

static void
gs_details_page_refresh_size (GsDetailsPage *self)
{
#if 0
	/* set the installed size */
	if (gs_app_get_size_installed (self->app) != GS_APP_SIZE_UNKNOWABLE &&
	    gs_app_get_size_installed (self->app) != 0) {
		g_autofree gchar *size = NULL;
		size = g_format_size (gs_app_get_size_installed (self->app));
		gtk_label_set_label (GTK_LABEL (self->label_details_size_installed_value), size);
		gtk_widget_show (self->label_details_size_installed_title);
		gtk_widget_show (self->label_details_size_installed_value);
	} else {
		gtk_widget_hide (self->label_details_size_installed_title);
		gtk_widget_hide (self->label_details_size_installed_value);
	}

	/* set the download size */
	if (!gs_app_is_installed (self->app) &&
	    gs_app_get_size_download (self->app) != GS_APP_SIZE_UNKNOWABLE) {
		g_autofree gchar *size = NULL;
		size = g_format_size (gs_app_get_size_download (self->app));
		gtk_label_set_label (GTK_LABEL (self->label_details_size_download_value), size);
		gtk_widget_show (self->label_details_size_download_title);
		gtk_widget_show (self->label_details_size_download_value);
	} else {
		gtk_widget_hide (self->label_details_size_download_title);
		gtk_widget_hide (self->label_details_size_download_value);
	}
#endif
	if (!gs_app_is_installed (self->app) &&
		gs_app_get_size_download (self->app) != GS_APP_SIZE_UNKNOWABLE) {
		g_autofree gchar *size = NULL;
		size = g_format_size (gs_app_get_size_download (self->app));
		gtk_label_set_label (GTK_LABEL (self->label_details_size_download_value), size);
		gtk_label_set_label (GTK_LABEL (self->label_details_size_download_title), _("Download Size"));
	}
	else {
		if (gs_app_get_size_installed (self->app) != GS_APP_SIZE_UNKNOWABLE &&
			gs_app_get_size_installed (self->app) != 0) {
			g_autofree gchar *size = NULL;
			size = g_format_size (gs_app_get_size_installed (self->app));
			gtk_label_set_label (GTK_LABEL (self->label_details_size_download_value), size);
			gtk_label_set_label (GTK_LABEL (self->label_details_size_download_title), _("Installed Size"));
		}
		else {
			g_autofree gchar *size = NULL;
			size = g_format_size (gs_app_get_size_download (self->app));
			gtk_label_set_label (GTK_LABEL (self->label_details_size_download_value), size);
		}
	}
	gtk_widget_hide (self->label_details_size_installed_title);
	gtk_widget_hide (self->label_details_size_installed_value);
	gtk_widget_show (self->label_details_size_download_title);
	gtk_widget_show (self->label_details_size_download_value);
}

static void
gs_details_page_refresh_all (GsDetailsPage *self)
{
	GsAppList *history;
	GdkPixbuf *pixbuf = NULL;
	GList *addons;
	const gchar *tmp;
	gchar **menu_path;
	guint64 updated;
	GsChannel *channel;
	g_autoptr(GError) error = NULL;
    GPtrArray *categories;
    GPtrArray *provides;
    const gchar *category_name;
	gboolean addon_visible = FALSE;
	gboolean category_visible = FALSE;

	/* change widgets */
	tmp = gs_app_get_name (self->app);
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_label_set_label (GTK_LABEL (self->application_details_title), tmp);
		gtk_widget_set_visible (self->application_details_title, TRUE);
	} else {
		gtk_widget_set_visible (self->application_details_title, FALSE);
	}

	categories = gs_app_get_categories (self->app);
	if (categories && categories->len > 0)
	    category_visible = TRUE;

	if (category_visible) {
		menu_path = gs_app_get_menu_path (self->app);
		if (menu_path == NULL || menu_path[0] == NULL || menu_path[0][0] == '\0') {
			const gchar *category = g_ptr_array_index (categories, 0);
			const gchar *desktop_name = gs_utils_get_desktop_category_label (category);
			if (desktop_name == NULL && categories->len > 1) {
				category = g_ptr_array_index (categories, 1);
				desktop_name = gs_utils_get_desktop_category_label (category);
			}
			category_name = g_strdup (_(desktop_name));
			gtk_label_set_label (GTK_LABEL (self->application_details_category), category_name);
		} else {
			gtk_label_set_label (GTK_LABEL (self->application_details_category),menu_path[0]);
		}
	}
	gtk_widget_set_visible (self->application_details_category, category_visible);

    provides = gs_app_get_provides (self->app);
    if (provides && 0 < provides->len) {
        AsProvide *provide = g_ptr_array_index (provides, 0);
        tmp = g_strdup (as_provide_get_value (provide));
    }
    else {
        tmp = gs_app_get_developer_name (self->app);
    }

    if (tmp != NULL) {
	    gtk_label_set_label (GTK_LABEL (self->application_details_developer), tmp);
	    gtk_widget_set_visible (self->application_details_developer, TRUE);
	} else {
	    gtk_widget_set_visible (self->application_details_developer, FALSE);
	}

	/* set the description */
	tmp = gs_app_get_description (self->app);
	gs_details_page_set_description (self, tmp);

	/* set the icon */
	pixbuf = gs_app_get_pixbuf (self->app);
	if (pixbuf != NULL) {
		gs_image_set_from_pixbuf (GTK_IMAGE (self->application_details_icon), pixbuf);
		gtk_widget_set_visible (self->application_details_icon, TRUE);
	} else {
		gtk_widget_set_visible (self->application_details_icon, FALSE);
	}

	/* set the developer name, falling back to the project group */
	tmp = gs_app_get_developer_name (self->app);
	if (tmp == NULL)
		tmp = gs_app_get_project_group (self->app);
	if (tmp == NULL) {
		gtk_widget_set_visible (self->label_details_developer_title, TRUE);
		gtk_widget_set_visible (self->label_details_developer_value, FALSE);
	} else {
		gtk_widget_set_visible (self->label_details_developer_title, TRUE);
		gtk_label_set_label (GTK_LABEL (self->label_details_developer_value), tmp);
		gtk_widget_set_visible (self->label_details_developer_value, TRUE);
	}
	
    /* set the license buttons */
	tmp = gs_app_get_license (self->app);
	if (tmp == NULL) {
		gtk_widget_set_visible (self->button_details_license_free, FALSE);
		gtk_widget_set_visible (self->button_details_license_nonfree, FALSE);
		gtk_widget_set_visible (self->button_details_license_unknown, TRUE);
	} else if (gs_app_get_license_is_free (self->app)) {
		gtk_widget_set_visible (self->button_details_license_free, TRUE);
		gtk_widget_set_visible (self->button_details_license_nonfree, FALSE);
		gtk_widget_set_visible (self->button_details_license_unknown, FALSE);
	} else {
		gtk_widget_set_visible (self->button_details_license_free, FALSE);
		gtk_widget_set_visible (self->button_details_license_nonfree, TRUE);
		gtk_widget_set_visible (self->button_details_license_unknown, FALSE);
	}

    /* set channel */
	channel = gs_app_get_active_channel (self->app);
	gtk_widget_set_visible (self->label_details_channel_title, TRUE);
	if (channel != NULL) {
        gtk_label_set_label (GTK_LABEL (self->label_details_channel_value), gs_channel_get_name (channel));
	}
    else {
        gtk_label_set_label (GTK_LABEL (self->label_details_channel_value), "stable");
    }
	/* refresh size information */
	gs_details_page_refresh_size (self);

	/* set the updated date */
	updated = gs_app_get_install_date (self->app);
	if (updated == GS_APP_INSTALL_DATE_UNSET) {
		gtk_label_set_label (GTK_LABEL (self->label_details_updated_value), C_("updated", "Never"));
		gtk_widget_set_visible (self->label_details_updated_title, TRUE);
		gtk_widget_set_visible (self->label_details_updated_value, TRUE);
	} else if (updated == GS_APP_INSTALL_DATE_UNKNOWN) {
		/* TRANSLATORS: this is where the updated date is not known */
		gtk_label_set_label (GTK_LABEL (self->label_details_updated_value), C_("updated", "Never"));
		gtk_widget_set_visible (self->label_details_updated_title, TRUE);
		gtk_widget_set_visible (self->label_details_updated_value, TRUE);
	} else {
		g_autoptr(GDateTime) dt = NULL;
		g_autofree gchar *updated_str = NULL;
		dt = g_date_time_new_from_unix_utc ((gint64) updated);
		updated_str = g_date_time_format (dt, "%x");

		history = gs_app_get_history (self->app);

		if (gs_app_list_length (history) == 0) {
			gtk_label_set_label (GTK_LABEL (self->label_details_updated_value), updated_str);
		} else {
			GString *url;

			url = g_string_new (NULL);
			g_string_printf (url, "<a href=\"show-history\">%s</a>", updated_str);
			gtk_label_set_markup (GTK_LABEL (self->label_details_updated_value), url->str);
			g_string_free (url, TRUE);
		}
		gtk_widget_set_visible (self->label_details_updated_title, TRUE);
		gtk_widget_set_visible (self->label_details_updated_value, TRUE);
	}
	
    /* set the category */
	if (category_visible) {
		menu_path = gs_app_get_menu_path (self->app);
		if (menu_path == NULL || menu_path[0] == NULL || menu_path[0][0] == '\0') {
			gtk_label_set_label (GTK_LABEL (self->label_details_category_value), category_name);
		} else {
			g_autofree gchar *path = NULL;
			if (gtk_widget_get_direction (self->label_details_category_value) == GTK_TEXT_DIR_RTL)
				path = g_strjoinv (" ← ", menu_path);
			else
				path = g_strjoinv (" → ", menu_path);
			gtk_label_set_label (GTK_LABEL (self->label_details_category_value), path);
		}
	}
	gtk_widget_set_visible (self->label_details_category_title, category_visible);
	gtk_widget_set_visible (self->label_details_category_value, category_visible);
 
    /* set the app size */
	#if 0
	if (gs_app_get_size_installed (self->app) != GS_APP_SIZE_UNKNOWABLE &&
	    gs_app_get_size_installed (self->app) != 0) {
		gtk_widget_show (self->label_details_size_installed_title);
		gtk_widget_hide (self->label_details_size_download_title);
	} else {
		gtk_widget_hide (self->label_details_size_installed_title);
		gtk_widget_show (self->label_details_size_download_title);
	}
	#endif
    gtk_widget_hide (self->label_details_size_installed_title);
    gtk_widget_show (self->label_details_size_download_title);

	/* are we trying to replace something in the baseos */
	gtk_widget_set_visible (self->infobar_details_package_baseos,
				gs_app_has_quirk (self->app, AS_APP_QUIRK_COMPULSORY) &&
				gs_app_get_state (self->app) == AS_APP_STATE_AVAILABLE_LOCAL);

	switch (gs_app_get_kind (self->app)) {
	case AS_APP_KIND_DESKTOP:
		/* installing an app with a repo file */
		gtk_widget_set_visible (self->infobar_details_app_repo,
					gs_app_has_quirk (self->app,
							  AS_APP_QUIRK_HAS_SOURCE) &&
					gs_app_get_state (self->app) == AS_APP_STATE_AVAILABLE_LOCAL);
		gtk_widget_set_visible (self->infobar_details_repo, FALSE);
		break;
	case AS_APP_KIND_GENERIC:
		/* installing a repo-release package */
		gtk_widget_set_visible (self->infobar_details_app_repo, FALSE);
		gtk_widget_set_visible (self->infobar_details_repo,
					gs_app_has_quirk (self->app,
							  AS_APP_QUIRK_HAS_SOURCE) &&
					gs_app_get_state (self->app) == AS_APP_STATE_AVAILABLE_LOCAL);
		break;
	default:
		gtk_widget_set_visible (self->infobar_details_app_repo, FALSE);
		gtk_widget_set_visible (self->infobar_details_repo, FALSE);
		break;
	}

	/* installing a app without a repo file */
	switch (gs_app_get_kind (self->app)) {
	case AS_APP_KIND_DESKTOP:
		if (gs_app_get_kind (self->app) == AS_APP_KIND_FIRMWARE) {
			gtk_widget_set_visible (self->infobar_details_app_norepo, FALSE);
		} else {
			gtk_widget_set_visible (self->infobar_details_app_norepo,
						!gs_app_has_quirk (self->app,
							  AS_APP_QUIRK_HAS_SOURCE) &&
						gs_app_get_state (self->app) == AS_APP_STATE_AVAILABLE_LOCAL);
		}
		break;
	default:
		gtk_widget_set_visible (self->infobar_details_app_norepo, FALSE);
		break;
	}

	/* only show the "select addons" string if the app isn't yet installed */
	switch (gs_app_get_state (self->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
	    addon_visible = TRUE;
		gtk_widget_set_visible (self->label_addons_uninstalled_app, FALSE);
		break;
	default:
		gtk_widget_set_visible (self->label_addons_uninstalled_app, TRUE);
		break;
	}

	/* update progress */
	gs_details_page_refresh_progress (self);

	if (addon_visible) {
		addons = gtk_container_get_children (GTK_CONTAINER (self->list_box_addons));
		gtk_widget_set_visible (self->box_addons, addons != NULL);
		g_list_free (addons);
	}
}

static void
list_header_func (GtkListBoxRow *row,
		  GtkListBoxRow *before,
		  gpointer user_data)
{
	GtkWidget *header = NULL;
	if (before != NULL)
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

static gint
list_sort_func (GtkListBoxRow *a,
		GtkListBoxRow *b,
		gpointer user_data)
{
	GsApp *a1 = gs_app_addon_row_get_addon (GS_APP_ADDON_ROW (a));
	GsApp *a2 = gs_app_addon_row_get_addon (GS_APP_ADDON_ROW (b));

	return g_strcmp0 (gs_app_get_name (a1),
			  gs_app_get_name (a2));
}

static void gs_details_page_addon_selected_cb (GsAppAddonRow *row, GParamSpec *pspec, GsDetailsPage *self);

static void
gs_details_page_refresh_addons (GsDetailsPage *self)
{
	guint i;
	GsAppList *addons;

    switch (gs_app_get_state (self->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
	    gtk_widget_set_visible (self->box_addons, TRUE);
	    break;
	default:
	    gtk_widget_set_visible (self->box_addons, FALSE);
		return;
	}

	gs_container_remove_all (GTK_CONTAINER (self->list_box_addons));

	addons = gs_app_get_addons (self->app);
	for (i = 0; i < gs_app_list_length (addons); i++) {
		GsApp *addon;
		GtkWidget *row;

		addon = gs_app_list_index (addons, i);
		if (gs_app_get_state (addon) == AS_APP_STATE_UNAVAILABLE)
			continue;

		row = gs_app_addon_row_new (addon);

		gtk_container_add (GTK_CONTAINER (self->list_box_addons), row);
		gtk_widget_show (row);

		g_signal_connect (row, "notify::selected",
				  G_CALLBACK (gs_details_page_addon_selected_cb),
				  self);
	}
}

static void
app_tile_clicked (GsAppTile *tile, gpointer user_data)
{
    GsDetailsPage *self = GS_DETAILS_PAGE (user_data);
    GsApp *app;
    app = gs_app_tile_get_app (tile);

    gs_shell_show_app (self->shell, app);
}

static void
gs_details_page_refresh_similar_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer user_data)
{
    guint cnt;
    GsApp *app;
    GtkWidget *tile;
    GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
    GsDetailsPage *self = GS_DETAILS_PAGE (user_data);
    //GPtrArray *similar_categories = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GsAppList) list = NULL;
    gs_container_remove_all (GTK_CONTAINER (self->box_similar));

    list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);

    gtk_widget_set_visible (self->box_similar, FALSE);
    gtk_widget_set_visible (self->similar_more, FALSE);
    gtk_widget_set_visible (self->similar_heading, FALSE);

    if (list == NULL)
        return;

    if (gs_app_list_length (list) == 0)
        return;

    gs_app_list_randomize (list);

    cnt = 0;
    for (guint i = 0; i < gs_app_list_length (list); i++) {
        app = gs_app_list_index (list, i);
        if (app == self->app)
           continue;
        if (gs_app_get_id (app) == NULL)
            continue;
        if (N_TILES <= cnt)
            break;

        cnt++;
        tile = gs_popular_tile_new (app);
        if (!gs_app_has_quirk (app, AS_APP_QUIRK_PROVENANCE) ||
             gs_utils_list_has_app_fuzzy (list, app))
             gs_popular_tile_show_source (GS_POPULAR_TILE (tile), TRUE);
        g_signal_connect (tile, "clicked",
              G_CALLBACK (app_tile_clicked), self);
        gtk_container_add (GTK_CONTAINER (self->box_similar), tile);
        gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
    }

    if (0 < cnt) {
        gtk_widget_set_visible (self->box_similar, TRUE);
        gtk_widget_set_visible (self->similar_heading, TRUE);
    }

    if (N_TILES  < gs_app_list_length (list)) {
        gtk_widget_set_visible (self->similar_more, TRUE);
    }
}

static void
gs_details_page_similar_more_cb (GtkWidget *button, GsDetailsPage *self)
{
    GsCategory *category;
    category = GS_CATEGORY (g_object_get_data (G_OBJECT (button),"details-category"));
	gs_shell_show_category (self->shell, gs_category_get_parent (category));
}

static void
gs_details_page_app_refine2_cb (GObject *source,
				GAsyncResult *res,
				gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsDetailsPage *self = GS_DETAILS_PAGE (user_data);
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		g_warning ("failed to refine %s: %s",
			   gs_app_get_id (self->app),
			   error->message);
		return;
	}
	gs_details_page_refresh_size (self);
}

static void
gs_details_page_app_refine2 (GsDetailsPage *self)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* if these tasks fail (e.g. because we have no networking) then it's
	 * of no huge importance if we don't get the required data */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", self->app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    gs_details_page_app_refine2_cb,
					    self);
}

static gboolean
_max_results_sort_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
    return gs_app_get_rating (app1) < gs_app_get_rating (app2);
}

static void
gs_details_page_refresh_similar (GsDetailsPage *self)
{
	GtkWidget *tile;
	GPtrArray *categories;
	GsCategory *category = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	categories = gs_app_get_categories (self->app);

	if (categories && 0 == categories->len)
		return;

	category = gs_shell_get_category (self->shell, categories);
	if (category == NULL)
		return;

	gs_container_remove_all (GTK_CONTAINER (self->box_similar));

	for (guint i = 0; i < N_TILES; i++) {
		tile = gs_popular_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (self->box_similar), tile);
	}

	g_object_set_data (G_OBJECT (self->similar_more), "details-category", category);
	g_signal_connect_object (G_OBJECT (self->similar_more),"clicked",
				G_CALLBACK (gs_details_page_similar_more_cb), self, 0);

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORY_APPS,
					"category", category,
					"filter-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
					"refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
					GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
					GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME |
					GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE,
					NULL);
	gs_plugin_job_set_sort_func (plugin_job, _max_results_sort_cb);
	gs_plugin_loader_job_process_async (self->plugin_loader,
					plugin_job,
					self->cancellable,
					gs_details_page_refresh_similar_cb,
					self);
}

static void
gs_details_page_app_refine_cb (GObject *source,
                               GAsyncResult *res,
                               gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsDetailsPage *self = GS_DETAILS_PAGE (user_data);
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *app_dump = NULL;

	ret = gs_plugin_loader_job_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to refine %s: %s",
			   gs_app_get_id (self->app),
			   error->message);
	}
#if 0
	/* Gooroom
	* Activate the UNKNOWN app in the Gooroom */
	if (gs_app_get_kind (self->app) == AS_APP_KIND_UNKNOWN ||
	    gs_app_get_state (self->app) == AS_APP_STATE_UNKNOWN) {
		g_autofree gchar *str = NULL;
		const gchar *id;

		id = gs_app_get_id (self->app);
		str = g_strdup_printf (_("Unable to find “%s”"), id == NULL ? gs_app_get_source_default (self->app) : id);
		gtk_label_set_text (GTK_LABEL (self->label_failed), str);
		gs_details_page_set_state (self, GS_DETAILS_PAGE_STATE_FAILED);
		return;
	}
#endif
	/* show some debugging */
	app_dump = gs_app_to_string (self->app);
	g_debug ("%s", app_dump);

	gs_details_page_refresh_screenshots (self);
	gs_details_page_refresh_addons (self);
	gs_details_page_refresh_all (self);
    gs_details_page_refresh_similar (self);
	gs_details_page_set_state (self, GS_DETAILS_PAGE_STATE_READY);

	/* do 2nd stage refine */
	gs_details_page_app_refine2 (self);
}

static void
set_app (GsDetailsPage *self, GsApp *app)
{
	g_autofree gchar *tmp = NULL;

	/* do not show all the reviews by default */
	self->show_all_reviews = FALSE;

	/* disconnect the old handlers */
	if (self->app != NULL) {
		g_signal_handlers_disconnect_by_func (self->app, gs_details_page_notify_state_changed_cb, self);
		g_signal_handlers_disconnect_by_func (self->app, gs_details_page_progress_changed_cb, self);
		g_signal_handlers_disconnect_by_func (self->app, gs_details_page_allow_cancel_changed_cb,
						      self);

		g_signal_handlers_disconnect_by_func (self->similar_more, gs_details_page_similar_more_cb, self);
	}

	/* save app */
	g_set_object (&self->app, app);
	if (self->app == NULL) {
		/* switch away from the details view that failed to load */
		gs_shell_set_mode (self->shell, GS_SHELL_MODE_OVERVIEW);
		return;
	}

	g_signal_connect_object (self->app, "notify::state",
				 G_CALLBACK (gs_details_page_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::size",
				 G_CALLBACK (gs_details_page_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::license",
				 G_CALLBACK (gs_details_page_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::progress",
				 G_CALLBACK (gs_details_page_progress_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::allow-cancel",
				 G_CALLBACK (gs_details_page_allow_cancel_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::pending-action",
				 G_CALLBACK (gs_details_page_notify_state_changed_cb),
				 self, 0);

	/* print what we've got */
	tmp = gs_app_to_string (self->app);
	g_debug ("%s", tmp);

	/* change widgets */
	gs_page_switch_to (GS_PAGE (self), TRUE);
	gs_details_page_refresh_screenshots (self);
	gs_details_page_refresh_addons (self);
	gs_details_page_refresh_all (self);
	gs_details_page_set_state (self, GS_DETAILS_PAGE_STATE_READY);

	g_set_object (&self->app_cancellable, gs_app_get_cancellable (app));

	/* do 2nd stage refine */
	gs_details_page_app_refine2 (self);
}

static void
gs_details_page_file_to_app_cb (GObject *source,
                                GAsyncResult *res,
                                gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsDetailsPage *self = GS_DETAILS_PAGE (user_data);
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) error = NULL;

	list = gs_plugin_loader_job_process_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		g_warning ("failed to convert file to GsApp: %s", error->message);
		/* go back to the overview */
		gs_shell_change_mode (self->shell, GS_SHELL_MODE_OVERVIEW, NULL, FALSE);
	} else {
		GsApp *app = gs_app_list_index (list, 0);
		set_app (self, app);
	}
}

static void
gs_details_page_url_to_app_cb (GObject *source,
                               GAsyncResult *res,
                               gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsDetailsPage *self = GS_DETAILS_PAGE (user_data);
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) error = NULL;

	list = gs_plugin_loader_job_process_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		g_warning ("failed to convert URL to GsApp: %s", error->message);
		/* go back to the overview */
		gs_shell_change_mode (self->shell, GS_SHELL_MODE_OVERVIEW, NULL, FALSE);
	} else {
		GsApp *app = gs_app_list_index (list, 0);
		set_app (self, app);
	}
}

void
gs_details_page_set_local_file (GsDetailsPage *self, GFile *file)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;
	gs_details_page_set_state (self, GS_DETAILS_PAGE_STATE_LOADING);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_CHANNELS,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    gs_details_page_file_to_app_cb,
					    self);
}

void
gs_details_page_set_url (GsDetailsPage *self, const gchar *url)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;
	gs_details_page_set_state (self, GS_DETAILS_PAGE_STATE_LOADING);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_URL_TO_APP,
					 "search", url,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_CHANNELS |
							 GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    gs_details_page_url_to_app_cb,
					    self);
}

static void
gs_details_page_load (GsDetailsPage *self)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", self->app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_CHANNELS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    gs_details_page_app_refine_cb,
					    self);
}

static void
gs_details_page_reload (GsPage *page)
{
	GsDetailsPage *self = GS_DETAILS_PAGE (page);
	if (self->app != NULL)
		gs_details_page_load (self);
}

static void
settings_changed_cb (GsDetailsPage *self,
		     const gchar *key,
		     gpointer data)
{
	if (g_strcmp0 (key, "show-nonfree-ui") == 0) {
		gs_details_page_refresh_all (self);
	}
}

void
gs_details_page_set_app (GsDetailsPage *self, GsApp *app)
{
	g_return_if_fail (GS_IS_DETAILS_PAGE (self));
	g_return_if_fail (GS_IS_APP (app));

	/* get extra details about the app */
	gs_details_page_set_state (self, GS_DETAILS_PAGE_STATE_LOADING);

	/* disconnect the old handlers */
	if (self->app != NULL) {
		g_signal_handlers_disconnect_by_func (self->app, gs_details_page_notify_state_changed_cb, self);
		g_signal_handlers_disconnect_by_func (self->app, gs_details_page_progress_changed_cb, self);
		g_signal_handlers_disconnect_by_func (self->settings,
						      settings_changed_cb,
						      self);
		g_signal_handlers_disconnect_by_func (self->app, gs_details_page_allow_cancel_changed_cb,
						      self);

		g_signal_handlers_disconnect_by_func (self->similar_more, gs_details_page_similar_more_cb, self);
	}
	/* save app */
	g_set_object (&self->app, app);

	g_signal_connect_object (self->app, "notify::state",
				 G_CALLBACK (gs_details_page_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::size",
				 G_CALLBACK (gs_details_page_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::license",
				 G_CALLBACK (gs_details_page_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::quirk",
				 G_CALLBACK (gs_details_page_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::progress",
				 G_CALLBACK (gs_details_page_progress_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::allow-cancel",
				 G_CALLBACK (gs_details_page_allow_cancel_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::pending-action",
				 G_CALLBACK (gs_details_page_notify_state_changed_cb),
				 self, 0);

	g_set_object (&self->app_cancellable, gs_app_get_cancellable (self->app));

	gs_details_page_load (self);

	/* change widgets */
	gs_details_page_refresh_all (self);

	g_signal_connect_swapped (self->settings, "changed",
				  G_CALLBACK (settings_changed_cb),
				  self);
}

GsApp *
gs_details_page_get_app (GsDetailsPage *self)
{
	return self->app;
}

static void
gs_details_page_remove_app (GsDetailsPage *self)
{
	g_set_object (&self->app_cancellable, gs_app_get_cancellable (self->app));
	gs_page_remove_app (GS_PAGE (self), self->app, self->app_cancellable);
}

static void
gs_details_page_app_cancel_button_cb (GtkWidget *widget, GsDetailsPage *self)
{
	g_cancellable_cancel (self->app_cancellable);
	gtk_widget_set_sensitive (widget, FALSE);

	/* reset the pending-action from the app if needed */
	gs_app_set_pending_action (self->app, GS_PLUGIN_ACTION_UNKNOWN);

	/* FIXME: We should be able to revert the QUEUED_FOR_INSTALL without
	 * having to pretend to remove the app */
	if (gs_app_get_state (self->app) == AS_APP_STATE_QUEUED_FOR_INSTALL)
		gs_details_page_remove_app (self);
}

typedef struct {
	GsDetailsPage	*self;
	GsChannel	*channel;
} GsDetailsPageChannelHelper;

static void
gs_details_page_channel_helper_free (GsDetailsPageChannelHelper *helper)
{
	g_object_unref (helper->self);
	g_object_unref (helper->channel);
	g_free (helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsDetailsPageChannelHelper, gs_details_page_channel_helper_free);

#if 0
static void
gs_page_channel_switch_refine_cb (GObject *source,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
	g_autoptr(GsDetailsPageChannelHelper) helper = (GsDetailsPageChannelHelper *) user_data;
	GsDetailsPage *self = helper->self;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = gs_plugin_loader_job_action_finish (plugin_loader,
						  res,
						  &error);
	if (g_error_matches (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_CANCELLED)) {
		g_debug ("%s", error->message);
		return;
	}
	if (!ret) {
		g_warning ("failed to refine %s: %s",
			   gs_app_get_id (self->app),
			   error->message);
		return;
	}

	gs_details_page_refresh_all (self);
}

static void
gs_page_channel_switched_cb (GObject *source,
                             GAsyncResult *res,
                             gpointer user_data)
{
	g_autoptr(GsDetailsPageChannelHelper) helper = (GsDetailsPageChannelHelper *) user_data;
	GsDetailsPage *self = helper->self;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	gboolean ret;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GError) error = NULL;

	ret = gs_plugin_loader_job_action_finish (plugin_loader,
						  res,
						  &error);
	if (g_error_matches (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_CANCELLED)) {
		g_debug ("%s", error->message);
		return;
	}
	if (!ret) {
		g_warning ("failed to switch channel %s: %s",
		           gs_app_get_id (self->app),
		           error->message);
		return;
	}

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", self->app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->app_cancellable,
					    gs_page_channel_switch_refine_cb,
					    g_steal_pointer (&helper));
}
#endif
static void
gs_details_page_app_install_button_cb (GtkWidget *widget, GsDetailsPage *self)
{
#if 0
	g_autoptr(GList) addons = NULL;

	/* Mark ticked addons to be installed together with the app */
	addons = gtk_container_get_children (GTK_CONTAINER (self->list_box_addons));
	for (GList *l = addons; l; l = l->next) {
		if (gs_app_addon_row_get_selected (l->data)) {
			GsApp *addon = gs_app_addon_row_get_addon (l->data);

			if (gs_app_get_state (addon) == AS_APP_STATE_AVAILABLE)
				gs_app_set_to_be_installed (addon, TRUE);
		}
	}
#endif
	g_set_object (&self->app_cancellable, gs_app_get_cancellable (self->app));

	if (gs_app_get_state (self->app) == AS_APP_STATE_UPDATABLE_LIVE) {
		gs_page_update_app (GS_PAGE (self), self->app, self->app_cancellable);
		return;
	}

	if (gs_plugin_loader_get_network_available (self->plugin_loader)) {
		gs_page_install_app (GS_PAGE (self), self->app, GS_SHELL_INTERACTION_FULL,
			     	self->app_cancellable);
	}
	else {
		gs_shell_notify (self->shell, _("Unable to install: internet access was required but wasn’t available"), GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS);
	}
}

static void
gs_details_page_addon_selected_cb (GsAppAddonRow *row,
                                   GParamSpec *pspec,
                                   GsDetailsPage *self)
{
	GsApp *addon;

	addon = gs_app_addon_row_get_addon (row);

	/* If the main app is already installed, ticking the addon checkbox
	 * triggers an immediate install. Otherwise we'll install the addon
	 * together with the main app. */
	switch (gs_app_get_state (self->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		if (gs_app_addon_row_get_selected (row)) {
			g_set_object (&self->app_cancellable, gs_app_get_cancellable (addon));
			gs_page_install_app (GS_PAGE (self), addon, GS_SHELL_INTERACTION_FULL,
					     self->app_cancellable);
		} else {
			g_set_object (&self->app_cancellable, gs_app_get_cancellable (addon));
			gs_page_remove_app (GS_PAGE (self), addon, self->app_cancellable);
			/* make sure the addon checkboxes are synced if the
			 * user clicks cancel in the remove confirmation dialog */
			gs_details_page_refresh_addons (self);
			gs_details_page_refresh_all (self);
		}
		break;
	default:
		break;
	}
}

static void
gs_details_page_app_launch_button_cb (GtkWidget *widget, GsDetailsPage *self)
{
	g_autoptr(GCancellable) cancellable = g_cancellable_new ();

	/* hide the notification */
	g_application_withdraw_notification (g_application_get_default (),
					     "installed");

	g_set_object (&self->cancellable, cancellable);
	gs_page_launch_app (GS_PAGE (self), self->app, self->cancellable);
}

static void
gs_page_reload_installed_cb (GObject *source,
							 GAsyncResult *res,
							 gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	g_autoptr(GError) error = NULL;
	gs_plugin_loader_job_action_finish (plugin_loader,
										res,
										&error);
	if (g_error_matches (error,
			GS_PLUGIN_ERROR,
			GS_PLUGIN_ERROR_CANCELLED)) {
		g_debug ("%s", error->message);
		return;
	}

	gs_details_page_reload (user_data);
	return;
}

static void
gs_details_page_app_installed (GsPage *page, GsApp *app)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;
	GsDetailsPage *self = GS_DETAILS_PAGE (page);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_RELOAD_INSTALLED, NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
								self->cancellable,
								gs_page_reload_installed_cb,
								self);
}

static void
gs_details_page_app_removed (GsPage *page, GsApp *app)
{
	gs_details_page_reload (page);
}

static void
show_all_cb (GtkWidget *widget, gpointer user_data)
{
	gtk_widget_show (widget);
}

static void
gs_details_page_more_reviews_button_cb (GtkWidget *widget, GsDetailsPage *self)
{
	self->show_all_reviews = TRUE;
	gtk_container_foreach (GTK_CONTAINER (self->list_box_reviews),
	                       show_all_cb, NULL);
	gtk_widget_set_visible (self->button_more_reviews, FALSE);
}

static gboolean
gs_details_page_activate_link_cb (GtkLabel *label,
                                  const gchar *uri,
                                  GsDetailsPage *self)
{
	gs_shell_show_uri (self->shell, uri);
	return TRUE;
}

static GtkWidget *
gs_details_page_label_widget (GsDetailsPage *self,
                              const gchar *title,
                              const gchar *url)
{
	GtkWidget *w;
	g_autofree gchar *markup = NULL;

	markup = g_strdup_printf ("<a href=\"%s\">%s</a>", url, title);
	w = gtk_label_new (markup);
	g_signal_connect (w, "activate-link",
			  G_CALLBACK (gs_details_page_activate_link_cb),
			  self);
	gtk_label_set_use_markup (GTK_LABEL (w), TRUE);
	gtk_label_set_xalign (GTK_LABEL (w), 0.f);
	gtk_label_set_selectable (GTK_LABEL (w), FALSE);
	gtk_widget_set_visible (w, TRUE);
	return w;
}

static GtkWidget *
gs_details_page_license_widget_for_token (GsDetailsPage *self, const gchar *token)
{
	/* public domain */
	if (g_strcmp0 (token, "@LicenseRef-public-domain") == 0) {
		/* TRANSLATORS: see the wikipedia page */
		return gs_details_page_label_widget (self, _("Public domain"),
			/* TRANSLATORS: Replace the link with a version in your language,
			 * e.g. https://de.wikipedia.org/wiki/Gemeinfreiheit */
			_("https://en.wikipedia.org/wiki/Public_domain"));
	}

	/* free software, license unspecified */
	if (g_str_has_prefix (token, "@LicenseRef-free")) {
		/* TRANSLATORS: Replace the link with a version in your language,
		 * e.g. https://www.gnu.org/philosophy/free-sw.de */
		const gchar *url = _("https://www.gnu.org/philosophy/free-sw");
		gchar *tmp;

		/* we support putting a custom URL in the
		 * token string, e.g. @LicenseRef-free=http://ubuntu.com */
		tmp = g_strstr_len (token, -1, "=");
		if (tmp != NULL)
			url = tmp + 1;

		/* TRANSLATORS: see GNU page */
		return gs_details_page_label_widget (self, _("Free Software"), url);
	}

	/* SPDX value */
	if (g_str_has_prefix (token, "@")) {
		g_autofree gchar *uri = NULL;
		uri = g_strdup_printf ("http://spdx.org/licenses/%s",
				       token + 1);
		return gs_details_page_label_widget (self, token + 1, uri);
	}

	/* new SPDX value the extractor didn't know about */
	if (as_utils_is_spdx_license_id (token)) {
		g_autofree gchar *uri = NULL;
		uri = g_strdup_printf ("http://spdx.org/licenses/%s",
				       token);
		return gs_details_page_label_widget (self, token, uri);
	}

	/* nothing to show */
	return NULL;
}

static void
gs_details_page_license_free_cb (GtkWidget *widget, GsDetailsPage *self)
{
	guint cnt = 0;
	guint i;
	g_auto(GStrv) tokens = NULL;

	/* URLify any SPDX IDs */
	gs_container_remove_all (GTK_CONTAINER (self->box_details_license_list));
	tokens = as_utils_spdx_license_tokenize (gs_app_get_license (self->app));
	for (i = 0; tokens[i] != NULL; i++) {
		GtkWidget *w = NULL;

		/* translated join */
		if (g_strcmp0 (tokens[i], "&") == 0)
			continue;
		if (g_strcmp0 (tokens[i], "|") == 0)
			continue;
		if (g_strcmp0 (tokens[i], "+") == 0)
			continue;

		/* add widget */
		w = gs_details_page_license_widget_for_token (self, tokens[i]);
		if (w == NULL)
			continue;
		gtk_container_add (GTK_CONTAINER (self->box_details_license_list), w);

		/* one more license */
		cnt++;
	}

	/* use the correct plural */
	gtk_label_set_label (GTK_LABEL (self->label_licenses_intro),
			     /* TRANSLATORS: for the free software popover */
			     ngettext ("Users are bound by the following license:",
				       "Users are bound by the following licenses:",
				       cnt));
	gtk_widget_set_visible (self->label_licenses_intro, cnt > 0);

	gtk_widget_show (self->popover_license_free);
}

static void
gs_details_page_license_nonfree_cb (GtkWidget *widget, GsDetailsPage *self)
{
	g_autofree gchar *str = NULL;
	g_autofree gchar *uri = NULL;
	g_auto(GStrv) tokens = NULL;

	/* license specified as a link */
	tokens = as_utils_spdx_license_tokenize (gs_app_get_license (self->app));
	for (guint i = 0; tokens[i] != NULL; i++) {
		if (g_str_has_prefix (tokens[i], "@LicenseRef-proprietary=")) {
			uri = g_strdup (tokens[i] + 24);
			break;
		}
	}
	if (uri == NULL)
		uri = g_settings_get_string (self->settings, "nonfree-software-uri");
	str = g_strdup_printf ("<a href=\"%s\">%s</a>",
			       uri,
			       _("More information"));
	gtk_label_set_label (GTK_LABEL (self->label_license_nonfree_details), str);
	gtk_widget_show (self->popover_license_nonfree);
}

static void
gs_details_page_license_unknown_cb (GtkWidget *widget, GsDetailsPage *self)
{
	gtk_widget_show (self->popover_license_unknown);
}

static gboolean
gs_details_page_setup (GsPage *page,
                       GsShell *shell,
                       GsPluginLoader *plugin_loader,
                       GtkBuilder *builder,
                       GCancellable *cancellable,
                       GError **error)
{
	GsDetailsPage *self = GS_DETAILS_PAGE (page);
	GtkAdjustment *adj;

	g_return_val_if_fail (GS_IS_DETAILS_PAGE (self), TRUE);

	self->shell = shell;

	self->plugin_loader = g_object_ref (plugin_loader);
	self->builder = g_object_ref (builder);
	self->cancellable = g_object_ref (cancellable);

	self->enable_reviews = FALSE;

	/* setup details */
	g_signal_connect (self->button_install, "clicked",
			  G_CALLBACK (gs_details_page_app_install_button_cb),
			  self);
	g_signal_connect (self->button_cancel, "clicked",
			  G_CALLBACK (gs_details_page_app_cancel_button_cb),
			  self);
	g_signal_connect (self->button_more_reviews, "clicked",
			  G_CALLBACK (gs_details_page_more_reviews_button_cb),
			  self);
	g_signal_connect (self->label_details_updated_value, "activate-link",
			  G_CALLBACK (gs_details_page_history_cb),
			  self);
	g_signal_connect (self->button_details_launch, "clicked",
			  G_CALLBACK (gs_details_page_app_launch_button_cb),
			  self);
	g_signal_connect (self->button_details_license_free, "clicked",
			  G_CALLBACK (gs_details_page_license_free_cb),
			  self);
	g_signal_connect (self->button_details_license_nonfree, "clicked",
			  G_CALLBACK (gs_details_page_license_nonfree_cb),
			  self);
	g_signal_connect (self->button_details_license_unknown, "clicked",
			  G_CALLBACK (gs_details_page_license_unknown_cb),
			  self);
	g_signal_connect (self->label_license_nonfree_details, "activate-link",
			  G_CALLBACK (gs_details_page_activate_link_cb),
			  self);

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_details));
	gtk_container_set_focus_vadjustment (GTK_CONTAINER (self->box_details), adj);

	return TRUE;
}

static void
gs_details_page_dispose (GObject *object)
{
	GsDetailsPage *self = GS_DETAILS_PAGE (object);
	if (self->app != NULL) {
		g_signal_handlers_disconnect_by_func (self->app, gs_details_page_notify_state_changed_cb, self);
		g_signal_handlers_disconnect_by_func (self->app, gs_details_page_progress_changed_cb, self);
		g_clear_object (&self->app);
	}
	g_clear_object (&self->builder);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->cancellable);
	g_clear_object (&self->app_cancellable);
	g_clear_object (&self->session);

	G_OBJECT_CLASS (gs_details_page_parent_class)->dispose (object);
}

static void
gs_details_page_class_init (GsDetailsPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_details_page_dispose;
	page_class->app_installed = gs_details_page_app_installed;
	page_class->app_removed = gs_details_page_app_removed;
	page_class->switch_to = gs_details_page_switch_to;
	page_class->reload = gs_details_page_reload;
	page_class->setup = gs_details_page_setup;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-details-page.ui");

    gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, application_details_icon);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, application_details_title);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, application_details_developer);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, application_details_category);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_addons);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_details);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_details_description);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_progress);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_progress2);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_details_screenshot);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_details_screenshot_thumbnails);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_details_license_list);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, button_details_launch);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, button_install);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, button_cancel);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, button_more_reviews);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, infobar_details_app_norepo);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, infobar_details_app_repo);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, infobar_details_package_baseos);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, infobar_details_repo);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_addons_uninstalled_app);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_progress_percentage);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_progress_status);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_category_title);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_category_value);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_developer_title);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_developer_value);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, button_details_license_free);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, button_details_license_nonfree);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, button_details_license_unknown);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_size_download_title);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_size_download_value);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_size_installed_title);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_size_installed_value);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_updated_title);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_updated_value);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_channel_title);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_details_channel_value);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_failed);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, list_box_addons);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_reviews);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_details_screenshot_fallback);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, histogram);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, button_review);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, list_box_reviews);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, scrolledwindow_details);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, spinner_details);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, stack_details);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, progressbar_top);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, popover_license_free);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, popover_license_nonfree);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, popover_license_unknown);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_license_nonfree_details);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_licenses_intro);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, popover_content_rating);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_content_rating_title);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_content_rating_message);
	gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, label_content_rating_none);

    gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, box_similar);
    gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, similar_heading);
    gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, similar_more);

    gtk_widget_class_bind_template_child (widget_class, GsDetailsPage, stack);
}

static void
gs_details_page_init (GsDetailsPage *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	/* setup networking */
	self->session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, gs_user_agent (),
	                                               NULL);
	self->settings = g_settings_new ("kr.gooroom.software");

	gtk_list_box_set_header_func (GTK_LIST_BOX (self->list_box_addons),
				      list_header_func,
				      self, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (self->list_box_addons),
				    list_sort_func,
				    self, NULL);
}

GsDetailsPage *
gs_details_page_new (void)
{
	GsDetailsPage *self;
	self = g_object_new (GS_TYPE_DETAILS_PAGE, NULL);
	return GS_DETAILS_PAGE (self);
}

/* vim: set noexpandtab: */
