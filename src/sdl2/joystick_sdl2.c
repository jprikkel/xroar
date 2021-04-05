/*

SDL2 joystick module

Copyright 2015-2016 Ciaran Anscomb

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

#include <string.h>

#include <SDL.h>

#include "pl-string.h"
#include "slist.h"
#include "xalloc.h"

#include "joystick.h"
#include "logging.h"
#include "module.h"

#include "sdl2/common.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *, unsigned);
static struct joystick_button *configure_button(char *, unsigned);
static void unmap_axis(struct joystick_axis *axis);
static void unmap_button(struct joystick_button *button);

struct joystick_submodule sdl_js_submod_physical = {
	.name = "physical",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
	.unmap_axis = unmap_axis,
	.unmap_button = unmap_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_submodule *js_submodlist[] = {
	&sdl_js_submod_physical,
	NULL
};

struct joystick_module sdl_js_mod_exported = {
	.common = { .name = "sdl", .description = "SDL2 joystick input" },
	.submodule_list = js_submodlist,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool initialised = 0;
static unsigned num_joysticks;

// Wrap SDL_Joystick up in struct device.  close_device() will only
// close the underlying joystick once open_count reaches 0.
struct device {
	int joystick_index;
	SDL_Joystick *joystick;
	unsigned num_axes;
	unsigned num_buttons;
	unsigned open_count;
};

static struct slist *device_list = NULL;

struct control {
	struct device *device;
	unsigned control;
	_Bool inverted;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void sdl_js_physical_init(void) {
	if (initialised)
		return;
	SDL_InitSubSystem(SDL_INIT_JOYSTICK);
	num_joysticks = SDL_NumJoysticks();
	if (num_joysticks < 1) {
		LOG_DEBUG(1, "\tNo joysticks found\n");
	} else {
		LOG_DEBUG(1, "\t%u joysticks found\n", num_joysticks);
	}
	initialised = 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct device *open_device(int joystick_index) {
	// If the device is already open, just up its count and return it
	for (struct slist *iter = device_list; iter; iter = iter->next) {
		struct device *d = iter->data;
		if (d->joystick_index == joystick_index) {
			d->open_count++;
			return d;
		}
	}
	// Otherwise open and throw it on the list
	SDL_Joystick *j = SDL_JoystickOpen(joystick_index);
	if (!j)
		return NULL;
	struct device *d = xmalloc(sizeof(*d));
	d->joystick_index = joystick_index;
	d->joystick = j;
	d->num_axes = SDL_JoystickNumAxes(j);
	d->num_buttons = SDL_JoystickNumButtons(j);
	d->open_count = 1;
	device_list = slist_prepend(device_list, d);
	return d;
}

static void close_device(struct device *d) {
	d->open_count--;
	if (d->open_count == 0) {
		SDL_JoystickClose(d->joystick);
		device_list = slist_remove(device_list, d);
		free(d);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static unsigned read_axis(struct control *c) {
	unsigned ret = SDL_JoystickGetAxis(c->device->joystick, c->control) + 32768;
	if (c->inverted)
		ret ^= 0xffff;
	return ret;
}

static _Bool read_button(struct control *c) {
	return SDL_JoystickGetButton(c->device->joystick, c->control);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// axis & button specs are basically the same, just track a different
// "selected" variable.
static struct control *configure_control(char *spec, unsigned control) {
	unsigned joystick = 0;
	_Bool inverted = 0;
	char *tmp = NULL;
	if (spec)
		tmp = strsep(&spec, ",");
	if (tmp && *tmp) {
		control = strtol(tmp, NULL, 0);
	}
	if (spec && *spec) {
		joystick = control;
		if (*spec == '-') {
			inverted = 1;
			spec++;
		}
		if (*spec) {
			control = strtol(spec, NULL, 0);
		}
	}
	struct device *d = open_device(joystick);
	if (!d)
		return NULL;
	struct control *c = xmalloc(sizeof(*c));
	c->device = d;
	c->control = control;
	c->inverted = inverted;
	return c;
}

static struct joystick_axis *configure_axis(char *spec, unsigned jaxis) {
	sdl_js_physical_init();
	struct control *c = configure_control(spec, jaxis);
	if (!c)
		return NULL;
	if (c->control >= c->device->num_axes) {
		close_device(c->device);
		free(c);
		return NULL;
	}
	struct joystick_axis *axis = xmalloc(sizeof(*axis));
	axis->read = (js_read_axis_func)read_axis;
	axis->data = c;
	return axis;
}

static struct joystick_button *configure_button(char *spec, unsigned jbutton) {
	sdl_js_physical_init();
	struct control *c = configure_control(spec, jbutton);
	if (!c)
		return NULL;
	if (c->control >= c->device->num_buttons) {
		close_device(c->device);
		free(c);
		return NULL;
	}
	struct joystick_button *button = xmalloc(sizeof(*button));
	button->read = (js_read_button_func)read_button;
	button->data = c;
	return button;
}

static void unmap_axis(struct joystick_axis *axis) {
	if (!axis)
		return;
	struct control *c = axis->data;
	close_device(c->device);
	free(c);
	free(axis);
}

static void unmap_button(struct joystick_button *button) {
	if (!button)
		return;
	struct control *c = button->data;
	close_device(c->device);
	free(c);
	free(button);
}
