/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-shell-view.c
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
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
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors:
 *   Ettore Perazzoli <ettore@ximian.com>
 *   Miguel de Icaza <miguel@ximian.com>
 *   Matt Loper <matt@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-window.h>
#include <libgnomeui/gnome-window-icon.h>
#include <libgnomeui/gnome-app.h>
#include <bonobo/bonobo-socket.h>
#include <bonobo/bonobo-ui-util.h>
#include <bonobo/bonobo-widget.h>

#include <gal/e-paned/e-hpaned.h>
#include <gal/util/e-util.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/widgets/e-unicode.h>
#include <gal/widgets/e-scroll-frame.h>

#include "widgets/misc/e-clipped-label.h"

#include "evolution-shell-view.h"

#include "e-shell-constants.h"
#include "e-shell-folder-title-bar.h"
#include "e-shell-utils.h"
#include "e-shell.h"
#include "e-shortcuts-view.h"
#include "e-storage-set-view.h"
#include "e-title-bar.h"

#include "e-shell-view.h"
#include "e-shell-view-menu.h"


static BonoboWindowClass *parent_class = NULL;

struct _EShellViewPrivate {
	/* The shell.  */
	EShell *shell;

	/* EvolutionShellView Bonobo object for implementing the
           Evolution::ShellView interface.  */
	EvolutionShellView *corba_interface;

	/* The UI handler & container.  */
	BonoboUIComponent *ui_component;
	BonoboUIContainer *ui_container;

	/* Currently displayed URI.  */
	char *uri;

	/* delayed selection, used when a path doesn't exist in an
           EStorage.  cleared when we're signaled with
           "folder_selected" */
	char *delayed_selection;

	/* uri to go to at timeout */
	unsigned int set_folder_timeout;
	char        *set_folder_uri;

	/* Tooltips.  */
	GtkTooltips *tooltips;

	/* The widgetry.  */
	GtkWidget *appbar;
	GtkWidget *hpaned;
	GtkWidget *view_vbox;
	GtkWidget *view_title_bar;
	GtkWidget *view_hpaned;
	GtkWidget *contents;
	GtkWidget *notebook;
	GtkWidget *shortcut_frame;
	GtkWidget *shortcut_bar;
	GtkWidget *storage_set_title_bar;
	GtkWidget *storage_set_view;
	GtkWidget *storage_set_view_box;

	/* The status bar widgetry.  */
	GtkWidget *status_bar;
	GtkWidget *offline_toggle;
	GtkWidget *offline_toggle_pixmap;
	GtkWidget *menu_hint_label;
	GtkWidget *task_bar;

	/* The view we have already open.  */
	GHashTable *uri_to_control;

	/* Position of the handles in the paneds, to be restored when we show elements
           after hiding them.  */
	unsigned int hpaned_position;
	unsigned int view_hpaned_position;

	/* Status of the shortcut and folder bars.  */
	EShellViewSubwindowMode shortcut_bar_mode;
	EShellViewSubwindowMode folder_bar_mode;

	/* List of sockets we created.  */
	GList *sockets;
};

enum {
	SHORTCUT_BAR_MODE_CHANGED,
	FOLDER_BAR_MODE_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define DEFAULT_SHORTCUT_BAR_WIDTH 100
#define DEFAULT_TREE_WIDTH         130

#define DEFAULT_WIDTH 705
#define DEFAULT_HEIGHT 550

#define DEFAULT_URI "evolution:/local/Inbox"

#define SET_FOLDER_DELAY 250


/* The icons for the offline/online status.  */

static GdkPixmap *offline_pixmap = NULL;
static GdkBitmap *offline_mask = NULL;

static GdkPixmap *online_pixmap = NULL;
static GdkBitmap *online_mask = NULL;


static void        update_for_current_uri         (EShellView *shell_view);
static void        update_offline_toggle_status   (EShellView *shell_view);
static const char *get_storage_set_path_from_uri  (const char *uri);


/* Utility functions.  */

static GtkWidget *
create_label_for_empty_page (void)
{
	GtkWidget *label;

	label = e_clipped_label_new (_("(No folder displayed)"));
	gtk_widget_show (label);

	return label;
}

/* Initialize the icons.  */
static void
load_images (void)
{
	GdkPixbuf *pixbuf;

	pixbuf = gdk_pixbuf_new_from_file (EVOLUTION_IMAGES "/offline.png");
	if (pixbuf == NULL) {
		g_warning ("Cannot load `%s'", EVOLUTION_IMAGES "/offline.png");
	} else {
		gdk_pixbuf_render_pixmap_and_mask (pixbuf, &offline_pixmap, &offline_mask, 128);
		gdk_pixbuf_unref (pixbuf);
	}

	pixbuf = gdk_pixbuf_new_from_file (EVOLUTION_IMAGES "/online.png");
	if (pixbuf == NULL) {
		g_warning ("Cannot load `%s'", EVOLUTION_IMAGES "/online.png");
	} else {
		gdk_pixbuf_render_pixmap_and_mask (pixbuf, &online_pixmap, &online_mask, 128);
		gdk_pixbuf_unref (pixbuf);
	}
}

/* FIXME this is broken.  */
static gboolean
bonobo_widget_is_dead (BonoboWidget *bonobo_widget)
{
	BonoboControlFrame *control_frame;
	CORBA_Object corba_object;
	CORBA_Environment ev;
	gboolean is_dead;

	control_frame = bonobo_widget_get_control_frame (bonobo_widget);
	corba_object = bonobo_control_frame_get_control (control_frame);

	CORBA_exception_init (&ev);
	is_dead = CORBA_Object_non_existent (corba_object, &ev);
	CORBA_exception_free (&ev);

	return is_dead;
}


/* Folder bar pop-up handling.  */

static void disconnect_popup_signals (EShellView *shell_view);

static void
popdown_transient_folder_bar (EShellView *shell_view)
{
	EShellViewPrivate *priv;

	priv = shell_view->priv;

	gdk_pointer_ungrab (GDK_CURRENT_TIME);
	gtk_grab_remove (priv->storage_set_view_box);

	e_shell_view_set_folder_bar_mode (shell_view, E_SHELL_VIEW_SUBWINDOW_HIDDEN);

	disconnect_popup_signals (shell_view);

	e_shell_folder_title_bar_set_toggle_state (E_SHELL_FOLDER_TITLE_BAR (priv->view_title_bar), FALSE);
}

static int
storage_set_view_box_button_release_event_cb (GtkWidget *widget,
					      GdkEventButton *button_event,
					      void *data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	if (button_event->window == E_PANED (priv->view_hpaned)->handle)
		return FALSE;

	popdown_transient_folder_bar (shell_view);

	return TRUE;
}

static int
storage_set_view_box_event_cb (GtkWidget *widget,
			       GdkEvent *event,
			       void *data)
{
	GtkWidget *event_widget;
	GtkWidget *tooltip;
	EShellView *shell_view;
	EShellViewPrivate *priv;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	event_widget = gtk_get_event_widget (event);

	if (!event_widget)
		return FALSE;

	tooltip = e_tree_get_tooltip (E_TREE(priv->storage_set_view));
	if (! (GTK_WIDGET_IS_SENSITIVE (event_widget) &&
	       tooltip &&
	       gtk_widget_is_ancestor (event_widget, tooltip)))
		return FALSE;

	switch (event->type) {
	case GDK_BUTTON_PRESS:
	case GDK_2BUTTON_PRESS:
	case GDK_3BUTTON_PRESS:
	case GDK_KEY_PRESS:
	case GDK_KEY_RELEASE:
	case GDK_MOTION_NOTIFY:
	case GDK_BUTTON_RELEASE:
	case GDK_PROXIMITY_IN:
	case GDK_PROXIMITY_OUT:
		gtk_propagate_event (event_widget, event);
		return TRUE;
		break;
	case GDK_ENTER_NOTIFY:
	case GDK_LEAVE_NOTIFY:
		gtk_widget_event (event_widget, event);
		return TRUE;
		break;
	default:
		break;
	}
	return FALSE;
}

static void
popup_storage_set_view_button_clicked (ETitleBar *title_bar,
				       void *data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	gdk_pointer_ungrab (GDK_CURRENT_TIME);
	gtk_grab_remove (priv->storage_set_view_box);

	disconnect_popup_signals (shell_view);

	e_shell_view_set_folder_bar_mode (shell_view, E_SHELL_VIEW_SUBWINDOW_STICKY);
	e_shell_folder_title_bar_set_toggle_state (E_SHELL_FOLDER_TITLE_BAR (priv->view_title_bar), FALSE);
}

static void
storage_set_view_box_map_cb (GtkWidget *widget,
			     void *data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	if (gdk_pointer_grab (widget->window, TRUE,
			      (GDK_BUTTON_PRESS_MASK
			       | GDK_BUTTON_RELEASE_MASK
			       | GDK_ENTER_NOTIFY_MASK
			       | GDK_LEAVE_NOTIFY_MASK
			       | GDK_POINTER_MOTION_MASK),
			      NULL, NULL, GDK_CURRENT_TIME) != 0) {
		g_warning ("e-shell-view.c:storage_set_view_box_map_cb() -- pointer grab failed.");
		e_shell_view_set_folder_bar_mode (shell_view, E_SHELL_VIEW_SUBWINDOW_STICKY);
		return;
	}

	gtk_grab_add (widget);
	gtk_signal_connect (GTK_OBJECT (widget), "event",
			    GTK_SIGNAL_FUNC (storage_set_view_box_event_cb), shell_view);
	gtk_signal_connect (GTK_OBJECT (widget), "button_release_event",
			    GTK_SIGNAL_FUNC (storage_set_view_box_button_release_event_cb), shell_view);
	gtk_signal_connect (GTK_OBJECT (priv->storage_set_view), "button_release_event",
			    GTK_SIGNAL_FUNC (storage_set_view_box_button_release_event_cb), shell_view);
	gtk_signal_connect (GTK_OBJECT (priv->storage_set_title_bar), "button_clicked",
			    GTK_SIGNAL_FUNC (popup_storage_set_view_button_clicked), shell_view);
}

static void
disconnect_popup_signals (EShellView *shell_view)
{
	EShellViewPrivate *priv;

	priv = shell_view->priv;

	gtk_signal_disconnect_by_func (GTK_OBJECT (priv->storage_set_view_box),
				       GTK_SIGNAL_FUNC (storage_set_view_box_button_release_event_cb),
				       shell_view);
	gtk_signal_disconnect_by_func (GTK_OBJECT (priv->storage_set_view),
				       GTK_SIGNAL_FUNC (storage_set_view_box_button_release_event_cb),
				       shell_view);
	gtk_signal_disconnect_by_func (GTK_OBJECT (priv->storage_set_title_bar),
				       GTK_SIGNAL_FUNC (popup_storage_set_view_button_clicked),
				       shell_view);
	gtk_signal_disconnect_by_func (GTK_OBJECT (priv->storage_set_view_box),
				       GTK_SIGNAL_FUNC (storage_set_view_box_map_cb),
				       shell_view);
}

static void
pop_up_folder_bar (EShellView *shell_view)
{
	EShellViewPrivate *priv;

	priv = shell_view->priv;

	priv->folder_bar_mode = E_SHELL_VIEW_SUBWINDOW_TRANSIENT;

	/* We need to show the storage set view box and do a pointer grab to catch the
           mouse clicks.  But until the box is shown, we cannot grab.  So we connect to
           the "map" signal; `storage_set_view_box_map_cb()' will do the grab.  */

	gtk_signal_connect (GTK_OBJECT (priv->storage_set_view_box), "map",
			    GTK_SIGNAL_FUNC (storage_set_view_box_map_cb), shell_view);
	gtk_widget_show (priv->storage_set_view_box);

	e_paned_set_position (E_PANED (priv->view_hpaned), priv->view_hpaned_position);
}


/* Switching views on a tree view click.  */

static void new_folder_cb (EStorageSet *storage_set, const char *path, void *data);

static int
set_folder_timeout (gpointer data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	/* set to 0 so we don't remove it in _display_uri() */
	priv->set_folder_timeout = 0;
	e_shell_view_display_uri (shell_view, priv->set_folder_uri);

	return FALSE;
}

static void
switch_on_folder_tree_click (EShellView *shell_view,
			     const char *path)
{
	EShellViewPrivate *priv;
	char *uri;

	priv = shell_view->priv;

	uri = g_strconcat (E_SHELL_URI_PREFIX, path, NULL);
	if (priv->uri != NULL && !strcmp (uri, priv->uri)) {
		g_free (uri);
		return;
	}

	if (priv->set_folder_timeout != 0)
		gtk_timeout_remove (priv->set_folder_timeout);
	g_free (priv->set_folder_uri);

	if (priv->delayed_selection) {
		g_free (priv->delayed_selection);
		priv->delayed_selection = NULL;
		gtk_signal_disconnect_by_func (GTK_OBJECT (e_shell_get_storage_set (priv->shell)),
					       GTK_SIGNAL_FUNC (new_folder_cb),
					       shell_view);
	}

	if (priv->folder_bar_mode == E_SHELL_VIEW_SUBWINDOW_TRANSIENT) {
		e_shell_view_display_uri (shell_view, uri);
		popdown_transient_folder_bar (shell_view);
		g_free (uri);
		return;
	}

	priv->set_folder_uri = uri;

	priv->set_folder_timeout = gtk_timeout_add (SET_FOLDER_DELAY, set_folder_timeout, shell_view);
}


/* Callbacks.  */

/* Callback when a new folder is added.  removed when we clear the
   delayed_selection */
static void
new_folder_cb (EStorageSet *storage_set,
	       const char *path,
	       void *data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;
	char *delayed_path;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	delayed_path = strchr (priv->delayed_selection, ':');
	if (delayed_path) {
		delayed_path ++;
		if (!strcmp(path, delayed_path)) {
			gtk_signal_disconnect_by_func (GTK_OBJECT (e_shell_get_storage_set(priv->shell)),
						       GTK_SIGNAL_FUNC (new_folder_cb),
						       shell_view);
			g_free (priv->uri);
			priv->uri = priv->delayed_selection;
			priv->delayed_selection = NULL;
			e_shell_view_display_uri (shell_view, priv->uri);
		}
	}
}

/* Callback called when an icon on the shortcut bar gets clicked.  */
static void
activate_shortcut_cb (EShortcutsView *shortcut_view,
		      EShortcuts *shortcuts,
		      const char *uri,
		      void *data)
{
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (data);

	e_shell_view_display_uri (shell_view, uri);
}

/* Callback when user chooses "Hide shortcut bar" via a right click */
static void
hide_requested_cb (EShortcutsView *shortcut_view,
		   void *data)
{
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (data);

	e_shell_view_set_shortcut_bar_mode (shell_view,
					    E_SHELL_VIEW_SUBWINDOW_HIDDEN);
}

/* Callback called when a folder on the tree view gets clicked.  */
static void
folder_selected_cb (EStorageSetView *storage_set_view,
		    const char *path,
		    void *data)
{
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (data);

	switch_on_folder_tree_click (shell_view, path);
}

/* Callback called when a storage in the tree view is clicked.  */
static void
storage_selected_cb (EStorageSetView *storage_set_view,
		     const char *name,
		     void *data)
{
	EShellView *shell_view;
	char *path;

	shell_view = E_SHELL_VIEW (data);

	path = g_strconcat (G_DIR_SEPARATOR_S, name, NULL);
	switch_on_folder_tree_click (shell_view, path);

	g_free (path);
}

/* Callback called when the button on the tree's title bar is clicked.  */
static void
storage_set_view_button_clicked_cb (ETitleBar *title_bar,
				    void *data)
{
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (data);

	e_shell_view_set_folder_bar_mode (shell_view, E_SHELL_VIEW_SUBWINDOW_HIDDEN);
}

/* Callback called when the title bar button is clicked.  */
static void
title_bar_toggled_cb (EShellFolderTitleBar *title_bar,
		      gboolean state,
		      void *data)
{
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (data);

	if (! state)
		return;

	if (e_shell_view_get_folder_bar_mode (shell_view) != E_SHELL_VIEW_SUBWINDOW_TRANSIENT)
		pop_up_folder_bar (shell_view);
}

/* Callback called when the offline toggle button is clicked.  */
static void
offline_toggle_clicked_cb (GtkButton *button,
			   void *data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	switch (e_shell_get_line_status (priv->shell)) {
	case E_SHELL_LINE_STATUS_ONLINE:
		e_shell_go_offline (priv->shell, shell_view);
		break;
	case E_SHELL_LINE_STATUS_OFFLINE:
		e_shell_go_online (priv->shell, shell_view);
		break;
	default:
		g_assert_not_reached ();
	}
}


/* Widget setup.  */

static void
setup_storage_set_subwindow (EShellView *shell_view)
{
	EShellViewPrivate *priv;
	GtkWidget *storage_set_view;
	GtkWidget *vbox;
	GtkWidget *scroll_frame;

	priv = shell_view->priv;

	storage_set_view = e_storage_set_view_new (e_shell_get_storage_set (priv->shell), priv->ui_container);
	gtk_signal_connect (GTK_OBJECT (storage_set_view), "folder_selected",
			    GTK_SIGNAL_FUNC (folder_selected_cb), shell_view);
	gtk_signal_connect (GTK_OBJECT (storage_set_view), "storage_selected",
			    GTK_SIGNAL_FUNC (storage_selected_cb), shell_view);

	scroll_frame = e_scroll_frame_new (NULL, NULL);
	e_scroll_frame_set_policy (E_SCROLL_FRAME (scroll_frame), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	e_scroll_frame_set_shadow_type (E_SCROLL_FRAME (scroll_frame), GTK_SHADOW_IN);

	gtk_container_add (GTK_CONTAINER (scroll_frame), storage_set_view);

	vbox = gtk_vbox_new (FALSE, 0);
	priv->storage_set_title_bar = e_title_bar_new (_("Folders"));

	gtk_box_pack_start (GTK_BOX (vbox), priv->storage_set_title_bar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), scroll_frame, TRUE, TRUE, 0);

	gtk_signal_connect (GTK_OBJECT (priv->storage_set_title_bar), "button_clicked",
			    GTK_SIGNAL_FUNC (storage_set_view_button_clicked_cb), shell_view);

	gtk_widget_show (vbox);
	gtk_widget_show (storage_set_view);
	gtk_widget_show (priv->storage_set_title_bar);
	gtk_widget_show (scroll_frame);

	priv->storage_set_view_box = vbox;
	priv->storage_set_view = storage_set_view;
}

static void
setup_offline_toggle (EShellView *shell_view)
{
	EShellViewPrivate *priv;
	GtkWidget *toggle;
	GtkWidget *pixmap;

	priv = shell_view->priv;

	toggle = gtk_button_new ();
	GTK_WIDGET_UNSET_FLAGS (toggle, GTK_CAN_FOCUS);
	gtk_button_set_relief (GTK_BUTTON (toggle), GTK_RELIEF_NONE);

	gtk_signal_connect (GTK_OBJECT (toggle), "clicked",
			    GTK_SIGNAL_FUNC (offline_toggle_clicked_cb), shell_view);

	pixmap = gtk_pixmap_new (offline_pixmap, offline_mask);

	gtk_container_add (GTK_CONTAINER (toggle), pixmap);

	gtk_widget_show (toggle);
	gtk_widget_show (pixmap);

	priv->offline_toggle        = toggle;
	priv->offline_toggle_pixmap = pixmap;

	update_offline_toggle_status (shell_view);

	g_assert (priv->status_bar != NULL);

	gtk_box_pack_start (GTK_BOX (priv->status_bar), priv->offline_toggle, FALSE, TRUE, 0);
}

static void
setup_menu_hint_label (EShellView *shell_view)
{
	EShellViewPrivate *priv;

	priv = shell_view->priv;

	priv->menu_hint_label = gtk_label_new ("");
	gtk_misc_set_alignment (GTK_MISC (priv->menu_hint_label), 0.0, 0.5);

	gtk_box_pack_start (GTK_BOX (priv->status_bar), priv->menu_hint_label, TRUE, TRUE, 0);
}

static void
setup_task_bar (EShellView *shell_view)
{
	EShellViewPrivate *priv;

	priv = shell_view->priv;

	priv->task_bar = e_task_bar_new ();

	g_assert (priv->status_bar != NULL);

	gtk_box_pack_start (GTK_BOX (priv->status_bar), priv->task_bar, TRUE, TRUE, 0);
	gtk_widget_show (priv->task_bar);
}

static void
create_status_bar (EShellView *shell_view)
{
	EShellViewPrivate *priv;

	priv = shell_view->priv;

	priv->status_bar = gtk_hbox_new (FALSE, 2);
	gtk_widget_show (priv->status_bar);

	setup_offline_toggle (shell_view);
	setup_menu_hint_label (shell_view);
	setup_task_bar (shell_view);
}


/* Menu hints for the status bar.  */

static void
ui_engine_add_hint_callback (BonoboUIEngine *engine,
			     const char *hint,
			     void *data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	gtk_label_set (GTK_LABEL (priv->menu_hint_label), hint);
	gtk_widget_show (priv->menu_hint_label);
	gtk_widget_hide (priv->task_bar);
}

static void
ui_engine_remove_hint_callback (BonoboUIEngine *engine,
				void *data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	gtk_widget_hide (priv->menu_hint_label);
	gtk_widget_show (priv->task_bar);
}

static void
setup_statusbar_hints (EShellView *shell_view)
{
	EShellViewPrivate *priv;
	BonoboUIEngine *ui_engine;

	priv = shell_view->priv;

	g_assert (priv->status_bar != NULL);

	ui_engine = bonobo_window_get_ui_engine (BONOBO_WINDOW (shell_view));
 
	gtk_signal_connect (GTK_OBJECT (ui_engine), "add_hint",
			    GTK_SIGNAL_FUNC (ui_engine_add_hint_callback), shell_view);
	gtk_signal_connect (GTK_OBJECT (ui_engine), "remove_hint",
			    GTK_SIGNAL_FUNC (ui_engine_remove_hint_callback), shell_view);
}


static void
setup_widgets (EShellView *shell_view)
{
	EShellViewPrivate *priv;
	GtkWidget *contents_vbox;

	priv = shell_view->priv;

	/* The shortcut bar.  */

	priv->shortcut_bar = e_shortcuts_new_view (e_shell_get_shortcuts (priv->shell));
	gtk_signal_connect (GTK_OBJECT (priv->shortcut_bar), "activate_shortcut",
			    GTK_SIGNAL_FUNC (activate_shortcut_cb), shell_view);

	gtk_signal_connect (GTK_OBJECT (priv->shortcut_bar), "hide_requested",
			    GTK_SIGNAL_FUNC (hide_requested_cb), shell_view);

	priv->shortcut_frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (priv->shortcut_frame), GTK_SHADOW_IN);

	/* The storage set view.  */

	setup_storage_set_subwindow (shell_view);

	/* The tabless notebook which we used to contain the views.  */

	priv->notebook = gtk_notebook_new ();
	gtk_notebook_set_show_border (GTK_NOTEBOOK (priv->notebook), FALSE);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (priv->notebook), FALSE);

	/* Page for "No URL displayed" message.  */

	gtk_notebook_append_page (GTK_NOTEBOOK (priv->notebook), create_label_for_empty_page (), NULL);

	/* Put things into a paned and the paned into the GnomeApp.  */

	priv->view_vbox = gtk_vbox_new (FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (priv->view_vbox), 2);

	priv->view_title_bar = e_shell_folder_title_bar_new ();
	gtk_signal_connect (GTK_OBJECT (priv->view_title_bar), "title_toggled",
			    GTK_SIGNAL_FUNC (title_bar_toggled_cb), shell_view);

	priv->view_hpaned = e_hpaned_new ();
	e_paned_pack1 (E_PANED (priv->view_hpaned), priv->storage_set_view_box, FALSE, FALSE);
	e_paned_pack2 (E_PANED (priv->view_hpaned), priv->notebook, TRUE, FALSE);
	e_paned_set_position (E_PANED (priv->view_hpaned), DEFAULT_TREE_WIDTH);

	gtk_box_pack_start (GTK_BOX (priv->view_vbox), priv->view_title_bar,
			    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (priv->view_vbox), priv->view_hpaned,
			    TRUE, TRUE, 2);

	priv->hpaned = e_hpaned_new ();
	gtk_container_add (GTK_CONTAINER (priv->shortcut_frame), priv->shortcut_bar);
	e_paned_pack1 (E_PANED (priv->hpaned), priv->shortcut_frame, FALSE, FALSE);
	e_paned_pack2 (E_PANED (priv->hpaned), priv->view_vbox, TRUE, FALSE);
	e_paned_set_position (E_PANED (priv->hpaned), DEFAULT_SHORTCUT_BAR_WIDTH);

	/* The status bar.  */

	create_status_bar (shell_view);
	setup_statusbar_hints (shell_view);

	/* The contents.  */

	contents_vbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (contents_vbox), priv->hpaned, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (contents_vbox), priv->status_bar, FALSE, TRUE, 0);
	gtk_widget_show (contents_vbox);

	bonobo_window_set_contents (BONOBO_WINDOW (shell_view), contents_vbox);

	/* Show stuff.  */

	gtk_widget_show (priv->shortcut_frame);
	gtk_widget_show (priv->shortcut_bar);
	gtk_widget_show (priv->storage_set_view);
	gtk_widget_show (priv->storage_set_view_box);
	gtk_widget_show (priv->notebook);
	gtk_widget_show (priv->hpaned);
	gtk_widget_show (priv->view_hpaned);
	gtk_widget_show (priv->view_vbox);
	gtk_widget_show (priv->view_title_bar);
	gtk_widget_show (priv->status_bar);

	/* By default, both the folder bar and shortcut bar are visible.  */
	priv->shortcut_bar_mode = E_SHELL_VIEW_SUBWINDOW_STICKY;
	priv->folder_bar_mode   = E_SHELL_VIEW_SUBWINDOW_STICKY;

	/* FIXME: Session management and stuff?  */
	gtk_window_set_default_size (GTK_WINDOW (shell_view), DEFAULT_WIDTH, DEFAULT_HEIGHT);
}


/* GtkObject methods.  */

static void
hash_forall_destroy_control (void *name,
			     void *value,
			     void *data)
{
	BonoboWidget *bonobo_widget;

	bonobo_widget = BONOBO_WIDGET (value);
	gtk_widget_destroy (GTK_WIDGET (bonobo_widget));

	g_free (name);
}

static void
destroy (GtkObject *object)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;
	GList *p;

	shell_view = E_SHELL_VIEW (object);
	priv = shell_view->priv;

	gtk_object_unref (GTK_OBJECT (priv->tooltips));

	if (priv->corba_interface != NULL)
		bonobo_object_unref (BONOBO_OBJECT (priv->corba_interface));

	for (p = priv->sockets; p != NULL; p = p->next) {
		GtkWidget *socket_widget;
		int destroy_connection_id;

		socket_widget = GTK_WIDGET (p->data);
		destroy_connection_id = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (socket_widget),
									      "e_shell_view_destroy_connection_id"));
		gtk_signal_disconnect (GTK_OBJECT (socket_widget), destroy_connection_id);
	}

	g_hash_table_foreach (priv->uri_to_control, hash_forall_destroy_control, NULL);
	g_hash_table_destroy (priv->uri_to_control);

	bonobo_object_unref (BONOBO_OBJECT (priv->ui_component));

	g_free (priv->uri);

	if (priv->set_folder_timeout != 0)
		gtk_timeout_remove (priv->set_folder_timeout);

	g_free (priv->set_folder_uri);

	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

static  int
delete_event (GtkWidget *widget,
	      GdkEventAny *event)
{
	EShell *shell;

	shell = e_shell_view_get_shell (E_SHELL_VIEW (widget));

	/* FIXME: Is this right, or should it be FALSE? */
	return FALSE;
}


/* Initialization.  */

static void
class_init (EShellViewClass *klass)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) klass;

	object_class->destroy = destroy;

	parent_class = gtk_type_class (BONOBO_TYPE_WINDOW);

	signals[SHORTCUT_BAR_MODE_CHANGED]
		= gtk_signal_new ("shortcut_bar_mode_changed",
				  GTK_RUN_FIRST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EShellViewClass, shortcut_bar_mode_changed),
				  gtk_marshal_NONE__INT,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_INT);

	signals[FOLDER_BAR_MODE_CHANGED]
		= gtk_signal_new ("folder_bar_mode_changed",
				  GTK_RUN_FIRST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EShellViewClass, folder_bar_mode_changed),
				  gtk_marshal_NONE__INT,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_INT);

	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);

	load_images ();
}

static void
init (EShellView *shell_view)
{
	EShellViewPrivate *priv;

	priv = g_new (EShellViewPrivate, 1);

	priv->shell                   = NULL;
	priv->corba_interface         = NULL;
	priv->ui_component            = NULL;
	priv->uri                     = NULL;
	priv->delayed_selection       = NULL;

	priv->tooltips                = gtk_tooltips_new ();

	priv->appbar                  = NULL;
	priv->hpaned                  = NULL;
	priv->view_hpaned             = NULL;
	priv->contents                = NULL;
	priv->notebook                = NULL;

	priv->storage_set_title_bar   = NULL;
	priv->storage_set_view        = NULL;
	priv->storage_set_view_box    = NULL;
	priv->shortcut_bar            = NULL;

	priv->status_bar              = NULL;
	priv->offline_toggle          = NULL;
	priv->offline_toggle_pixmap   = NULL;
	priv->menu_hint_label         = NULL;
	priv->task_bar                = NULL;

	priv->shortcut_bar_mode       = E_SHELL_VIEW_SUBWINDOW_HIDDEN;
	priv->folder_bar_mode         = E_SHELL_VIEW_SUBWINDOW_HIDDEN;

	priv->hpaned_position         = 0;
	priv->view_hpaned_position    = 0;

	priv->uri_to_control          = g_hash_table_new (g_str_hash, g_str_equal);

	priv->sockets		      = NULL;

	priv->set_folder_timeout      = 0;
	priv->set_folder_uri          = NULL;

	shell_view->priv = priv;
}


/* EvolutionShellView interface callbacks.  */

static void
corba_interface_set_message_cb (EvolutionShellView *shell_view,
				     const char *message,
				     gboolean busy,
				     void *data)
{
	/* Don't do anything here anymore.  The interface is going to be
	   deprecated soon.  */
}

static void
corba_interface_unset_message_cb (EvolutionShellView *shell_view,
				       void *data)
{
	/* Don't do anything here anymore.  The interface is going to be
	   deprecated soon.  */
}

static void
corba_interface_change_current_view_cb (EvolutionShellView *shell_view,
					     const char *uri,
					     void *data)
{
	EShellView *view;

	view = E_SHELL_VIEW (data);

	g_return_if_fail (view != NULL);

	e_shell_view_display_uri (view, uri);
}

static void
corba_interface_set_title (EvolutionShellView *shell_view,
			   const char *title,
			   void *data)
{
	EShellView *view;

	view = E_SHELL_VIEW (data);
	
	g_return_if_fail (view != NULL);

	gtk_window_set_title (GTK_WINDOW (view), title);
}

static void
corba_interface_set_folder_bar_label (EvolutionShellView *evolution_shell_view,
				      const char *text,
				      void *data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;

	g_return_if_fail (data != NULL);
	g_return_if_fail (E_IS_SHELL_VIEW (data));

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	e_shell_folder_title_bar_set_folder_bar_label (E_SHELL_FOLDER_TITLE_BAR (priv->view_title_bar),
						       text);
}

static void
unmerge_on_error (BonoboObject *object,
		  CORBA_Object  cobject,
		  CORBA_Environment *ev)
{
	BonoboWindow *win;

	win = bonobo_ui_container_get_win (BONOBO_UI_CONTAINER (object));

	if (win)
		bonobo_window_deregister_component_by_ref (win, cobject);
}

static void
updated_folder_cb (EStorageSet *storage_set,
		   const char *path,
		   void *data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;
	char *uri;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	uri = g_strconcat (E_SHELL_URI_PREFIX, path, NULL);

	/* Update the shortcut bar */
	e_shortcuts_update_shortcut_by_uri (e_shell_get_shortcuts (priv->shell), uri);
	g_free (uri);

	/* Update the folder title bar and the window title bar */
	update_for_current_uri (shell_view);
}


/* Shell callbacks.  */

static void
shell_line_status_changed_cb (EShell *shell,
			      EShellLineStatus new_status,
			      void *data)
{
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (data);
	update_offline_toggle_status (shell_view);
}


EShellView *
e_shell_view_construct (EShellView *shell_view,
			EShell     *shell)
{
	EShellViewPrivate *priv;
	EShellView *view;
	GtkObject *window;

	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (shell_view != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	priv = shell_view->priv;

	view = E_SHELL_VIEW (bonobo_window_construct (BONOBO_WINDOW (shell_view), "evolution", "Evolution"));

	if (!view) {
		gtk_object_unref (GTK_OBJECT (shell_view));
		return NULL;
	}		

	window = GTK_OBJECT (view);

	gtk_signal_connect (window, "delete_event", GTK_SIGNAL_FUNC (delete_event), NULL);

	priv->shell = shell;

	gtk_signal_connect_while_alive (GTK_OBJECT (e_shell_get_storage_set (priv->shell)), "updated_folder",
					updated_folder_cb, shell_view, GTK_OBJECT (shell_view));

	priv->ui_container = bonobo_ui_container_new ();
	bonobo_ui_container_set_win (priv->ui_container, BONOBO_WINDOW (shell_view));
	gtk_signal_connect (GTK_OBJECT (priv->ui_container),
			    "system_exception", GTK_SIGNAL_FUNC (unmerge_on_error), NULL);

	priv->ui_component = bonobo_ui_component_new ("evolution");
	bonobo_ui_component_set_container (priv->ui_component,
					   bonobo_object_corba_objref (BONOBO_OBJECT (priv->ui_container)));

	bonobo_ui_component_freeze (priv->ui_component, NULL);

	bonobo_ui_util_set_ui (priv->ui_component, EVOLUTION_DATADIR, "evolution.xml", "evolution");

	setup_widgets (shell_view);

	bonobo_ui_engine_config_set_path (bonobo_window_get_ui_engine (BONOBO_WINDOW (shell_view)),
					  "/evolution/UIConf/kvps");
	e_shell_view_menu_setup (shell_view);

	e_shell_view_set_folder_bar_mode (shell_view, E_SHELL_VIEW_SUBWINDOW_HIDDEN);

	bonobo_ui_component_thaw (priv->ui_component, NULL);

	gtk_signal_connect (GTK_OBJECT (shell), "line_status_changed",
			    GTK_SIGNAL_FUNC (shell_line_status_changed_cb), view);

	return view;
}

/* WARNING: Don't use `e_shell_view_new()' to create new views for the shell
   unless you know what you are doing; this is just the standard GTK+
   constructor thing and it won't allow the shell to do the required
   bookkeeping for the created views.  Instead, the right way to create a new
   view is calling `e_shell_create_view()'.  */
EShellView *
e_shell_view_new (EShell *shell)
{
	GtkWidget *new;

	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	new = gtk_type_new (e_shell_view_get_type ());

	return e_shell_view_construct (E_SHELL_VIEW (new), shell);
}

const GNOME_Evolution_ShellView
e_shell_view_get_corba_interface (EShellView *shell_view)
{
	EShellViewPrivate *priv;

	g_return_val_if_fail (shell_view != NULL, CORBA_OBJECT_NIL);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), CORBA_OBJECT_NIL);

	priv = shell_view->priv;

	return bonobo_object_corba_objref (BONOBO_OBJECT (priv->corba_interface));
}


static const char *
get_storage_set_path_from_uri (const char *uri)
{
	const char *colon;

	if (uri == NULL)
		return NULL;

	if (g_path_is_absolute (uri))
		return NULL;

	colon = strchr (uri, ':');
	if (colon == NULL || colon == uri || colon[1] == '\0')
		return NULL;

	if (! g_path_is_absolute (colon + 1))
		return NULL;

	if (g_strncasecmp (uri, E_SHELL_URI_PREFIX, colon - uri) != 0)
		return NULL;

	return colon + 1;
}

static void
update_window_icon (EShellView *shell_view,
		    EFolder *folder,
		    gboolean is_my_evolution)
{
	EShellViewPrivate *priv;
	const char *type;
	const char *icon_name;
	char *icon_path;

	priv = shell_view->priv;

	if (folder == NULL) {
		if (is_my_evolution) {
			type = "My Evolution";
		} else {
			type = NULL;
		}
	} else {
		type = e_folder_get_type_string (folder);
	}

	if (type == NULL) {
		icon_path = NULL;
	} else {
		EFolderTypeRegistry *folder_type_registry;

		folder_type_registry = e_shell_get_folder_type_registry (priv->shell);
		icon_name = e_folder_type_registry_get_icon_name_for_type (folder_type_registry, type);
		if (icon_name == NULL)
			icon_path = NULL;
		else
			icon_path = e_shell_get_icon_path (icon_name, TRUE);
	}

	if (icon_path == NULL) {
		gnome_window_icon_set_from_default (GTK_WINDOW (shell_view));
	} else {
		gnome_window_icon_set_from_file (GTK_WINDOW (shell_view), icon_path);
		g_free (icon_path);
	}
}

static void
update_folder_title_bar (EShellView *shell_view,
			 EFolder *folder,
			 gboolean is_my_evolution)
{
	EShellViewPrivate *priv;
	EFolderTypeRegistry *folder_type_registry;
	GdkPixbuf *folder_icon;
	const char *folder_name;
	const char *folder_type_name;

	priv = shell_view->priv;

	if (folder == NULL) {
		if (is_my_evolution) {
			folder_type_name = "My Evolution";
		} else {
			folder_type_name = NULL;
		}
	} else {
		folder_type_name = e_folder_get_type_string (folder);
	}

	if (folder_type_name == NULL) {
		folder_name = NULL;
		folder_icon = NULL;
	} else {
		folder_type_registry = e_shell_get_folder_type_registry (priv->shell);
		folder_icon = e_folder_type_registry_get_icon_for_type (folder_type_registry,
									folder_type_name,
									TRUE);
		if (is_my_evolution) {
			folder_name = "My Evolution";
		} else {
			folder_name = e_folder_get_name (folder);
		}
	}

	if (folder_icon)
		e_shell_folder_title_bar_set_icon (E_SHELL_FOLDER_TITLE_BAR (priv->view_title_bar), folder_icon);
	if (folder_name) {
		gchar * utf;
		utf = e_utf8_to_gtk_string (GTK_WIDGET (priv->view_title_bar), folder_name);
		e_shell_folder_title_bar_set_title (E_SHELL_FOLDER_TITLE_BAR (priv->view_title_bar), utf);
		g_free (utf);
	}
}

static void
update_for_current_uri (EShellView *shell_view)
{
	EShellViewPrivate *priv;
	EFolder *folder;
	char *folder_name;
	const char *path;
	char *window_title;
	gboolean is_my_evolution = FALSE;

	priv = shell_view->priv;

	/* if we update when there is a timeout set, the selection
	 * will jump around against the user's wishes.  so we just
	 * return.
	 */     
	if (priv->set_folder_timeout != 0)
		return;

	path = get_storage_set_path_from_uri (priv->uri);

	if (priv->uri != NULL && strcmp (priv->uri, "evolution:/My Evolution") == 0) {
		/* Special case for My Evolution */
		folder_name = g_strdup (_("My Evolution"));
		is_my_evolution = TRUE;
		folder = NULL;
	} else {
		if (path == NULL)
			folder = NULL;
		else
			folder = e_storage_set_get_folder (e_shell_get_storage_set (priv->shell),
							   path);
		
		if (folder == NULL)
			folder_name = g_strdup (_("None"));
		else
			folder_name = e_utf8_to_gtk_string ((GtkWidget *) shell_view, e_folder_get_name (folder));
	}

	if (SUB_VERSION[0] == '\0')
		window_title = g_strdup_printf (_("%s - Evolution %s"), folder_name, VERSION);
	else
		window_title = g_strdup_printf (_("%s - Evolution %s [%s]"), folder_name, VERSION, SUB_VERSION);

	gtk_window_set_title (GTK_WINDOW (shell_view), window_title);
	g_free (window_title);
	g_free (folder_name);
	
	update_folder_title_bar (shell_view, folder, is_my_evolution);
	
	update_window_icon (shell_view, folder, is_my_evolution);

	gtk_signal_handler_block_by_func (GTK_OBJECT (priv->storage_set_view),
					  GTK_SIGNAL_FUNC (folder_selected_cb),
					  shell_view);

	if (path != NULL)
		e_storage_set_view_set_current_folder (E_STORAGE_SET_VIEW (priv->storage_set_view), path);

	gtk_signal_handler_unblock_by_func (GTK_OBJECT (priv->storage_set_view),
					    GTK_SIGNAL_FUNC (folder_selected_cb),
					    shell_view);
}

static void
update_offline_toggle_status (EShellView *shell_view)
{
	EShellViewPrivate *priv;
	GdkPixmap *icon_pixmap;
	GdkBitmap *icon_mask;
	const char *tooltip;
	gboolean sensitive;

	priv = shell_view->priv;

	switch (e_shell_get_line_status (priv->shell)) {
	case E_SHELL_LINE_STATUS_ONLINE:
		icon_pixmap = online_pixmap;
		icon_mask   = online_mask;
		sensitive   = TRUE;
		tooltip     = _("Evolution is currently online.  "
				"Click on this button to work offline.");
		break;
	case E_SHELL_LINE_STATUS_GOING_OFFLINE:
		icon_pixmap = online_pixmap;
		icon_mask   = online_mask;
		sensitive   = FALSE;
		tooltip     = _("Evolution is in the process of going offline.");
		break;
	case E_SHELL_LINE_STATUS_OFFLINE:
		icon_pixmap = offline_pixmap;
		icon_mask   = offline_mask;
		sensitive   = TRUE;
		tooltip     = _("Evolution is currently offline.  "
				"Click on this button to work online.");
		break;
	default:
		g_assert_not_reached ();
		return;
	}

	gtk_pixmap_set (GTK_PIXMAP (priv->offline_toggle_pixmap), icon_pixmap, icon_mask);
	gtk_widget_set_sensitive (priv->offline_toggle, sensitive);
	gtk_tooltips_set_tip (priv->tooltips, priv->offline_toggle, tooltip, NULL);
}

/* This displays the specified page, doing the appropriate Bonobo activation/deactivation
   magic to make sure things work nicely.  FIXME: Crappy way to solve the issue.  */
static void
set_current_notebook_page (EShellView *shell_view,
			   int page_num)
{
	EShellViewPrivate *priv;
	GtkNotebook *notebook;
	GtkWidget *current;
	BonoboControlFrame *control_frame;
	int current_page;

	priv = shell_view->priv;
	notebook = GTK_NOTEBOOK (priv->notebook);

	current_page = gtk_notebook_get_current_page (notebook);
	if (current_page == page_num)
		return;

	if (current_page != -1 && current_page != 0) {
		current = gtk_notebook_get_nth_page (notebook, current_page);
		control_frame = bonobo_widget_get_control_frame (BONOBO_WIDGET (current));

		bonobo_control_frame_set_autoactivate (control_frame, FALSE);
		bonobo_control_frame_control_deactivate (control_frame);
	}

	gtk_notebook_set_page (notebook, page_num);

	if (page_num == -1 || page_num == 0)
		return;

	current = gtk_notebook_get_nth_page (notebook, page_num);
	control_frame = bonobo_widget_get_control_frame (BONOBO_WIDGET (current));

	bonobo_control_frame_set_autoactivate (control_frame, FALSE);
	bonobo_control_frame_control_activate (control_frame);
}

static void
setup_corba_interface (EShellView *shell_view,
		       GtkWidget *control)
{
	EShellViewPrivate *priv;
	BonoboControlFrame *control_frame;
	EvolutionShellView *corba_interface;

	g_return_if_fail (control != NULL);

	priv = shell_view->priv;

	control_frame = bonobo_widget_get_control_frame (BONOBO_WIDGET (control));
	corba_interface = evolution_shell_view_new ();

	gtk_signal_connect_while_alive (GTK_OBJECT (corba_interface), "set_message",
					GTK_SIGNAL_FUNC (corba_interface_set_message_cb),
					shell_view, GTK_OBJECT (shell_view));
	gtk_signal_connect_while_alive (GTK_OBJECT (corba_interface), "unset_message",
					GTK_SIGNAL_FUNC (corba_interface_unset_message_cb),
					shell_view, GTK_OBJECT (shell_view));
	gtk_signal_connect_while_alive (GTK_OBJECT (corba_interface), "change_current_view",
					GTK_SIGNAL_FUNC (corba_interface_change_current_view_cb),
					shell_view, GTK_OBJECT (shell_view));
	gtk_signal_connect_while_alive (GTK_OBJECT (corba_interface), "set_title",
					GTK_SIGNAL_FUNC (corba_interface_set_title),
					shell_view, GTK_OBJECT (shell_view));
	gtk_signal_connect_while_alive (GTK_OBJECT (corba_interface), "set_folder_bar_label",
					GTK_SIGNAL_FUNC (corba_interface_set_folder_bar_label),
					shell_view, GTK_OBJECT (shell_view));

	bonobo_object_add_interface (BONOBO_OBJECT (control_frame),
				     BONOBO_OBJECT (corba_interface));

	bonobo_object_ref (BONOBO_OBJECT (corba_interface));
	priv->corba_interface = corba_interface;
}


/* Socket destruction handling.  */

static GtkWidget *
find_socket (GtkContainer *container)
{
	GList *children, *tmp;

	children = gtk_container_children (container);
	while (children) {
		if (BONOBO_IS_SOCKET (children->data))
			return children->data;
		else if (GTK_IS_CONTAINER (children->data)) {
			GtkWidget *socket = find_socket (children->data);
			if (socket)
				return socket;
		}
		tmp = children->next;
		g_list_free_1 (children);
		children = tmp;
	}
	return NULL;
}

static void
socket_destroy_cb (GtkWidget *socket_widget, gpointer data)
{
	EShellView *shell_view;
	EShellViewPrivate *priv;
	EFolder *folder;
	GtkWidget *control;
	const char *uri;
	gboolean viewing_closed_uri;
	char *copy_of_uri;

	shell_view = E_SHELL_VIEW (data);
	priv = shell_view->priv;

	uri = (const char *) gtk_object_get_data (GTK_OBJECT (socket_widget), "e_shell_view_folder_uri");

	/* Strdup here as the string will be freed when the socket is destroyed.  */
 	copy_of_uri = g_strdup (uri);

	control = g_hash_table_lookup (priv->uri_to_control, uri);
	if (control == NULL) {
		g_warning ("What?! Destroyed socket for non-existing URI?  -- %s", uri);
		return;
	}

	priv->sockets = g_list_remove (priv->sockets, socket_widget);

	gtk_widget_destroy (control);
	g_hash_table_remove (priv->uri_to_control, uri);

	folder = e_storage_set_get_folder (e_shell_get_storage_set (priv->shell),
					   get_storage_set_path_from_uri (uri));

	/* See if we were actively viewing the uri for the socket that's being closed */
	viewing_closed_uri = !strcmp (uri, e_shell_view_get_current_uri (shell_view));

	if (viewing_closed_uri)
		e_shell_view_display_uri (shell_view, NULL);

	e_shell_component_maybe_crashed (priv->shell,
					 uri,
					 e_folder_get_type_string (folder),
					 shell_view);

	/* We were actively viewing the component that just crashed, so flip to the Inbox */
	if (viewing_closed_uri)
		e_shell_view_display_uri (shell_view, DEFAULT_URI);

	g_free (copy_of_uri);
}


static const char *
get_type_for_storage (EShellView *shell_view,
		      const char *name,
		      const char **physical_uri_return)
{
	EShellViewPrivate *priv;
	EStorageSet *storage_set;
	EStorage *storage;

	priv = shell_view->priv;

	storage_set = e_shell_get_storage_set (priv->shell);
	storage = e_storage_set_get_storage (storage_set, name);
	if (!storage)
		return NULL;

	*physical_uri_return = e_storage_get_toplevel_node_uri (storage);

	return e_storage_get_toplevel_node_type (storage);
}

static const char *
get_type_for_folder (EShellView *shell_view,
		     const char *path,
		     const char **physical_uri_return)
{
	EShellViewPrivate *priv;
	EStorageSet *storage_set;
	EFolder *folder;

	priv = shell_view->priv;

	storage_set = e_shell_get_storage_set (priv->shell);
	folder = e_storage_set_get_folder (storage_set, path);
	if (!folder)
		return NULL;

	*physical_uri_return = e_folder_get_physical_uri (folder);

	return e_folder_get_type_string (folder);
}

/* Create a new view for @uri with @control.  It assumes a view for @uri does not exist yet.  */
static GtkWidget *
get_control_for_uri (EShellView *shell_view,
		     const char *uri)
{
	EShellViewPrivate *priv;
	CORBA_Environment ev;
	EvolutionShellComponentClient *handler_client;
	EFolderTypeRegistry *folder_type_registry;
	GNOME_Evolution_ShellComponent handler;
	Bonobo_UIContainer container;
	GtkWidget *control;
	GtkWidget *socket;
	Bonobo_Control corba_control;
	const char *path;
	const char *slash;
	const char *physical_uri;
	const char *folder_type;
	int destroy_connection_id;

	priv = shell_view->priv;

	path = strchr (uri, ':');
	if (path == NULL)
		return NULL;

	path++;
	if (*path == '\0')
		return NULL;

	/* Hack for My Evolution */
	if (strcmp (path, "/My Evolution") == 0) {
		folder_type = "My Evolution";
		physical_uri = "";
	} else {
		/* FIXME: This code needs to be made more robust.  */
		
		slash = strchr (path + 1, G_DIR_SEPARATOR);
		if (slash == NULL || slash[1] == '\0')
			folder_type = get_type_for_storage (shell_view, path + 1, &physical_uri);
		else
			folder_type = get_type_for_folder (shell_view, path, &physical_uri);
		if (folder_type == NULL)
			return NULL;
	}

	folder_type_registry = e_shell_get_folder_type_registry (e_shell_view_get_shell (shell_view));

	handler_client = e_folder_type_registry_get_handler_for_type (folder_type_registry, folder_type);
	if (handler_client == CORBA_OBJECT_NIL)
		return NULL;

	handler = bonobo_object_corba_objref (BONOBO_OBJECT (handler_client));

	CORBA_exception_init (&ev);

	corba_control = GNOME_Evolution_ShellComponent_createView (handler, physical_uri, folder_type, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		CORBA_exception_free (&ev);
		return NULL;
	}

	CORBA_exception_free (&ev);

	if (corba_control == CORBA_OBJECT_NIL)
		return NULL;

	container = bonobo_ui_component_get_container (priv->ui_component);
	control = bonobo_widget_new_control_from_objref (corba_control, container);

	socket = find_socket (GTK_CONTAINER (control));
	destroy_connection_id = gtk_signal_connect (GTK_OBJECT (socket), "destroy",
						    GTK_SIGNAL_FUNC (socket_destroy_cb),
						    shell_view);
	gtk_object_set_data (GTK_OBJECT (socket),
			     "e_shell_view_destroy_connection_id",
			     GINT_TO_POINTER (destroy_connection_id));
	gtk_object_set_data_full (GTK_OBJECT (socket), "e_shell_view_folder_uri", g_strdup (uri), g_free);

	priv->sockets = g_list_prepend (priv->sockets, socket);

	setup_corba_interface (shell_view, control);

	return control;
}

static gboolean
show_existing_view (EShellView *shell_view,
		    const char *uri,
		    GtkWidget *control)
{
	EShellViewPrivate *priv;
	int notebook_page;

	priv = shell_view->priv;

	notebook_page = gtk_notebook_page_num (GTK_NOTEBOOK (priv->notebook), control);
	g_assert (notebook_page != -1);

	/* A BonoboWidget can be a "zombie" in the sense that its actual
	   control is dead; if it's zombie, we have to recreate it.  */
	if (bonobo_widget_is_dead (BONOBO_WIDGET (control))) {
		GtkWidget *parent;

		parent = control->parent;

		/* Out with the old.  */
		gtk_container_remove (GTK_CONTAINER (parent), control);
		g_hash_table_remove (priv->uri_to_control, uri);

		/* In with the new.  */
		control = get_control_for_uri (shell_view, uri);
		if (control == NULL)
			return FALSE;

		gtk_container_add (GTK_CONTAINER (parent), control);
		g_hash_table_insert (priv->uri_to_control, g_strdup (uri), control);

		/* Show.  */
		gtk_widget_show (control);
	}

	g_free (priv->uri);
	priv->uri = g_strdup (uri);

	set_current_notebook_page (shell_view, notebook_page);

	return TRUE;
}

static gboolean
create_new_view_for_uri (EShellView *shell_view,
			 const char *uri)
{
	GtkWidget *control;
	EShellViewPrivate *priv;
	int page_num;

	priv = shell_view->priv;

	control = get_control_for_uri (shell_view, uri);
	if (control == NULL)
		return FALSE;

	g_free (priv->uri);
	priv->uri = g_strdup (uri);

	gtk_widget_show (control);

	gtk_notebook_append_page (GTK_NOTEBOOK (priv->notebook), control, NULL);

	page_num = gtk_notebook_page_num (GTK_NOTEBOOK (priv->notebook), control);
	g_assert (page_num != -1);
	set_current_notebook_page (shell_view, page_num);

	g_hash_table_insert (priv->uri_to_control, g_strdup (uri), control);

	return TRUE;
}

gboolean
e_shell_view_display_uri (EShellView *shell_view,
			  const char *uri)
{
	EShellViewPrivate *priv;
	GtkWidget *control;
	gboolean retval;

	g_return_val_if_fail (shell_view != NULL, FALSE);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), FALSE);

	priv = shell_view->priv;

	bonobo_window_freeze (BONOBO_WINDOW (shell_view));

	if (uri == NULL) {
		gtk_notebook_remove_page (GTK_NOTEBOOK (priv->notebook), 0);
		gtk_notebook_prepend_page (GTK_NOTEBOOK (priv->notebook), create_label_for_empty_page (), NULL);

		set_current_notebook_page (shell_view, 0);

		g_free (priv->uri);
		priv->uri = NULL;

		retval = TRUE;
		goto end;
	}

	if (strncmp (uri, E_SHELL_URI_PREFIX, E_SHELL_URI_PREFIX_LEN) != 0) {
		retval = FALSE;
		goto end;
	}

	control = g_hash_table_lookup (priv->uri_to_control, uri);
	if (control != NULL) {
		g_assert (GTK_IS_WIDGET (control));
		show_existing_view (shell_view, uri, control);
	} else if (create_new_view_for_uri (shell_view, uri)) {
		priv->delayed_selection = g_strdup (uri);
		gtk_signal_connect_after (GTK_OBJECT (e_shell_get_storage_set (priv->shell)), "new_folder",
					  GTK_SIGNAL_FUNC (new_folder_cb), shell_view);
		retval = FALSE;
		goto end;
	}

	retval = TRUE;

 end:
	g_free (priv->set_folder_uri);
	priv->set_folder_uri = NULL;

	if (priv->set_folder_timeout != 0) {
		gtk_timeout_remove (priv->set_folder_timeout);
		priv->set_folder_timeout = 0;
	}

	update_for_current_uri (shell_view);

	bonobo_window_thaw (BONOBO_WINDOW (shell_view));

	return retval;
}

gboolean
e_shell_view_remove_control_for_uri (EShellView *shell_view,
				     const char *uri)
{
	EShellViewPrivate *priv;
	GtkWidget *control;
	GtkWidget *socket;
	int page_num;

	g_return_val_if_fail (shell_view != NULL, FALSE);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), FALSE);

	priv = shell_view->priv;

	/* Get the control, remove it from our hash of controls */
	control = g_hash_table_lookup (priv->uri_to_control, uri);
	if (control != NULL)
		g_hash_table_remove (priv->uri_to_control, uri);
	else
		return FALSE;

	/* Get the socket, remove it from our list of sockets */
	socket = find_socket (GTK_CONTAINER (control));
	priv->sockets = g_list_remove (priv->sockets, socket);

	/* Remove the notebook page */
	page_num = gtk_notebook_page_num (GTK_NOTEBOOK (priv->notebook),
					  control);
	gtk_notebook_remove_page (GTK_NOTEBOOK (priv->notebook),
				  page_num);

	/* Destroy things, socket first because otherwise shell will
           think the control crashed */
	gtk_widget_destroy (socket);
	gtk_widget_destroy (control);

	return TRUE;
}


void
e_shell_view_set_shortcut_bar_mode (EShellView *shell_view,
				    EShellViewSubwindowMode mode)
{
	EShellViewPrivate *priv;

	g_return_if_fail (shell_view != NULL);
	g_return_if_fail (E_IS_SHELL_VIEW (shell_view));
	g_return_if_fail (mode == E_SHELL_VIEW_SUBWINDOW_STICKY
			  || mode == E_SHELL_VIEW_SUBWINDOW_HIDDEN);

	priv = shell_view->priv;

	if (priv->shortcut_bar_mode == mode)
		return;

	if (mode == E_SHELL_VIEW_SUBWINDOW_STICKY) {
		if (! GTK_WIDGET_VISIBLE (priv->shortcut_frame)) {
			gtk_widget_show (priv->shortcut_frame);
			e_paned_set_position (E_PANED (priv->hpaned), priv->hpaned_position);
		}
	} else {
		if (GTK_WIDGET_VISIBLE (priv->shortcut_frame)) {
			gtk_widget_hide (priv->shortcut_frame);
			/* FIXME this is a private field!  */
			priv->hpaned_position = E_PANED (priv->hpaned)->child1_size;
			e_paned_set_position (E_PANED (priv->hpaned), 0);
		}
	}

	priv->shortcut_bar_mode = mode;

	gtk_signal_emit (GTK_OBJECT (shell_view), signals[SHORTCUT_BAR_MODE_CHANGED], mode);
}

/**
 * e_shell_view_set_folder_bar_mode:
 * @shell_view: 
 * @mode: 
 * 
 * Set the visualization mode for the folder bar's subwindow.
 **/
void
e_shell_view_set_folder_bar_mode (EShellView *shell_view,
				  EShellViewSubwindowMode mode)
{
	EShellViewPrivate *priv;

	g_return_if_fail (shell_view != NULL);
	g_return_if_fail (E_IS_SHELL_VIEW (shell_view));
	g_return_if_fail (mode == E_SHELL_VIEW_SUBWINDOW_STICKY
			  || mode == E_SHELL_VIEW_SUBWINDOW_HIDDEN);

	priv = shell_view->priv;

	if (priv->folder_bar_mode == mode)
		return;

	if (mode == E_SHELL_VIEW_SUBWINDOW_STICKY) {
		if (! GTK_WIDGET_VISIBLE (priv->storage_set_view_box)) {
			gtk_widget_show (priv->storage_set_view_box);
			e_paned_set_position (E_PANED (priv->view_hpaned), priv->view_hpaned_position);
		}

		e_title_bar_set_button_mode (E_TITLE_BAR (priv->storage_set_title_bar),
					     E_TITLE_BAR_BUTTON_MODE_CLOSE);

		e_shell_folder_title_bar_set_clickable (E_SHELL_FOLDER_TITLE_BAR (priv->view_title_bar),
							FALSE);
	} else {
		if (GTK_WIDGET_VISIBLE (priv->storage_set_view_box)) {
			gtk_widget_hide (priv->storage_set_view_box);
			/* FIXME this is a private field!  */
			priv->view_hpaned_position = E_PANED (priv->view_hpaned)->child1_size;
			e_paned_set_position (E_PANED (priv->view_hpaned), 0);
		}

		e_title_bar_set_button_mode (E_TITLE_BAR (priv->storage_set_title_bar),
					     E_TITLE_BAR_BUTTON_MODE_PIN);

		e_shell_folder_title_bar_set_clickable (E_SHELL_FOLDER_TITLE_BAR (priv->view_title_bar),
							TRUE);
	}

        priv->folder_bar_mode = mode;

	gtk_signal_emit (GTK_OBJECT (shell_view), signals[FOLDER_BAR_MODE_CHANGED], mode);
}

EShellViewSubwindowMode
e_shell_view_get_shortcut_bar_mode (EShellView *shell_view)
{
	g_return_val_if_fail (shell_view != NULL, E_SHELL_VIEW_SUBWINDOW_HIDDEN);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), E_SHELL_VIEW_SUBWINDOW_HIDDEN);

	return shell_view->priv->shortcut_bar_mode;
}

EShellViewSubwindowMode
e_shell_view_get_folder_bar_mode (EShellView *shell_view)
{
	g_return_val_if_fail (shell_view != NULL, E_SHELL_VIEW_SUBWINDOW_HIDDEN);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), E_SHELL_VIEW_SUBWINDOW_HIDDEN);

	return shell_view->priv->folder_bar_mode;
}


ETaskBar *
e_shell_view_get_task_bar (EShellView *shell_view)
{
	g_return_val_if_fail (shell_view != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return E_TASK_BAR (shell_view->priv->task_bar);
}

EShell *
e_shell_view_get_shell (EShellView *shell_view)
{
	g_return_val_if_fail (shell_view != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return shell_view->priv->shell;
}

BonoboUIComponent *
e_shell_view_get_bonobo_ui_component (EShellView *shell_view)
{
	g_return_val_if_fail (shell_view != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return shell_view->priv->ui_component;
}

BonoboUIContainer *
e_shell_view_get_bonobo_ui_container (EShellView *shell_view)
{
	g_return_val_if_fail (shell_view != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return shell_view->priv->ui_container;
}

GtkWidget *
e_shell_view_get_appbar (EShellView *shell_view)
{
	g_return_val_if_fail (shell_view != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return shell_view->priv->appbar;
}

/**
 * e_shell_view_get_current_uri:
 * @shell_view: A pointer to an EShellView object
 * 
 * Get the URI currently displayed by this shell view.
 * 
 * Return value: 
 **/
const char *
e_shell_view_get_current_uri (EShellView *shell_view)
{
	g_return_val_if_fail (shell_view != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return shell_view->priv->uri;
}

/**
 * e_shell_view_get_current_path:
 * @shell_view: A pointer to an EShellView object
 * 
 * Get the path of the current displayed folder.
 * 
 * Return value: 
 **/
const char *
e_shell_view_get_current_path (EShellView *shell_view)
{
	const char *current_uri;
	const char *current_path;

	current_uri = e_shell_view_get_current_uri (shell_view);
	if (current_uri == NULL)
		return NULL;

	if (strncmp (current_uri, E_SHELL_URI_PREFIX, E_SHELL_URI_PREFIX_LEN) == 0)
		current_path = current_uri + E_SHELL_URI_PREFIX_LEN;
	else
		current_path = NULL;

	return current_path;
}

static void
save_shortcut_bar_icon_modes (EShellView *shell_view)
{
	EShellViewPrivate *priv;
	EShortcutBar *shortcut_bar;
	int num_groups;
	int group;

	priv = shell_view->priv;
	shortcut_bar = E_SHORTCUT_BAR (priv->shortcut_bar);

	num_groups = e_shortcut_model_get_num_groups (shortcut_bar->model);

	for (group = 0; group < num_groups; group++) {
		char *tmp;

		tmp = g_strdup_printf ("ShortcutBarGroup%dIconMode", group);
		gnome_config_set_int (tmp, e_shortcut_bar_get_view_type (shortcut_bar, group));
		g_free (tmp);
	}
}

static void
load_shortcut_bar_icon_modes (EShellView *shell_view)
{
	EShellViewPrivate *priv;
	EShortcutBar *shortcut_bar;
	int num_groups;
	int group;

	priv = shell_view->priv;
	shortcut_bar = E_SHORTCUT_BAR (priv->shortcut_bar);

	num_groups = e_shortcut_model_get_num_groups (shortcut_bar->model);

	for (group = 0; group < num_groups; group++) {
		char *tmp;
		int iconmode;

		tmp = g_strdup_printf ("ShortcutBarGroup%dIconMode", group);
		iconmode = gnome_config_get_int (tmp);
		g_free (tmp);

		e_shortcut_bar_set_view_type (shortcut_bar, group, iconmode);
	}
}

static char *
get_local_prefix_for_view (EShellView *shell_view,
			   int view_num)
{
	EShellViewPrivate *priv;
	char *prefix;
	const char *local_directory;

	priv = shell_view->priv;

	local_directory = e_shell_get_local_directory (priv->shell);

	prefix = g_strdup_printf ("=%s/config/Shell=/Views/%d/",
				  local_directory, view_num);
	
	return prefix;
}


/**
 * e_shell_view_save_settings:
 * @shell_view: 
 * @prefix: 
 * 
 * Save settings for @shell_view at the specified gnome config @prefix
 * 
 * Return value: TRUE if successful, FALSE if not.
 **/
gboolean
e_shell_view_save_settings (EShellView *shell_view,
			    int view_num)
{
	EShellViewPrivate *priv;
	const char *uri;
	char *prefix;
	char *filename;

	g_return_val_if_fail (shell_view != NULL, FALSE);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), FALSE);

	priv = shell_view->priv;

	prefix = get_local_prefix_for_view (shell_view, view_num);
	g_return_val_if_fail (prefix != NULL, FALSE);

	gnome_config_push_prefix (prefix);

	gnome_config_set_int ("CurrentShortcutsGroupNum", e_shell_view_get_current_shortcuts_group_num (shell_view));
	gnome_config_set_int ("FolderBarMode",      e_shell_view_get_folder_bar_mode (shell_view));
	gnome_config_set_int ("ShortcutBarMode",    e_shell_view_get_shortcut_bar_mode (shell_view));
	gnome_config_set_int ("HPanedPosition",     e_paned_get_position (E_PANED (priv->hpaned)));
	gnome_config_set_int ("ViewHPanedPosition", e_paned_get_position (E_PANED (priv->view_hpaned)));

	uri = e_shell_view_get_current_uri (shell_view);
	if (uri != NULL)
		gnome_config_set_string ("DisplayedURI", uri);
	else
		gnome_config_set_string ("DisplayedURI", DEFAULT_URI);

	save_shortcut_bar_icon_modes (shell_view);

	gnome_config_pop_prefix ();

	/* Save the expanded state for this ShellViews StorageSetView */
	filename = g_strdup_printf ("%s/config/storage-set-view-expanded:view_%d",
				    e_shell_get_local_directory (priv->shell),
				    view_num);
	e_tree_save_expanded_state (E_TREE (priv->storage_set_view),
				    filename);

	g_free (filename);
	g_free (prefix);

	return TRUE;
}

/**
 * e_shell_view_load_settings:
 * @shell_view: 
 * @prefix: 
 * 
 * Load settings for @shell_view at the specified gnome config @prefix
 * 
 * Return value: 
 **/
gboolean
e_shell_view_load_settings (EShellView *shell_view,
			    int view_num)
{
	EShellViewPrivate *priv;
	int val;
	char *stringval;
	char *prefix;
	char *filename;

	g_return_val_if_fail (shell_view != NULL, FALSE);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), FALSE);

	priv = shell_view->priv;

	prefix = get_local_prefix_for_view (shell_view, view_num);
	g_return_val_if_fail (prefix != NULL, FALSE);

	gnome_config_push_prefix (prefix);

	val = gnome_config_get_int ("CurrentShortcutsGroupNum");
	e_shell_view_set_current_shortcuts_group_num (shell_view, val);

	val = gnome_config_get_int ("FolderBarMode");
	e_shell_view_set_folder_bar_mode (shell_view, val);

	val = gnome_config_get_int ("ShortcutBarMode");
	e_shell_view_set_shortcut_bar_mode (shell_view, val);

	val = gnome_config_get_int ("HPanedPosition");
	e_paned_set_position (E_PANED (priv->hpaned), val);

	val = gnome_config_get_int ("ViewHPanedPosition");
	e_paned_set_position (E_PANED (priv->view_hpaned), val);

	stringval = gnome_config_get_string ("DisplayedURI");
	if (! e_shell_view_display_uri (shell_view, stringval))
		e_shell_view_display_uri (shell_view, DEFAULT_URI);
	g_free (stringval);

	load_shortcut_bar_icon_modes (shell_view);

	gnome_config_pop_prefix ();

	/* Load the expanded state for the ShellView's StorageSetView */
	filename = g_strdup_printf ("%s/config/storage-set-view-expanded:view_%d",
				    e_shell_get_local_directory (priv->shell),
				    view_num);

	e_tree_load_expanded_state (E_TREE (priv->storage_set_view),
				    filename);

	g_free (filename);
	g_free (prefix);

	return TRUE;
}


/* FIXME: This function could become static */
void
e_shell_view_set_current_shortcuts_group_num (EShellView *shell_view, int group_num)
{
	EShellViewPrivate *priv;
	EShortcutsView *shortcuts_view;

	g_return_if_fail (shell_view != NULL);
	g_return_if_fail (E_IS_SHELL_VIEW (shell_view));

	priv = shell_view->priv;

	shortcuts_view = E_SHORTCUTS_VIEW (priv->shortcut_bar);

	e_group_bar_set_current_group_num (E_GROUP_BAR (E_SHORTCUT_BAR (shortcuts_view)), group_num, FALSE);
}

int
e_shell_view_get_current_shortcuts_group_num (EShellView *shell_view)
{
	EShellViewPrivate *priv;
	EShortcutsView *shortcuts_view;
	int group;

	g_return_val_if_fail (shell_view != NULL, -1);
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), -1);

	priv = shell_view->priv;

	shortcuts_view = E_SHORTCUTS_VIEW (priv->shortcut_bar);

	group = e_group_bar_get_current_group_num (E_GROUP_BAR (E_SHORTCUT_BAR (shortcuts_view)));

	return group;
}


E_MAKE_TYPE (e_shell_view, "EShellView", EShellView, class_init, init, BONOBO_TYPE_WINDOW)
