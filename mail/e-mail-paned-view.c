/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>  
 *
 *
 * Authors:
 *		Srinivasa Ragavan <sragavan@gnome.org>
 *
 * Copyright (C) 2010 Intel corporation. (www.intel.com)
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <glib.h>
#include <glib/gi18n.h>
#include "e-mail-pane.h"

#include "e-util/e-util-private.h"
#include "e-util/e-binding.h"
#include "e-util/gconf-bridge.h"
#include "widgets/menus/gal-view-etable.h"
#include "widgets/menus/gal-view-instance.h"
#include "widgets/misc/e-paned.h"
#include "widgets/misc/e-preview-pane.h"
#include "widgets/misc/e-search-bar.h"

#include "em-utils.h"
#include "mail-config.h"
#include "mail-ops.h"
#include "message-list.h"

G_DEFINE_TYPE (EMailPanedView, e_mail_paned_view, GTK_TYPE_VBOX)

struct _EMailPanePrivate {
	GtkWidget *paned;
	GtkWidget *scrolled_window;
	GtkWidget *message_list;
	GtkWidget *search_bar;

	EMFormatHTMLDisplay *formatter;
	GalViewInstance *view_instance;
	GtkOrientation orientation;

	/* ETable scrolling hack */
	gdouble default_scrollbar_position;

	guint paned_binding_id;

	/* Signal handler IDs */
	guint message_list_built_id;

	guint preview_visible	: 1;
	guint show_deleted	: 1;
};

enum {
	PANE_CLOSE,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_GROUP_BY_THREADS,
	PROP_ORIENTATION,
	PROP_PREVIEW_VISIBLE,
	PROP_SHOW_DELETED
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
e_mail_paned_view_init (EMailPanedView  *shell)
{
	shell->priv = g_new0(EMailPanedViewPrivate, 1);
}

static void
mail_paned_view_dispose (GObject *object)
{
	EMailPanedViewPrivate *priv;

	priv = E_MAIL_PANED_VIEW(object)->priv;

	if (priv->paned != NULL) {
		g_object_unref (priv->paned);
		priv->paned = NULL;
	}

	if (priv->scrolled_window != NULL) {
		g_object_unref (priv->scrolled_window);
		priv->scrolled_window = NULL;
	}

	if (priv->message_list != NULL) {
		g_object_unref (priv->message_list);
		priv->message_list = NULL;
	}

	if (priv->search_bar != NULL) {
		g_object_unref (priv->search_bar);
		priv->search_bar = NULL;
	}

	if (priv->formatter != NULL) {
		g_object_unref (priv->formatter);
		priv->formatter = NULL;
	}

	if (priv->view_instance != NULL) {
		g_object_unref (priv->view_instance);
		priv->view_instance = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
mail_paned_view_constructed (GObject *object)
{
	EMailPanedViewPrivate *priv;
	EShellContent *shell_content;
	EShellBackend *shell_backend;
	EShellWindow *shell_window;
	EShellView *shell_view;
	ESearchBar *search_bar;
	EMailReader *reader;
	GtkWidget *message_list;
	GtkWidget *container;
	GtkWidget *widget;
	EWebView *web_view;

	priv = E_MAIL_PANED_VIEW (object)->priv;
	priv->formatter = em_format_html_display_new ();

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (parent_class)->constructed (object);

	shell_content = E_MAIL_VIEW (object)->content;
	shell_view = e_shell_content_get_shell_view (shell_content);
	shell_window = e_shell_view_get_shell_window (shell_view);
	shell_backend = e_shell_view_get_shell_backend (shell_view);

	web_view = em_format_html_get_web_view (
		EM_FORMAT_HTML (priv->formatter));

	/* Build content widgets. */

	container = GTK_WIDGET (object);

	widget = e_paned_new (GTK_ORIENTATION_VERTICAL);
	gtk_container_add (GTK_CONTAINER (container), widget);
	priv->paned = g_object_ref (widget);
	gtk_widget_show (widget);

	e_binding_new (object, "orientation", widget, "orientation");

	container = priv->paned;

	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (
		GTK_SCROLLED_WINDOW (widget),
		GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_shadow_type (
		GTK_SCROLLED_WINDOW (widget), GTK_SHADOW_IN);
	priv->scrolled_window = g_object_ref (widget);
	gtk_paned_pack1 (GTK_PANED (container), widget, TRUE, FALSE);
	gtk_widget_show (widget);

	container = widget;

	widget = message_list_new (shell_backend);
	gtk_container_add (GTK_CONTAINER (container), widget);
	priv->message_list = g_object_ref (widget);
	gtk_widget_show (widget);

	container = priv->paned;

	gtk_widget_show (GTK_WIDGET (web_view));

	widget = e_preview_pane_new (web_view);
	gtk_paned_pack2 (GTK_PANED (container), widget, FALSE, FALSE);
	gtk_widget_show (widget);

	e_binding_new (object, "preview-visible", widget, "visible");

	search_bar = e_preview_pane_get_search_bar (E_PREVIEW_PANE (widget));
	priv->search_bar = g_object_ref (search_bar);

	g_signal_connect_swapped (
		search_bar, "changed",
		G_CALLBACK (em_format_redraw), priv->formatter);

	/* Load the view instance. */

	e_mail_shell_content_update_view_instance (
		E_MAIL_SHELL_CONTENT (shell_content));

	/* Message list customizations. */

	reader = E_MAIL_READER (shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	g_signal_connect_swapped (
		message_list, "message-selected",
		G_CALLBACK (mail_shell_content_message_selected_cb),
		shell_content);

	/* Restore pane positions from the last session once
	 * the shell view is fully initialized and visible. */
	g_signal_connect (
		shell_window, "shell-view-created::mail",
		G_CALLBACK (mail_shell_content_restore_state_cb),
		shell_content);

	e_mail_reader_connect_headers (reader);
}

static void
e_mail_paned_view_class_init (EMailPanedViewClass *klass)
{
	GObjectClass * object_class = G_OBJECT_CLASS (klass);

	e_mail_paned_view_parent_class = g_type_class_peek_parent (klass);
	object_class->finalize = e_mail_paned_view_finalize;
	object_class->dispose = mail_paned_view_dispose;
	object_class->constructed = mail_paned_view_constructed;


	signals[PANE_CLOSE] =
		g_signal_new ("pane-close",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EMailPanedViewClass , view_close),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);


	/* Inherited from EMailReader */
	g_object_class_override_property (
		object_class,
		PROP_GROUP_BY_THREADS,
		"group-by-threads");

	g_object_class_install_property (
		object_class,
		PROP_PREVIEW_VISIBLE,
		g_param_spec_boolean (
			"preview-visible",
			"Preview is Visible",
			"Whether the preview pane is visible",
			TRUE,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_SHOW_DELETED,
		g_param_spec_boolean (
			"show-deleted",
			"Show Deleted",
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	g_object_class_override_property (
		object_class, PROP_ORIENTATION, "orientation");
}

static void
mail_pane_reader_init (EMailReaderIface *iface)
{
	iface->get_action_group = mail_pane_get_action_group;
	iface->get_formatter = mail_pane_get_formatter;
	iface->get_hide_deleted = mail_pane_get_hide_deleted;
	iface->get_message_list = mail_pane_get_message_list;
	iface->get_popup_menu = mail_pane_get_popup_menu;
	iface->get_shell_backend = mail_pane_get_shell_backend;
	iface->get_window = mail_pane_get_window;
	iface->set_folder = mail_pane_set_folder;
	iface->show_search_bar = mail_pane_show_search_bar;
}

GtkWidget *
e_mail_paned_view_new (EShellContent *content)
{
	g_return_val_if_fail (E_IS_SHELL_CONTENT (content), NULL);

	return g_object_new (
		E_TYPE_MAIL_PANED_VIEW,
		"shell-content", content, NULL);
}

gboolean
e_mail_paned_view_get_preview_visible (EMailPanedView *view)
{
	g_return_val_if_fail (
		E_IS_MAIL_PANED_VIEW (view), FALSE);

	return view->priv->preview_visible;
}

void
e_mail_paned_view_set_preview_visible (EMailPanedView *view,
                                          gboolean preview_visible)
{
	g_return_if_fail (E_IS_MAIL_PANED_VIEW (view));

	if (preview_visible == view->priv->preview_visible)
		return;

	/* If we're showing the preview, tell EMailReader to reload the
	 * selected message.  This should force it to download the full
	 * message if necessary, so we don't get an empty preview. */
	if (preview_visible) {
		EMailReader *reader;
		GtkWidget *message_list;
		const gchar *cursor_uid;

		reader = E_MAIL_READER (view);
		message_list = e_mail_reader_get_message_list (reader);
		cursor_uid = MESSAGE_LIST (message_list)->cursor_uid;

		if (cursor_uid != NULL)
			e_mail_reader_set_message (reader, cursor_uid);
	}

	view->priv->preview_visible = preview_visible;

	mail_paned_view_save_boolean (
		view,
		STATE_KEY_PREVIEW_VISIBLE, preview_visible);

	g_object_notify (G_OBJECT (view), "preview-visible");
}

EShellSearchbar *
e_mail_paned_view_get_searchbar (EMailPanedView *view)
{
	EShellView *shell_view;
	EShellContent *shell_content;
	GtkWidget *widget;

	g_return_val_if_fail (
		E_IS_MAIL_PANED_VIEW (view), NULL);

	shell_content = E_PANED_VIEW (view)->content;
	shell_view = e_shell_content_get_shell_view (shell_content);
	widget = e_shell_view_get_searchbar (shell_view);

	return E_SHELL_SEARCHBAR (widget);
}

gboolean
e_mail_paned_view_get_show_deleted (EMailPanedView *view)
{
	g_return_val_if_fail (
		E_IS_MAIL_PANED_VIEW (view), FALSE);

	return view->priv->show_deleted;
}

void
e_mail_paned_view_set_show_deleted (EMailPanedView *view,
                                       gboolean show_deleted)
{
	g_return_if_fail (E_IS_MAIL_PANED_VIEW (view));

	view->priv->show_deleted = show_deleted;

	g_object_notify (G_OBJECT (view), "show-deleted");
}

GalViewInstance *
e_mail_paned_view_get_view_instance (EMailPanedView *view)
{
	g_return_val_if_fail (
		E_IS_MAIL_PANED_VIEW (view), NULL);

	return view->priv->view_instance;
}

void
e_mail_paned_view_set_search_strings (EmailPanedView *view,
                                         GSList *search_strings)
{
	ESearchBar *search_bar;
	ESearchingTokenizer *tokenizer;

	g_return_if_fail (E_IS_MAIL_PANED_VIEW (view));

	search_bar = E_SEARCH_BAR (view->priv->search_bar);
	tokenizer = e_search_bar_get_tokenizer (search_bar);

	e_searching_tokenizer_set_secondary_case_sensitivity (tokenizer, FALSE);
	e_searching_tokenizer_set_secondary_search_string (tokenizer, NULL);

	while (search_strings != NULL) {
		e_searching_tokenizer_add_secondary_search_string (
			tokenizer, search_strings->data);
		search_strings = g_slist_next (search_strings);
	}

	e_search_bar_changed (search_bar);
}

static void
mail_paned_display_view_cb (EMailPanedView *view,
                                    GalView *gal_view)
{
	EMailReader *reader;
	GtkWidget *message_list;

	reader = E_MAIL_READER (view);
	message_list = e_mail_reader_get_message_list (reader);

	if (GAL_IS_VIEW_ETABLE (gal_view))
		gal_view_etable_attach_tree (
			GAL_VIEW_ETABLE (gal_view),
			E_TREE (message_list));
}

void
e_mail_shell_content_update_view_instance (EMailPanedView *view)
{
	EMailReader *reader;
	EShell *shell;
	EShellContent *shell_content;
	EShellView *shell_view;
	EShellWindow *shell_window;
	EShellViewClass *shell_view_class;
	EShellSettings *shell_settings;
	GalViewCollection *view_collection;
	GalViewInstance *view_instance;
	CamelFolder *folder;
	GtkOrientable *orientable;
	GtkOrientation orientation;
	gboolean outgoing_folder;
	gboolean show_vertical_view;
	const gchar *folder_uri;
	gchar *view_id;

	g_return_if_fail (E_IS_MAIL_SHELL_CONTENT (mail_shell_content));

	shell_content = E_MAIL_VIEW(view)->content;
	shell_view = e_shell_content_get_shell_view (shell_content);
	shell_view_class = E_SHELL_VIEW_GET_CLASS (shell_view);
	view_collection = shell_view_class->view_collection;

	shell_window = e_shell_view_get_shell_window (shell_view);
	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);

	reader = E_MAIL_READER (view);
	folder = e_mail_reader_get_folder (reader);
	folder_uri = e_mail_reader_get_folder_uri (reader);

	/* If no folder is selected, return silently. */
	if (folder == NULL)
		return;

	/* If we have a folder, we should also have a URI. */
	g_return_if_fail (folder_uri != NULL);

	if (view->priv->view_instance != NULL) {
		g_object_unref (view->priv->view_instance);
		view->priv->view_instance = NULL;
	}

	view_id = mail_config_folder_to_safe_url (folder);
	if (e_shell_settings_get_boolean (shell_settings, "mail-global-view-setting"))
		view_instance = e_shell_view_new_view_instance (shell_view, "global_view_setting");
	else
		view_instance = e_shell_view_new_view_instance (shell_view, view_id);

	view->priv->view_instance = view_instance;

	orientable = GTK_ORIENTABLE (view);
	orientation = gtk_orientable_get_orientation (orientable);
	show_vertical_view = (orientation == GTK_ORIENTATION_HORIZONTAL);

	if (show_vertical_view) {
		gchar *filename;
		gchar *safe_view_id;

		/* Force the view instance into vertical view. */

		g_free (view_instance->custom_filename);
		g_free (view_instance->current_view_filename);

		safe_view_id = g_strdup (view_id);
		e_filename_make_safe (safe_view_id);

		filename = g_strdup_printf (
			"custom_wide_view-%s.xml", safe_view_id);
		view_instance->custom_filename = g_build_filename (
			view_collection->local_dir, filename, NULL);
		g_free (filename);

		filename = g_strdup_printf (
			"current_wide_view-%s.xml", safe_view_id);
		view_instance->current_view_filename = g_build_filename (
			view_collection->local_dir, filename, NULL);
		g_free (filename);

		g_free (safe_view_id);
	}

	g_free (view_id);

	outgoing_folder =
		em_utils_folder_is_drafts (folder, folder_uri) ||
		em_utils_folder_is_outbox (folder, folder_uri) ||
		em_utils_folder_is_sent (folder, folder_uri);

	if (outgoing_folder) {
		if (show_vertical_view)
			gal_view_instance_set_default_view (
				view_instance, "Wide_View_Sent");
		else
			gal_view_instance_set_default_view (
				view_instance, "As_Sent_Folder");
	} else if (show_vertical_view) {
		gal_view_instance_set_default_view (
			view_instance, "Wide_View_Normal");
	}

	gal_view_instance_load (view_instance);

	if (!gal_view_instance_exists (view_instance)) {
		gchar *state_filename;

		state_filename = mail_config_folder_to_cachename (
			folder, "et-header-");

		if (g_file_test (state_filename, G_FILE_TEST_IS_REGULAR)) {
			ETableSpecification *spec;
			ETableState *state;
			GalView *view;
			gchar *spec_filename;

			spec = e_table_specification_new ();
			spec_filename = g_build_filename (
				EVOLUTION_ETSPECDIR,
				"message-list.etspec",
				NULL);
			e_table_specification_load_from_file (
				spec, spec_filename);
			g_free (spec_filename);

			state = e_table_state_new ();
			view = gal_view_etable_new (spec, "");

			e_table_state_load_from_file (
				state, state_filename);
			gal_view_etable_set_state (
				GAL_VIEW_ETABLE (view), state);
			gal_view_instance_set_custom_view (
				view_instance, view);

			g_object_unref (state);
			g_object_unref (view);
			g_object_unref (spec);
		}

		g_free (state_filename);
	}

	g_signal_connect_swapped (
		view_instance, "display-view",
		G_CALLBACK (mail_paned_display_view_cb),
		view);

	mail_paned_display_view_cb (
		view,
		gal_view_instance_get_current_view (view_instance));
}
