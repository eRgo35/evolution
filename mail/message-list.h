/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _MESSAGE_LIST_H_
#define _MESSAGE_LIST_H_

#include <gnome.h>
#include "mail-types.h"
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-ui-component.h>
#include <gal/e-table/e-table-scrolled.h>
#include <gal/e-table/e-table-simple.h>
#include <gal/e-table/e-tree-simple.h>
#include <gal/e-table/e-cell-text.h>
#include <gal/e-table/e-cell-toggle.h>
#include <gal/e-table/e-cell-checkbox.h>
#include <gal/e-table/e-cell-tree.h>

#define MESSAGE_LIST_TYPE        (message_list_get_type ())
#define MESSAGE_LIST(o)          (GTK_CHECK_CAST ((o), MESSAGE_LIST_TYPE, MessageList))
#define MESSAGE_LIST_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), MESSAGE_LIST_TYPE, MessageListClass))
#define IS_MESSAGE_LIST(o)       (GTK_CHECK_TYPE ((o), MESSAGE_LIST_TYPE))
#define IS_MESSAGE_LIST_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), MESSAGE_LIST_TYPE))

typedef struct _Renderer Renderer;

enum {
	COL_MESSAGE_STATUS,
	COL_FLAGGED,
	COL_SCORE,
	COL_ATTACHMENT,
	COL_FROM,
	COL_SUBJECT,
	COL_SENT,
	COL_RECEIVED,
	COL_TO,
	COL_SIZE,
	
	COL_LAST,
	
	/* Invisible columns */
	COL_DELETED,
	COL_UNREAD,
	COL_COLOUR,
};

struct _MessageList {
	BonoboObject parent;
	
	ETableModel  *table_model;
	
	ETreePath    *tree_root;
	
	GtkWidget    *etable;

	CamelFolder  *folder;

	GHashTable	*uid_rowmap; /* key is the uid, value is the row number.
					Note: The key string is owned by table_model */
	
	char *search;		/* current search string */

	gboolean threaded;	/* are we displaying threaded view? */
	int cursor_row;
	const char *cursor_uid;
	
	/* row-selection and seen-marking timers */
	guint idle_id, seen_id;
};

typedef struct {
	BonoboObjectClass parent_class;

	/* signals - select a message */
	void (*message_selected)(MessageList *ml, const char *uid);
} MessageListClass;

typedef void (*MessageListForeachFunc) (MessageList *message_list,
					const char *uid,
					gpointer user_data);

typedef enum {
	MESSAGE_LIST_SELECT_NEXT = 1,
	MESSAGE_LIST_SELECT_PREVIOUS = -1
} MessageListSelectDirection;

GtkType        message_list_get_type   (void);
BonoboObject   *message_list_new        (void);
void           message_list_set_folder (MessageList *message_list,
					CamelFolder *camel_folder);
GtkWidget     *message_list_get_widget (MessageList *message_list);

void           message_list_foreach    (MessageList *message_list,
					MessageListForeachFunc callback,
					gpointer user_data);

void           message_list_select     (MessageList *message_list,
					int base_row,
					MessageListSelectDirection direction,
					guint32 flags, guint32 mask);

void	       message_list_set_threaded(MessageList *ml, gboolean threaded);
void	       message_list_set_search(MessageList *ml, const char *search);

#endif /* _MESSAGE_LIST_H_ */
