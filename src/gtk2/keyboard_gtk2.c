/*

GTK+2 keyboard support

Copyright 2010-2016 Ciaran Anscomb

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

// For strsep()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <glib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "pl-string.h"

#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "module.h"
#include "printer.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/ui_gtk2.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *, unsigned);
static struct joystick_button *configure_button(char *, unsigned);
static void unmap_axis(struct joystick_axis *axis);
static void unmap_button(struct joystick_button *button);

struct joystick_submodule gtk2_js_submod_keyboard = {
	.name = "keyboard",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
	.unmap_axis = unmap_axis,
	.unmap_button = unmap_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct axis {
	unsigned key0, key1;
	unsigned value;
};

struct button {
	unsigned key;
	_Bool value;
};

#define MAX_AXES (4)
#define MAX_BUTTONS (4)

static struct axis *enabled_axis[MAX_AXES];
static struct button *enabled_button[MAX_BUTTONS];

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static gboolean keypress(GtkWidget *, GdkEventKey *, gpointer);
static gboolean keyrelease(GtkWidget *, GdkEventKey *, gpointer);

struct sym_dkey_mapping {
	unsigned sym;
	int8_t dkey;
	_Bool priority;
};

struct keymap {
	const char *name;
	const char *description;
	int num_mappings;
	struct sym_dkey_mapping *mappings;
};

#include "keyboard_gtk2_mappings.c"

/*
 * Groups of 256 keyvals:
 * 0x0000-0x00ff ... 0x0000-0x00ff
 * 0x0100-0x01ff ... 0x0100-0x01ff
 * 0xfe00-0xfeff ... 0x0200-0x02ff
 * 0xff00-0xffff ... 0x0300-0x03ff
 */

static int8_t keyval_to_dkey[0x0400];
static _Bool keyval_priority[0x0400];

/* Need to define some sort of sensible limit to the keycodes: */
#define MAX_KEYCODE (256)

/* For untranslated mode: unshifted keyvals for each keycode: */
static guint keycode_to_keyval[MAX_KEYCODE];

/* For translated mode: unicode value last generated for each keycode: */
static guint32 last_unicode[MAX_KEYCODE];

static struct sym_dkey_mapping keyval_dkey_default[] = {
	// Common
	{ GDK_Escape, DSCAN_BREAK, 1 },
	{ GDK_Return, DSCAN_ENTER, 0 },
	{ GDK_Home, DSCAN_CLEAR, 1 },
	{ GDK_Shift_L, DSCAN_SHIFT, 1 },
	{ GDK_Shift_R, DSCAN_SHIFT, 1 },
	{ GDK_space, DSCAN_SPACE, 0 },
	{ GDK_F1, DSCAN_F1, 1 },
	{ GDK_F2, DSCAN_F2, 1 },

        // Not so common
        { GDK_Break, DSCAN_BREAK, 1 },
        { GDK_Clear, DSCAN_CLEAR, 1 },

        // Cursor keys
        { GDK_Up, DSCAN_UP, 1 },
        { GDK_Down, DSCAN_DOWN, 1 },
        { GDK_Left, DSCAN_LEFT, 1 },
        { GDK_Right, DSCAN_RIGHT, 1 },
        { GDK_BackSpace, DSCAN_LEFT, 1 },
        { GDK_KP_Delete, DSCAN_LEFT, 1 },
        { GDK_Tab, DSCAN_RIGHT, 1 },

        // Keypad
	{ GDK_KP_Up, DSCAN_UP, 1 },
	{ GDK_KP_Down, DSCAN_DOWN, 1 },
	{ GDK_KP_Left, DSCAN_LEFT, 1 },
	{ GDK_KP_Right, DSCAN_RIGHT, 1 },
        { GDK_KP_Multiply, DSCAN_COLON, 1 },
        { GDK_KP_Subtract, DSCAN_MINUS, 1 },
        { GDK_KP_Add, DSCAN_SEMICOLON, 1 },
        { GDK_KP_Decimal, DSCAN_FULL_STOP, 1 },
        { GDK_KP_Divide, DSCAN_SLASH, 1 },
        { GDK_KP_Enter, DSCAN_ENTER, 0 },
};

static int keyval_index(guint keyval) {
	switch ((keyval >> 8) & 0xff) {
	case 0: case 1:
		return keyval;
	case 0xfe: case 0xff:
		return keyval & 0x3ff;
	default:
		break;
	}
	return 0xff;
}

static gboolean map_keyboard(GdkKeymap *gdk_keymap, gpointer user_data) {
	struct keymap *keymap = (struct keymap *)user_data;

	/* Map keycodes to unshifted keyval: */
	for (int i = 0; i < MAX_KEYCODE; i++) {
		guint *keyvals;
		gint n_entries;
		if (gdk_keymap_get_entries_for_keycode(gdk_keymap, i, NULL, &keyvals, &n_entries) == TRUE) {
			if (n_entries > 0) {
				guint keyval = keyvals[0];
				keycode_to_keyval[i] = keyval;
			}
			g_free(keyvals);
		} else {
			keycode_to_keyval[i] = 0;
		}
	}

	/* Initialise keycode->unicode tracking: */
	for (int i = 0; i < MAX_KEYCODE; i++)
		last_unicode[i] = 0;

	/* First clear the table and map obvious keys */
	for (unsigned i = 0; i < G_N_ELEMENTS(keyval_to_dkey); i++) {
		keyval_to_dkey[i] = DSCAN_INVALID;
		keyval_priority[i] = 0;
	}
	for (unsigned i = 0; i < G_N_ELEMENTS(keyval_dkey_default); i++) {
		keyval_to_dkey[keyval_index(keyval_dkey_default[i].sym)] = keyval_dkey_default[i].dkey;
		keyval_priority[keyval_index(keyval_dkey_default[i].sym)] = keyval_dkey_default[i].priority;
	}
	// 0 - 9
	for (int i = 0; i <= 9; i++) {
		keyval_to_dkey[keyval_index(GDK_0 + i)] = DSCAN_0 + i;
		keyval_to_dkey[keyval_index(GDK_KP_0 + i)] = DSCAN_0 + i;
	}
	// A - Z
	for (int i = 0; i <= 25; i++)
		keyval_to_dkey[keyval_index(GDK_a + i)] = DSCAN_A + i;

	/* Apply keyboard map if selected: */
	if (keymap == NULL)
		return FALSE;
	int num_mappings = keymap->num_mappings;
	struct sym_dkey_mapping *mappings = keymap->mappings;
	for (int i = 0; i < num_mappings; i++) {
		keyval_to_dkey[keyval_index(mappings[i].sym)] = mappings[i].dkey;
		keyval_priority[keyval_index(mappings[i].sym)] = mappings[i].priority;
	}
	return FALSE;
}

void gtk2_keyboard_init(struct ui_cfg *ui_cfg) {
	const char *keymap_option = ui_cfg->keymap;
	struct keymap *selected_keymap = &keymaps[0];
	if (keymap_option) {
		if (0 == strcmp(keymap_option, "help")) {
			for (unsigned i = 0; i < G_N_ELEMENTS(keymaps); i++) {
				if (keymaps[i].description)
					printf("\t%-10s %s\n", keymaps[i].name, keymaps[i].description);
			}
			exit(EXIT_SUCCESS);
		}
		for (unsigned i = 0; i < G_N_ELEMENTS(keymaps); i++) {
			if (0 == strcmp(keymap_option, keymaps[i].name)) {
				selected_keymap = &keymaps[i];
				LOG_DEBUG(1, "\tSelecting '%s' keymap\n", keymap_option);
			}
		}
	}
	/* Map initial layout and connect keys-changed signal */
	GdkKeymap *gdk_keymap = gdk_keymap_get_for_display(gdk_display_get_default());
	map_keyboard(gdk_keymap, selected_keymap);
	g_signal_connect(G_OBJECT(gdk_keymap), "keys-changed", G_CALLBACK(map_keyboard), selected_keymap);
	/* Connect GTK key press/release signals to handlers */
	g_signal_connect(G_OBJECT(global_uigtk2->top_window), "key-press-event", G_CALLBACK(keypress), NULL);
	g_signal_connect(G_OBJECT(global_uigtk2->top_window), "key-release-event", G_CALLBACK(keyrelease), NULL);
}

static void emulator_command(guint keyval, int shift) {
	switch (keyval) {
	case GDK_1: case GDK_2: case GDK_3: case GDK_4:
		if (shift) {
			xroar_new_disk(keyval - GDK_1);
		}
		break;
	case GDK_5: case GDK_6: case GDK_7: case GDK_8:
		if (shift) {
			xroar_set_write_back(1, keyval - GDK_5, XROAR_NEXT);
		} else {
			xroar_set_write_enable(1, keyval - GDK_5, XROAR_NEXT);
		}
		break;
	case GDK_a:
		xroar_set_cross_colour(1, XROAR_NEXT);
		break;
	case GDK_e:
		xroar_toggle_cart();
		break;
	case GDK_f:
		xroar_set_fullscreen(1, XROAR_NEXT);
		break;
	case GDK_h:
		if (shift)
			xroar_set_pause(1, XROAR_NEXT);
		break;
	case GDK_i:
		if (shift)
			xroar_set_vdg_inverted_text(1, XROAR_NEXT);
		else
			xroar_run_file(NULL);
		break;
	case GDK_j:
		if (shift) {
			xroar_swap_joysticks(1);
		} else {
			xroar_cycle_joysticks(1);
		}
		break;
	case GDK_k:
		xroar_set_keymap(1, XROAR_NEXT);
		break;
	case GDK_m:
		xroar_set_machine(1, XROAR_NEXT);
		break;
	case GDK_p:
		if (shift)
			printer_flush(xroar_printer_interface);
		break;
	case GDK_w:
		xroar_insert_output_tape();
		break;
#ifdef TRACE
	case GDK_v:
		xroar_set_trace(XROAR_NEXT);  /* toggle */
		break;
#endif
	default:
		break;
	}
	return;
}

static gboolean keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	int control, shift;
	(void)widget;
	(void)user_data;
	if (gtk_window_activate_key(GTK_WINDOW(global_uigtk2->top_window), event) == TRUE) {
		return TRUE;
	}
	if (event->hardware_keycode >= MAX_KEYCODE) {
		return FALSE;
	}
	guint keyval = keycode_to_keyval[event->hardware_keycode];
	control = event->state & GDK_CONTROL_MASK;

	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (enabled_axis[i]) {
			if (keyval == enabled_axis[i]->key0) {
				enabled_axis[i]->value = 0;
				return FALSE;
			}
			if (keyval == enabled_axis[i]->key1) {
				enabled_axis[i]->value = 65535;
				return FALSE;
			}
		}
	}
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (enabled_button[i]) {
			if (keyval == enabled_button[i]->key) {
				enabled_button[i]->value = 1;
				return FALSE;
			}
		}
	}

	if (keyval == GDK_Shift_L || keyval == GDK_Shift_R) {
		KEYBOARD_PRESS_SHIFT(xroar_keyboard_interface);
		return FALSE;
	}
	shift = event->state & GDK_SHIFT_MASK;
	if (!shift) {
		KEYBOARD_RELEASE_SHIFT(xroar_keyboard_interface);
	}
	if (keyval == GDK_F12) {
		if (shift) {
			xroar_set_ratelimit_latch(1, XROAR_NEXT);
		} else {
			xroar_set_ratelimit(0);
		}
	}
	if (keyval == GDK_Pause) {
		xroar_set_pause(1, XROAR_NEXT);
		return FALSE;
	}
	if (control) {
		emulator_command(keyval, shift);
		return FALSE;
	}

	int keyval_i = keyval_index(keyval);
	if (keyval_priority[keyval_i]) {
		if (xroar_cfg.debug_ui & XROAR_DEBUG_UI_KBD_EVENT)
			printf("gtk press   keycode %6d   keyval %04x   %s\n", event->hardware_keycode, keyval, gdk_keyval_name(keyval));
		keyboard_press(xroar_keyboard_interface, keyval_to_dkey[keyval_i]);
		return FALSE;
	}

	if (xroar_cfg.kbd_translate) {
		guint16 keycode = event->hardware_keycode;
		guint32 unicode = gdk_keyval_to_unicode(event->keyval);
		if (xroar_cfg.debug_ui & XROAR_DEBUG_UI_KBD_EVENT)
			printf("gtk press   keycode %6d   keyval %04x   unicode %08x   %s\n", keycode, keyval, unicode, gdk_keyval_name(keyval));
		if (unicode == 0) {
			if (event->keyval == GDK_Return)
				unicode = 0x0d;
			else if (event->keyval == GDK_KP_Enter)
				unicode = 0x0d;
		}
		/* shift + backspace -> erase line */
		if (shift && (unicode == 0x08 || unicode == 0x7f))
			unicode = DKBD_U_ERASE_LINE;
		/* shift + enter -> caps lock */
		if (keyval_to_dkey[keyval_i] == DSCAN_ENTER)
			unicode = shift ? DKBD_U_CAPS_LOCK : 0x0d;
		/* shift + clear -> pause output */
		if (keyval_to_dkey[keyval_i] == DSCAN_SPACE)
			unicode = shift ? DKBD_U_PAUSE_OUTPUT : 0x20;
		last_unicode[keycode] = unicode;
		keyboard_unicode_press(xroar_keyboard_interface, unicode);
		return FALSE;
	}

	if (xroar_cfg.debug_ui & XROAR_DEBUG_UI_KBD_EVENT)
		printf("gtk press   keycode %6d   keyval %04x   %s\n", event->hardware_keycode, keyval, gdk_keyval_name(keyval));
	keyboard_press(xroar_keyboard_interface, keyval_to_dkey[keyval_i]);
	return FALSE;
}

static gboolean keyrelease(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	int shift;
	(void)widget;
	(void)user_data;
	if (event->hardware_keycode >= MAX_KEYCODE) {
		return FALSE;
	}
	guint keyval = keycode_to_keyval[event->hardware_keycode];

	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (enabled_axis[i]) {
			if (keyval == enabled_axis[i]->key0) {
				if (enabled_axis[i]->value < 32768)
					enabled_axis[i]->value = 32767;
				return FALSE;
			}
			if (keyval == enabled_axis[i]->key1) {
				if (enabled_axis[i]->value >= 32768)
					enabled_axis[i]->value = 32768;
				return FALSE;
			}
		}
	}
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (enabled_button[i]) {
			if (keyval == enabled_button[i]->key) {
				enabled_button[i]->value = 0;
				return FALSE;
			}
		}
	}

	if (keyval == GDK_Shift_L || keyval == GDK_Shift_R) {
		KEYBOARD_RELEASE_SHIFT(xroar_keyboard_interface);
		return FALSE;
	}
	shift = event->state & GDK_SHIFT_MASK;
	if (!shift) {
		KEYBOARD_RELEASE_SHIFT(xroar_keyboard_interface);
	}
	if (keyval == GDK_F12) {
		xroar_set_ratelimit(1);
	}

	int keyval_i = keyval_index(keyval);
	if (keyval_priority[keyval_i]) {
		if (xroar_cfg.debug_ui & XROAR_DEBUG_UI_KBD_EVENT)
			printf("gtk release keycode %6d   keyval %04x   %s\n", event->hardware_keycode, keyval, gdk_keyval_name(keyval));
		keyboard_release(xroar_keyboard_interface, keyval_to_dkey[keyval_i]);
		return FALSE;
	}

	if (xroar_cfg.kbd_translate) {
		guint16 keycode = event->hardware_keycode;
		guint32 unicode = last_unicode[keycode];
		if (xroar_cfg.debug_ui & XROAR_DEBUG_UI_KBD_EVENT)
			printf("gtk release keycode %6d   keyval %04x   unicode %08x   %s\n", keycode, keyval, unicode, gdk_keyval_name(keyval));
		keyboard_unicode_release(xroar_keyboard_interface, unicode);
		/* Put shift back the way it should be */
		if (shift)
			KEYBOARD_PRESS_SHIFT(xroar_keyboard_interface);
		else
			KEYBOARD_RELEASE_SHIFT(xroar_keyboard_interface);
		return FALSE;
	}

	if (xroar_cfg.debug_ui & XROAR_DEBUG_UI_KBD_EVENT)
		printf("gtk release keycode %6d   keyval %04x   %s\n", event->hardware_keycode, keyval, gdk_keyval_name(keyval));
	keyboard_release(xroar_keyboard_interface, keyval_to_dkey[keyval_i]);
	return FALSE;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static unsigned read_axis(struct axis *a) {
	return a->value;
}

static _Bool read_button(struct button *b) {
	return b->value;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static unsigned get_key_by_name(const char *name) {
	if (isdigit(name[0]))
		return strtol(name, NULL, 0);
	return gdk_keyval_from_name(name);
}

static struct joystick_axis *configure_axis(char *spec, unsigned jaxis) {
	unsigned key0, key1;
	// sensible defaults
	if (jaxis == 0) {
		key0 = GDK_Left;
		key1 = GDK_Right;
	} else {
		key0 = GDK_Up;
		key1 = GDK_Down;
	}
	char *a0 = NULL;
	char *a1 = NULL;
	if (spec) {
		a0 = strsep(&spec, ",");
		a1 = spec;
	}
	if (a0 && *a0)
		key0 = get_key_by_name(a0);
	if (a1 && *a1)
		key1 = get_key_by_name(a1);
	struct axis *axis_data = g_malloc(sizeof(*axis_data));
	axis_data->key0 = key0;
	axis_data->key1 = key1;
	axis_data->value = 32767;
	struct joystick_axis *axis = g_malloc(sizeof(*axis));
	axis->read = (js_read_axis_func)read_axis;
	axis->data = axis_data;
	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (!enabled_axis[i]) {
			enabled_axis[i] = axis_data;
			break;
		}
	}
	return axis;
}

static struct joystick_button *configure_button(char *spec, unsigned jbutton) {
	unsigned key = (jbutton == 0) ? GDK_Alt_L : GDK_VoidSymbol;
	if (spec && *spec)
		key = get_key_by_name(spec);
	struct button *button_data = g_malloc(sizeof(*button_data));
	button_data->key = key;
	button_data->value = 0;
	struct joystick_button *button = g_malloc(sizeof(*button));
	button->read = (js_read_button_func)read_button;
	button->data = button_data;
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (!enabled_button[i]) {
			enabled_button[i] = button_data;
			break;
		}
	}
	return button;
}

static void unmap_axis(struct joystick_axis *axis) {
	if (!axis)
		return;
	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (axis->data == enabled_axis[i]) {
			enabled_axis[i] = NULL;
		}
	}
	g_free(axis->data);
	g_free(axis);
}

static void unmap_button(struct joystick_button *button) {
	if (!button)
		return;
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (button->data == enabled_button[i]) {
			enabled_button[i] = NULL;
		}
	}
	g_free(button->data);
	g_free(button);
}
