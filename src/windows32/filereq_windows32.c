/*

Windows file requester module

Copyright 2005-2019 Ciaran Anscomb

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

/* This Windows32 code is probably all wrong, but it does seem to work */

#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

/* Windows has a habit of making include order important: */
#include <windows.h>
#include <commdlg.h>

#include "fs.h"
#include "logging.h"
#include "module.h"
#include "vo.h"
#include "xroar.h"

#include "windows32/common_windows32.h"

static void *filereq_windows32_new(void *cfg);

struct module filereq_windows32_module = {
	.name = "windows32", .description = "Windows file requester",
	.new = filereq_windows32_new
};

struct windows32_filereq_interface {
	struct filereq_interface public;

	char *filename;
};

static void filereq_windows32_free(void *sptr);
static char *load_filename(void *sptr, char const * const *extensions);
static char *save_filename(void *sptr, char const * const *extensions);

static void *filereq_windows32_new(void *cfg) {
	(void)cfg;
	struct windows32_filereq_interface *frw32 = xmalloc(sizeof(*frw32));
	*frw32 = (struct windows32_filereq_interface){0};
	frw32->public.free = DELEGATE_AS0(void, filereq_windows32_free, frw32);
	frw32->public.load_filename = DELEGATE_AS1(charp, charcpcp, load_filename, frw32);
	frw32->public.save_filename = DELEGATE_AS1(charp, charcpcp, save_filename, frw32);
	return frw32;
}

static void filereq_windows32_free(void *sptr) {
	struct windows32_filereq_interface *frw32 = sptr;
	free(frw32->filename);
	free(frw32);
}

static const char *lpstrFilter =
	"All\0"             "*.*\0"
	"Binary files\0"    "*.BIN;*.HEX\0"
	"Cassette images\0" "*.ASC;*.BAS;*.CAS;*.WAV\0"
	"Cartridges\0"      "*.ROM;*.CCC\0"
	"Disk images\0"     "*.DMK;*.DSK;*.JVC;*.OS9;*.VDK\0"
	"Snapshots\0"       "*.SNA\0"
	;

static char *load_filename(void *sptr, char const * const *extensions) {
	struct windows32_filereq_interface *frw32 = sptr;
	OPENFILENAME ofn;
	char fn_buf[260];
	int was_fullscreen;

	(void)extensions;  /* unused */
	was_fullscreen = xroar_vo_interface->is_fullscreen;
	if (was_fullscreen)
		DELEGATE_SAFE_CALL1(xroar_vo_interface->set_fullscreen, 0);

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = windows32_main_hwnd;
	ofn.lpstrFile = fn_buf;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(fn_buf);
	ofn.lpstrFilter = lpstrFilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR
		| OFN_HIDEREADONLY;

	if (frw32->filename)
		free(frw32->filename);
	frw32->filename = NULL;
	if (GetOpenFileName(&ofn)==TRUE) {
		frw32->filename = xstrdup(ofn.lpstrFile);
	}
	if (was_fullscreen)
		DELEGATE_SAFE_CALL1(xroar_vo_interface->set_fullscreen, 1);
	return frw32->filename;
}

static char *save_filename(void *sptr, char const * const *extensions) {
	struct windows32_filereq_interface *frw32 = sptr;
	OPENFILENAME ofn;
	char fn_buf[260];
	int was_fullscreen;

	(void)extensions;  /* unused */
	was_fullscreen = xroar_vo_interface->is_fullscreen;
	if (was_fullscreen)
		DELEGATE_SAFE_CALL1(xroar_vo_interface->set_fullscreen, 0);

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = windows32_main_hwnd;
	ofn.lpstrFile = fn_buf;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(fn_buf);
	ofn.lpstrFilter = lpstrFilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY
		| OFN_OVERWRITEPROMPT;

	if (frw32->filename)
		free(frw32->filename);
	frw32->filename = NULL;
	if (GetSaveFileName(&ofn)==TRUE) {
		frw32->filename = xstrdup(ofn.lpstrFile);
	}
	if (was_fullscreen)
		DELEGATE_SAFE_CALL1(xroar_vo_interface->set_fullscreen, 1);
	return frw32->filename;
}
