/*
 * GnomeCalendar widget
 * Copyright (C) 1998 the Free Software Foundation
 *
 * Author: Miguel de Icaza (miguel@kernel.org)
 */

#include <gnome.h>
#include "calendar.h"
#include "gnome-cal.h"
#include "gncal-full-day.h"
#include "gncal-week-view.h"
#include "timeutil.h"
#include "views.h"

static void gnome_calendar_init                    (GnomeCalendar *gcal);

GnomeApp *parent_class;

guint
gnome_calendar_get_type (void)
{
	static guint gnome_calendar_type = 0;
	if(!gnome_calendar_type) {
		GtkTypeInfo gnome_calendar_info = {
			"GnomeCalendar",
			sizeof(GnomeCalendar),
			sizeof(GnomeCalendarClass),
			(GtkClassInitFunc) NULL,
			(GtkObjectInitFunc) gnome_calendar_init,
			(GtkArgSetFunc) NULL,
			(GtkArgGetFunc) NULL,
		};
		gnome_calendar_type = gtk_type_unique(gnome_app_get_type(), &gnome_calendar_info);
		parent_class = gtk_type_class (gnome_app_get_type());
	}
	return gnome_calendar_type;
}

static void
setup_widgets (GnomeCalendar *gcal)
{
	time_t now;
	GtkWidget *sw;

	now = time (NULL);
	
	gcal->notebook  = gtk_notebook_new ();
	gcal->day_view  = day_view_create (gcal);
	gcal->week_view = gncal_week_view_new (gcal, now);
	gcal->year_view = year_view_create (gcal);
	gcal->task_view = tasks_create (gcal);

	if (1)
	{
		struct tm tm;
		time_t a, b;

		tm = *localtime (&now);
/* 		tm.tm_mday = 2; */
		tm.tm_hour = 0;
		tm.tm_min  = 0;
		tm.tm_sec  = 0;

		a = mktime (&tm);

		tm.tm_mday++;

		b = mktime (&tm);

		gcal->day_view = gncal_full_day_new (gcal, a, b);

		sw = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_AUTOMATIC);
		gtk_container_add (GTK_CONTAINER (sw), gcal->day_view);
		gtk_widget_show (gcal->day_view);
	}

	gtk_notebook_append_page (GTK_NOTEBOOK (gcal->notebook), sw,  gtk_label_new (_("Day View")));
	gtk_notebook_append_page (GTK_NOTEBOOK (gcal->notebook), gcal->week_view, gtk_label_new (_("Week View")));
	gtk_notebook_append_page (GTK_NOTEBOOK (gcal->notebook), gcal->year_view, gtk_label_new (_("Year View")));
	gtk_notebook_append_page (GTK_NOTEBOOK (gcal->notebook), gcal->task_view, gtk_label_new (_("Todo")));

	gtk_widget_show_all (gcal->notebook);
	
	gnome_app_set_contents (GNOME_APP (gcal), gcal->notebook);
	
}

static void
gnome_calendar_init(GnomeCalendar *gcal)
{
	gcal->cal = 0;
	gcal->day_view = 0;
	gcal->week_view = 0;
	gcal->event_editor = 0;

	setup_widgets (gcal);
}

static GtkWidget *
get_current_page (GnomeCalendar *gcal)
{
	return GTK_NOTEBOOK (gcal->notebook)->cur_page->child;
}

void
gnome_calendar_goto (GnomeCalendar *gcal, time_t new_time)
{
	GtkWidget *current = get_current_page (gcal);
	g_assert (new_time != -1);

	if (current == gcal->week_view)
		gncal_week_view_set (GNCAL_WEEK_VIEW (gcal->week_view), new_time);
	else if (current == gcal->day_view)
		printf ("updating day view\n");
	else if (current == gcal->year_view)
		printf ("updating year view\n");
	else
		printf ("My penguin is gone!\n");
	gcal->current_display = new_time;
}

static void
gnome_calendar_direction (GnomeCalendar *gcal, int direction)
{
	GtkWidget *cp = get_current_page (gcal);
	time_t new_time;
	
	if (cp == gcal->week_view)
		new_time = time_add_day (gcal->current_display, 7 * direction);
	else if (cp == gcal->day_view)
		new_time = time_add_day (gcal->current_display, 1 * direction);
	else if (cp == gcal->year_view)
		new_time = time_add_year (gcal->current_display, 1 * direction);
	else
		g_warning ("Weee!  Where did the penguin go?");
	
	gnome_calendar_goto (gcal, new_time);
}

void
gnome_calendar_next (GnomeCalendar *gcal)
{
	gnome_calendar_direction (gcal, 1);
}

void
gnome_calendar_previous (GnomeCalendar *gcal)
{
	gnome_calendar_direction (gcal, -1);
}

GtkWidget *
gnome_calendar_new (char *title)
{
	GtkWidget      *retval;
	GnomeCalendar  *gcal;
	GnomeApp       *app;
		
	retval = gtk_type_new (gnome_calendar_get_type ());
	app = GNOME_APP (retval);
	gcal = GNOME_CALENDAR (retval);
	
	app->name = g_strdup ("calendar");
	app->prefix = g_copy_strings ("/", app->name, "/", NULL);
	
	gtk_window_set_title(GTK_WINDOW(retval), title);

	gcal->current_display = time (NULL);
	gcal->cal = calendar_new (title);
	return retval;
}

void
gnome_calendar_update_all (GnomeCalendar *cal)
{
	gncal_full_day_update (GNCAL_FULL_DAY (cal->day_view));
	gncal_week_view_update (GNCAL_WEEK_VIEW (cal->week_view));
}

void
gnome_calendar_load (GnomeCalendar *gcal, char *file)
{
	calendar_load (gcal->cal, file);
	gnome_calendar_update_all (gcal);
}

void
gnome_calendar_add_object  (GnomeCalendar *gcal, iCalObject *obj)
{
	printf ("Adding object at: ");
	print_time_t (obj->dtstart);
	calendar_add_object (gcal->cal, obj);
	gnome_calendar_update_all (gcal);
}
