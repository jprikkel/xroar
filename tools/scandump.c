/*  Copyright 2015 Ciaran Anscomb
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "array.h"

#include "scancodes_darwin.h"
#include "scancodes_xfree86.h"

static void dump_keycode_table(const SDL_Scancode *scancodes, int nscancodes) {
	int sdl_to_keycode[SDL_NUM_SCANCODES];
	memset(sdl_to_keycode, 0, sizeof(sdl_to_keycode));
	for (int i = 0; i < nscancodes; i++) {
		if (scancodes[i] != 0) {
			if (sdl_to_keycode[scancodes[i]] == 0) {
				sdl_to_keycode[scancodes[i]] = i + 8;
			}
		}
	}

	for (int i = 0; i < SDL_NUM_SCANCODES; i++) {
		if ((i % 8) == 0) {
			printf("\t");
		}
		printf("%3d,", sdl_to_keycode[i]);
		if ((i % 8) == 7 || (i + 1) == SDL_NUM_SCANCODES) {
			printf("\n");
		} else {
			printf(" ");
		}
	}
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	puts("/* Keycode tables invert scancode tables from SDL2 */\n");
	puts("/* Generated by tools/scandump - add new tables there */\n");

	printf("static const int xfree86_keycode_table[] = {\n");
	dump_keycode_table(xfree86_scancode_table, ARRAY_N_ELEMENTS(xfree86_scancode_table));
	puts("};\n");

	printf("static const int xfree86_keycode_table2[] = {\n");
	dump_keycode_table(xfree86_scancode_table2, ARRAY_N_ELEMENTS(xfree86_scancode_table2));
	puts("};\n");

	printf("static const int darwin_keycode_table[] = {\n");
	dump_keycode_table(darwin_scancode_table, ARRAY_N_ELEMENTS(darwin_scancode_table));
	puts("};\n");

	return EXIT_SUCCESS;
}
