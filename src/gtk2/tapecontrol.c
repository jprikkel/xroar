/*

GTK+2 tape control window

Copyright 2011-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "events.h"
#include "fs.h"
#include "tape.h"
#include "vdrive.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/tapecontrol.h"
#include "gtk2/ui_gtk2.h"

/* UI events */
static void update_tape_counters(void *);
static struct event update_tape_counters_event;

/* Tape control widgets */
static GtkWidget *tc_window = NULL;
static GtkWidget *tc_input_filename = NULL;
static GtkWidget *tc_output_filename = NULL;
static GtkWidget *tc_input_time = NULL;
static GtkWidget *tc_output_time = NULL;
static GtkAdjustment *tc_input_adjustment = NULL;
static GtkAdjustment *tc_output_adjustment = NULL;
static GtkTreeView *tc_input_list = NULL;
static GtkListStore *tc_input_list_store = NULL;
static GtkScrollbar *tc_input_progress = NULL;
static GtkScrollbar *tc_output_progress = NULL;
static GtkToggleButton *tc_fast = NULL;
static GtkToggleButton *tc_pad_auto = NULL;
static GtkToggleButton *tc_rewrite = NULL;

static void hide_tc_window(GtkEntry *entry, gpointer user_data);
static void tc_input_rewind(GtkButton *button, gpointer user_data);
static void tc_output_rewind(GtkButton *button, gpointer user_data);
static void tc_input_insert(GtkButton *button, gpointer user_data);
static void tc_output_insert(GtkButton *button, gpointer user_data);
static void tc_input_eject(GtkButton *button, gpointer user_data);
static void tc_output_eject(GtkButton *button, gpointer user_data);

/* Tape control window */

static gchar *ms_to_string(int ms);

enum {
	TC_FILENAME = 0,
	TC_POSITION,
	TC_FILE_POINTER,
	TC_MAX
};

static int have_input_list_store = 0;

static void input_file_selected(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {
	GtkTreeIter iter;
	struct tape_file *file;
	(void)tree_view;
	(void)column;
	(void)user_data;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(tc_input_list_store), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(tc_input_list_store), &iter, TC_FILE_POINTER, &file, -1);
	tape_seek_to_file(xroar_tape_interface->tape_input, file);
}

static void tc_toggled_fast(GtkToggleButton *togglebutton, gpointer user_data);
static void tc_toggled_pad_auto(GtkToggleButton *togglebutton, gpointer user_data);
static void tc_toggled_rewrite(GtkToggleButton *togglebutton, gpointer user_data);
static gboolean tc_input_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data);
static gboolean tc_output_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data);

/* Tape control */

void gtk2_create_tc_window(struct ui_gtk2_interface *uigtk2) {
	GtkBuilder *builder;
	GtkWidget *widget;
	GError *error = NULL;
	builder = gtk_builder_new();

	GBytes *res_tapecontrol = g_resources_lookup_data("/uk/org/6809/xroar/ui_gtk2/tapecontrol.glade", 0, NULL);
	if (!gtk_builder_add_from_string(builder, g_bytes_get_data(res_tapecontrol, NULL), -1, &error)) {
		g_warning("Couldn't create UI: %s", error->message);
		g_error_free(error);
		return;
	}
	g_bytes_unref(res_tapecontrol);

	/* Extract UI elements modified elsewhere */
	tc_window = GTK_WIDGET(gtk_builder_get_object(builder, "tc_window"));
	tc_input_filename = GTK_WIDGET(gtk_builder_get_object(builder, "input_filename"));
	tc_output_filename = GTK_WIDGET(gtk_builder_get_object(builder, "output_filename"));
	tc_input_time = GTK_WIDGET(gtk_builder_get_object(builder, "input_file_time"));
	tc_output_time = GTK_WIDGET(gtk_builder_get_object(builder, "output_file_time"));
	tc_input_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "input_file_adjustment"));
	tc_output_adjustment = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "output_file_adjustment"));
	tc_input_list = GTK_TREE_VIEW(gtk_builder_get_object(builder, "input_file_list_view"));
	tc_input_list_store = GTK_LIST_STORE(gtk_builder_get_object(builder, "input_file_list_store"));
	tc_input_progress = GTK_SCROLLBAR(gtk_builder_get_object(builder, "input_file_progress"));
	tc_output_progress = GTK_SCROLLBAR(gtk_builder_get_object(builder, "output_file_progress"));
	tc_fast = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "fast"));
	tc_pad_auto = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "pad_auto"));
	tc_rewrite = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "rewrite"));

	/* Connect signals */
	g_signal_connect(tc_window, "delete-event", G_CALLBACK(hide_tc_window), uigtk2);
	g_signal_connect(tc_window, "key-press-event", G_CALLBACK(gtk2_dummy_keypress), uigtk2);
	g_signal_connect(tc_input_list, "row-activated", G_CALLBACK(input_file_selected), uigtk2);
	g_signal_connect(tc_input_progress, "change-value", G_CALLBACK(tc_input_progress_change), uigtk2);
	g_signal_connect(tc_output_progress, "change-value", G_CALLBACK(tc_output_progress_change), uigtk2);
	g_signal_connect(tc_fast, "toggled", G_CALLBACK(tc_toggled_fast), uigtk2);
	g_signal_connect(tc_pad_auto, "toggled", G_CALLBACK(tc_toggled_pad_auto), uigtk2);
	g_signal_connect(tc_rewrite, "toggled", G_CALLBACK(tc_toggled_rewrite), uigtk2);

	widget = GTK_WIDGET(gtk_builder_get_object(builder, "input_rewind"));
	g_signal_connect(widget, "clicked", G_CALLBACK(tc_input_rewind), uigtk2);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "input_insert"));
	g_signal_connect(widget, "clicked", G_CALLBACK(tc_input_insert), uigtk2);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "input_eject"));
	g_signal_connect(widget, "clicked", G_CALLBACK(tc_input_eject), uigtk2);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "output_rewind"));
	g_signal_connect(widget, "clicked", G_CALLBACK(tc_output_rewind), uigtk2);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "output_insert"));
	g_signal_connect(widget, "clicked", G_CALLBACK(tc_output_insert), uigtk2);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "output_eject"));
	g_signal_connect(widget, "clicked", G_CALLBACK(tc_output_eject), uigtk2);

	/* In case any signals remain... */
	gtk_builder_connect_signals(builder, uigtk2);
	g_object_unref(builder);

	event_init(&update_tape_counters_event, DELEGATE_AS0(void, update_tape_counters, uigtk2));
	update_tape_counters_event.at_tick = event_current_tick + EVENT_MS(500);
	event_queue(&UI_EVENT_LIST, &update_tape_counters_event);
}

/* Tape Control - helper functions */

static void update_input_list_store(void) {
	if (have_input_list_store) return;
	if (!xroar_tape_interface || !xroar_tape_interface->tape_input) return;
	have_input_list_store = 1;
	struct tape_file *file;
	long old_offset = tape_tell(xroar_tape_interface->tape_input);
	tape_rewind(xroar_tape_interface->tape_input);
	while ((file = tape_file_next(xroar_tape_interface->tape_input, 1))) {
		GtkTreeIter iter;
		int ms = tape_to_ms(xroar_tape_interface->tape_input, file->offset);
		gchar *timestr = ms_to_string(ms);
		gtk_list_store_append(tc_input_list_store, &iter);
		gtk_list_store_set(tc_input_list_store, &iter,
				   TC_FILENAME, file->name,
				   TC_POSITION, timestr,
				   TC_FILE_POINTER, file,
				   -1);
	}
	tape_seek(xroar_tape_interface->tape_input, old_offset, SEEK_SET);
}

static gchar *ms_to_string(int ms) {
	static gchar timestr[9];
	int min, sec;
	sec = ms / 1000;
	min = sec / 60;
	sec %= 60;
	min %= 60;
	snprintf(timestr, sizeof(timestr), "%02d:%02d", min, sec);
	return timestr;
}

static void tc_seek(struct tape *tape, GtkScrollType scroll, gdouble value) {
	if (tape) {
		int seekms = 0;
		switch (scroll) {
			case GTK_SCROLL_STEP_BACKWARD:
				seekms = tape_to_ms(tape, tape->offset) - 1000;
				break;
			case GTK_SCROLL_STEP_FORWARD:
				seekms = tape_to_ms(tape, tape->offset) + 1000;
				break;
			case GTK_SCROLL_PAGE_BACKWARD:
				seekms = tape_to_ms(tape, tape->offset) - 5000;
				break;
			case GTK_SCROLL_PAGE_FORWARD:
				seekms = tape_to_ms(tape, tape->offset) + 5000;
				break;
			case GTK_SCROLL_JUMP:
				seekms = (int)value;
				break;
			default:
				return;
		}
		if (seekms < 0) return;
		long seek_to = tape_ms_to(tape, seekms);
		if (seek_to > tape->size) seek_to = tape->size;
		tape_seek(tape, seek_to, SEEK_SET);
	}
}

/* Tape Control - scheduled event handlers */

static void update_tape_counters(void *data) {
	(void)data;
	static long omax = -1, opos = -1;
	static long imax = -1, ipos = -1;
	long new_omax = 0, new_opos = 0;
	long new_imax = 0, new_ipos = 0;
	if (xroar_tape_interface->tape_input) {
		new_imax = tape_to_ms(xroar_tape_interface->tape_input, xroar_tape_interface->tape_input->size);
		new_ipos = tape_to_ms(xroar_tape_interface->tape_input, xroar_tape_interface->tape_input->offset);
	}
	if (xroar_tape_interface->tape_output) {
		new_omax = tape_to_ms(xroar_tape_interface->tape_output, xroar_tape_interface->tape_output->size);
		new_opos = tape_to_ms(xroar_tape_interface->tape_output, xroar_tape_interface->tape_output->offset);
	}
	if (imax != new_imax) {
		imax = new_imax;
		gtk_adjustment_set_upper(GTK_ADJUSTMENT(tc_input_adjustment), (gdouble)imax);
	}
	if (ipos != new_ipos) {
		ipos = new_ipos;
		gtk_adjustment_set_value(GTK_ADJUSTMENT(tc_input_adjustment), (gdouble)ipos);
		gtk_label_set_text(GTK_LABEL(tc_input_time), ms_to_string(new_ipos));
	}
	if (omax != new_omax) {
		omax = new_omax;
		gtk_adjustment_set_upper(GTK_ADJUSTMENT(tc_output_adjustment), (gdouble)omax);
	}
	if (opos != new_opos) {
		opos = new_opos;
		gtk_adjustment_set_value(GTK_ADJUSTMENT(tc_output_adjustment), (gdouble)opos);
		gtk_label_set_text(GTK_LABEL(tc_output_time), ms_to_string(new_opos));
	}
	update_tape_counters_event.at_tick += EVENT_MS(500);
	event_queue(&UI_EVENT_LIST, &update_tape_counters_event);
}

/* Tape Control - UI callbacks */

void gtk2_input_tape_filename_cb(struct ui_gtk2_interface *uigtk2, const char *filename) {
	GtkToggleAction *toggle = (GtkToggleAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/ToolMenu/TapeControl");
	gtk_label_set_text(GTK_LABEL(tc_input_filename), filename);
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tc_input_list_store), &iter)) {
		do {
			struct tape_file *file;
			gtk_tree_model_get(GTK_TREE_MODEL(tc_input_list_store), &iter, TC_FILE_POINTER, &file, -1);
			g_free(file);
		} while (gtk_tree_model_iter_next(GTK_TREE_MODEL(tc_input_list_store), &iter));
	}
	gtk_list_store_clear(tc_input_list_store);
	have_input_list_store = 0;
	if (gtk_toggle_action_get_active(toggle)) {
		update_input_list_store();
	}
}

void gtk2_output_tape_filename_cb(struct ui_gtk2_interface *uigtk2, const char *filename) {
	(void)uigtk2;
	gtk_label_set_text(GTK_LABEL(tc_output_filename), filename);
}

static void tc_toggled_fast(GtkToggleButton *togglebutton, gpointer user_data) {
	(void)user_data;
	int set = gtk_toggle_button_get_active(togglebutton) ? TAPE_FAST : 0;
	int flags = (tape_get_state(xroar_tape_interface) & ~TAPE_FAST) | set;
	tape_set_state(xroar_tape_interface, flags);
}

static void tc_toggled_pad_auto(GtkToggleButton *togglebutton, gpointer user_data) {
	(void)user_data;
	int set = gtk_toggle_button_get_active(togglebutton) ? TAPE_PAD_AUTO : 0;
	int flags = (tape_get_state(xroar_tape_interface) & ~TAPE_PAD_AUTO) | set;
	tape_set_state(xroar_tape_interface, flags);
}

static void tc_toggled_rewrite(GtkToggleButton *togglebutton, gpointer user_data) {
	(void)user_data;
	int set = gtk_toggle_button_get_active(togglebutton) ? TAPE_REWRITE : 0;
	int flags = (tape_get_state(xroar_tape_interface) & ~TAPE_REWRITE) | set;
	tape_set_state(xroar_tape_interface, flags);
}

void gtk2_update_tape_state(struct ui_gtk2_interface *uigtk2, int flags) {
	uigtk2_notify_toggle_button_set(tc_fast, (flags & TAPE_FAST) ? TRUE : FALSE, tc_toggled_fast, uigtk2);
	uigtk2_notify_toggle_button_set(tc_pad_auto, (flags & TAPE_PAD_AUTO) ? TRUE : FALSE, tc_toggled_pad_auto, uigtk2);
	uigtk2_notify_toggle_button_set(tc_rewrite, (flags & TAPE_REWRITE) ? TRUE : FALSE, tc_toggled_rewrite, uigtk2);
}

/* Tape Control - Signal Handlers */

void gtk2_toggle_tc_window(GtkToggleAction *current, gpointer user_data) {
	gboolean val = gtk_toggle_action_get_active(current);
	(void)user_data;
	if (val) {
		gtk_widget_show(tc_window);
		update_input_list_store();
	} else {
		gtk_widget_hide(tc_window);
	}
}

static void hide_tc_window(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	GtkToggleAction *toggle = (GtkToggleAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/ToolMenu/TapeControl");
	gtk_toggle_action_set_active(toggle, 0);
}

/* Tape Control - Signal Handlers - Input Tab */

static gboolean tc_input_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data) {
	(void)range;
	(void)user_data;
	tc_seek(xroar_tape_interface->tape_input, scroll, value);
	return TRUE;
}

static void tc_input_rewind(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	if (xroar_tape_interface->tape_input) {
		tape_seek(xroar_tape_interface->tape_input, 0, SEEK_SET);
	}
}

static void tc_input_insert(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_insert_input_tape();
}

static void tc_input_eject(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_eject_input_tape();
}

static gboolean tc_output_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data) {
	(void)range;
	(void)user_data;
	tc_seek(xroar_tape_interface->tape_output, scroll, value);
	return TRUE;
}

static void tc_output_rewind(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	if (xroar_tape_interface && xroar_tape_interface->tape_output) {
		tape_seek(xroar_tape_interface->tape_output, 0, SEEK_SET);
	}
}

static void tc_output_insert(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_insert_output_tape();
}

static void tc_output_eject(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_eject_output_tape();
}
