/*

VDG measured voltage "palette"s

Copyright 2011-2016 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_VDG_PALETTE_H_
#define XROAR_VDG_PALETTE_H_

#define NUM_VDG_COLOURS (12)

struct vdg_colour_entry {
	float y, chb, b, a;
};

struct vdg_palette {
	const char *name;
	const char *description;
	float sync_y;
	float blank_y;
	float white_y;
	float black_level;
	float rgb_black_level;
	struct vdg_colour_entry palette[NUM_VDG_COLOURS];
};

struct vdg_palette *vdg_palette_new(void);
int vdg_palette_count(void);
struct vdg_palette *vdg_palette_index(int i);
struct vdg_palette *vdg_palette_by_name(const char *name);

/* Map Y'U'V' from palette to pixel value */
void vdg_palette_RGB(struct vdg_palette *vp, int colour,
                     float *Rout, float *Gout, float *Bout);

#endif
