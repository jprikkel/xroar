/*

Support for various binary representations

Copyright 2003-2016 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

Handles:

 * Intel HEX
 * DragonDOS binary
 * CoCo RS-DOS ("DECB") binary

*/

#ifndef XROAR_HEXS19_H_
#define XROAR_HEXS19_H_

int intel_hex_read(const char *filename, int autorun);
int bin_load(const char *filename, int autorun);

#endif
