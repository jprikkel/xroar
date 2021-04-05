/*

C locale character handling

Copyright 2014 Ciaran Anscomb

This file is part of Portalib.

Portalib is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

See COPYING.LGPL and COPYING.GPL for redistribution conditions.

*/

// Assumes ASCII.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "c-ctype.h"

int c_tolower(int c) {
	if (c_isupper(c))
		return c | 0x20;
	return c;
}

int c_toupper(int c) {
	if (c_islower(c))
		return c & ~0x20;
	return c;
}
