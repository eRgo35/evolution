/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * E-shell-view.c: Implements a Shell View of Evolution
 *
 * Authors:
 *   Miguel de Icaza (miguel@helixcode.com)
 *
 * (C) 2000 Helix Code, Inc.
 */
#include <config.h>
#include <gnome.h>
#include "shortcut-bar/e-shortcut-bar.h"
#include "e-util/e-util.h"
#include "e-shell-view.h"
#include "e-shell-view-menu.h"
#include "e-shell-shortcut.h"

#include <bonobo.h>
#include <libgnorba/gnorba.h>

#define PARENT_TYPE gnome_app_get_type ()

static GtkObjectClass *parent_class;

static void
esv_destroy (GtkObject *object)
{
	EShellView *eshell_view = E_SHELL_VIEW (object);

	e_shell_unregister_view (eshell_view->eshell, eshell_view);

	parent_class->destroy (object);
}

static void
e_shell_view_class_init (GtkObjectClass *object_class)
{
	object_class->destroy = esv_destroy;

	parent_class = gtk_type_class (PARENT_TYPE);
}

static void
e_shell_view_setup (EShellView *eshell_view)
{
	/*
	 * FIXME, should load the config if (load_config)....
	 */
	gtk_window_set_default_size (GTK_WINDOW (eshell_view), 600, 400);
}

static void
e_shell_view_setup_shortcut_display (EShellView *eshell_view)
{
	eshell_view->shortcut_bar = e_shortcut_bar_view_new (eshell_view->eshell->shortcut_bar);
	
	eshell_view->shortcut_hpaned = gtk_hpaned_new ();
	gtk_widget_show (eshell_view->shortcut_hpaned);
	gtk_paned_set_position (GTK_PANED (eshell_view->shortcut_hpaned), 100);

	gtk_paned_pack1 (GTK_PANED (eshell_view->shortcut_hpaned),
			 eshell_view->shortcut_bar, FALSE, TRUE);
	gtk_widget_show (eshell_view->shortcut_bar);

	gnome_app_set_contents (GNOME_APP (eshell_view), eshell_view->shortcut_hpaned);

	gtk_signal_connect (
		GTK_OBJECT (eshell_view->shortcut_bar), "item_selected",
		GTK_SIGNAL_FUNC (shortcut_bar_item_selected), eshell_view);
}

static GtkWidget *
get_view (EFolder *efolder)
{
  	GtkWidget *w;
	/*char buffer [80];

	sprintf (buffer, "I am the view for %s\n",
		 e_folder_get_description (efolder));
	
	w = gtk_label_new (buffer);

	*/
  	w = bonobo_widget_new_control ("GOADID:Evolution:FolderBrowser:1.0",
				     NULL);
	gtk_widget_show (w);
	
	return w;

}

void
e_shell_view_set_view (EShellView *eshell_view, EFolder *efolder)
{
	GtkWidget *w = get_view (efolder);

	if (eshell_view->contents){
		gtk_widget_destroy (eshell_view->contents);
	}
	
	eshell_view->contents = w;

	if (eshell_view->shortcut_displayed){
		gtk_paned_pack2 (GTK_PANED (eshell_view->shortcut_hpaned),
				 eshell_view->contents, FALSE, TRUE);
	} else {
		gnome_app_set_contents (GNOME_APP (eshell_view), eshell_view->contents);
	}
}

GtkWidget *
e_shell_view_new (EShell *eshell, EFolder *efolder, gboolean show_shortcut_bar)
{
	EShellView *eshell_view;

	eshell_view = gtk_type_new (e_shell_view_get_type ());

	gnome_app_construct (GNOME_APP (eshell_view), "Evolution", "Evolution");

	eshell_view->eshell = eshell;
	e_shell_view_setup (eshell_view);
	e_shell_view_setup_menus (eshell_view);

	e_shell_register_view (eshell, eshell_view);
	eshell_view->shortcut_displayed = show_shortcut_bar;
	e_shell_view_setup_shortcut_display (eshell_view);

	e_shell_view_set_view (eshell_view, efolder);
	
	return (GtkWidget *) eshell_view;
}

void
e_shell_view_display_shortcut_bar (EShellView *eshell_view, gboolean display)
{
	g_return_if_fail (eshell_view != NULL);
	g_return_if_fail (E_IS_SHELL_VIEW (eshell_view));

	g_error ("Switching code for the shortcut bar is not written yet");
}

E_MAKE_TYPE (e_shell_view, "EShellView", EShellView, e_shell_view_class_init, NULL, PARENT_TYPE);

void
e_shell_view_new_folder (EShellView *esv)
{
	g_return_if_fail (esv != NULL);
	g_return_if_fail (E_IS_SHELL_VIEW (esv));
}

void
e_shell_view_new_shortcut (EShellView *esv)
{
	g_return_if_fail (esv != NULL);
	g_return_if_fail (E_IS_SHELL_VIEW (esv));
}
