/*
 * EventEditor widget
 * Copyright (C) 1998 the Free Software Foundation
 *
 * Author: Miguel de Icaza (miguel@kernel.org)
 */

#include <gnome.h>
#include <string.h>
#include "calendar.h"
#include "eventedit.h"
#include "main.h"
#include "timeutil.h"

static void event_editor_init          (EventEditor *ee);
GtkWindow *parent_class;

/* Note: do not i18n these strings, they are part of the vCalendar protocol */
char *class_names [] = { "PUBLIC", "PRIVATE", "CONFIDENTIAL" };

guint
event_editor_get_type (void)
{
	static guint event_editor_type = 0;
	
	if(!event_editor_type) {
		GtkTypeInfo event_editor_info = {
			"EventEditor",
			sizeof(EventEditor),
			sizeof(EventEditorClass),
			(GtkClassInitFunc) NULL,
			(GtkObjectInitFunc) event_editor_init,
			(GtkArgSetFunc) NULL,
			(GtkArgGetFunc) NULL,
		};
		event_editor_type = gtk_type_unique (gtk_window_get_type (), &event_editor_info);
		parent_class = gtk_type_class (gtk_window_get_type ());
	}
	return event_editor_type;
}

/*
 * when the start time is changed, this adjusts the end time. 
 */
static void
adjust_end_time (GtkWidget *widget, EventEditor *ee)
{
	struct tm *tm;
	time_t start_t;

	start_t = gnome_date_edit_get_date (GNOME_DATE_EDIT (ee->start_time));
	tm = localtime (&start_t);
	if (tm->tm_hour < 22)
		tm->tm_hour++;
	gnome_date_edit_set_time (GNOME_DATE_EDIT (ee->end_time), mktime (tm));
}

GtkWidget *
adjust (GtkWidget *w, gfloat x, gfloat y, gfloat xs, gfloat ys)
{
	GtkWidget *a = gtk_alignment_new (x, y, xs, ys);
	
	gtk_container_add (GTK_CONTAINER (a), w);
	return a;
}

/*
 * Checks if the day range occupies all the day, and if so, check the
 * box accordingly
 */
static void
ee_check_all_day (EventEditor *ee)
{
	time_t ev_start, ev_end;

	ev_start = gnome_date_edit_get_date (GNOME_DATE_EDIT (ee->start_time));
	ev_end   = gnome_date_edit_get_date (GNOME_DATE_EDIT (ee->end_time)); 
	
	if (get_time_t_hour (ev_start) <= day_begin && get_time_t_hour (ev_end) >= day_end){
		gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (ee->general_allday), 1);
	} else{
		gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (ee->general_allday), 0);
	}
}

/*
 * Callback: checks if the selected hour range spans all of the day
 */
static void
check_times (GtkWidget *widget, EventEditor *ee)
{
	ee_check_all_day (ee);
}

/*
 * Callback: all day event box clicked
 */
static void
set_all_day (GtkToggleButton *toggle, EventEditor *ee)
{
	struct tm *tm;
	time_t start_t;

	start_t = gnome_date_edit_get_date (GNOME_DATE_EDIT (ee->start_time));
	tm = localtime (&start_t);
	tm->tm_hour = day_begin;
	tm->tm_min  = 0;
	tm->tm_sec  = 0;
	gnome_date_edit_set_time (GNOME_DATE_EDIT (ee->start_time), mktime (tm));
	
	if (toggle->active)
		tm->tm_hour = day_end;
	else
		tm->tm_hour++;
	
	gnome_date_edit_set_time (GNOME_DATE_EDIT (ee->end_time), mktime (tm));
}

static GtkWidget *
event_editor_setup_time_frame (EventEditor *ee)
{
	GtkWidget *frame;
	GtkWidget *start_time, *end_time;
	GtkTable  *t;
	
	frame = gtk_frame_new (_("Time"));
	t = GTK_TABLE (ee->general_time_table = gtk_table_new (1, 1, 0));
	gtk_container_add (GTK_CONTAINER (frame), ee->general_time_table);

	/* 1. Start time */
	ee->start_time = start_time = gnome_date_edit_new (ee->ical->dtstart);
	gnome_date_edit_set_popup_range ((GnomeDateEdit *) start_time, day_begin, day_end);
	gtk_signal_connect (GTK_OBJECT (start_time), "time_changed",
			    GTK_SIGNAL_FUNC (adjust_end_time), ee);
	gtk_signal_connect (GTK_OBJECT (start_time), "time_changed",
			    GTK_SIGNAL_FUNC (check_times), ee);
	gtk_table_attach (t, gtk_label_new (_("Start time")), 1, 2, 1, 2, 0, 0, 0, 0);
	gtk_table_attach (t, start_time, 2, 3, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

	/* 2. End time */
	ee->end_time   = end_time   = gnome_date_edit_new (ee->ical->dtend);
	gnome_date_edit_set_popup_range ((GnomeDateEdit *) end_time,   day_begin, day_end);
	gtk_signal_connect (GTK_OBJECT (end_time), "time_changed",
			    GTK_SIGNAL_FUNC (check_times), ee);
	gtk_table_attach (t, gtk_label_new (_("End time")), 1, 2, 2, 3, 0, 0, 0, 0);
	gtk_table_attach (t, end_time, 2, 3, 2, 3,   GTK_EXPAND | GTK_FILL, 0, 0, 0);

	/* 3. All day checkbox */
	ee->general_allday = gtk_check_button_new_with_label (_("All day event"));
	gtk_signal_connect (GTK_OBJECT (ee->general_allday), "toggled",
			    GTK_SIGNAL_FUNC (set_all_day), ee);
	gtk_table_attach (t, ee->general_allday, 3, 4, 1, 2, 0, 0, 0, 0);
	ee_check_all_day (ee);

	/* 4. Recurring event checkbox */
	ee->general_recur  = gtk_check_button_new_with_label (_("Recurring event"));
	gtk_table_attach (t, ee->general_recur, 3, 4, 2, 3, 0, 0, 0, 0);

	gtk_container_border_width (GTK_CONTAINER (frame), 5);
	return frame;
}

static GtkWidget *
timesel_new (void)
{
	GtkWidget *menu, *option_menu;
	char *items [] = { N_("Minutes"), N_("Hours"), N_("Days") };
	int i;
	
	option_menu = gtk_option_menu_new ();
	menu = gtk_menu_new ();
	for (i = 0; i < 3; i++){
		GtkWidget *item;

		item = gtk_menu_item_new_with_label (_(items [i]));
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_widget_show (item);
	}
	gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);
	return option_menu;
}

/*
 * Set the sensitive state depending on whether the alarm enabled flag.
 */
static void
ee_alarm_setting (CalendarAlarm *alarm, int sensitive)
{
	gtk_widget_set_sensitive (GTK_WIDGET (alarm->w_count), sensitive);
	gtk_widget_set_sensitive (GTK_WIDGET (alarm->w_timesel), sensitive);

	if (alarm->type == ALARM_PROGRAM || alarm->type == ALARM_MAIL){
		gtk_widget_set_sensitive (GTK_WIDGET (alarm->w_entry), sensitive);
		gtk_widget_set_sensitive (GTK_WIDGET (alarm->w_label), sensitive);
	}
}

static void
alarm_toggle (GtkToggleButton *toggle, CalendarAlarm *alarm)
{
	ee_alarm_setting (alarm, toggle->active);
}

#define FX GTK_FILL | GTK_EXPAND
#define XCOL 6
static void
ee_create_ae (GtkTable *table, char *str, CalendarAlarm *alarm, enum AlarmType type, int y)
{
	char buffer [40];

	alarm->w_enabled = gtk_check_button_new_with_label (str);
	gtk_signal_connect (GTK_OBJECT (alarm->w_enabled), "toggled",
			    GTK_SIGNAL_FUNC (alarm_toggle), alarm);
	gtk_table_attach (table, alarm->w_enabled, 2, 3, y, y+1, FX, 0, 0, 0);

	gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (alarm->w_enabled), alarm->enabled);
	
	alarm->w_count = gtk_entry_new ();
	gtk_widget_set_usize (alarm->w_count, 40, 0);
	gtk_table_attach (table, alarm->w_count, 3, 4, y, y+1, FX, 0, 5, 0);
	sprintf (buffer, "%d", alarm->count);
	gtk_entry_set_text (GTK_ENTRY (alarm->w_count), buffer);
	
	alarm->w_timesel = timesel_new ();
	gtk_option_menu_set_history (GTK_OPTION_MENU (alarm->w_timesel), alarm->units);
	gtk_table_attach (table, alarm->w_timesel, 4, 5, y, y+1, 0, 0, 0, 0);
	
	switch (type){
	case ALARM_MAIL:
		alarm->w_label = gtk_label_new (_("Mail to:"));
		gtk_misc_set_alignment (GTK_MISC (alarm->w_label), 1.0, 0.5);	
		gtk_table_attach (table, alarm->w_label, XCOL, XCOL+1, y, y+1, FX, 0, 5, 0);
		alarm->w_entry = gtk_entry_new ();
		gtk_table_attach (table, alarm->w_entry, XCOL+1, XCOL+2, y, y+1, FX, 0, 6, 0);
		gtk_entry_set_text (GTK_ENTRY (alarm->w_entry), alarm->data);
		break;

	case ALARM_PROGRAM:
		alarm->w_label = gtk_label_new (_("Run program:"));
		gtk_misc_set_alignment (GTK_MISC (alarm->w_label), 1.0, 0.5);	
		gtk_table_attach (table, alarm->w_label, XCOL, XCOL+1, y, y+1, FX, 0, 5, 0);
		alarm->w_entry = gnome_file_entry_new ("alarm-program", _("Select program to run at alarm time"));
		gtk_table_attach (table, alarm->w_entry, XCOL+1, XCOL+2, y, y+1, 0, 0, 6, 0);
		break;

	default:
		/* Nothing */
	}

	ee_alarm_setting (alarm, alarm->enabled);
}

static GtkWidget *
ee_alarm_widgets (EventEditor *ee)
{
	GtkWidget *table, *mailto, *mailte, *l;
	
	l = gtk_frame_new (_("Alarms"));
	
	table = gtk_table_new (1, 1, 0);
	gtk_table_set_row_spacings (GTK_TABLE (table), 3);
	gtk_container_add (GTK_CONTAINER (l), table);
	
	mailto  = gtk_label_new (_("Mail to:"));
	mailte  = gtk_entry_new ();

	ee_create_ae (GTK_TABLE (table), _("Display"), &ee->ical->dalarm, ALARM_DISPLAY, 1);
	ee_create_ae (GTK_TABLE (table), _("Audio"),   &ee->ical->aalarm, ALARM_AUDIO, 2);
	ee_create_ae (GTK_TABLE (table), _("Program"), &ee->ical->palarm, ALARM_PROGRAM, 3);
	ee_create_ae (GTK_TABLE (table), _("Mail"),    &ee->ical->malarm, ALARM_MAIL, 4);
	
	return l;
}

static void
connect_and_pack (EventEditor *ee, GtkWidget *hbox, GtkWidget *toggle, char *value)
{
	gtk_box_pack_start_defaults (GTK_BOX (hbox), toggle);
}

static GtkWidget *
ee_classification_widgets (EventEditor *ee)
{
	GtkWidget *rpub, *rpriv, *conf;
	GtkWidget *frame, *hbox;

	frame = gtk_frame_new (_("Classification"));
	hbox  = gtk_hbox_new (0, 0);
	gtk_container_add (GTK_CONTAINER (frame), hbox);
	
	rpub  = gtk_radio_button_new_with_label (NULL, _("Public"));
	rpriv = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (rpub), _("Private"));
	conf  = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (rpub), _("Confidential"));

	connect_and_pack (ee, hbox, rpub,  class_names [0]);
	connect_and_pack (ee, hbox, rpriv, class_names [1]);
	connect_and_pack (ee, hbox, conf,  class_names [2]);
	ee->general_radios = rpub;
	
	return frame;
}

/*
 * Retrieves the information from the CalendarAlarm widgets and stores them
 * on the CalendarAlarm generic values
 */
void
ee_store_alarm (CalendarAlarm *alarm, enum AlarmType type)
{
	GtkWidget *item;
	GtkMenu   *menu;
	GList     *child;
	int idx;
	
	if (alarm->data){
		g_free (alarm->data);
		alarm->data = 0;
	}
	
	alarm->enabled = GTK_TOGGLE_BUTTON (alarm->w_enabled)->active;

	if (!alarm->enabled)
		return;
	
	if (type == ALARM_PROGRAM)
		alarm->data = g_strdup (gtk_entry_get_text (GTK_ENTRY (gnome_file_entry_gtk_entry (alarm->w_entry))));
	if (type == ALARM_MAIL)
		alarm->data = g_strdup (gtk_entry_get_text (GTK_ENTRY (alarm->w_entry)));

	/* Find out the index */
	menu = GTK_MENU (GTK_OPTION_MENU (alarm->w_timesel)->menu);
	
	item = gtk_menu_get_active (menu);
	
	for (idx = 0, child = menu->children; child->data != item; child = child->next)
		idx++;
	
	alarm->units = idx;
	alarm->count = atoi (gtk_entry_get_text (GTK_ENTRY (alarm->w_count)));
}

/*
 * Retrieves all of the information from the different widgets and updates
 * the iCalObject accordingly.
 */
static void
ee_store_dlg_values_to_ical (EventEditor *ee)
{
	GtkRadioButton *radio = GTK_RADIO_BUTTON (ee->general_radios);
	iCalObject *ical = ee->ical;
	GSList *list = radio->group;
	int idx;
	time_t now;

	now = time (NULL);
	ical->dtstart = gnome_date_edit_get_date (GNOME_DATE_EDIT (ee->start_time));
	ical->dtend   = gnome_date_edit_get_date (GNOME_DATE_EDIT (ee->end_time));

	ee_store_alarm (&ical->dalarm, ALARM_DISPLAY);
	ee_store_alarm (&ical->aalarm, ALARM_AUDIO);
	ee_store_alarm (&ical->palarm, ALARM_PROGRAM);
	ee_store_alarm (&ical->malarm, ALARM_MAIL);

	for (idx = 0; list; list = list->next){
		if (GTK_TOGGLE_BUTTON (list->data)->active)
			break;
		idx++;
	}
	g_free (ical->class);
	ical->class = g_strdup (class_names [idx]);

	/* FIXME: This is not entirely correct; we should check if the values actually changed */
	ical->last_mod = now;

	if (ee->new_ical)
		ical->created = now;

	g_free (ical->summary);
	ical->summary = gtk_editable_get_chars (GTK_EDITABLE (ee->general_summary), 0, -1);
}

static void
ee_ok (GtkWidget *widget, EventEditor *ee)
{
	ee_store_dlg_values_to_ical (ee);

	if (ee->new_ical)
		gnome_calendar_add_object (GNOME_CALENDAR (ee->gnome_cal), ee->ical);

	gtk_widget_destroy (GTK_WIDGET (ee));
}

static void
ee_cancel (GtkWidget *widget, EventEditor *ee)
{
	if (ee->new_ical)
		ical_object_destroy (ee->ical);
	gtk_widget_destroy (GTK_WIDGET (ee));
}

static GtkWidget *
ee_create_buttons (EventEditor *ee)
{
	GtkWidget *box = gtk_hbox_new (1, 5);
	GtkWidget *ok, *cancel;

	ok     = gnome_stock_button (GNOME_STOCK_BUTTON_OK);
	cancel = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);

	gtk_box_pack_start (GTK_BOX (box), ok, 0, 0, 5);
	gtk_box_pack_start (GTK_BOX (box), cancel, 0, 0, 5);

	gtk_signal_connect (GTK_OBJECT (ok),     "clicked", GTK_SIGNAL_FUNC(ee_ok), ee);
	gtk_signal_connect (GTK_OBJECT (cancel), "clicked", GTK_SIGNAL_FUNC(ee_cancel), ee);
	
	return box;
}

/*
 * Load the contents in a delayed fashion, as the GtkText widget needs it
 */
static void
ee_fill_summary (GtkWidget *widget, EventEditor *ee)
{
	int pos = 0;

	gtk_editable_insert_text (GTK_EDITABLE (ee->general_summary), ee->ical->summary,
				  strlen (ee->ical->summary), &pos);
	gtk_text_thaw (GTK_TEXT (ee->general_summary));
}

enum {
	OWNER_LINE,
	DESC_LINE,
	SUMMARY_LINE,
	TIME_LINE = 4,
	ALARM_LINE,
	CLASS_LINE = 8
};

#define LABEL_SPAN 2

static void
event_editor_init_widgets (EventEditor *ee)
{
	GtkWidget *frame, *l;
	
	ee->hbox = gtk_vbox_new (0, 0);
	gtk_container_add (GTK_CONTAINER (ee), ee->hbox);
	gtk_container_border_width (GTK_CONTAINER (ee), 5);
	
	ee->notebook = gtk_notebook_new ();
	gtk_box_pack_start (GTK_BOX (ee->hbox), ee->notebook, 1, 1, 0);
	
	ee->general_table = (GtkTable *) gtk_table_new (1, 1, 0);
	gtk_notebook_append_page (GTK_NOTEBOOK (ee->notebook), GTK_WIDGET (ee->general_table),
				  gtk_label_new (_("General")));
	
	l = adjust (gtk_label_new (_("Owner:")), 1.0, 0.5, 1.0, 1.0);
	gtk_table_attach (ee->general_table, l,
			  1, LABEL_SPAN, OWNER_LINE, OWNER_LINE + 1, GTK_FILL|GTK_EXPAND, 0, 0, 6);

	ee->general_owner = gtk_label_new (ee->ical->organizer);
	gtk_table_attach (ee->general_table, ee->general_owner,
			  LABEL_SPAN, LABEL_SPAN + 1, OWNER_LINE, OWNER_LINE + 1, GTK_FILL|GTK_EXPAND, 0, 0, 0);

	l = gtk_label_new (_("Description:"));
	gtk_table_attach (ee->general_table, l,
			  1, LABEL_SPAN, DESC_LINE, DESC_LINE + 1, GTK_FILL|GTK_EXPAND, 0, 0, 0);
	
	ee->general_summary = gtk_text_new (NULL, NULL);
	gtk_text_freeze (GTK_TEXT (ee->general_summary));
	gtk_signal_connect (GTK_OBJECT (ee->general_summary), "realize",
			    GTK_SIGNAL_FUNC (ee_fill_summary), ee);
	gtk_widget_set_usize (ee->general_summary, 0, 60);
	gtk_text_set_editable (GTK_TEXT (ee->general_summary), 1);
	gtk_table_attach (ee->general_table, ee->general_summary,
			  1, 40, SUMMARY_LINE, SUMMARY_LINE+1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 6, 0);

	frame = event_editor_setup_time_frame (ee);
	gtk_table_attach (ee->general_table, frame,
			  1, 40, TIME_LINE + 2, TIME_LINE + 3,
			  GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);

	l = ee_alarm_widgets (ee);
	gtk_table_attach (ee->general_table, l,
			  1, 40, ALARM_LINE, ALARM_LINE + 1,
			  0, 0, 0, 0);

	l = ee_classification_widgets (ee);
	gtk_table_attach (ee->general_table, l,
			  1, 40, CLASS_LINE, CLASS_LINE + 1,
			  0, 0, 0, 0);
	/* Separator */
	gtk_box_pack_start (GTK_BOX (ee->hbox), gtk_hseparator_new (), 1, 1, 0);

	/* Buttons */
	gtk_box_pack_start (GTK_BOX (ee->hbox), ee_create_buttons (ee), 0, 0, 5);
	
	/* We show all of the contained widgets */
	gtk_widget_show_all (GTK_WIDGET (ee));
	/* And we hide the toplevel, to be consistent with the rest of Gtk */
	gtk_widget_hide (GTK_WIDGET (ee));
}

static void
event_editor_init (EventEditor *ee)
{
	ee->ical = 0;
}

GtkWidget *
event_editor_new (GnomeCalendar *gcal, iCalObject *ical)
{
	GtkWidget *retval;
	EventEditor *ee;
	
	retval = gtk_type_new (event_editor_get_type ());
	ee = EVENT_EDITOR (retval);
	
	if (ical == 0){
		ee->new_ical = 1;
		ical = ical_new ("Test Comment", user_name, "Test Summary");
	} else
		ee->new_ical = 0;
	
	ee->ical = ical;
	ee->gnome_cal = gcal;
	event_editor_init_widgets (ee);
	
	return retval;
}

/*
 * New event:  Create iCal, edit, check result: Ok: insert;  Cancel: destroy iCal
 * Edit event: fetch  iCal, edit, check result: Ok: remove from calendar, add to calendar; Cancel: nothing
 */
