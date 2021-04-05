/*

WD279x Floppy Drive Controller

Copyright 2003-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_WD279X_H_
#define XROAR_WD279X_H_

#include <stdint.h>

#include "delegate.h"

#include "events.h"

enum WD279X_type {
	WD2791, WD2793, WD2795, WD2797
};

/* FDC states: */
enum WD279X_state {
	WD279X_state_accept_command,
	WD279X_state_type1_1,
	WD279X_state_type1_2,
	WD279X_state_type1_3,
	WD279X_state_verify_track_1,
	WD279X_state_verify_track_2,
	WD279X_state_type2_1,
	WD279X_state_type2_2,
	WD279X_state_read_sector_1,
	WD279X_state_read_sector_2,
	WD279X_state_read_sector_3,
	WD279X_state_write_sector_1,
	WD279X_state_write_sector_2,
	WD279X_state_write_sector_3,
	WD279X_state_write_sector_4,
	WD279X_state_write_sector_5,
	WD279X_state_write_sector_6,
	WD279X_state_type3_1,
	WD279X_state_read_address_1,
	WD279X_state_read_address_2,
	WD279X_state_read_address_3,
	WD279X_state_write_track_1,
	WD279X_state_write_track_2,
	WD279X_state_write_track_2b,
	WD279X_state_write_track_3,
	WD279X_state_invalid
};

typedef struct WD279X WD279X;
struct WD279X {
	struct part part;

	enum WD279X_type type;

	/* Registers */
	uint8_t status_register;
	uint8_t track_register;
	uint8_t sector_register;
	uint8_t data_register;
	uint8_t command_register;

	/* External interface */
	DELEGATE_T1(void, int) set_dirc;
	DELEGATE_T1(void, bool) set_dden;
	DELEGATE_T1(void, unsigned) set_sso;
	DELEGATE_T1(void, bool) set_drq;
	DELEGATE_T1(void, bool) set_intrq;

	DELEGATE_T0(unsigned) get_head_pos;
	DELEGATE_T0(void) step;
	DELEGATE_T1(void, uint8) write;
	DELEGATE_T0(void) skip;
	DELEGATE_T0(uint8) read;
	DELEGATE_T0(void) write_idam;
	DELEGATE_T0(unsigned) time_to_next_byte;
	DELEGATE_T0(unsigned) time_to_next_idam;
	DELEGATE_T0(uint8p) next_idam;
	DELEGATE_T0(void) update_connection;

	/* WD279X internal state */
	enum WD279X_state state;
	struct event state_event;
	int direction;
	int side;
	int step_delay;
	_Bool double_density;
	_Bool ready_state;
	_Bool tr00_state;
	_Bool index_state;
	_Bool write_protect_state;
	/* During & after type 1 commands, and sometimes after a forced
	 * interrupt, status reads reflect tr00 & index status: */
	_Bool status_type1;
	/* Forced interrupts: */
	_Bool intrq_nready_to_ready;
	_Bool intrq_ready_to_nready;
	_Bool intrq_index_pulse;
	_Bool intrq_immediate;

	_Bool is_step_cmd;
	uint16_t crc;
	int dam;
	int bytes_left;
	int index_holes_count;
	uint8_t track_register_tmp;

	/* Private */

	_Bool has_sso;
	_Bool has_length_flag;
	uint8_t invert_data;

};

WD279X *wd279x_new(enum WD279X_type type);
void wd279x_reset(WD279X *fdc);
void wd279x_disconnect(WD279X *fdc);

/* Signal all connected delegates */
void wd279x_update_connection(WD279X *fdc);

void wd279x_ready(void *sptr, _Bool state);
void wd279x_tr00(void *sptr, _Bool state);
void wd279x_index_pulse(void *sptr, _Bool state);
void wd279x_write_protect(void *sptr, _Bool state);
void wd279x_set_dden(WD279X *fdc, _Bool dden);  /* 1 = Double density, 0 = Single */
uint8_t wd279x_read(WD279X *fdc, uint16_t A);
void wd279x_write(WD279X *fdc, uint16_t A, uint8_t D);

#endif
