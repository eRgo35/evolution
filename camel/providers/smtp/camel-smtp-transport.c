/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-smtp-transport.c : class for a smtp transport */

/* 
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 2000 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#undef MIN
#undef MAX
#include "camel-mime-filter-crlf.h"
#include "camel-stream-filter.h"
#include "camel-smtp-transport.h"
#include "camel-mime-message.h"
#include "camel-multipart.h"
#include "camel-mime-part.h"
#include "camel-operation.h"
#include "camel-stream-buffer.h"
#include "camel-tcp-stream.h"
#include "camel-tcp-stream-raw.h"
#ifdef HAVE_SSL
#include "camel-tcp-stream-ssl.h"
#endif
#include "camel-session.h"
#include "camel-exception.h"
#include "camel-sasl.h"
#include "string-utils.h"

#define d(x) x

/* Specified in RFC 821 */
#define SMTP_PORT 25

/* camel smtp transport class prototypes */
static gboolean smtp_send_to (CamelTransport *transport, CamelMimeMessage *message,
			      CamelAddress *from, CamelAddress *recipients, CamelException *ex);

/* support prototypes */
static void smtp_construct (CamelService *service, CamelSession *session,
			    CamelProvider *provider, CamelURL *url,
			    CamelException *ex);
static gboolean smtp_connect (CamelService *service, CamelException *ex);
static gboolean smtp_disconnect (CamelService *service, gboolean clean, CamelException *ex);
static GHashTable *esmtp_get_authtypes (const unsigned char *buffer);
static GList *query_auth_types (CamelService *service, CamelException *ex);
static char *get_name (CamelService *service, gboolean brief);

static gboolean smtp_helo (CamelSmtpTransport *transport, CamelException *ex);
static gboolean smtp_auth (CamelSmtpTransport *transport, const char *mech, CamelException *ex);
static gboolean smtp_mail (CamelSmtpTransport *transport, const char *sender,
			   gboolean has_8bit_parts, CamelException *ex);
static gboolean smtp_rcpt (CamelSmtpTransport *transport, const char *recipient, CamelException *ex);
static gboolean smtp_data (CamelSmtpTransport *transport, CamelMimeMessage *message,
			   gboolean has_8bit_parts, CamelException *ex);
static gboolean smtp_rset (CamelSmtpTransport *transport, CamelException *ex);
static gboolean smtp_quit (CamelSmtpTransport *transport, CamelException *ex);

static void smtp_set_exception (CamelSmtpTransport *transport, const char *respbuf,
				const char *message, CamelException *ex);

/* private data members */
static CamelTransportClass *parent_class = NULL;

static void
camel_smtp_transport_class_init (CamelSmtpTransportClass *camel_smtp_transport_class)
{
	CamelTransportClass *camel_transport_class =
		CAMEL_TRANSPORT_CLASS (camel_smtp_transport_class);
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_smtp_transport_class);
	
	parent_class = CAMEL_TRANSPORT_CLASS (camel_type_get_global_classfuncs (camel_transport_get_type ()));
	
	/* virtual method overload */
	camel_service_class->construct = smtp_construct;
	camel_service_class->connect = smtp_connect;
	camel_service_class->disconnect = smtp_disconnect;
	camel_service_class->query_auth_types = query_auth_types;
	camel_service_class->get_name = get_name;
	
	camel_transport_class->send_to = smtp_send_to;
}

static void
camel_smtp_transport_init (gpointer object)
{
	CamelSmtpTransport *smtp = CAMEL_SMTP_TRANSPORT (object);
	
	smtp->flags = 0;
}

CamelType
camel_smtp_transport_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type =
			camel_type_register (CAMEL_TRANSPORT_TYPE,
					     "CamelSmtpTransport",
					     sizeof (CamelSmtpTransport),
					     sizeof (CamelSmtpTransportClass),
					     (CamelObjectClassInitFunc) camel_smtp_transport_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_smtp_transport_init,
					     NULL);
	}
	
	return type;
}

static void
smtp_construct (CamelService *service, CamelSession *session,
		CamelProvider *provider, CamelURL *url,
		CamelException *ex)
{
	CamelSmtpTransport *smtp_transport = CAMEL_SMTP_TRANSPORT (service);
	const char *use_ssl;
	
	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	
	if ((use_ssl = camel_url_get_param (url, "use_ssl"))) {
		/* Note: previous versions would use "" to toggle use_ssl to 'on' */
		if (!*use_ssl || !strcmp (use_ssl, "always"))
			smtp_transport->flags |= CAMEL_SMTP_TRANSPORT_USE_SSL_ALWAYS;
		else if (!strcmp (use_ssl, "when-possible"))
			smtp_transport->flags |= CAMEL_SMTP_TRANSPORT_USE_SSL_WHEN_POSSIBLE;
	}
}

static const char *
smtp_error_string (int error)
{
	/* SMTP error codes grabbed from rfc821 */
	switch (error) {
	case 0:
		/* looks like a read problem, check errno */
		if (errno)
			return g_strerror (errno);
		else
			return _("Unknown");
	case 500:
		return _("Syntax error, command unrecognized");
	case 501:
		return _("Syntax error in parameters or arguments");
	case 502:
		return _("Command not implemented");
	case 504:
		return _("Command parameter not implemented");
	case 211:
		return _("System status, or system help reply");
	case 214:
		return _("Help message");
	case 220:
		return _("Service ready");
	case 221:
		return _("Service closing transmission channel");
	case 421:
		return _("Service not available, closing transmission channel");
	case 250:
		return _("Requested mail action okay, completed");
	case 251:
		return _("User not local; will forward to <forward-path>");
	case 450:
		return _("Requested mail action not taken: mailbox unavailable");
	case 550:
		return _("Requested action not taken: mailbox unavailable");
	case 451:
		return _("Requested action aborted: error in processing");
	case 551:
		return _("User not local; please try <forward-path>");
	case 452:
		return _("Requested action not taken: insufficient system storage");
	case 552:
		return _("Requested mail action aborted: exceeded storage allocation");
	case 553:
		return _("Requested action not taken: mailbox name not allowed");
	case 354:
		return _("Start mail input; end with <CRLF>.<CRLF>");
	case 554:
		return _("Transaction failed");
		
	/* AUTH error codes: */
	case 432:
		return _("A password transition is needed");
	case 534:
		return _("Authentication mechanism is too weak");
	case 538:
		return _("Encryption required for requested authentication mechanism");
	case 454:
		return _("Temporary authentication failure");
	case 530:
		return _("Authentication required");
		
	default:
		return _("Unknown");
	}
}

static gboolean
connect_to_server (CamelService *service, int try_starttls, CamelException *ex)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);
	CamelStream *tcp_stream;
	char *respbuf = NULL;
	struct hostent *h;
	int port, ret;
	
	if (!CAMEL_SERVICE_CLASS (parent_class)->connect (service, ex))
		return FALSE;
	
	h = camel_service_gethost (service, ex);
	if (!h)
		return FALSE;
	
	/* set some smtp transport defaults */
	transport->flags &= ~(CAMEL_SMTP_TRANSPORT_IS_ESMTP |
			      CAMEL_SMTP_TRANSPORT_8BITMIME |
			      CAMEL_SMTP_TRANSPORT_STARTTLS |
			      CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES);
	
	transport->authtypes = NULL;
	
	port = service->url->port ? service->url->port : SMTP_PORT;
	
#ifdef HAVE_SSL
	if (transport->flags & CAMEL_SMTP_TRANSPORT_USE_SSL) {
		if (try_starttls)
			tcp_stream = camel_tcp_stream_ssl_new_raw (service, service->url->host);
		else {
			port = service->url->port ? service->url->port : 465;
			tcp_stream = camel_tcp_stream_ssl_new (service, service->url->host);
		}
	} else {
		tcp_stream = camel_tcp_stream_raw_new ();
	}
#else
	tcp_stream = camel_tcp_stream_raw_new ();
#endif /* HAVE_SSL */
	
	ret = camel_tcp_stream_connect (CAMEL_TCP_STREAM (tcp_stream), h, port);
	camel_free_host (h);
	if (ret == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not connect to %s (port %d): %s"),
				      service->url->host, port,
				      g_strerror (errno));
		
		camel_object_unref (CAMEL_OBJECT (tcp_stream));
		
		return FALSE;
	}
	
	/* get the localaddr - needed later by smtp_helo */
	transport->localaddr = camel_tcp_stream_get_local_address (CAMEL_TCP_STREAM (tcp_stream));
	
	transport->ostream = tcp_stream;
	transport->istream = camel_stream_buffer_new (tcp_stream, CAMEL_STREAM_BUFFER_READ);
	
	/* Read the greeting, note whether the server is ESMTP or not. */
	do {
		/* Check for "220" */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		if (!respbuf || strncmp (respbuf, "220", 3)) {
			int error;
			
			error = respbuf ? atoi (respbuf) : 0;
			g_free (respbuf);
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Welcome response error: %s: possibly non-fatal"),
					      smtp_error_string (error));
			return FALSE;
		}
		if (strstr (respbuf, "ESMTP"))
			transport->flags |= CAMEL_SMTP_TRANSPORT_IS_ESMTP;
	} while (*(respbuf+3) == '-'); /* if we got "220-" then loop again */
	g_free (respbuf);
	
	/* send HELO (or EHLO, depending on the service type) */
	if (!(transport->flags & CAMEL_SMTP_TRANSPORT_IS_ESMTP)) {
		/* If we did not auto-detect ESMTP, we should still send EHLO */
		transport->flags |= CAMEL_SMTP_TRANSPORT_IS_ESMTP;
		if (!smtp_helo (transport, NULL)) {
			/* Okay, apprently this server doesn't support ESMTP */
			transport->flags &= ~CAMEL_SMTP_TRANSPORT_IS_ESMTP;
			smtp_helo (transport, ex);
		}
	} else {
		/* send EHLO */
		smtp_helo (transport, ex);
	}
	
#ifdef HAVE_SSL
	if (transport->flags & CAMEL_SMTP_TRANSPORT_USE_SSL_WHEN_POSSIBLE) {
		/* try_starttls is always TRUE here */
		if (transport->flags & CAMEL_SMTP_TRANSPORT_STARTTLS)
			goto starttls;
	} else if (transport->flags & CAMEL_SMTP_TRANSPORT_USE_SSL_ALWAYS) {
		if (try_starttls) {
			if (transport->flags & CAMEL_SMTP_TRANSPORT_STARTTLS) {
				goto starttls;
			} else {
				/* server doesn't support STARTTLS, abort */
				camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
						      _("Failed to connect to SMTP server %s in secure mode: %s"),
						      service->url->host, _("server does not appear to support SSL"));
				goto exception_cleanup;
			}
		}
	}
#endif /* HAVE_SSL */
	
	return TRUE;
	
#ifdef HAVE_SSL
 starttls:
	d(fprintf (stderr, "sending : STARTTLS\r\n"));
	if (camel_stream_write (tcp_stream, "STARTTLS\r\n", 10) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("STARTTLS request timed out: %s"),
				      g_strerror (errno));
		goto exception_cleanup;
	}
	
	respbuf = NULL;
	
	do {
		/* Check for "220 Ready for TLS" */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		
		d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
		
		if (!respbuf || strncmp (respbuf, "220", 3)) {
			smtp_set_exception (transport, respbuf, _("STARTTLS response error"), ex);
			g_free (respbuf);
			goto exception_cleanup;
		}
	} while (*(respbuf+3) == '-'); /* if we got "220-" then loop again */
	
	/* Okay, now toggle SSL/TLS mode */
	ret = camel_tcp_stream_ssl_enable_ssl (CAMEL_TCP_STREAM_SSL (tcp_stream));
	if (ret != -1)
		return TRUE;
	
	camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
			      _("Failed to connect to SMTP server %s in secure mode: %s"),
			      service->url->host, g_strerror (errno));
	
 exception_cleanup:
	camel_object_unref (CAMEL_OBJECT (transport->istream));
	transport->istream = NULL;
	camel_object_unref (CAMEL_OBJECT (transport->ostream));
	transport->ostream = NULL;
	
	return FALSE;
#endif /* HAVE_SSL */
}

static gboolean
connect_to_server_wrapper (CamelService *service, CamelException *ex)
{
#ifdef HAVE_SSL
	CamelSmtpTransport *transport = (CamelSmtpTransport *) service;
	
	if (transport->flags & CAMEL_SMTP_TRANSPORT_USE_SSL_ALWAYS) {
		/* First try connecting to the SSL port  */
		if (!connect_to_server (service, FALSE, ex)) {
			if (camel_exception_get_id (ex) == CAMEL_EXCEPTION_SERVICE_UNAVAILABLE) {
				/* Seems the SSL port is unavailable, lets try STARTTLS */
				camel_exception_clear (ex);
				return connect_to_server (service, TRUE, ex);
			} else {
				return FALSE;
			}
		}
		
		return TRUE;
	} else if (transport->flags & CAMEL_SMTP_TRANSPORT_USE_SSL_WHEN_POSSIBLE) {
		/* If the server supports STARTTLS, use it */
		return connect_to_server (service, TRUE, ex);
	} else {
		/* User doesn't care about SSL */
		return connect_to_server (service, FALSE, ex);
	}
#else
	return connect_to_server (service, FALSE, ex);
#endif
}

static gboolean
smtp_connect (CamelService *service, CamelException *ex)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);
	gboolean has_authtypes;
	
	/* We (probably) need to check popb4smtp before we connect ... */
	if (service->url->authmech && !strcmp (service->url->authmech, "POPB4SMTP")) {
		int truth;
		GByteArray *chal;
		CamelSasl *sasl;
		
		sasl = camel_sasl_new ("smtp", "POPB4SMTP", service);
		chal = camel_sasl_challenge (sasl, NULL, ex);
		truth = camel_sasl_authenticated (sasl);
		if (chal)
			g_byte_array_free (chal, TRUE);
		camel_object_unref (CAMEL_OBJECT (sasl));
		
		if (!truth)
			return FALSE;
		
		return connect_to_server_wrapper (service, ex);
	}
	
	if (!connect_to_server_wrapper (service, ex))
		return FALSE;
	
	/* check to see if AUTH is required, if so...then AUTH ourselves */
	has_authtypes = g_hash_table_size (transport->authtypes) > 0;
	if (service->url->authmech && (transport->flags & CAMEL_SMTP_TRANSPORT_IS_ESMTP) && has_authtypes) {
		CamelSession *session = camel_service_get_session (service);
		CamelServiceAuthType *authtype;
		gboolean authenticated = FALSE;
		char *errbuf = NULL;
		
		if (!g_hash_table_lookup (transport->authtypes, service->url->authmech)) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
					      _("SMTP server %s does not support requested "
						"authentication type %s."),
					      service->url->host, service->url->authmech);
			camel_service_disconnect (service, TRUE, NULL);
			return FALSE;
		}
		
		authtype = camel_sasl_authtype (service->url->authmech);
		if (!authtype) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
					      _("No support for authentication type %s"),
					      service->url->authmech);
			camel_service_disconnect (service, TRUE, NULL);
			return FALSE;
		}
		
		if (!authtype->need_password) {
			/* authentication mechanism doesn't need a password,
			   so if it fails there's nothing we can do */
			authenticated = smtp_auth (transport, authtype->authproto, ex);
			if (!authenticated) {
				camel_service_disconnect (service, TRUE, NULL);
				return FALSE;
			}
		}
		
		/* keep trying to login until either we succeed or the user cancels */
		while (!authenticated) {
			if (errbuf) {
				/* We need to un-cache the password before prompting again */
				camel_session_forget_password (session, service, "password", ex);
				g_free (service->url->passwd);
				service->url->passwd = NULL;
			}
			
			if (!service->url->passwd) {
				char *prompt;
				
				prompt = g_strdup_printf (_("%sPlease enter the SMTP password for %s@%s"),
							  errbuf ? errbuf : "", service->url->user,
							  service->url->host);
				
				service->url->passwd = camel_session_get_password (session, prompt, TRUE,
										   service, "password", ex);
				
				g_free (prompt);
				g_free (errbuf);
				errbuf = NULL;
				
				if (!service->url->passwd) {
					camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
							     _("You didn't enter a password."));
					camel_service_disconnect (service, TRUE, NULL);
					return FALSE;
				}
			}
			
			authenticated = smtp_auth (transport, authtype->authproto, ex);
			if (!authenticated) {
				errbuf = g_strdup_printf (_("Unable to authenticate "
							    "to SMTP server.\n%s\n\n"),
							  camel_exception_get_description (ex));
				camel_exception_clear (ex);
			}
		}
		
		/* The spec says we have to re-EHLO, but some servers
		 * we won't bother to name don't want you to... so ignore
		 * errors.
		 */
		smtp_helo (transport, NULL);
	}
	
	return TRUE;
}

static gboolean
authtypes_free (gpointer key, gpointer value, gpointer data)
{
	g_free (value);
	
	return TRUE;
}

static gboolean
smtp_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);
	
	/*if (!service->connected)
	 *	return TRUE;
	 */
	
	if (clean) {
		/* send the QUIT command to the SMTP server */
		smtp_quit (transport, ex);
	}
	
	if (!CAMEL_SERVICE_CLASS (parent_class)->disconnect (service, clean, ex))
		return FALSE;
	
	if (transport->authtypes) {
		g_hash_table_foreach_remove (transport->authtypes, authtypes_free, NULL);
		g_hash_table_destroy (transport->authtypes);
		transport->authtypes = NULL;
	}
	
	camel_object_unref (CAMEL_OBJECT (transport->ostream));
	camel_object_unref (CAMEL_OBJECT (transport->istream));
	transport->ostream = NULL;
	transport->istream = NULL;

	camel_tcp_address_free (transport->localaddr);
	transport->localaddr = NULL;
	
	return TRUE;
}

static GHashTable *
esmtp_get_authtypes (const unsigned char *buffer)
{
	const unsigned char *start, *end;
	GHashTable *table = NULL;
	
	/* advance to the first token */
	start = buffer;
	while (isspace ((int) *start) || *start == '=')
		start++;
	
	if (!*start)
		return NULL;
	
	table = g_hash_table_new (g_str_hash, g_str_equal);
	
	for ( ; *start; ) {
		char *type;
		
		/* advance to the end of the token */
		end = start;
		while (*end && !isspace ((int) *end))
			end++;
		
		type = g_strndup (start, end - start);
		g_hash_table_insert (table, type, type);
		
		/* advance to the next token */
		start = end;
		while (isspace ((int) *start))
			start++;
	}
	
	return table;
}

static GList *
query_auth_types (CamelService *service, CamelException *ex)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);
	CamelServiceAuthType *authtype;
	GList *types, *t, *next;
	
	if (!connect_to_server_wrapper (service, ex))
		return NULL;
	
	types = g_list_copy (service->provider->authtypes);
	for (t = types; t; t = next) {
		authtype = t->data;
		next = t->next;
		
		if (!g_hash_table_lookup (transport->authtypes, authtype->authproto)) {
			types = g_list_remove_link (types, t);
			g_list_free_1 (t);
		}
	}
	
	smtp_disconnect (service, TRUE, NULL);
	
	return types;
}

static char *
get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf (_("SMTP server %s"), service->url->host);
	else {
		return g_strdup_printf (_("SMTP mail delivery via %s"),
					service->url->host);
	}
}

static gboolean
smtp_send_to (CamelTransport *transport, CamelMimeMessage *message,
	      CamelAddress *from, CamelAddress *recipients,
	      CamelException *ex)
{
	CamelSmtpTransport *smtp_transport = CAMEL_SMTP_TRANSPORT (transport);
	const CamelInternetAddress *cia;
	gboolean has_8bit_parts;
	const char *addr;
	int i, len;
	
	if (!camel_internet_address_get (CAMEL_INTERNET_ADDRESS (from), 0, NULL, &addr)) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot send message: "
					"sender address not valid."));
		return FALSE;
	}
	
	camel_operation_start (NULL, _("Sending message"));
	
	/* find out if the message has 8bit mime parts */
	has_8bit_parts = camel_mime_message_has_8bit_parts (message);
	
	/* rfc1652 (8BITMIME) requires that you notify the ESMTP daemon that
	   you'll be sending an 8bit mime message at "MAIL FROM:" time. */
	smtp_mail (smtp_transport, addr, has_8bit_parts, ex);
	
	if (!recipients) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot send message: no recipients defined."));
		camel_operation_end (NULL);
		return FALSE;
	}
	
	len = camel_address_length (recipients);
	cia = CAMEL_INTERNET_ADDRESS (recipients);
	for (i = 0; i < len; i++) {
		if (!camel_internet_address_get (cia, i, NULL, &addr)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Cannot send message: one or more invalid recipients"));
			camel_operation_end (NULL);
			return FALSE;
		}
		
		if (!smtp_rcpt (smtp_transport, addr, ex)) {
			camel_operation_end (NULL);
			return FALSE;
		}
	}
	
	/* passing in has_8bit_parts saves time as we don't have to
           recurse through the message all over again if the user is
           not sending 8bit mime parts */
	if (!smtp_data (smtp_transport, message, has_8bit_parts, ex)) {
		camel_operation_end (NULL);
		return FALSE;
	}
	
	/* reset the service for our next transfer session */
	smtp_rset (smtp_transport, ex);
	
	camel_operation_end (NULL);
	
	return TRUE;
}

static const char *
smtp_next_token (const char *buf)
{
	const unsigned char *token;
	
	token = (const unsigned char *) buf;
	while (*token && !isspace ((int) *token))
		token++;
	
	while (*token && isspace ((int) *token))
		token++;
	
	return (const char *) token;
}

#define HEXVAL(c) (isdigit (c) ? (c) - '0' : (c) - 'A' + 10)

/**
 * example (rfc2034):
 * 5.1.1 Mailbox "nosuchuser" does not exist
 *
 * The human-readable status code is what we want. Since this text
 * could possibly be encoded, we must decode it.
 *
 * "xtext" is formally defined as follows:
 *
 *   xtext = *( xchar / hexchar / linear-white-space / comment )
 *
 *   xchar = any ASCII CHAR between "!" (33) and "~" (126) inclusive,
 *        except for "+", "\" and "(".
 *
 * "hexchar"s are intended to encode octets that cannot be represented
 * as plain text, either because they are reserved, or because they are
 * non-printable.  However, any octet value may be represented by a
 * "hexchar".
 *
 *   hexchar = ASCII "+" immediately followed by two upper case
 *        hexadecimal digits
 **/
static char *
smtp_decode_status_code (const char *in, size_t len)
{
	unsigned char *inptr, *outptr;
	const unsigned char *inend;
	char *outbuf;
	
	outptr = outbuf = g_malloc (len + 1);
	
	inptr = (unsigned char *) in;
	inend = inptr + len;
	while (inptr < inend) {
		if (*inptr == '+') {
			if (isxdigit (inptr[1]) && isxdigit (inptr[2])) {
				*outptr++ = HEXVAL (inptr[1]) * 16 + HEXVAL (inptr[2]);
				inptr += 3;
			} else
				*outptr++ = *inptr++;
		} else
			*outptr++ = *inptr++;
	}
	
	*outptr = '\0';
	
	return outbuf;
}

static void
smtp_set_exception (CamelSmtpTransport *transport, const char *respbuf, const char *message, CamelException *ex)
{
	const char *token, *rbuf = respbuf;
	char *buffer = NULL;
	GString *string;
	int error;
	
	if (!respbuf || !(transport->flags & CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES)) {
	fake_status_code:
		error = respbuf ? atoi (respbuf) : 0;
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      "%s: %s", message,
				      smtp_error_string (error));
	} else {
		string = g_string_new ("");
		do {
			token = smtp_next_token (rbuf + 4);
			if (*token == '\0') {
				g_free (buffer);
				g_string_free (string, TRUE);
				goto fake_status_code;
			}
			
			g_string_append (string, token);
			if (*(rbuf + 3) == '-') {
				g_free (buffer);
				buffer = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
			} else {
				g_free (buffer);
				buffer = NULL;
			}
			
			rbuf = buffer;
		} while (rbuf);
		
		buffer = smtp_decode_status_code (string->str, string->len);
		g_string_free (string, TRUE);
		if (!buffer)
			goto fake_status_code;
		
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      "%s: %s", message, buffer);
		
		g_free (buffer);
	}
}

static gboolean
smtp_helo (CamelSmtpTransport *transport, CamelException *ex)
{
	/* say hello to the server */
	char *name, *cmdbuf, *respbuf = NULL;
	struct hostent *host;
	CamelException err;
	const char *token;
	
	camel_operation_start_transient (NULL, _("SMTP Greeting"));
	
	/* get the local host name */
	camel_exception_init (&err);
	host = camel_gethostbyaddr ((char *) &transport->localaddr->address,
				    transport->localaddr->length, AF_INET, &err);
	camel_exception_clear (&err);
	
	if (host && host->h_name) {
		name = g_strdup (host->h_name);
	} else {
		name = g_strdup_printf ("[%d.%d.%d.%d]",
					transport->localaddr->address[0],
					transport->localaddr->address[1],
					transport->localaddr->address[2],
					transport->localaddr->address[3]);
	}
	
	camel_free_host (host);
	
	/* hiya server! how are you today? */
	if (transport->flags & CAMEL_SMTP_TRANSPORT_IS_ESMTP)
		cmdbuf = g_strdup_printf ("EHLO %s\r\n", name);
	else
		cmdbuf = g_strdup_printf ("HELO %s\r\n", name);
	g_free (name);
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("HELO request timed out: %s: non-fatal"),
				      g_strerror (errno));
		camel_operation_end (NULL);
		return FALSE;
	}
	g_free (cmdbuf);
	
	do {
		/* Check for "250" */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		
		d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
		
		if (!respbuf || strncmp (respbuf, "250", 3)) {
			int error;
			
			error = respbuf ? atoi (respbuf) : 0;
			g_free (respbuf);
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("HELO response error: %s: non-fatal"),
					      smtp_error_string (error));
			camel_operation_end (NULL);
			return FALSE;
		}
		
		token = respbuf + 4;
		
		if (transport->flags & CAMEL_SMTP_TRANSPORT_IS_ESMTP) {
			if (!strncmp (token, "8BITMIME", 8)) {
				d(fprintf (stderr, "This server supports 8bit MIME\n"));
				transport->flags |= CAMEL_SMTP_TRANSPORT_8BITMIME;
			} else if (!strncmp (token, "ENHANCEDSTATUSCODES", 19)) {
				d(fprintf (stderr, "This server supports enhanced status codes\n"));
				transport->flags |= CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES;
			} else if (!strncmp (token, "STARTTLS", 8)) {
				d(fprintf (stderr, "This server supports STARTTLS\n"));
				transport->flags |= CAMEL_SMTP_TRANSPORT_STARTTLS;
			} else if (!transport->authtypes && !strncmp (token, "AUTH", 4)) {
				/* Don't bother parsing any authtypes if we already have a list.
				 * Some servers will list AUTH twice, once the standard way and
				 * once the way Microsoft Outlook requires them to be:
				 *
				 * 250-AUTH LOGIN PLAIN DIGEST-MD5 CRAM-MD5
				 * 250-AUTH=LOGIN PLAIN DIGEST-MD5 CRAM-MD5
				 **/
				
				/* parse for supported AUTH types */
				token += 5;
				
				transport->authtypes = esmtp_get_authtypes (token);
			}
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);
	
	camel_operation_end (NULL);
	
	return TRUE;
}

static gboolean
smtp_auth (CamelSmtpTransport *transport, const char *mech, CamelException *ex)
{
	char *cmdbuf, *respbuf = NULL, *challenge;
	gboolean auth_challenge = FALSE;
	CamelSasl *sasl = NULL;
	
	camel_operation_start_transient (NULL, _("SMTP Authentication"));
	
	sasl = camel_sasl_new ("smtp", mech, CAMEL_SERVICE (transport));
	if (!sasl) {
		camel_operation_end (NULL);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Error creating SASL authentication object."));
		return FALSE;
	}
	
	challenge = camel_sasl_challenge_base64 (sasl, NULL, ex);
	if (challenge) {
		auth_challenge = TRUE;
		cmdbuf = g_strdup_printf ("AUTH %s %s\r\n", mech, challenge);
		g_free (challenge);
	} else {
		cmdbuf = g_strdup_printf ("AUTH %s\r\n", mech);
	}
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("AUTH request timed out: %s"),
				      g_strerror (errno));
		goto lose;
	}
	g_free (cmdbuf);
	
	respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
	d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
	
	while (!camel_sasl_authenticated (sasl)) {
		if (!respbuf) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("AUTH request timed out: %s"),
					      g_strerror (errno));
			goto lose;
		}
		
		/* the server challenge/response should follow a 334 code */
		if (strncmp (respbuf, "334", 3)) {
			g_free (respbuf);
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("AUTH request failed."));
			goto lose;
		}
		
		if (FALSE) {
		broken_smtp_server:
			d(fprintf (stderr, "Your SMTP server's implementation of the %s SASL\n"
				   "authentication mechanism is broken. Please report this to the\n"
				   "appropriate vendor and suggest that they re-read rfc2222 again\n"
				   "for the first time (specifically Section 4, paragraph 2).\n",
				   mech));
		}
		
		/* eat whtspc */
		for (challenge = respbuf + 4; isspace (*challenge); challenge++);
		
		challenge = camel_sasl_challenge_base64 (sasl, challenge, ex);
		g_free (respbuf);
		if (challenge == NULL)
			goto break_and_lose;
		
		/* send our challenge */
		cmdbuf = g_strdup_printf ("%s\r\n", challenge);
		g_free (challenge);
		d(fprintf (stderr, "sending : %s", cmdbuf));
		if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
			g_free (cmdbuf);
			goto lose;
		}
		g_free (cmdbuf);
		
		/* get the server's response */
		respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
	}
	
	/* check that the server says we are authenticated */
	if (!respbuf || strncmp (respbuf, "235", 3)) {
		if (respbuf && auth_challenge && !strncmp (respbuf, "334", 3)) {
			/* broken server, but lets try and work around it anyway... */
			goto broken_smtp_server;
		}
		g_free (respbuf);
		goto lose;
	}
	
	camel_object_unref (CAMEL_OBJECT (sasl));
	camel_operation_end (NULL);
	
	return TRUE;
	
 break_and_lose:
	/* Get the server out of "waiting for continuation data" mode. */
	d(fprintf (stderr, "sending : *\n"));
	camel_stream_write (transport->ostream, "*\r\n", 3);
	respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
	d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
	
 lose:
	if (!camel_exception_is_set (ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
				     _("Bad authentication response from server.\n"));
	}
	
	camel_object_unref (CAMEL_OBJECT (sasl));
	camel_operation_end (NULL);
	
	return FALSE;
}

static gboolean
smtp_mail (CamelSmtpTransport *transport, const char *sender, gboolean has_8bit_parts, CamelException *ex)
{
	/* we gotta tell the smtp server who we are. (our email addy) */
	char *cmdbuf, *respbuf = NULL;
	
	if (transport->flags & CAMEL_SMTP_TRANSPORT_8BITMIME && has_8bit_parts)
		cmdbuf = g_strdup_printf ("MAIL FROM:<%s> BODY=8BITMIME\r\n", sender);
	else
		cmdbuf = g_strdup_printf ("MAIL FROM:<%s>\r\n", sender);
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	
	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("MAIL FROM request timed out: %s: mail not sent"),
				      g_strerror (errno));
		return FALSE;
	}
	g_free (cmdbuf);
	
	do {
		/* Check for "250 Sender OK..." */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		
		d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
		
		if (!respbuf || strncmp (respbuf, "250", 3)) {
			smtp_set_exception (transport, respbuf, _("MAIL FROM response error"), ex);
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);
	
	return TRUE;
}

static gboolean
smtp_rcpt (CamelSmtpTransport *transport, const char *recipient, CamelException *ex)
{
	/* we gotta tell the smtp server who we are going to be sending
	 * our email to */
	char *cmdbuf, *respbuf = NULL;
	
	cmdbuf = g_strdup_printf ("RCPT TO:<%s>\r\n", recipient);
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	
	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("RCPT TO request timed out: %s: mail not sent"),
				      g_strerror (errno));
		return FALSE;
	}
	g_free (cmdbuf);
	
	do {
		/* Check for "250 Recipient OK..." */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		
		d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
		
		if (!respbuf || strncmp (respbuf, "250", 3)) {
			char *message;
			
			message = g_strdup_printf (_("RCPT TO <%s> failed"), recipient);
			smtp_set_exception (transport, respbuf, message, ex);
			g_free (message);
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);
	
	return TRUE;
}

static gboolean
smtp_data (CamelSmtpTransport *transport, CamelMimeMessage *message, gboolean has_8bit_parts, CamelException *ex)
{
	/* now we can actually send what's important :p */
	CamelBestencRequired required = CAMEL_BESTENC_GET_ENCODING;
	CamelBestencEncoding enctype = CAMEL_BESTENC_8BIT;
	char *cmdbuf, *respbuf = NULL;
	CamelStreamFilter *filtered_stream;
	CamelMimeFilter *crlffilter;
	struct _header_raw *header;
	GSList *h, *bcc = NULL;
	int ret;
	
	/* if the message contains 8bit/binary mime parts and the server
	   doesn't support it, set our required encoding to be 7bit */
	if (has_8bit_parts && !(transport->flags & CAMEL_SMTP_TRANSPORT_8BITMIME))
		enctype = CAMEL_BESTENC_7BIT;
	
	/* FIXME: should we get the best charset too?? */
	/* Changes the encoding of any 8bit/binary mime parts to fit
	   within our required encoding type and also force any text
	   parts with long lines (longer than 998 octets) to wrap by
	   QP or base64 encoding them. */
	camel_mime_message_set_best_encoding (message, required, enctype);
	
	cmdbuf = g_strdup ("DATA\r\n");
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	
	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("DATA request timed out: %s: mail not sent"),
				      g_strerror (errno));
		return FALSE;
	}
	g_free (cmdbuf);
	
	respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
	
	d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
	
	if (!respbuf || strncmp (respbuf, "354", 3)) {
		/* we should have gotten instructions on how to use the DATA command:
		 * 354 Enter mail, end with "." on a line by itself
		 */
		smtp_set_exception (transport, respbuf, _("DATA response error"), ex);
		g_free (respbuf);
		return FALSE;
	}
	
	g_free (respbuf);
	respbuf = NULL;
	
	/* setup stream filtering */
	crlffilter = camel_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_ENCODE, CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS);
	filtered_stream = camel_stream_filter_new_with_stream (transport->ostream);
	camel_stream_filter_add (filtered_stream, CAMEL_MIME_FILTER (crlffilter));
	camel_object_unref (CAMEL_OBJECT (crlffilter));
	
	/* copy and remove the bcc headers */
	header = CAMEL_MIME_PART (message)->headers;
	while (header) {
		if (!g_strcasecmp (header->name, "Bcc"))
			bcc = g_slist_append (bcc, g_strdup (header->value));
		header = header->next;
	}
	
	camel_medium_remove_header (CAMEL_MEDIUM (message), "Bcc");
	
	/* write the message */
	ret = camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message), CAMEL_STREAM (filtered_stream));
	
	/* add the bcc headers back */
	if (bcc) {
		h = bcc;
		while (h) {
			camel_medium_add_header (CAMEL_MEDIUM (message), "Bcc", h->data);
			g_free (h->data);
			h = h->next;
		}
		g_slist_free (bcc);
	}
	
	if (ret == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("DATA send timed out: message termination: "
					"%s: mail not sent"),
				      g_strerror (errno));
		
		camel_object_unref (CAMEL_OBJECT (filtered_stream));
		
		return FALSE;
	}
	
	camel_stream_flush (CAMEL_STREAM (filtered_stream));
	camel_object_unref (CAMEL_OBJECT (filtered_stream));
	
	/* terminate the message body */
	
	d(fprintf (stderr, "sending : \\r\\n.\\r\\n\n"));
	
	if (camel_stream_write (transport->ostream, "\r\n.\r\n", 5) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("DATA send timed out: message termination: "
					"%s: mail not sent"),
				      g_strerror (errno));
		return FALSE;
	}
	
	do {
		/* Check for "250 Sender OK..." */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		
		d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
		
		if (!respbuf || strncmp (respbuf, "250", 3)) {
			smtp_set_exception (transport, respbuf, _("DATA termination response error"), ex);
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);
	
	return TRUE;
}

static gboolean
smtp_rset (CamelSmtpTransport *transport, CamelException *ex)
{
	/* we are going to reset the smtp server (just to be nice) */
	char *cmdbuf, *respbuf = NULL;
	
	cmdbuf = g_strdup ("RSET\r\n");
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	
	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("RSET request timed out: %s"),
				      g_strerror (errno));
		return FALSE;
	}
	g_free (cmdbuf);
	
	do {
		/* Check for "250" */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		
		d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
		
		if (!respbuf || strncmp (respbuf, "250", 3)) {
			smtp_set_exception (transport, respbuf, _("RSET response error"), ex);
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);
	
	return TRUE;
}

static gboolean
smtp_quit (CamelSmtpTransport *transport, CamelException *ex)
{
	/* we are going to reset the smtp server (just to be nice) */
	char *cmdbuf, *respbuf = NULL;
	
	cmdbuf = g_strdup ("QUIT\r\n");
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	
	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("QUIT request timed out: %s: non-fatal"),
				      g_strerror (errno));
		return FALSE;
	}
	g_free (cmdbuf);
	
	do {
		/* Check for "221" */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (CAMEL_STREAM_BUFFER (transport->istream));
		
		d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
		
		if (!respbuf || strncmp (respbuf, "221", 3)) {
			smtp_set_exception (transport, respbuf, _("QUIT response error"), ex);
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "221-" then loop again */
	g_free (respbuf);
	
	return TRUE;
}
