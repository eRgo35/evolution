/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-medium.h : class for a medium object */

/* 
 *
 * Copyright (C) 1999 Bertrand Guiheneuf <Bertrand.Guiheneuf@inria.fr> .
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


#ifndef CAMEL_MEDIUM_H
#define CAMEL_MEDIUM_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <gtk/gtk.h>
#include "camel-data-wrapper.h"
#include "camel-stream.h"


#define CAMEL_MEDIUM_TYPE     (camel_medium_get_type ())
#define CAMEL_MEDIUM(obj)     (GTK_CHECK_CAST((obj), CAMEL_MEDIUM_TYPE, CamelMedium))
#define CAMEL_MEDIUM_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), CAMEL_MEDIUM_TYPE, CamelMediumClass))
#define IS_CAMEL_MEDIUM(o)    (GTK_CHECK_TYPE((o), CAMEL_MEDIUM_TYPE))


typedef struct 
{
	CamelDataWrapper parent_object;
	
	GHashTable *headers;
	
	GMimeContentField *content_type;
	CamelDataWrapper *content; /* part real content */
	
} CamelMedium;



typedef struct {
	CamelDataWrapperClass parent_class;
	
	/* Virtual methods */	
	void  (*add_header) (CamelMedium *medium, gchar *header_name, gchar *header_value);
	void  (*remove_header) (CamelMedium *medium, const gchar *header_name);
	const gchar * (*get_header) (CamelMedium *medium, const gchar *header_name);

} CamelMediumClass;



/* Standard Gtk function */
GtkType camel_medium_get_type (void);


/* public methods */
void camel_medium_add_header (CamelMedium *medium, gchar *header_name, gchar *header_value);
void camel_medium_remove_header (CamelMedium *medium, const gchar *header_name);
const gchar *camel_medium_get_header (CamelMedium *medium, const gchar *header_name);




#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_MEDIUM_H */

