/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* mail-config.c: Mail configuration dialogs/wizard. */

/* 
 * Authors: 
 *  Dan Winship <danw@helixcode.com>
 *  Jeffrey Stedfast <fejj@helixcode.com>
 *  JP Rosevear <jpr@helixcode.com>
 *
 * Copyright 2000 Helix Code, Inc. (http://www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <pwd.h>

#include <gnome.h>
#include <gtkhtml/gtkhtml.h>
#include <glade/glade.h>

#include "e-util/e-html-utils.h"
#include "e-util/e-unicode.h"
#include "mail.h"
#include "mail-threads.h"
#include "mail-config.h"
#include "mail-config-gui.h"

typedef struct _MailDialogIdentityPage MailDialogIdentityPage;
typedef struct _MailDialogServicePage MailDialogServicePage;

typedef void (*IdentityPageCallback) (MailDialogIdentityPage *, gpointer);
typedef void (*ServicePageCallback) (MailDialogServicePage *, gpointer);

typedef struct 
{
	CamelProvider *provider;
	CamelService *service;
	CamelProviderType type;
	GList *authtypes;
} MailService;

struct _MailDialogIdentityPage
{
	GtkWidget *vbox;
	GtkWidget *name;
	GtkWidget *address;
	GtkWidget *org;
	GtkWidget *sig;
	IdentityPageCallback undonecb;
	gpointer undonedata;
	IdentityPageCallback donecb;
	gpointer donedata;
};

typedef struct
{
	GtkWidget *item;
	GtkWidget *vbox;
	CamelProviderType type;
	gchar *protocol;
	GtkWidget *user;
	gboolean userneed;
	GtkWidget *host;
	gboolean hostneed;
	GtkWidget *port;
	GtkWidget *path;
	gboolean pathneed;
	GtkWidget *auth_optionmenu;
	GList *auth_items;
	GtkWidget *auth_html;
	GtkWidget *auth_detect;
	GtkWidget *keep_on_server;
	gint pnum;
	gint default_port;
} MailDialogServicePageItem;

struct _MailDialogServicePage
{
	GtkWidget *vbox;
	GtkWidget *optionmenu;
	GList *items;
	GtkWidget *notebook;
	MailDialogServicePageItem *spitem;
	ServicePageCallback changedcb;
	gpointer changeddata;
	ServicePageCallback undonecb;
	gpointer undonedata;
	ServicePageCallback donecb;
	gpointer donedata;
};

typedef struct
{
	GtkWidget *vbox;
	MailDialogServicePage *page;
} MailDialogSourcePage;

typedef struct
{
	GtkWidget *vbox;
	MailDialogServicePage *page;
} MailDialogNewsPage;

typedef struct
{
	GtkWidget *vbox;
	MailDialogServicePage *page;
} MailDialogTransportPage;

typedef struct 
{
	GtkWidget *dialog;
	MailDialogIdentityPage *page;
	MailConfigIdentity *id;
} MailDialogIdentity;

typedef struct
{
	GtkWidget *dialog;
	MailDialogSourcePage *page;
	MailConfigService *source;
} MailDialogSource;

typedef struct
{
	GtkWidget *dialog;
	MailDialogNewsPage *page;
	MailConfigService *source;
} MailDialogNews;

typedef struct
{
	Evolution_Shell shell;
	GladeXML *gui;
	GtkWidget *dialog;
	GtkWidget *druid;
	MailDialogIdentityPage *idpage;
	gboolean iddone;
	MailDialogSourcePage *spage;
	gboolean sdone;
	MailDialogTransportPage *tpage;
	gboolean tdone;
} MailDruidDialog;

typedef struct
{
	Evolution_Shell shell;
	GladeXML *gui;
	GtkWidget *dialog;
	GtkWidget *clistIdentities;
	gint idrow;
	gint maxidrow;
	GtkWidget *clistSources;
	gint srow;
	gint maxsrow;
	GtkWidget *clistNews;
	gint nrow;
	gint maxnrow;
	MailDialogTransportPage *page;
	gboolean tpagedone;
	GtkWidget *chkFormat;
	GtkWidget *spinTimeout;
} MailDialog;

/* private prototypes - these are ugly, rename some of them? */
static void config_do_test_service (const char *url, CamelProviderType type);

static void html_size_req (GtkWidget *widget, GtkRequisition *requisition);
static GtkWidget *html_new (gboolean white);
static void put_html (GtkHTML *html, char *text);
static void error_dialog (GtkWidget *parent_finder, const char *fmt, ...);
static GdkImlibImage *load_image (const char *name);
static void service_page_menuitem_activate (GtkWidget *item, 
					    MailDialogServicePage *page);
static void service_page_item_changed (GtkWidget *item, 
				       MailDialogServicePage *page);
static void service_page_item_auth_activate (GtkWidget *menuitem, 
					     MailDialogServicePageItem *spitem);


/* HTML Helpers */
static void
html_size_req (GtkWidget *widget, GtkRequisition *requisition)
{
	 requisition->height = GTK_LAYOUT (widget)->height;
}

/* Returns a GtkHTML which is already inside a GtkScrolledWindow. If
 * @white is TRUE, the GtkScrolledWindow will be inside a GtkFrame.
 */
static GtkWidget *
html_new (gboolean white)
{
	GtkWidget *html, *scrolled, *frame;
	GtkStyle *style;
	
	html = gtk_html_new ();
	GTK_LAYOUT (html)->height = 0;
	gtk_signal_connect (GTK_OBJECT (html), "size_request",
			    GTK_SIGNAL_FUNC (html_size_req), NULL);
	gtk_html_set_editable (GTK_HTML (html), FALSE);
	style = gtk_rc_get_style (html);
	if (style) {
		gtk_html_set_default_background_color (GTK_HTML (html),
						       white ? &style->white :
						       &style->bg[0]);
	}
	gtk_widget_set_sensitive (html, FALSE);
	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
					GTK_POLICY_NEVER,
					GTK_POLICY_NEVER);
	gtk_container_add (GTK_CONTAINER (scrolled), html);
	
	if (white) {
		frame = gtk_frame_new (NULL);
		gtk_frame_set_shadow_type (GTK_FRAME (frame),
					   GTK_SHADOW_ETCHED_IN);
		gtk_container_add (GTK_CONTAINER (frame), scrolled);
		gtk_widget_show_all (frame);
	} else
		gtk_widget_show_all (scrolled);
	
	return html;
}

static void
put_html (GtkHTML *html, char *text)
{
	GtkHTMLStream *handle;
	
	text = e_text_to_html (text, E_TEXT_TO_HTML_CONVERT_NL);
	handle = gtk_html_begin (html);
	gtk_html_write (html, handle, "<HTML><BODY>", 12);
	gtk_html_write (html, handle, text, strlen (text));
	gtk_html_write (html, handle, "</BODY></HTML>", 14);
	g_free (text);
	gtk_html_end (html, handle, GTK_HTML_STREAM_OK);
}


/* Standard Dialog Helpers */
static void
error_dialog (GtkWidget *parent_finder, const char *fmt, ...)
{
	GtkWidget *parent, *dialog;
	char *msg;
	va_list ap;
	
	parent = gtk_widget_get_ancestor (parent_finder, GTK_TYPE_WINDOW);
	
	va_start (ap, fmt);
	msg = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	
	dialog = gnome_error_dialog_parented (msg, GTK_WINDOW (parent));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	g_free (msg);
}

#if 0
static void
info_dialog (GtkWidget *parent_finder, const char *fmt, ...)
{
	GtkWidget *parent, *dialog;
	char *msg;
	va_list ap;
	
	parent = gtk_widget_get_ancestor (parent_finder, GTK_TYPE_WINDOW);
	
	va_start (ap, fmt);
	msg = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	
	dialog = gnome_ok_dialog_parented (msg, GTK_WINDOW (parent));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	g_free (msg);
}
#endif

/* Provider List */
static GSList *
provider_list_add (GSList *services, CamelProviderType type, 
		   CamelProvider *prov)
{
	MailService *mcs;
	CamelService *service;
	CamelException *ex;
	char *url;

	ex = camel_exception_new ();

	url = g_strdup_printf ("%s:", prov->protocol);
	service = camel_session_get_service (session, url, type, ex);
	g_free (url);
	if (!service) {
		camel_exception_free (ex);
		return services;
	}

	mcs = g_new (MailService, 1);
	mcs->provider = prov;
	mcs->service = service;
	mcs->type = type;
	mcs->authtypes = camel_service_query_auth_types (mcs->service, ex);
	camel_exception_free (ex);

	return g_slist_prepend (services, mcs);
}

static void 
provider_list (GSList **sources, GSList **news, GSList **transports)
{
	GList *providers, *p;
	
	/* Fetch list of all providers. */
	providers = camel_session_list_providers (session, TRUE);
	*sources = *transports = *news = NULL;
	for (p = providers; p; p = p->next) {
		CamelProvider *prov = p->data;
		
		if (!strcmp (prov->domain, "news")) {
			if (prov->object_types[CAMEL_PROVIDER_STORE]) {
				*news = provider_list_add (*news,
							   CAMEL_PROVIDER_STORE,
							   prov);
			}
		}
    
		if (strcmp (prov->domain, "mail"))
			continue;
		
		if (prov->object_types[CAMEL_PROVIDER_STORE] &&
		    prov->flags & CAMEL_PROVIDER_IS_SOURCE) {
			*sources = provider_list_add (*sources,
						      CAMEL_PROVIDER_STORE,
						      prov);
		} else if (prov->object_types[CAMEL_PROVIDER_TRANSPORT]) {
			*transports = provider_list_add (*transports,
							 CAMEL_PROVIDER_TRANSPORT,
							 prov);
		}
	}	
}

/* Utility routines */
static GdkImlibImage *
load_image (const char *name)
{
	char *path;
	GdkImlibImage *image;

	path = g_strdup_printf (EVOLUTION_ICONSDIR "/%s", name);
	image = gdk_imlib_load_image (path);
	g_free (path);

	return image;
}

/* Identity Page */
static void
identity_page_changed (GtkWidget *widget, MailDialogIdentityPage *page)
{
	gchar *name, *addr;
	
	name = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (page->name), 0, -1);
	addr = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (page->address), 0, -1);
	
	if (addr && *addr && name && *name && page->donecb)
		page->donecb (page, page->donedata);
	else if (page->undonecb)
		page->undonecb (page, page->undonedata);

	g_free (name);
	g_free (addr);
}

static MailConfigIdentity *
identity_page_extract (MailDialogIdentityPage *page)
{
	MailConfigIdentity *id = g_new0 (MailConfigIdentity, 1);

	id->name = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (page->name), 0, -1);
	id->address = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (page->address), 0, -1);
	id->org = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (page->org), 0, -1);
	id->sig = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (page->sig), 0, -1);

	return id;
}

static void
identity_page_set_undone_cb (MailDialogIdentityPage *page, 
			     IdentityPageCallback cb, gpointer data)
{
	page->undonecb = cb;
	page->undonedata = data;
}

static void
identity_page_set_done_cb (MailDialogIdentityPage *page, 
			   IdentityPageCallback cb, gpointer data)
{
	page->donecb = cb;
	page->donedata = data;
}

static MailDialogIdentityPage *
identity_page_new (const MailConfigIdentity *id)
{
	MailDialogIdentityPage *page = g_new0 (MailDialogIdentityPage, 1);
	GtkWidget *html, *table;
	GtkWidget *label, *fentry, *hsep;
	gchar *user = NULL;
	gboolean new = !id;
	
	page->vbox = gtk_vbox_new (FALSE, 5);
	
	html = html_new (FALSE);
	put_html (GTK_HTML (html),
		  _("Enter your name and email address to be used in "
		    "outgoing mail. You may also, optionally, enter the "
		    "name of your organization, and the name of a file "
		    "to read your signature from."));
	gtk_box_pack_start (GTK_BOX (page->vbox), html->parent, 
			    FALSE, TRUE, 0);

	table = gtk_table_new (5, 2, FALSE);
	gtk_table_set_row_spacings (GTK_TABLE (table), 10);
	gtk_table_set_col_spacings (GTK_TABLE (table), 6);
	gtk_container_set_border_width (GTK_CONTAINER (table), 8);
	gtk_box_pack_start (GTK_BOX (page->vbox), table, FALSE, FALSE, 0);

	label = gtk_label_new (_("Full name:"));
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
			  GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);

	page->name = gtk_entry_new ();
	gtk_table_attach (GTK_TABLE (table), page->name, 1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0, 0, 0);

	if (!id || !id->name)
		user = g_get_real_name ();

	if ((id && id->name) || user) {
		char *name;

		if (id && id->name)
			name = g_strdup (id->name);
		else
			name = g_strdup (user);

		e_utf8_gtk_entry_set_text (GTK_ENTRY (page->name), name);
		g_free (name);
	}

	label = gtk_label_new (_("Email address:"));
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
			  GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);

	page->address = gtk_entry_new ();
	if (id && id->address)
		e_utf8_gtk_entry_set_text (GTK_ENTRY (page->address), id->address);
	gtk_table_attach (GTK_TABLE (table), page->address, 1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0, 0, 0);

	hsep = gtk_hseparator_new ();
	gtk_table_attach (GTK_TABLE (table), hsep, 0, 2, 2, 3,
			  GTK_FILL, 0, 0, 8);

	label = gtk_label_new (_("Organization:"));
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
			  GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);

	page->org = gtk_entry_new ();
	if (id && id->org)
		e_utf8_gtk_entry_set_text (GTK_ENTRY (page->org), id->org);
	gtk_table_attach (GTK_TABLE (table), page->org, 1, 2, 3, 4,
			  GTK_EXPAND | GTK_FILL, 0, 0, 0);

	label = gtk_label_new (_("Signature file:"));
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
			  GTK_FILL, GTK_FILL, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label), 1, 0);

	fentry = gnome_file_entry_new (NULL, _("Signature File"));
	page->sig = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (fentry));
	if (id && id->sig) {
		e_utf8_gtk_entry_set_text (GTK_ENTRY (page->sig), id->sig);
	} else {
		gchar *default_sig;
		
		default_sig = g_strconcat (g_get_home_dir (), 
					   G_DIR_SEPARATOR_S,
					   ".signature", NULL);
		if (g_file_exists (default_sig))
			e_utf8_gtk_entry_set_text (GTK_ENTRY (page->sig), default_sig);
		g_free (default_sig);
	}
	
	gtk_table_attach (GTK_TABLE (table), fentry, 1, 2, 4, 5,
			  GTK_FILL, 0, 0, 0);
	gnome_file_entry_set_default_path (GNOME_FILE_ENTRY (fentry),
					   g_get_home_dir ());
	
	gtk_signal_connect (GTK_OBJECT (page->name), "changed",
			    GTK_SIGNAL_FUNC (identity_page_changed), page);
	gtk_signal_connect (GTK_OBJECT (page->address), "changed",
			    GTK_SIGNAL_FUNC (identity_page_changed), page);
	if (!new) {
		gtk_signal_connect (GTK_OBJECT (page->org), "changed",
				    GTK_SIGNAL_FUNC (identity_page_changed), 
				    page);
		gtk_signal_connect (GTK_OBJECT (page->sig), "changed",
				    GTK_SIGNAL_FUNC (identity_page_changed), 
				    page);
	}

	gtk_widget_show_all (table);

	return page;
}

/* Service page */
static MailDialogServicePageItem *
service_page_item_by_protocol (MailDialogServicePage *page, gchar *protocol)
{
	MailDialogServicePageItem *spitem;
	gint len, i;
	
	len = g_list_length (page->items);
	for (i = 0; i < len; i++) {
		spitem = (MailDialogServicePageItem *)
			g_list_nth_data (page->items, i);
		if (!g_strcasecmp (spitem->protocol, protocol)) 
			return spitem;
	}
	
	return NULL;
}

static MailDialogServicePageItem *
service_page_item_by_menuitem (MailDialogServicePage *page, 
			       GtkWidget *menuitem)
{
	MailDialogServicePageItem *spitem;
	gint len, i;
	
	len = g_list_length (page->items);
	for (i = 0; i < len; i++) {
		spitem = (MailDialogServicePageItem *)
			g_list_nth_data (page->items, i);
		if (spitem->item == menuitem) 
			return spitem;
	}

	return NULL;
}

static char *
service_page_get_url (MailDialogServicePage *page)
{
	MailDialogServicePageItem *spitem;
	CamelURL *url;
	char *url_str;

	spitem = page->spitem;
	
	url = g_new0 (CamelURL, 1);
	url->protocol = g_strdup (spitem->protocol);

	if (spitem->user)
		url->user = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (spitem->user), 0, -1);
	if (spitem->host)
		url->host = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (spitem->host), 0, -1);
	if (spitem->port) {
		gchar *val;

		val = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (spitem->port), 0, -1);

		if (*val)
			url->port = atoi (val);
		else
			url->port = 0;

		g_free (val);
	}
	if (spitem->path) {
		gchar *path;
		path = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (spitem->path),
					       0, -1);
		url->path = g_strdup_printf ("%s%s", url->host ? "/" : "", 
					     path);
		g_free (path);
	}
	
	if (spitem->auth_optionmenu) {
		GtkWidget *menu, *item;
		CamelServiceAuthType *authtype;

		menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (spitem->auth_optionmenu));
		if (menu) {
			item = gtk_menu_get_active (GTK_MENU (menu));
			authtype = gtk_object_get_data (GTK_OBJECT (item),
							"authtype");
			if (*authtype->authproto)
				url->authmech = g_strdup (authtype->authproto);
		}
	}

	url_str = camel_url_to_string (url, FALSE);
	camel_url_free (url);
	
	return url_str;
}

static void
service_page_set_url (MailDialogServicePage *page, MailConfigService *service)
{
	CamelURL *url;
	CamelException *ex;
	MailDialogServicePageItem *spitem = NULL;
	
	if (!service || !service->url)
		return;
	
	ex = camel_exception_new ();
	
	url = camel_url_new (service->url, ex);
	if (camel_exception_get_id (ex) != CAMEL_EXCEPTION_NONE) {
		camel_exception_free (ex);
		return;
	}

	/* Find the right protocol */
	spitem = service_page_item_by_protocol (page, url->protocol);
	service_page_menuitem_activate (spitem->item, page);
	gtk_option_menu_set_history (GTK_OPTION_MENU (page->optionmenu), 
				     spitem->pnum);
	
	if (spitem->user && url && url->user)
		e_utf8_gtk_entry_set_text (GTK_ENTRY (spitem->user), url->user);

	if (spitem->host && url && url->host)
		e_utf8_gtk_entry_set_text (GTK_ENTRY (spitem->host), url->host);
	
	if (spitem->port) {
		gchar *tmp;
			
		if (url && url->port) {
			tmp = g_strdup_printf ("%d", url->port);
		} else if (spitem->default_port) {
			tmp = g_strdup_printf ("%d", spitem->default_port);
		} else {
			tmp = g_strdup ("");
		}

		e_utf8_gtk_entry_set_text (GTK_ENTRY (spitem->port), tmp);
		g_free (tmp);
	}

	if (spitem->path && url && url->path) {
		if (url->host && *url->path)
			e_utf8_gtk_entry_set_text (GTK_ENTRY (spitem->path),
						   url->path + 1);
		else
			e_utf8_gtk_entry_set_text (GTK_ENTRY (spitem->path), 
						   url->path);
	}

	/* Set the auth menu */
	if (spitem->auth_optionmenu) {
		GtkWidget *menu, *item;
		CamelServiceAuthType *authtype;
		gint len, i;
		
		menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (spitem->auth_optionmenu));
		len = g_list_length (spitem->auth_items);
		for (i = 0; i < len; i++) {
			item = g_list_nth_data (spitem->auth_items, i);
			authtype = gtk_object_get_data (GTK_OBJECT (item),
							"authtype");
			
			if ((!url->authmech && !*authtype->authproto) ||
			    (url->authmech && 
			     !strcmp (authtype->authproto, url->authmech))) {
				
				service_page_item_auth_activate (item, spitem);
				gtk_option_menu_set_history (GTK_OPTION_MENU (spitem->auth_optionmenu), i);
			}
		}
	}

	if (spitem->keep_on_server)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (spitem->keep_on_server), service->keep_on_server);

	camel_exception_free (ex);
	camel_url_free (url);
}

static void
service_page_item_auth_activate (GtkWidget *menuitem, 
				 MailDialogServicePageItem *spitem)
{
	CamelServiceAuthType *authtype;

	authtype = gtk_object_get_data (GTK_OBJECT (menuitem), "authtype");
	put_html (GTK_HTML (spitem->auth_html), 
		  authtype->description);
}

static void
service_page_item_auth_fill (MailDialogServicePage *page,
			     MailDialogServicePageItem *spitem, 
			     GList *authtypes)
{
	CamelServiceAuthType *authtype;
	GtkWidget *menu, *item, *firstitem = NULL;

	menu = gtk_menu_new ();
	for (; authtypes; authtypes = authtypes->next) {
		authtype = authtypes->data;

		item = e_utf8_gtk_menu_item_new_with_label (_(authtype->name));
		if (!firstitem)
			firstitem = item;
		spitem->auth_items = g_list_append (spitem->auth_items, item);

		gtk_menu_append (GTK_MENU (menu), item);
		gtk_object_set_data (GTK_OBJECT (item), "authtype", authtype);

		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (service_page_item_auth_activate),
				    spitem);
		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (service_page_item_changed),
				    page);
	}
	gtk_widget_show_all (menu);

	gtk_option_menu_set_menu (GTK_OPTION_MENU (spitem->auth_optionmenu), 
				  menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (spitem->auth_optionmenu), 0);
	if (firstitem)
		service_page_item_auth_activate (firstitem, spitem);
}

static void
service_acceptable (MailDialogServicePage *page)
{
	char *url;

	url = service_page_get_url (page);
	config_do_test_service (url, page->spitem->type);
	g_free (url);
}

static MailConfigService *
service_page_extract (MailDialogServicePage *page)
{
	MailConfigService *source = g_new0 (MailConfigService, 1);
	MailDialogServicePageItem *spitem = page->spitem;

	source->url = service_page_get_url (page);
	if (spitem->keep_on_server) {
		source->keep_on_server = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (spitem->keep_on_server));
	}

	return source;
}

static void
service_page_item_changed (GtkWidget *item, MailDialogServicePage *page) 
{
	MailDialogServicePageItem *spitem;
	char *data;
	gboolean complete = TRUE;

	spitem = page->spitem;

	if (complete && page->changedcb) {
		page->changedcb (page, page->changeddata);
	}
	
	if (spitem->host && spitem->hostneed) {
		data = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (spitem->host), 0, -1);
		if (!data || !*data)
			complete = FALSE;
		g_free (data);
	}

	if (complete) {
		if (spitem->user && spitem->userneed) {
			data = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (spitem->user), 0, -1);
			if (!data || !*data)
				complete = FALSE;
			g_free (data);
		}
	}

	if (complete) {
		if (spitem->path && spitem->pathneed) {
			data = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (spitem->path), 0, -1);
			if (!data || !*data)
				complete = FALSE;
			g_free (data);
		}
	}

	if (spitem->auth_detect)
		gtk_widget_set_sensitive (spitem->auth_detect, complete);

	if (complete && page->donecb) {
		page->donecb (page, page->donedata);
	} else if (!complete && page->undonecb) {
		page->undonecb (page, page->undonedata);
	}
}

static void
service_page_detect (GtkWidget *button, MailDialogServicePage *page)
{
	MailDialogServicePageItem *spitem;
	char *url = NULL;
	CamelException *ex;
	CamelService *service;
	GList *authtypes;

	spitem = page->spitem;
	url = service_page_get_url (page);

	ex = camel_exception_new ();
	service = camel_session_get_service (session, url, spitem->type, ex);
	g_free (url);
	if (camel_exception_get_id (ex) != CAMEL_EXCEPTION_NONE)
		goto error;

	authtypes = camel_service_query_auth_types (service, ex);
	if (camel_exception_get_id (ex) != CAMEL_EXCEPTION_NONE)
		goto error;

	service_page_item_auth_fill (page, spitem, authtypes);

	camel_exception_free (ex);

	return;

 error:
	error_dialog (button, "Could not detect supported authentication "
		      "types:\n%s", camel_exception_get_description (ex));
	camel_exception_free (ex);
}

static void
service_page_item_test (GtkWidget *button, MailDialogServicePage *page) 
{
	service_acceptable (page);
}

static GtkWidget *
service_page_add_elem (MailDialogServicePage *page, GtkWidget *table, 
		       int row, const char *label_text)
{
	GtkWidget *label, *entry;

	label = gtk_label_new (label_text);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 
			  row, row + 1, GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);

	entry = gtk_entry_new ();
	gtk_table_attach (GTK_TABLE (table), entry, 1, 3, row, row + 1,
			  GTK_EXPAND | GTK_FILL, 0, 0, 0);

	gtk_signal_connect (GTK_OBJECT (entry), "changed",
			    GTK_SIGNAL_FUNC (service_page_item_changed), page);

	return entry;
}

static MailDialogServicePageItem *
service_page_item_new (MailDialogServicePage *page, MailService *mcs)
{
	MailDialogServicePageItem *item;
	GtkWidget *table, *description;
	int row, service_flags;

	item = g_new0 (MailDialogServicePageItem, 1);
	
	item->vbox = gtk_vbox_new (FALSE, 0);

	/* Description */
	description = html_new (TRUE);
	put_html (GTK_HTML (description), mcs->provider->description);
	gtk_box_pack_start (GTK_BOX (item->vbox),
			    description->parent->parent,
			    TRUE, TRUE, 0);
	
	table = gtk_table_new (6, 3, FALSE);
	gtk_table_set_row_spacings (GTK_TABLE (table), 2);
	gtk_table_set_col_spacings (GTK_TABLE (table), 10);
	gtk_container_set_border_width (GTK_CONTAINER (table), 8);
	gtk_box_pack_start (GTK_BOX (item->vbox), table, TRUE, TRUE, 0);

	item->protocol = g_strdup (mcs->provider->protocol);
	item->type = mcs->type;
	
	row = 0;
	service_flags = mcs->service->url_flags & ~CAMEL_SERVICE_URL_NEED_AUTH;

	if (service_flags & CAMEL_SERVICE_URL_ALLOW_HOST) {
		item->host = service_page_add_elem (page, table, row++, _("Server:"));
		item->hostneed = ((service_flags & CAMEL_SERVICE_URL_NEED_HOST)
				  == CAMEL_SERVICE_URL_NEED_HOST);
		item->port = service_page_add_elem (page, table, row++, _("Port:"));
		item->default_port = mcs->provider->default_ports[mcs->type];
	}

	if (service_flags & CAMEL_SERVICE_URL_ALLOW_USER) {
		item->user = service_page_add_elem (page, table, row++, _("Username:"));
		item->userneed = ((service_flags & CAMEL_SERVICE_URL_NEED_USER)
				  == CAMEL_SERVICE_URL_NEED_USER);
	}

	if (service_flags & CAMEL_SERVICE_URL_ALLOW_PATH) {
		item->path = service_page_add_elem (page, table, row++, _("Path:"));
		item->pathneed = ((service_flags & CAMEL_SERVICE_URL_NEED_PATH)
				  == CAMEL_SERVICE_URL_NEED_PATH);
	}

	if (mcs->authtypes) {
		GtkWidget *label;

		label = gtk_label_new (_("Authentication:"));
		gtk_table_attach (GTK_TABLE (table), label, 0, 1,
				  row, row + 1, GTK_FILL, 0, 0, 0);
		gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);

		item->auth_optionmenu = gtk_option_menu_new ();
		gtk_table_attach (GTK_TABLE (table), 
				  item->auth_optionmenu, 
				  1, 2, row, row + 1, 
				  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
				  0, 0);

		item->auth_detect = gtk_button_new_with_label (_("Detect supported types..."));
		gtk_table_attach (GTK_TABLE (table), item->auth_detect, 
				  2, 3, row, row + 1,
				  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
				  0, 0);
		gtk_widget_set_sensitive (item->auth_detect, FALSE);
		gtk_signal_connect (GTK_OBJECT (item->auth_detect), 
				    "clicked",
				    GTK_SIGNAL_FUNC (service_page_detect), 
				    page);

		item->auth_html = html_new (TRUE);
		gtk_table_attach (GTK_TABLE (table), 
				  item->auth_html->parent->parent,
				  0, 3, row + 1, row + 2,
				  GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

		service_page_item_auth_fill (page, item, mcs->authtypes);

		row += 2;
	}

	if ((mcs->provider->flags & CAMEL_PROVIDER_IS_REMOTE) &&
	    !(mcs->provider->flags & CAMEL_PROVIDER_IS_STORAGE)) {
		item->keep_on_server = gtk_check_button_new_with_label (
			_("Don't delete messages from server"));
		gtk_signal_connect (GTK_OBJECT (item->keep_on_server), "toggled",
				    GTK_SIGNAL_FUNC (service_page_item_changed),
				    page);
		gtk_table_attach (GTK_TABLE (table), item->keep_on_server,
				  0, 3, row, row + 1, GTK_FILL, 0, 0, 0);
		row++;
	}

	if (row != 0) {
		GtkWidget *btn;

		btn = gtk_button_new_with_label (_("Test Settings"));

		gtk_table_attach (GTK_TABLE (table), btn, 2, 3,
				  row, row + 1, GTK_FILL, GTK_FILL, 0, 0);

		gtk_signal_connect (GTK_OBJECT (btn), "clicked",
				    GTK_SIGNAL_FUNC (service_page_item_test), 
				    page);
		row += 1;
	}

	gtk_table_resize (GTK_TABLE (table), row, 3);
	gtk_widget_show_all (table);

	return item;
}

static void
service_page_menuitem_activate (GtkWidget *item, MailDialogServicePage *page)
{
	MailDialogServicePageItem *spitem;

	spitem = service_page_item_by_menuitem (page, item);

	g_return_if_fail (spitem);
	
	gtk_notebook_set_page (GTK_NOTEBOOK (page->notebook), spitem->pnum);
	page->spitem = spitem;

	service_page_item_changed (item, page);
}

static void
service_page_set_changed_cb (MailDialogServicePage *page, 
			     ServicePageCallback cb, gpointer data)
{
	page->changedcb = cb;
	page->changeddata = data;
}

static void
service_page_set_undone_cb (MailDialogServicePage *page, 
			    ServicePageCallback cb, gpointer data)
{
	page->undonecb = cb;
	page->undonedata = data;
}

static void
service_page_set_done_cb (MailDialogServicePage *page, 
			  ServicePageCallback cb, gpointer data)
{
	page->donecb = cb;
	page->donedata = data;
}

static MailDialogServicePage *
service_page_new (const char *label_text, GSList *services)
{
	MailDialogServicePage *page;
	GtkWidget *hbox, *label, *menu;
	GtkWidget *first_item = NULL;
	int pnum;
	GSList *s;

	page = g_new0 (MailDialogServicePage, 1);
	
	page->vbox = gtk_vbox_new (FALSE, 5);
	
	hbox = gtk_hbox_new (FALSE, 8);
	gtk_box_pack_start (GTK_BOX (page->vbox), hbox, FALSE, TRUE, 0);

	label = gtk_label_new (label_text);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
	gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);

	page->optionmenu = gtk_option_menu_new ();
	menu = gtk_menu_new ();
	gtk_box_pack_start (GTK_BOX (hbox), page->optionmenu, TRUE, TRUE, 0);
	
	/* Notebook */
	page->notebook = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (page->notebook), FALSE);
	gtk_box_pack_start (GTK_BOX (page->vbox), page->notebook, 
			    TRUE, TRUE, 0);

	/* Build the list of services and the service item pages */
	for (s = services, pnum = 0; s; s = s->next, pnum++) {
		MailService *mcs = s->data;
		MailDialogServicePageItem *spitem;
		
		spitem = service_page_item_new (page, mcs);
		spitem->pnum = pnum;
		
		gtk_notebook_append_page (GTK_NOTEBOOK (page->notebook), 
					  spitem->vbox, NULL);

		spitem->item = e_utf8_gtk_menu_item_new_with_label (_(mcs->provider->name));
		if (!first_item)
			first_item = spitem->item;

		gtk_signal_connect (GTK_OBJECT (spitem->item), "activate",
				    GTK_SIGNAL_FUNC (service_page_menuitem_activate),
				    page);

		gtk_menu_append (GTK_MENU (menu), spitem->item);
		page->items = g_list_append (page->items, spitem);

		gtk_widget_show (spitem->item);
	}

	gtk_option_menu_set_menu (GTK_OPTION_MENU (page->optionmenu), menu);
	service_page_menuitem_activate (first_item, page);
	gtk_option_menu_set_history (GTK_OPTION_MENU (page->optionmenu), 0);

	gtk_widget_show_all (page->vbox);

	return page;
}

/* Source Page */
static MailDialogSourcePage *
source_page_new (GSList *sources)
{
	MailDialogSourcePage *page = g_new0 (MailDialogSourcePage, 1);
	GtkWidget *html;

	page->page = service_page_new ("Mail source type:", sources);
	page->vbox = page->page->vbox;
	
	html = html_new (FALSE);
	put_html (GTK_HTML (html),
		  _("Select the kind of mail server you have, and enter "
		    "the relevant information about it.\n\nIf the server "
		    "requires authentication, you can click the "
		    "\"Detect supported types...\" button after entering "
		    "the other information."));
	gtk_box_pack_start (GTK_BOX (page->vbox), html->parent, 
			    FALSE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (page->vbox), html->parent, 0);

	return page;
}

/* News Page */
static MailDialogNewsPage *
news_page_new (GSList *sources)
{
	MailDialogNewsPage *page = g_new0 (MailDialogNewsPage, 1);
	GtkWidget *html;

	page->page = service_page_new ("News source type:", sources);
	page->vbox = page->page->vbox;
	
	html = html_new (FALSE);
	put_html (GTK_HTML (html),
		  _("Select the kind of news server you have, and enter "
		    "the relevant information about it.\n\nIf the server "
		    "requires authentication, you can click the "
		    "\"Detect supported types...\" button after entering "
		    "the other information."));
	gtk_box_pack_start (GTK_BOX (page->vbox), html->parent, 
			    FALSE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (page->vbox), html->parent, 0);

	return page;
}

/* Transport page */
static MailDialogTransportPage *
transport_page_new (GSList *transports)
{
	MailDialogTransportPage *page = g_new0 (MailDialogTransportPage, 1);
	GtkWidget *html;

	page->page = service_page_new (_("Mail transport type:"), transports);
	page->vbox = page->page->vbox;
	
	html = html_new (FALSE);
	put_html (GTK_HTML (html),
		  _("Select the kind of mail server you have, and enter "
		    "the relevant information about it.\n\nIf the server "
		    "requires authentication, you can click the "
		    "\"Detect supported types...\" button after entering "
		    "the other information."));
	gtk_box_pack_start (GTK_BOX (page->vbox), html->parent, 
			    FALSE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (page->vbox), html->parent, 0);

	return page;
}

/* Identity dialog */
static void
iddialog_page_undone (MailDialogIdentityPage *page, gpointer data)
{
	MailDialogIdentity *iddialog = (MailDialogIdentity *)data;

	gnome_dialog_set_sensitive (GNOME_DIALOG (iddialog->dialog), 0, FALSE);
}

static void
iddialog_page_done (MailDialogIdentityPage *page, gpointer data)
{
	MailDialogIdentity *iddialog = (MailDialogIdentity *)data;

	gnome_dialog_set_sensitive (GNOME_DIALOG (iddialog->dialog), 0, TRUE);
}

static void
iddialog_ok_clicked (GtkWidget *dialog, MailDialogIdentity *iddialog)
{
	g_return_if_fail (iddialog);
	
	iddialog->id = identity_page_extract (iddialog->page);
}

static MailConfigIdentity *
identity_dialog (const MailConfigIdentity *id, GtkWidget *parent)

{
	MailDialogIdentity *iddialog;
	MailConfigIdentity *returnid;
	GtkWidget *dialog_vbox;
	GtkWidget *area;
	gboolean new = !id;
	
	iddialog = g_new0 (MailDialogIdentity, 1);

	if (new)
		iddialog->dialog = gnome_dialog_new (_("Add Identity"), NULL);
	else
		iddialog->dialog = gnome_dialog_new (_("Edit Identity"), NULL);

	gtk_window_set_modal (GTK_WINDOW (iddialog->dialog), TRUE);
	gtk_window_set_policy (GTK_WINDOW (iddialog->dialog), 
			       FALSE, TRUE, FALSE);
	gnome_dialog_set_parent (GNOME_DIALOG (iddialog->dialog),
				 GTK_WINDOW (parent));

	/* Create the vbox that we will pack the identity widget into */
	dialog_vbox = GNOME_DIALOG (iddialog->dialog)->vbox;
	gtk_widget_show (dialog_vbox);

        /* Get the identity widget */
	iddialog->page = identity_page_new (id);
	gtk_box_pack_start (GTK_BOX (dialog_vbox), 
			    iddialog->page->vbox, TRUE, TRUE, 0);

	identity_page_set_undone_cb (iddialog->page, 
				     iddialog_page_undone, 
				     iddialog);
	identity_page_set_done_cb (iddialog->page, 
				   iddialog_page_done, 
				   iddialog);
	gtk_widget_show (iddialog->page->vbox);

	/* Buttons */
	area = GNOME_DIALOG (iddialog->dialog)->action_area;
	gtk_widget_show (area);
	gtk_button_box_set_layout (GTK_BUTTON_BOX (area), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing (GTK_BUTTON_BOX (area), 8);

	gnome_dialog_append_button (GNOME_DIALOG (iddialog->dialog), 
				    GNOME_STOCK_BUTTON_OK);
	gnome_dialog_append_button (GNOME_DIALOG (iddialog->dialog), 
				    GNOME_STOCK_BUTTON_CANCEL);	

	gnome_dialog_set_default (GNOME_DIALOG (iddialog->dialog), 0);
	gnome_dialog_set_default (GNOME_DIALOG (iddialog->dialog), 1);
	
	gnome_dialog_set_sensitive (GNOME_DIALOG (iddialog->dialog), 0, FALSE);
	
	gnome_dialog_button_connect( GNOME_DIALOG (iddialog->dialog), 0,
				     GTK_SIGNAL_FUNC (iddialog_ok_clicked),
				     iddialog);

	gnome_dialog_run_and_close (GNOME_DIALOG (iddialog->dialog));

	returnid = iddialog->id;
	g_free (iddialog);

	return returnid;
}

/* Source Dialog */
static void
sdialog_page_undone (MailDialogServicePage *page, gpointer data)
{
	MailDialogSource *sdialog = (MailDialogSource *)data;

	gnome_dialog_set_sensitive (GNOME_DIALOG (sdialog->dialog), 0, FALSE);
}

static void
sdialog_page_done (MailDialogServicePage *page, gpointer data)
{
	MailDialogSource *sdialog = (MailDialogSource *)data;

	gnome_dialog_set_sensitive (GNOME_DIALOG (sdialog->dialog), 0, TRUE);
}

static void
sdialog_ok_clicked (GtkWidget *widget, MailDialogSource *sdialog) 
{
	g_return_if_fail (sdialog);
	
	sdialog->source = service_page_extract (sdialog->page->page);
}

static MailConfigService *
source_dialog (MailConfigService *source, GtkWidget *parent)
{
	MailDialogSource *sdialog;
	MailConfigService *returnsource;
	GtkWidget *dialog_vbox, *area;
	GSList *sources, *news, *transports;
	gboolean new = !source;
	
	sdialog = g_new0 (MailDialogSource, 1);
	
	provider_list (&sources, &news, &transports);
	
	if (new)
		sdialog->dialog = gnome_dialog_new (_("Add Source"), NULL);
	else
		sdialog->dialog = gnome_dialog_new (_("Edit Source"), NULL);

	gtk_window_set_modal (GTK_WINDOW (sdialog->dialog), TRUE);
	gtk_window_set_policy (GTK_WINDOW (sdialog->dialog), 
			       FALSE, TRUE, FALSE);
	gtk_window_set_default_size (GTK_WINDOW (sdialog->dialog), 380, 450);
	gnome_dialog_set_parent (GNOME_DIALOG (sdialog->dialog),
				 GTK_WINDOW (parent));

	/* Create the vbox that we will pack the identity widget into */
	dialog_vbox = GNOME_DIALOG (sdialog->dialog)->vbox;
	gtk_widget_show (dialog_vbox);

        /* Get the identity widget */
	sdialog->page = source_page_new (sources);
	if (!new)
		service_page_set_url (sdialog->page->page, source);
	gtk_box_pack_start (GTK_BOX (dialog_vbox), sdialog->page->vbox, 
			    TRUE, TRUE, 0);

	service_page_set_undone_cb (sdialog->page->page, 
				    sdialog_page_undone, 
				    sdialog);
	service_page_set_done_cb (sdialog->page->page, 
				  sdialog_page_done, 
				  sdialog);
	gtk_widget_show (sdialog->page->vbox);

	/* Buttons */
	area = GNOME_DIALOG (sdialog->dialog)->action_area;
	gtk_widget_show (area);
	gtk_button_box_set_layout (GTK_BUTTON_BOX (area), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing (GTK_BUTTON_BOX (area), 8);

	gnome_dialog_append_button (GNOME_DIALOG (sdialog->dialog), 
				    GNOME_STOCK_BUTTON_OK);
	gnome_dialog_append_button (GNOME_DIALOG (sdialog->dialog), 
				    GNOME_STOCK_BUTTON_CANCEL);	

	gnome_dialog_set_default (GNOME_DIALOG (sdialog->dialog), 0);
	gnome_dialog_set_default (GNOME_DIALOG (sdialog->dialog), 1);
	
	gnome_dialog_set_sensitive (GNOME_DIALOG (sdialog->dialog), 0, FALSE);
	
	gnome_dialog_button_connect(GNOME_DIALOG (sdialog->dialog), 0,
				    GTK_SIGNAL_FUNC (sdialog_ok_clicked),
				    sdialog);

	gnome_dialog_run_and_close (GNOME_DIALOG (sdialog->dialog));

	returnsource = sdialog->source;
	g_free (sdialog);

	return returnsource;
}

/* News Dialog */
static void
ndialog_page_undone (MailDialogServicePage *page, gpointer data)
{
	MailDialogNews *ndialog = (MailDialogNews *)data;

	gnome_dialog_set_sensitive (GNOME_DIALOG (ndialog->dialog), 0, FALSE);
}

static void
ndialog_page_done (MailDialogServicePage *page, gpointer data)
{
	MailDialogNews *ndialog = (MailDialogNews *)data;

	gnome_dialog_set_sensitive (GNOME_DIALOG (ndialog->dialog), 0, TRUE);
}

static void
ndialog_ok_clicked (GtkWidget *widget, MailDialogNews *ndialog) 
{
	g_return_if_fail (ndialog);
	
	ndialog->source = service_page_extract (ndialog->page->page);
}

static MailConfigService *
news_dialog (MailConfigService *source, GtkWidget *parent)
{
	MailDialogNews *ndialog;
	MailConfigService *returnsource;
	GtkWidget *dialog_vbox, *area;
	GSList *sources, *news, *transports;
	gboolean new = !source;
	
	ndialog = g_new0 (MailDialogNews, 1);
	
	provider_list (&sources, &news, &transports);
	
	if (new)
		ndialog->dialog = gnome_dialog_new (_("Add News Server"), NULL);
	else
		ndialog->dialog = gnome_dialog_new (_("Edit News Server"), NULL);

	gtk_window_set_modal (GTK_WINDOW (ndialog->dialog), TRUE);
	gtk_window_set_policy (GTK_WINDOW (ndialog->dialog), 
			       FALSE, TRUE, FALSE);
	gtk_window_set_default_size (GTK_WINDOW (ndialog->dialog), 380, 450);
	gnome_dialog_set_parent (GNOME_DIALOG (ndialog->dialog),
				 GTK_WINDOW (parent));

	/* Create the vbox that we will pack the identity widget into */
	dialog_vbox = GNOME_DIALOG (ndialog->dialog)->vbox;
	gtk_widget_show (dialog_vbox);

        /* Get the identity widget */
	ndialog->page = news_page_new (news);
	service_page_set_url (ndialog->page->page, source);
	gtk_box_pack_start (GTK_BOX (dialog_vbox), ndialog->page->vbox, 
			    TRUE, TRUE, 0);

	service_page_set_undone_cb (ndialog->page->page, 
				    ndialog_page_undone, 
				    ndialog);
	service_page_set_done_cb (ndialog->page->page, 
				  ndialog_page_done, 
				  ndialog);
	gtk_widget_show (ndialog->page->vbox);
	
	/* Buttons */
	area = GNOME_DIALOG (ndialog->dialog)->action_area;
	gtk_widget_show (area);
	gtk_button_box_set_layout (GTK_BUTTON_BOX (area), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing (GTK_BUTTON_BOX (area), 8);

	gnome_dialog_append_button (GNOME_DIALOG (ndialog->dialog), 
				    GNOME_STOCK_BUTTON_OK);
	gnome_dialog_append_button (GNOME_DIALOG (ndialog->dialog), 
				    GNOME_STOCK_BUTTON_CANCEL);	

	gnome_dialog_set_default (GNOME_DIALOG (ndialog->dialog), 0);
	gnome_dialog_set_default (GNOME_DIALOG (ndialog->dialog), 1);
	
	gnome_dialog_set_sensitive (GNOME_DIALOG (ndialog->dialog), 0, FALSE);
	
	gnome_dialog_button_connect(GNOME_DIALOG (ndialog->dialog), 0,
				    GTK_SIGNAL_FUNC (ndialog_ok_clicked),
				    ndialog);

	gnome_dialog_run_and_close (GNOME_DIALOG (ndialog->dialog));

	returnsource = ndialog->source;
	g_free (ndialog);

	return returnsource;
}

/* Mail configuration druid */
static gboolean
mail_druid_prepare (GnomeDruidPage *page, GnomeDruid *druid, gboolean *active)
{
	gnome_druid_set_buttons_sensitive (druid, TRUE, *active, TRUE);

	return FALSE;
}

static void
mail_druid_identity_undone (MailDialogIdentityPage *page, gpointer data)
{
	MailDruidDialog *dialog = (MailDruidDialog *) data;
	
	dialog->iddone = FALSE;
	gnome_druid_set_buttons_sensitive (GNOME_DRUID (dialog->druid), 
					   TRUE, FALSE, TRUE);
}

static void
mail_druid_identity_done (MailDialogIdentityPage *page, gpointer data)
{
	MailDruidDialog *dialog = (MailDruidDialog *) data;
	
	dialog->iddone = TRUE;
	gnome_druid_set_buttons_sensitive (GNOME_DRUID (dialog->druid), 
					   TRUE, TRUE, TRUE);
}

static void
mail_druid_source_undone (MailDialogServicePage *page, gpointer data)
{
	MailDruidDialog *dialog = (MailDruidDialog *) data;

	dialog->sdone = FALSE;
	gnome_druid_set_buttons_sensitive (GNOME_DRUID (dialog->druid), 
					   TRUE, FALSE, TRUE);
}

static void
mail_druid_source_done (MailDialogServicePage *page, gpointer data)
{
	MailDruidDialog *dialog = (MailDruidDialog *) data;

	dialog->sdone = TRUE;
	gnome_druid_set_buttons_sensitive (GNOME_DRUID (dialog->druid), 
					   TRUE, TRUE, TRUE);
}

static void
mail_druid_transport_undone (MailDialogServicePage *page, gpointer data)
{
	MailDruidDialog *dialog = (MailDruidDialog *) data;

	dialog->tdone = FALSE;
	gnome_druid_set_buttons_sensitive (GNOME_DRUID (dialog->druid), 
					   TRUE, FALSE, TRUE);
}

static void
mail_druid_transport_done (MailDialogServicePage *page, gpointer data)
{
	MailDruidDialog *dialog = (MailDruidDialog *) data;

	dialog->tdone = TRUE;
	gnome_druid_set_buttons_sensitive (GNOME_DRUID (dialog->druid), 
					   TRUE, TRUE, TRUE);
}

static void
mail_druid_cancel (GnomeDruid *druid, GtkWindow *window)
{
	gtk_window_set_modal (window, FALSE);
	gtk_widget_destroy (GTK_WIDGET (window));
	gtk_main_quit ();
}

static void
mail_druid_finish (GnomeDruidPage *page, GnomeDruid *druid, 
		   MailDruidDialog *dialog)
{
	MailConfigIdentity *id;
	MailConfigService *source;
	MailConfigService *transport;
	GSList *mini;

	mail_config_clear ();
	
	/* Identity */
	id = identity_page_extract (dialog->idpage);
	mail_config_add_identity (id);

	/* Source */
	source = service_page_extract (dialog->spage->page);
	mail_config_add_source (source);

	mini = g_slist_prepend (NULL, source);
	mail_load_storages (dialog->shell, mini);
	g_slist_free (mini);

	/* Transport */
	transport = service_page_extract (dialog->tpage->page);
	mail_config_set_transport (transport);

	mail_config_write ();
	
	mail_druid_cancel (druid, GTK_WINDOW (dialog->dialog));
}

void
mail_config_druid (Evolution_Shell shell)
{
	MailDruidDialog *dialog;
	GnomeDruidPageStart *spage;
	GnomeDruidPageFinish *fpage;
	GnomeDruidPageStandard *dpage;
	GSList *sources, *news, *transports;
	GdkImlibImage *mail_logo, *identity_logo;
	GdkImlibImage *source_logo, *transport_logo;

	provider_list (&sources, &news, &transports);

	mail_logo = load_image ("evolution-inbox.png");
	identity_logo = load_image ("malehead.png");
	source_logo = mail_logo;
	transport_logo = load_image ("envelope.png");

	dialog = g_new0 (MailDruidDialog, 1);
	dialog->shell = shell; /*should ref this somewhere*/
	dialog->gui = glade_xml_new (EVOLUTION_GLADEDIR 
				     "/mail-config-druid.glade", NULL);
	dialog->dialog = glade_xml_get_widget (dialog->gui, "dialog");
	dialog->druid = glade_xml_get_widget (dialog->gui, "druid");

	/* Cancel button */
	gtk_signal_connect (GTK_OBJECT (dialog->druid), "cancel", 
			    GTK_SIGNAL_FUNC (mail_druid_cancel), 
			    dialog->dialog);

	/* Start page */
	spage = GNOME_DRUID_PAGE_START (glade_xml_get_widget (dialog->gui, "startpage"));
	gnome_druid_page_start_set_logo (spage, mail_logo);
	
	/* Identity page */
	dpage = GNOME_DRUID_PAGE_STANDARD (glade_xml_get_widget (dialog->gui, "standardpage1"));
	gnome_druid_page_standard_set_logo (dpage, identity_logo);

	dialog->idpage = identity_page_new (NULL);
	gtk_box_pack_start (GTK_BOX (dpage->vbox), 
			    dialog->idpage->vbox, 
			    TRUE, TRUE, 0);

	identity_page_set_undone_cb (dialog->idpage, 
				     mail_druid_identity_undone, 
				     dialog);
	identity_page_set_done_cb (dialog->idpage, 
				   mail_druid_identity_done, 
				   dialog);
	gtk_signal_connect (GTK_OBJECT (dpage), "prepare", 
			    GTK_SIGNAL_FUNC (mail_druid_prepare), 
			    &dialog->iddone);
	gtk_widget_show (dialog->idpage->vbox);

	/* Source page */
	dpage = GNOME_DRUID_PAGE_STANDARD (glade_xml_get_widget (dialog->gui, "standardpage2"));
	gnome_druid_page_standard_set_logo (dpage, source_logo);

	dialog->spage = source_page_new (sources);
	gtk_box_pack_start (GTK_BOX (dpage->vbox), 
			    dialog->spage->vbox, 
			    TRUE, TRUE, 0);

	service_page_set_done_cb (dialog->spage->page, 
				  mail_druid_source_done, 
				  dialog);
	service_page_set_undone_cb (dialog->spage->page, 
				    mail_druid_source_undone, 
				    dialog);
	gtk_signal_connect (GTK_OBJECT (dpage), "prepare", 
			    GTK_SIGNAL_FUNC (mail_druid_prepare), 
			    &dialog->sdone);

	/* In case its already done */
	service_page_item_changed (dialog->spage->page->spitem->item, 
				   dialog->spage->page);

	gtk_widget_show (dialog->spage->vbox);

	/* Transport page */
	dpage = GNOME_DRUID_PAGE_STANDARD (glade_xml_get_widget (dialog->gui, "standardpage3"));
	gnome_druid_page_standard_set_logo (dpage, transport_logo);

	dialog->tpage = transport_page_new (transports);
	gtk_box_pack_start (GTK_BOX (dpage->vbox), 
			    dialog->tpage->vbox, 
			    TRUE, TRUE, 0);

	service_page_set_undone_cb (dialog->tpage->page, 
				    mail_druid_transport_undone, 
				    dialog);
	service_page_set_done_cb (dialog->tpage->page, 
				  mail_druid_transport_done, 
				  dialog);
	gtk_signal_connect (GTK_OBJECT (dpage), "prepare", 
			    GTK_SIGNAL_FUNC (mail_druid_prepare), 
			    &dialog->tdone);

	/* In case its already as done as it needs to be */
	service_page_item_changed (dialog->tpage->page->spitem->item, 
				   dialog->tpage->page);

	gtk_widget_show (dialog->tpage->vbox);


	/* Finish page */
	fpage = GNOME_DRUID_PAGE_FINISH (glade_xml_get_widget (dialog->gui, "finishpage"));
	gnome_druid_page_finish_set_logo (fpage, mail_logo);

	gtk_signal_connect (GTK_OBJECT (fpage), "finish",
			    GTK_SIGNAL_FUNC (mail_druid_finish),
			    dialog);
	
	/* GDK_THREADS_ENTER (); */
	gtk_main ();
	/* GDK_THREADS_LEAVE (); */
}

/* Main configuration dialog */
static void
identities_select_row (GtkWidget *widget, gint row, gint column,
		       GdkEventButton *event, MailDialog *dialog)
{
	dialog->idrow = row;
}

static void
identities_add_clicked (GtkWidget *widget, MailDialog *dialog)
{
	MailConfigIdentity *id;

	id = identity_dialog (NULL, dialog->dialog);
	if (id) {
		GtkWidget *clist = dialog->clistIdentities;
		gchar *text[4];
		gint row = 0;
		
		text[0] = id->name;
		text[1] = id->address;
		text[2] = id->org;
		text[3] = id->sig;

		row = e_utf8_gtk_clist_append (GTK_CLIST (clist), text);
		gtk_clist_set_row_data (GTK_CLIST (clist), row, id);
		gtk_clist_select_row (GTK_CLIST (clist), row, 0);
		dialog->maxidrow++;

		gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
	}
}

static void
identities_edit_clicked (GtkWidget *widget, MailDialog *dialog)
{
	MailConfigIdentity *id, *id2;

	if (dialog->idrow < 0)
		return;
	
	id = gtk_clist_get_row_data (GTK_CLIST (dialog->clistIdentities), 
				     dialog->idrow);
	
	id2 = identity_dialog (id, dialog->dialog);
	if (id2) {
		GtkCList *clist = GTK_CLIST (dialog->clistIdentities);
	
		e_utf8_gtk_clist_set_text (clist, dialog->idrow, 0, id2->name);
		e_utf8_gtk_clist_set_text (clist, dialog->idrow, 1, id2->address);
		e_utf8_gtk_clist_set_text (clist, dialog->idrow, 2, id2->org);
		e_utf8_gtk_clist_set_text (clist, dialog->idrow, 3, id2->sig);
	
		gtk_clist_set_row_data (clist, dialog->idrow, id2);

		gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
	}
}

static void
identities_delete_clicked (GtkWidget *widget, MailDialog *dialog)
{
	GtkCList *clist;
	
	if (dialog->idrow == -1)
		return;

	clist = GTK_CLIST (dialog->clistIdentities);
	
	gtk_clist_remove (clist, dialog->idrow);
	dialog->maxidrow--;

	if (dialog->idrow > dialog->maxidrow)
		gtk_clist_select_row (clist, dialog->maxidrow, 0);
	else
		gtk_clist_select_row (clist, dialog->idrow, 0);

	gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
}

static void
sources_select_row (GtkWidget *widget, gint row, gint column,
		    GdkEventButton *event, MailDialog *dialog)
{
	dialog->srow = row;
}

static void
sources_add_clicked (GtkWidget *widget, MailDialog *dialog)
{
	MailConfigService *source;

	source = source_dialog (NULL, dialog->dialog);
	if (source) {
		GtkCList *clist = GTK_CLIST (dialog->clistSources);
		gchar *text[1];
		gint row = 0;
		
		text[0] = source->url;
		
		row = e_utf8_gtk_clist_append (clist, text);
		gtk_clist_set_row_data (clist, row, source);
		gtk_clist_select_row (clist, row, 0);
		dialog->maxsrow++;
		
		gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
	}
}

static void
sources_edit_clicked (GtkWidget *widget, MailDialog *dialog)
{
	MailConfigService *source, *source2;

	if (dialog->srow < 0)
		return;

	source = gtk_clist_get_row_data (GTK_CLIST (dialog->clistSources),
					 dialog->srow);
	
	source2 = source_dialog (source, dialog->dialog);
	if (source2) {
		GtkCList *clist = GTK_CLIST (dialog->clistSources);
		
		e_utf8_gtk_clist_set_text (clist, dialog->srow, 0, source2->url);
		gtk_clist_set_row_data (clist, dialog->srow, source2);

		gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
	}
}

static void
sources_delete_clicked (GtkWidget *widget, MailDialog *dialog)
{
	GtkCList *clist;
	
	if (dialog->srow == -1)
		return;
	
	clist = GTK_CLIST (dialog->clistSources);

	gtk_clist_remove (clist, dialog->srow);
	dialog->maxsrow--;

	if (dialog->srow > dialog->maxsrow)
		gtk_clist_select_row (clist, dialog->maxsrow, 0);
	else
		gtk_clist_select_row (clist, dialog->srow, 0);

	gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
}

static void
news_select_row (GtkWidget *widget, gint row, gint column,
		 GdkEventButton *event, MailDialog *dialog)
{
	dialog->nrow = row;
}

static void
news_add_clicked (GtkWidget *widget, MailDialog *dialog)
{
	MailConfigService *news;

	news = news_dialog (NULL, dialog->dialog);
	if (news) {
		GtkCList *clist = GTK_CLIST (dialog->clistNews);
		gchar *text[1];
		gint row = 0;
		
		text[0] = news->url;

		row = e_utf8_gtk_clist_append (clist, text);
		gtk_clist_set_row_data (clist, row, news);
		gtk_clist_select_row (clist, row, 0);
		dialog->maxnrow++;
		
		gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
	}
}

static void
news_edit_clicked (GtkWidget *widget, MailDialog *dialog)
{
	MailConfigService *news, *news2;

	if (dialog->nrow < 0)
		return;

	news = gtk_clist_get_row_data (GTK_CLIST (dialog->clistNews),
				       dialog->nrow);
	
	news2 = news_dialog (news, dialog->dialog);
	if (news2) {
		GtkCList *clist = GTK_CLIST (dialog->clistNews);
		
		e_utf8_gtk_clist_set_text (clist, dialog->nrow, 0, news2->url);
		gtk_clist_set_row_data (clist, dialog->nrow, news2);

		gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
	}
}

static void
news_delete_clicked (GtkWidget *widget, MailDialog *dialog)
{
	GtkCList *clist;
	
	if (dialog->nrow == -1)
		return;
	
	clist = GTK_CLIST (dialog->clistNews);

	gtk_clist_remove (clist, dialog->nrow);
	dialog->maxnrow--;

	if (dialog->nrow > dialog->maxnrow)
		gtk_clist_select_row (clist, dialog->maxnrow, 0);
	else
		gtk_clist_select_row (clist, dialog->nrow, 0);

	gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
}

static void
mail_config_tpage_changed (MailDialogServicePage *page, gpointer data)
{
	MailDialog *dialog = (MailDialog *)data;

	gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
}

static void
mail_config_tpage_done (MailDialogServicePage *page, gpointer data)
{
	MailDialog *dialog = (MailDialog *)data;
	
	dialog->tpagedone = TRUE;
}

static void
format_toggled (GtkWidget *widget, MailDialog *dialog)
{
	gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
}

static void
timeout_changed (GtkWidget *widget, MailDialog *dialog)
{
	gnome_property_box_changed (GNOME_PROPERTY_BOX (dialog->dialog));
}

static void
mail_config_apply_clicked (GnomePropertyBox *property_box, 
			   gint page_num,
			   MailDialog *dialog)
{
	GtkCList *clist;
	GtkToggleButton *chk;
	GtkSpinButton *spin;
	MailConfigService *t;
	gboolean send_html;
	gpointer data;
	glong seen_timeout;
	int i;
	
	if (page_num != -1)
		return;

	mail_config_clear ();

	/* Identities */
	for (i = 0; i <= dialog->maxidrow; i++) {	
		clist = GTK_CLIST (dialog->clistIdentities);
		
		data = gtk_clist_get_row_data (clist, i);
		mail_config_add_identity ((MailConfigIdentity *) data);
	}

	/* Sources */
	for (i = 0; i <= dialog->maxsrow; i++) {	
		GSList *mini;

		clist = GTK_CLIST (dialog->clistSources);

		data = gtk_clist_get_row_data (clist, i);
		mail_config_add_source ((MailConfigService *) data);

		mini = g_slist_prepend (NULL, data);
		mail_load_storages (dialog->shell, mini);
		g_slist_free (mini);
	}

	/* Transport */
	t = service_page_extract (dialog->page->page);
	mail_config_set_transport (t);

	/* News */
	for (i = 0; i <= dialog->maxnrow; i++) {	
		GSList *mini;

		clist = GTK_CLIST (dialog->clistNews);

		data = gtk_clist_get_row_data (clist, i);
		mail_config_add_news ((MailConfigService *) data);

		mini = g_slist_prepend (NULL, data);
		mail_load_storages (dialog->shell, mini);
		g_slist_free (mini);
	}
	
	/* Format */
	chk = GTK_TOGGLE_BUTTON (dialog->chkFormat);
	send_html = gtk_toggle_button_get_active (chk);
	mail_config_set_send_html (send_html);

	/* Mark as seen timeout */
	spin = GTK_SPIN_BUTTON (dialog->spinTimeout);
	seen_timeout = gtk_spin_button_get_value_as_int (spin);
	mail_config_set_mark_as_seen_timeout (seen_timeout);

	mail_config_write ();
}

static void
mail_config_close (GnomePropertyBox *property_box, MailDialog *dialog) 
{
	gtk_object_unref (GTK_OBJECT (dialog->gui));
	g_free (dialog);
}
	
void
mail_config (Evolution_Shell shell)
{
	MailDialog *dialog;
	GladeXML *gui;
	MailConfigService *transport;
	GtkCList *clist;
	GtkWidget *button, *tvbox;
	GSList *l, *sources, *news, *transports;
	
	provider_list (&sources, &news, &transports);

	dialog = g_new0 (MailDialog, 1);	

	gui = glade_xml_new (EVOLUTION_GLADEDIR "/mail-config.glade", NULL);
	dialog->gui = gui;	
	dialog->shell = shell;

	dialog->dialog = glade_xml_get_widget (gui, "dialog");
	
	/* Identities Page */
	dialog->clistIdentities = 
		glade_xml_get_widget (gui, "clistIdentities");
	clist = GTK_CLIST (dialog->clistIdentities);
	gtk_clist_set_column_width (clist, 0, 80);

	l = mail_config_get_identities ();

	dialog->maxidrow = g_slist_length (l) - 1;
	dialog->idrow = -1;

	for (; l != NULL; l = l->next) {
		MailConfigIdentity *id;
		gint row;
		gchar *text[4];
		
		id = identity_copy ((MailConfigIdentity *)l->data);
		
		text[0] = id->name;
		text[1] = id->address;
		text[2] = id->org;
		text[3] = id->sig;
		
	 	row = e_utf8_gtk_clist_append (clist, text);
		gtk_clist_set_row_data_full (clist, row, id, (GtkDestroyNotify) identity_destroy);
	}

	gtk_signal_connect (GTK_OBJECT (clist), "select_row",
			    GTK_SIGNAL_FUNC (identities_select_row),
			    dialog);

	button = glade_xml_get_widget (gui, "cmdIdentitiesAdd");
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (identities_add_clicked),
			    dialog);
	button = glade_xml_get_widget (gui, "cmdIdentitiesEdit");
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (identities_edit_clicked),
			    dialog);
	button = glade_xml_get_widget (gui, "cmdIdentitiesDelete");
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (identities_delete_clicked),
			    dialog);

	/* Sources Page */
	dialog->clistSources = glade_xml_get_widget (gui, "clistSources");
	clist = GTK_CLIST (dialog->clistSources);
	gtk_clist_set_column_width (clist, 0, 80);

	l = mail_config_get_sources ();	

	dialog->maxsrow = g_slist_length (l) - 1;
	dialog->srow = -1;

	for (; l != NULL; l = l->next) {
		MailConfigService *source;
		gint row;
		gchar *text[1];
		
		source = service_copy ((MailConfigService *)l->data);
		
		text[0] = source->url;

	 	row = e_utf8_gtk_clist_append (clist, text);
		gtk_clist_set_row_data_full (clist, row, source, (GtkDestroyNotify) service_destroy);
	}

	gtk_signal_connect (GTK_OBJECT (clist), "select_row",
			    GTK_SIGNAL_FUNC (sources_select_row),
			    dialog);

	button = glade_xml_get_widget (gui, "cmdSourcesAdd");
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (sources_add_clicked),
			    dialog);
	button = glade_xml_get_widget (gui, "cmdSourcesEdit");
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (sources_edit_clicked),
			    dialog);
	button = glade_xml_get_widget (gui, "cmdSourcesDelete");
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (sources_delete_clicked),
			    dialog);

	/* News Page */
	dialog->clistNews = glade_xml_get_widget (gui, "clistNews");
	clist = GTK_CLIST (dialog->clistNews);
	gtk_clist_set_column_width (clist, 0, 80);

	l = mail_config_get_news ();	

	dialog->maxnrow = g_slist_length (l) - 1;
	dialog->nrow = -1;

	for (; l != NULL; l = l->next) {
		MailConfigService *news;
		gint row;
		gchar *text[1];
		
		news = service_copy ((MailConfigService *)l->data);
		
		text[0] = news->url;

	 	row = e_utf8_gtk_clist_append (clist, text);
		gtk_clist_set_row_data_full (clist, row, news, (GtkDestroyNotify) service_destroy);
	}

	gtk_signal_connect (GTK_OBJECT (clist), "select_row",
			    GTK_SIGNAL_FUNC (news_select_row),
			    dialog);

	button = glade_xml_get_widget (gui, "cmdNewsServersAdd");
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (news_add_clicked),
			    dialog);
	button = glade_xml_get_widget (gui, "cmdNewsServersEdit");
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (news_edit_clicked),
			    dialog);
	button = glade_xml_get_widget (gui, "cmdNewsServersDelete");
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (news_delete_clicked),
			    dialog);

	/* Transport Page */
	tvbox = glade_xml_get_widget (gui, "transport_vbox");
	dialog->page = transport_page_new (transports);
	transport = mail_config_get_transport ();
	service_page_set_url (dialog->page->page, transport);
	service_page_set_changed_cb (dialog->page->page,
				     mail_config_tpage_changed, dialog);
	service_page_set_done_cb (dialog->page->page, 
				  mail_config_tpage_done, dialog);
	gtk_box_pack_start (GTK_BOX (tvbox), 
			    dialog->page->vbox, TRUE, TRUE, 0);
	gtk_widget_show (dialog->page->vbox);
	
	/* Other Page */
	dialog->chkFormat = glade_xml_get_widget (gui, "chkFormat");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->chkFormat), 
				      mail_config_send_html ());
	gtk_signal_connect (GTK_OBJECT (dialog->chkFormat), "toggled",
			    GTK_SIGNAL_FUNC (format_toggled),
			    dialog);

	dialog->spinTimeout = glade_xml_get_widget (gui, "spinTimeout");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (dialog->spinTimeout),
				   mail_config_mark_as_seen_timeout ());

	gtk_signal_connect (GTK_OBJECT (dialog->spinTimeout), "changed",
			    GTK_SIGNAL_FUNC (timeout_changed),
			    dialog);

	/* Listen for signals */
	gtk_signal_connect (GTK_OBJECT (dialog->dialog), "apply",
			    GTK_SIGNAL_FUNC (mail_config_apply_clicked),
			    dialog);
	gtk_signal_connect (GTK_OBJECT (dialog->dialog), "destroy",
			    GTK_SIGNAL_FUNC (mail_config_close),
			    dialog);
	gtk_signal_connect (GTK_OBJECT (dialog->dialog), "delete_event",
			    GTK_SIGNAL_FUNC (mail_config_close),
			    dialog);

	gtk_widget_show (dialog->dialog);
}

/* ************************************************************************ */

typedef struct test_service_input_s {
	gchar *url;
	CamelProviderType type;
} test_service_input_t;

typedef struct test_service_data_s {
	gboolean success;
} test_service_data_t;

static gchar *describe_test_service (gpointer in_data, gboolean gerund);
static void setup_test_service   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_test_service      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_test_service (gpointer in_data, gpointer op_data, CamelException *ex);

static gchar *describe_test_service (gpointer in_data, gboolean gerund)
{
        test_service_input_t *input = (test_service_input_t *) in_data;

        if (gerund) {
                return g_strdup_printf ("Testing \"%s\"", input->url);
        } else {
                return g_strdup_printf ("Test connection to \"%s\"", input->url);
        }
}

static void setup_test_service (gpointer in_data, gpointer op_data, CamelException *ex)
{
        test_service_input_t *input = (test_service_input_t *) in_data;
        test_service_data_t *data = (test_service_data_t *) op_data;

	if (!input->url) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No URL was provided to test");
		return;
	}

	data->success = FALSE;
}

static void do_test_service (gpointer in_data, gpointer op_data, CamelException *ex)
{
        test_service_input_t *input = (test_service_input_t *) in_data;
        test_service_data_t *data = (test_service_data_t *) op_data;

	CamelService *service;
	
	service = camel_session_get_service (session, input->url, 
					     input->type, ex);

	if (camel_exception_get_id (ex) != CAMEL_EXCEPTION_NONE) {
		data->success = FALSE;
	} else if (camel_service_connect (service, ex)) {
		camel_service_disconnect (service, ex);
		data->success = TRUE;
	} else {
		data->success = FALSE;
	}

	camel_object_unref (CAMEL_OBJECT (service));
}

static void cleanup_test_service (gpointer in_data, gpointer op_data, CamelException *ex)
{
        test_service_input_t *input = (test_service_input_t *) in_data;
        test_service_data_t *data = (test_service_data_t *) op_data;

	GtkWidget *dlg;

	if (data->success) {
		dlg = gnome_ok_dialog (_("The connection was successful!"));
		gnome_dialog_run_and_close (GNOME_DIALOG (dlg));
	}

	g_free (input->url);
}

static const mail_operation_spec op_test_service = {
        describe_test_service,
        sizeof (test_service_data_t),
        setup_test_service,
        do_test_service,
        cleanup_test_service
};

static void
config_do_test_service (const char *url, CamelProviderType type)
{
        test_service_input_t *input;

        input = g_new (test_service_input_t, 1);
	input->url = g_strdup (url);
	input->type = type;

        mail_operation_queue (&op_test_service, input, TRUE);
}
