/*

WD279x Floppy Drive Controller

Copyright 2003-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

/* Sources:
 *     WD279x:
 *         http://www.swtpc.com/mholley/DC_5/TMS279X_DataSheet.pdf
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "crc16.h"
#include "events.h"
#include "logging.h"
#include "part.h"
#include "vdrive.h"
#include "wd279x.h"
#include "xroar.h"

#define STATUS_NOT_READY     (1<<7)
#define STATUS_WRITE_PROTECT (1<<6)
#define STATUS_HEAD_LOADED   (1<<5)
#define STATUS_RECORD_TYPE   (1<<5)
#define STATUS_SEEK_ERROR    (1<<4)
#define STATUS_RNF           (1<<4)
#define STATUS_CRC_ERROR     (1<<3)
#define STATUS_TRACK_0       (1<<2)
#define STATUS_LOST_DATA     (1<<2)
#define STATUS_INDEX_PULSE   (1<<1)
#define STATUS_DRQ           (1<<1)
#define STATUS_BUSY          (1<<0)

#define W_BYTE_TIME (EVENT_TICK_RATE / 31250)

#define SET_DRQ do { \
		fdc->status_register |= STATUS_DRQ; \
		DELEGATE_CALL1(fdc->set_drq, 1); \
	} while (0)
#define RESET_DRQ do { \
		fdc->status_register &= ~(STATUS_DRQ); \
		DELEGATE_CALL1(fdc->set_drq, 0); \
	} while (0)
#define SET_INTRQ do { \
		DELEGATE_CALL1(fdc->set_intrq, 1); \
	} while (0)
#define RESET_INTRQ do { \
		DELEGATE_CALL1(fdc->set_intrq, 0); \
	} while (0)

#define NEXT_STATE(f,t) do { \
		fdc->state = f; \
		fdc->state_event.at_tick = event_current_tick + t; \
		event_queue(&MACHINE_EVENT_LIST, &fdc->state_event); \
	} while (0)
#define GOTO_STATE(f) fdc->state = f; continue

#define IS_DOUBLE_DENSITY (fdc->double_density)
#define IS_SINGLE_DENSITY (!fdc->double_density)

#define SET_DIRECTION do { fdc->direction = 1; DELEGATE_CALL1(fdc->set_dirc, 1); } while (0)
#define RESET_DIRECTION do { \
		fdc->direction = -1; DELEGATE_CALL1(fdc->set_dirc, -1); \
	} while (0)
#define SET_SIDE(s) do { \
		fdc->side = (s) ? 1 : 0; \
		if (fdc->has_sso) \
			DELEGATE_CALL1(fdc->set_sso, fdc->side); \
	} while (0)

static void state_machine(void *);

static int const stepping_rate[4] = { 6, 12, 20, 30 };
static int const sector_size[2][4] = {
	{ 256, 512, 1024, 128 },
	{ 128, 256, 512, 1024 }
};

static const char *wd279x_type_name[4] = {
	"WD2791", "WD2793", "WD2795", "WD2797"
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Debugging

struct log_handle *log_rsec_hex = NULL;
struct log_handle *log_wsec_hex = NULL;
struct log_handle *log_wtrk_hex = NULL;

static void debug_state(WD279X *fdc);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static uint8_t _vdrive_read(WD279X *fdc) {
	uint8_t b = DELEGATE_CALL0(fdc->read);
	fdc->crc = crc16_byte(fdc->crc, b);
	return b;
}

static void _vdrive_write(WD279X *fdc, uint8_t b) {
	DELEGATE_CALL1(fdc->write, b);
	fdc->crc = crc16_byte(fdc->crc, b);
}

static void wd279x_free(struct part *p);

#define VDRIVE_WRITE_CRC16 do { \
		uint16_t tmp = fdc->crc; \
		_vdrive_write(fdc, tmp >> 8); \
		_vdrive_write(fdc, tmp & 0xff); \
	} while (0)

WD279X *wd279x_new(enum WD279X_type type) {
	assert(type >= WD2791 && type <= WD2797);
	WD279X *fdc = part_new(sizeof(*fdc));
	*fdc = (WD279X){0};
	part_init(&fdc->part, wd279x_type_name[type]);
	fdc->part.free = wd279x_free;

	fdc->type = type;
	fdc->has_sso = (type == WD2795 || type == WD2797);
	fdc->has_length_flag = (type == WD2795 || type == WD2797);
	fdc->invert_data = (type == WD2791 || type == WD2795) ? 0xff : 0;
	wd279x_disconnect(fdc);

	fdc->state = WD279X_state_accept_command;
	event_init(&fdc->state_event, DELEGATE_AS0(void, state_machine, fdc));

	return fdc;
}

static void wd279x_free(struct part *p) {
	WD279X *fdc = (WD279X *)p;
	event_dequeue(&fdc->state_event);
}

void wd279x_disconnect(WD279X *fdc) {
	if (!fdc)
		return;
	fdc->set_dirc = DELEGATE_DEFAULT1(void, int);
	fdc->set_dden = DELEGATE_DEFAULT1(void, bool);
	fdc->set_sso = DELEGATE_DEFAULT1(void, unsigned);
	fdc->set_drq = DELEGATE_DEFAULT1(void, bool);
	fdc->set_intrq = DELEGATE_DEFAULT1(void, bool);
	fdc->get_head_pos = DELEGATE_DEFAULT0(unsigned);
	fdc->step = DELEGATE_DEFAULT0(void);
	fdc->write = DELEGATE_DEFAULT1(void, uint8);
	fdc->skip = DELEGATE_DEFAULT0(void);
	fdc->read = DELEGATE_DEFAULT0(uint8);
	fdc->write_idam = DELEGATE_DEFAULT0(void);
	fdc->time_to_next_byte = DELEGATE_DEFAULT0(unsigned);
	fdc->time_to_next_idam = DELEGATE_DEFAULT0(unsigned);
	fdc->next_idam = DELEGATE_DEFAULT0(uint8p);
	fdc->update_connection = DELEGATE_DEFAULT0(void);
}

void wd279x_reset(WD279X *fdc) {
	assert(fdc != NULL);
	event_dequeue(&fdc->state_event);
	fdc->status_register = 0;
	fdc->track_register = 0;
	fdc->sector_register = 0;
	fdc->data_register = 0;
	fdc->command_register = 0;
	RESET_DIRECTION;
	SET_SIDE(0);
}

void wd279x_ready(void *sptr, _Bool state) {
	WD279X *fdc = sptr;
	if (fdc->ready_state == state)
		return;
	fdc->ready_state = state;
	if (state && fdc->intrq_nready_to_ready) {
		event_dequeue(&fdc->state_event);
		SET_INTRQ;
	}
	if (!state && fdc->intrq_ready_to_nready) {
		event_dequeue(&fdc->state_event);
		SET_INTRQ;
	}
}

void wd279x_tr00(void *sptr, _Bool state) {
	WD279X *fdc = sptr;
	if (fdc->tr00_state == state)
		return;
	fdc->tr00_state = state;
}

void wd279x_index_pulse(void *sptr, _Bool state) {
	WD279X *fdc = sptr;
	if (fdc->index_state == state)
		return;
	fdc->index_state = state;
	if (state) {
		fdc->index_holes_count++;
		if (fdc->intrq_index_pulse) {
			event_dequeue(&fdc->state_event);
			SET_INTRQ;
		}
	}
}

void wd279x_write_protect(void *sptr, _Bool state) {
	WD279X *fdc = sptr;
	if (fdc->write_protect_state == state)
		return;
	fdc->write_protect_state = state;
}

void wd279x_set_dden(WD279X *fdc, _Bool dden) {
	fdc->double_density = dden;
	DELEGATE_CALL1(fdc->set_dden, dden);
}

void wd279x_update_connection(WD279X *fdc) {
	DELEGATE_CALL1(fdc->set_dden, fdc->double_density);
	if (fdc->has_sso)
		DELEGATE_CALL1(fdc->set_sso, fdc->side);
	DELEGATE_CALL1(fdc->set_dirc, fdc->direction);
	DELEGATE_CALL0(fdc->update_connection);
}

uint8_t wd279x_read(WD279X *fdc, uint16_t A) {
	uint8_t D;
	switch (A & 3) {
		default:
		case 0:
			if (!fdc->intrq_immediate)
				RESET_INTRQ;
			if (fdc->ready_state)
				fdc->status_register &= ~STATUS_NOT_READY;
			else
				fdc->status_register |= STATUS_NOT_READY;
			if (fdc->status_type1) {
				fdc->status_register &= ~(STATUS_TRACK_0|STATUS_INDEX_PULSE);
				if (fdc->tr00_state)
					fdc->status_register |= STATUS_TRACK_0;
				if (fdc->index_state)
					fdc->status_register |= STATUS_INDEX_PULSE;
			}
			D = fdc->status_register;
			break;
		case 1:
			D = fdc->track_register;
			break;
		case 2:
			D = fdc->sector_register;
			break;
		case 3:
			RESET_DRQ;
			D = fdc->data_register;
			break;
	}
	return D ^ fdc->invert_data;
}

void wd279x_write(WD279X *fdc, uint16_t A, uint8_t D) {
	D ^= fdc->invert_data;
	switch (A & 3) {
		default:
		case 0:
			fdc->command_register = D;
			/* FORCE INTERRUPT */
			if ((D & 0xf0) == 0xd0) {
				if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_STATE) {
					debug_state(fdc);
				}
				fdc->intrq_nready_to_ready = D & 1;
				fdc->intrq_ready_to_nready = D & 2;
				fdc->intrq_index_pulse = D & 4;
				/* XXX Data sheet wording implies that *only*
				 * 0xd0 can clear this.  Needs testing... */
				fdc->intrq_immediate = D & 8;
				if (!(fdc->status_register & STATUS_BUSY)) {
					fdc->status_type1 = 1;
				}
				event_dequeue(&fdc->state_event);
				fdc->status_register &= ~(STATUS_BUSY);
				if (fdc->intrq_immediate)
					SET_INTRQ;
				return;
			}
			/* Ignore any other command if busy */
			if (fdc->status_register & STATUS_BUSY) {
				LOG_DEBUG(3, "WD279X: Command received while busy!\n");
				return;
			}
			if (!fdc->intrq_immediate)
				RESET_INTRQ;
			fdc->state = WD279X_state_accept_command;
			state_machine(fdc);
			break;
		case 1:
			fdc->track_register = D;
			break;
		case 2:
			fdc->sector_register = D;
			break;
		case 3:
			RESET_DRQ;
			fdc->data_register = D;
			break;
	}
}

/* One big state machine.  This is called from an event dispatch and from the
 * write command function. */

static void state_machine(void *sptr) {
	WD279X *fdc = (WD279X *)sptr;
	uint8_t *idam;
	uint8_t data;
	int i;
	for (;;) {

		// Log new states if requested:
		if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_STATE) {
			static enum WD279X_state last_state = WD279X_state_invalid;
			if (fdc->state != last_state) {
				debug_state(fdc);
				last_state = fdc->state;
			}
		}

		switch (fdc->state) {

		case WD279X_state_accept_command:
			/* 0xxxxxxx = RESTORE / SEEK / STEP / STEP-IN / STEP-OUT */
			if ((fdc->command_register & 0x80) == 0x00) {
				fdc->status_type1 = 1;
				fdc->status_register |= STATUS_BUSY;
				fdc->status_register &= ~(STATUS_CRC_ERROR|STATUS_SEEK_ERROR);
				RESET_DRQ;
				fdc->step_delay = stepping_rate[fdc->command_register & 3];
				fdc->is_step_cmd = 0;
				if ((fdc->command_register & 0xe0) == 0x20) {
					fdc->is_step_cmd = 1;
				} else if ((fdc->command_register & 0xe0) == 0x40) {
					fdc->is_step_cmd = 1;
					SET_DIRECTION;
				} else if ((fdc->command_register & 0xe0) == 0x60) {
					fdc->is_step_cmd = 1;
					RESET_DIRECTION;
				}
				if (fdc->is_step_cmd) {
					if (fdc->command_register & 0x10) {
						GOTO_STATE(WD279X_state_type1_2);
					}
					GOTO_STATE(WD279X_state_type1_3);
				}
				if ((fdc->command_register & 0xf0) == 0x00) {
					fdc->track_register = 0xff;
					fdc->data_register = 0x00;
				}
				GOTO_STATE(WD279X_state_type1_1);
			}

			/* 10xxxxxx = READ/WRITE SECTOR */
			if ((fdc->command_register & 0xc0) == 0x80) {
				fdc->status_type1 = 0;
				fdc->status_register |= STATUS_BUSY;
				fdc->status_register &= ~(STATUS_LOST_DATA|STATUS_RNF|(1<<5)|(1<<6));
				RESET_DRQ;
				if (!fdc->ready_state) {
					fdc->status_register &= ~(STATUS_BUSY);
					SET_INTRQ;
					return;
				}
				if (fdc->has_sso)
					SET_SIDE(fdc->command_register & 0x02);  /* 'U' */
				else
					SET_SIDE(fdc->command_register & 0x08);  /* 'S' */
				if (fdc->command_register & 0x04) {  /* 'E' set */
					NEXT_STATE(WD279X_state_type2_1, EVENT_MS(30));
					return;
				}
				GOTO_STATE(WD279X_state_type2_1);
			}

			/* 11000xx0 = READ ADDRESS */
			/* 11100xx0 = READ TRACK */
			/* 11110xx0 = WRITE TRACK */
			if (((fdc->command_register & 0xf9) == 0xc0)
					|| ((fdc->command_register & 0xf9) == 0xe0)
					|| ((fdc->command_register & 0xf9) == 0xf0)) {
				fdc->status_type1 = 0;
				fdc->status_register |= STATUS_BUSY;
				fdc->status_register &= ~(STATUS_LOST_DATA|(1<<4)|(1<<5));
				if ((fdc->command_register & 0xf0) == 0xf0)
					RESET_DRQ;
				if (!fdc->ready_state) {
					fdc->status_register &= ~(STATUS_BUSY);
					SET_INTRQ;
					return;
				}
				if (fdc->has_sso)
					SET_SIDE(fdc->command_register & 0x02);  /* 'U' */
				else
					SET_SIDE(fdc->command_register & 0x08);  /* 'S' */
				if (fdc->command_register & 0x04) {  /* 'E' set */
					NEXT_STATE(WD279X_state_type3_1, EVENT_MS(30));
					return;
				}
				GOTO_STATE(WD279X_state_type3_1);
			}
			LOG_WARN("WD279X: CMD: Unknown command %02x\n", fdc->command_register);
			return;


		case WD279X_state_type1_1:
			if (fdc->data_register == fdc->track_register) {
				GOTO_STATE(WD279X_state_verify_track_1);
			}
			if (fdc->data_register > fdc->track_register)
				SET_DIRECTION;
			else
				RESET_DIRECTION;
			GOTO_STATE(WD279X_state_type1_2);


		case WD279X_state_type1_2:
			fdc->track_register += fdc->direction;
			GOTO_STATE(WD279X_state_type1_3);


		case WD279X_state_type1_3:
			if (fdc->tr00_state && fdc->direction == -1) {
				LOG_DEBUG(3, "WD279X: TR00!\n");
				fdc->track_register = 0;
				// The WD279x flow chart implies this delay is
				// not incurred in this situation, but real
				// code fails without it.
				NEXT_STATE(WD279X_state_verify_track_1, EVENT_MS(fdc->step_delay));
				return;
			}
			DELEGATE_CALL0(fdc->step);
			if (fdc->is_step_cmd) {
				NEXT_STATE(WD279X_state_verify_track_1, EVENT_MS(fdc->step_delay));
				return;
			}
			NEXT_STATE(WD279X_state_type1_1, EVENT_MS(fdc->step_delay));
			return;


		case WD279X_state_verify_track_1:
			if (!(fdc->command_register & 0x04)) {
				fdc->status_register &= ~(STATUS_BUSY);
				SET_INTRQ;
				return;
			}
			fdc->index_holes_count = 0;
			NEXT_STATE(WD279X_state_verify_track_2, DELEGATE_CALL0(fdc->time_to_next_idam));
			return;


		case WD279X_state_verify_track_2:
			idam = DELEGATE_CALL0(fdc->next_idam);
			if (fdc->index_holes_count >= 5) {
				LOG_DEBUG(3, "WD279X: index_holes_count >= 5: seek error\n");
				fdc->status_register &= ~(STATUS_BUSY);
				fdc->status_register |= STATUS_SEEK_ERROR;
				SET_INTRQ;
				return;
			}
			if (idam == NULL) {
				LOG_DEBUG(3, "WD279X: null IDAM: -> WD279X_state_verify_track_2\n");
				NEXT_STATE(WD279X_state_verify_track_2, DELEGATE_CALL0(fdc->time_to_next_idam));
				return;
			}
			fdc->crc = CRC16_RESET;
			if (IS_DOUBLE_DENSITY) {
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
			}
			(void)_vdrive_read(fdc);  /* Include IDAM in CRC */
			if (fdc->track_register != _vdrive_read(fdc)) {
				LOG_DEBUG(3, "WD279X: track_register != idam[1]: -> WD279X_state_verify_track_2\n");
				NEXT_STATE(WD279X_state_verify_track_2, DELEGATE_CALL0(fdc->time_to_next_idam));
				return;
			}
			/* Include rest of ID field - should result in computed CRC = 0 */
			for (i = 0; i < 5; i++)
				(void)_vdrive_read(fdc);
			if (fdc->crc != 0) {
				LOG_DEBUG(3, "WD279X: Verify track %d CRC16 error: $%04x != 0\n", fdc->track_register, fdc->crc);
				fdc->status_register |= STATUS_CRC_ERROR;
				NEXT_STATE(WD279X_state_verify_track_2, DELEGATE_CALL0(fdc->time_to_next_idam));
				return;
			}
			fdc->status_register &= ~(STATUS_CRC_ERROR|STATUS_BUSY);
			SET_INTRQ;
			return;


		case WD279X_state_type2_1:
			if ((fdc->command_register & 0x20) && fdc->write_protect_state) {
				fdc->status_register &= ~(STATUS_BUSY);
				fdc->status_register |= STATUS_WRITE_PROTECT;
				SET_INTRQ;
				return;
			}
			fdc->index_holes_count = 0;
			NEXT_STATE(WD279X_state_type2_2, DELEGATE_CALL0(fdc->time_to_next_idam));
			return;


		case WD279X_state_type2_2:
			idam = DELEGATE_CALL0(fdc->next_idam);
			if (fdc->index_holes_count >= 5) {
				fdc->status_register &= ~(STATUS_BUSY);
				fdc->status_register |= STATUS_RNF;
				SET_INTRQ;
				return;
			}
			if (idam == NULL) {
				NEXT_STATE(WD279X_state_type2_2, DELEGATE_CALL0(fdc->time_to_next_idam));
				return;
			}
			fdc->crc = CRC16_RESET;
			if (IS_DOUBLE_DENSITY) {
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
			}
			(void)_vdrive_read(fdc);  /* Include IDAM in CRC */
			if (fdc->track_register != _vdrive_read(fdc)) {
				NEXT_STATE(WD279X_state_type2_2, DELEGATE_CALL0(fdc->time_to_next_idam));
				return;
			}
			if (fdc->side != (int)_vdrive_read(fdc)) {
				/* No error if no SSO or 'C' not set */
				if (fdc->has_sso || fdc->command_register & 0x02) {
					NEXT_STATE(WD279X_state_type2_2, DELEGATE_CALL0(fdc->time_to_next_idam));
					return;
				}
			}
			if (fdc->sector_register != _vdrive_read(fdc)) {
				NEXT_STATE(WD279X_state_type2_2, DELEGATE_CALL0(fdc->time_to_next_idam));
				return;
			}
			i = _vdrive_read(fdc);
			if (fdc->has_length_flag)
				fdc->bytes_left = sector_size[(fdc->command_register & 0x08)?1:0][i&3];
			else
				fdc->bytes_left = sector_size[1][i&3];
			/* Including CRC bytes should result in computed CRC = 0 */
			(void)_vdrive_read(fdc);
			(void)_vdrive_read(fdc);
			if (fdc->crc != 0) {
				fdc->status_register |= STATUS_CRC_ERROR;
				LOG_DEBUG(3, "WD279X: Type 2 tr %d se %d CRC16 error: $%04x != 0\n", fdc->track_register, fdc->sector_register, fdc->crc);
				NEXT_STATE(WD279X_state_type2_2, DELEGATE_CALL0(fdc->time_to_next_idam));
				return;
			}

			if ((fdc->command_register & 0x20) == 0) {
				int bytes_to_scan, j;
				if (IS_SINGLE_DENSITY)
					bytes_to_scan = 30;
				else
					bytes_to_scan = 43;
				j = 0;
				fdc->dam = 0;
				do {
					fdc->crc = CRC16_RESET;
					if (IS_DOUBLE_DENSITY) {
						fdc->crc = crc16_byte(fdc->crc, 0xa1);
						fdc->crc = crc16_byte(fdc->crc, 0xa1);
						fdc->crc = crc16_byte(fdc->crc, 0xa1);
					}
					int tmp = _vdrive_read(fdc);
					if (tmp == 0xfb || tmp == 0xf8)
						fdc->dam = tmp;
					j++;
				} while (j < bytes_to_scan && fdc->dam == 0);
				if (fdc->dam == 0) {
					NEXT_STATE(WD279X_state_type2_2, DELEGATE_CALL0(fdc->time_to_next_byte));
					return;
				}
				NEXT_STATE(WD279X_state_read_sector_1, DELEGATE_CALL0(fdc->time_to_next_byte));
				return;
			}
			DELEGATE_CALL0(fdc->skip);
			DELEGATE_CALL0(fdc->skip);
			NEXT_STATE(WD279X_state_write_sector_1, DELEGATE_CALL0(fdc->time_to_next_byte));
			return;


		case WD279X_state_read_sector_1:
			LOG_DEBUG(3, "WD279X: Reading %d-byte sector (Tr %d, Se %d) from head_pos=%04x\n", fdc->bytes_left, fdc->track_register, fdc->sector_register, DELEGATE_CALL0(fdc->get_head_pos));
			if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
				log_open_hexdump(&log_rsec_hex, "WD279X: read-sector");
			fdc->status_register |= ((~fdc->dam & 1) << 5);
			fdc->data_register = _vdrive_read(fdc);
			if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
				log_hexdump_byte(log_rsec_hex, fdc->data_register);
			fdc->bytes_left--;
			SET_DRQ;
			NEXT_STATE(WD279X_state_read_sector_2, DELEGATE_CALL0(fdc->time_to_next_byte));
			return;


		case WD279X_state_read_sector_2:
			if (fdc->status_register & STATUS_DRQ) {
				fdc->status_register |= STATUS_LOST_DATA;
				if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
					log_hexdump_flag(log_rsec_hex);
				/* RESET_DRQ;  XXX */
			}
			if (fdc->bytes_left > 0) {
				fdc->data_register = _vdrive_read(fdc);
				if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
					log_hexdump_byte(log_rsec_hex, fdc->data_register);
				fdc->bytes_left--;
				SET_DRQ;
				NEXT_STATE(WD279X_state_read_sector_2, DELEGATE_CALL0(fdc->time_to_next_byte));
				return;
			}
			log_close(&log_rsec_hex);
			/* Including CRC bytes should result in computed CRC = 0 */
			(void)_vdrive_read(fdc);
			(void)_vdrive_read(fdc);
			NEXT_STATE(WD279X_state_read_sector_3, DELEGATE_CALL0(fdc->time_to_next_byte));
			return;


		case WD279X_state_read_sector_3:
			if (fdc->crc != 0) {
				LOG_DEBUG(3, "WD279X: Read sector data tr %d se %d CRC16 error: $%04x != 0\n", fdc->track_register, fdc->sector_register, fdc->crc);
				fdc->status_register |= STATUS_CRC_ERROR;
			}
			if (fdc->command_register & 0x10) {
				/* XXX what happens on overflow here? */
				fdc->sector_register++;
				GOTO_STATE(WD279X_state_type2_1);
			}
			fdc->status_register &= ~(STATUS_BUSY);
			SET_INTRQ;
			return;


		case WD279X_state_write_sector_1:
			SET_DRQ;
			for (i = 0; i < 8; i++)
				DELEGATE_CALL0(fdc->skip);
			NEXT_STATE(WD279X_state_write_sector_2, DELEGATE_CALL0(fdc->time_to_next_byte));
			return;


		case WD279X_state_write_sector_2:
			if (fdc->status_register & STATUS_DRQ) {
				fdc->status_register &= ~(STATUS_BUSY);
				RESET_DRQ;  /* XXX */
				fdc->status_register |= STATUS_LOST_DATA;
				SET_INTRQ;
				return;
			}
			DELEGATE_CALL0(fdc->skip);
			NEXT_STATE(WD279X_state_write_sector_3, DELEGATE_CALL0(fdc->time_to_next_byte));
			return;


		case WD279X_state_write_sector_3:
			if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
				log_open_hexdump(&log_wsec_hex, "WD279X: write-sector");
			if (IS_DOUBLE_DENSITY) {
				for (i = 0; i < 11; i++)
					DELEGATE_CALL0(fdc->skip);
				for (i = 0; i < 12; i++)
					_vdrive_write(fdc, 0);
				NEXT_STATE(WD279X_state_write_sector_4, DELEGATE_CALL0(fdc->time_to_next_byte));
				return;
			}
			for (i = 0; i < 6; i++)
				_vdrive_write(fdc, 0);
			NEXT_STATE(WD279X_state_write_sector_4, DELEGATE_CALL0(fdc->time_to_next_byte));
			return;


		case WD279X_state_write_sector_4:
			fdc->crc = CRC16_RESET;
			if (IS_DOUBLE_DENSITY) {
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
			}
			if (fdc->command_register & 1)
				_vdrive_write(fdc, 0xf8);
			else
				_vdrive_write(fdc, 0xfb);
			NEXT_STATE(WD279X_state_write_sector_5, DELEGATE_CALL0(fdc->time_to_next_byte));
			return;


		case WD279X_state_write_sector_5:
			data = fdc->data_register;
			if (fdc->status_register & STATUS_DRQ) {
				data = 0;
				fdc->status_register |= STATUS_LOST_DATA;
				if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
					log_hexdump_flag(log_wsec_hex);
				RESET_DRQ;  /* XXX */
			}
			if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
				log_hexdump_byte(log_wsec_hex, data);
			_vdrive_write(fdc, data);
			fdc->bytes_left--;
			if (fdc->bytes_left > 0) {
				SET_DRQ;
				NEXT_STATE(WD279X_state_write_sector_5, DELEGATE_CALL0(fdc->time_to_next_byte));
				return;
			}
			log_close(&log_wsec_hex);
			VDRIVE_WRITE_CRC16;
			NEXT_STATE(WD279X_state_write_sector_6, DELEGATE_CALL0(fdc->time_to_next_byte) + EVENT_US(20));
			return;


		case WD279X_state_write_sector_6:
			_vdrive_write(fdc, 0xfe);
			if (fdc->command_register & 0x10) {
				/* XXX what happens on overflow here? */
				fdc->sector_register++;
				GOTO_STATE(WD279X_state_type2_1);
			}
			fdc->status_register &= ~(STATUS_BUSY);
			SET_INTRQ;
			return;


		case WD279X_state_type3_1:
			switch (fdc->command_register & 0xf0) {
				case 0xc0:
					fdc->index_holes_count = 0;
					NEXT_STATE(WD279X_state_read_address_1, DELEGATE_CALL0(fdc->time_to_next_idam));
					return;
				case 0xe0:
					LOG_WARN("WD279X: CMD: Read track not implemented\n");
					SET_INTRQ;
					break;
				case 0xf0:
					GOTO_STATE(WD279X_state_write_track_1);
				default:
					break;
			}
			return;


		case WD279X_state_read_address_1:
			idam = DELEGATE_CALL0(fdc->next_idam);
			if (fdc->index_holes_count >= 6) {
				fdc->status_register &= ~(STATUS_BUSY);
				fdc->status_register |= STATUS_RNF;
				SET_INTRQ;
				return;
			}
			if (idam == NULL) {
				NEXT_STATE(WD279X_state_read_address_1, DELEGATE_CALL0(fdc->time_to_next_idam));
				return;
			}
			fdc->crc = CRC16_RESET;
			if (IS_DOUBLE_DENSITY) {
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
			}
			(void)_vdrive_read(fdc);
			NEXT_STATE(WD279X_state_read_address_2, DELEGATE_CALL0(fdc->time_to_next_byte));
			return;


		case WD279X_state_read_address_2:
			fdc->bytes_left = 5;
			fdc->data_register = _vdrive_read(fdc);
			/* At end of command, this is transferred to the sector register: */
			fdc->track_register_tmp = fdc->data_register;
			SET_DRQ;
			NEXT_STATE(WD279X_state_read_address_3, DELEGATE_CALL0(fdc->time_to_next_byte));
			return;


		case WD279X_state_read_address_3:
			/* Lost data not mentioned in data sheet, so not checking
			   for now */
			if (fdc->bytes_left > 0) {
				fdc->data_register = _vdrive_read(fdc);
				fdc->bytes_left--;
				SET_DRQ;
				NEXT_STATE(WD279X_state_read_address_3, DELEGATE_CALL0(fdc->time_to_next_byte));
				return;
			}
			fdc->sector_register = fdc->track_register_tmp;
			if (fdc->crc != 0) {
				fdc->status_register |= STATUS_CRC_ERROR;
			}
			fdc->status_register &= ~(STATUS_BUSY);
			SET_INTRQ;
			return;


		case WD279X_state_write_track_1:
			if (fdc->write_protect_state) {
				fdc->status_register &= ~(STATUS_BUSY);
				fdc->status_register |= STATUS_WRITE_PROTECT;
				SET_INTRQ;
				return;
			}
			SET_DRQ;
			/* Data sheet says 3 byte times, but CoCo NitrOS9 fails unless I set
			 * this delay higher. */
			NEXT_STATE(WD279X_state_write_track_2, 6 * W_BYTE_TIME);
			return;


		case WD279X_state_write_track_2:
			if (fdc->status_register & STATUS_DRQ) {
				RESET_DRQ;  /* XXX */
				fdc->status_register |= STATUS_LOST_DATA;
				fdc->status_register &= ~(STATUS_BUSY);
				SET_INTRQ;
				return;
			}
			fdc->index_holes_count = 0;
			NEXT_STATE(WD279X_state_write_track_2b, DELEGATE_CALL0(fdc->time_to_next_idam));
			return;


		case WD279X_state_write_track_2b:
			if (fdc->index_holes_count == 0) {
				LOG_DEBUG(3, "WD279X: Waiting for index pulse, head_pos=%04x\n", DELEGATE_CALL0(fdc->get_head_pos));
				NEXT_STATE(WD279X_state_write_track_2b, DELEGATE_CALL0(fdc->time_to_next_idam));
				return;
			}
			fdc->index_holes_count = 0;
			LOG_DEBUG(3, "WD279X: Writing track from head_pos=%04x\n", DELEGATE_CALL0(fdc->get_head_pos));
			if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
				log_open_hexdump(&log_wtrk_hex, "WD279X: write-track");
			GOTO_STATE(WD279X_state_write_track_3);


		case WD279X_state_write_track_3:
			data = fdc->data_register;
			if (fdc->index_holes_count > 0) {
				if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
					log_close(&log_wtrk_hex);
				LOG_DEBUG(3, "WD279X: Finished writing track at head_pos=%04x\n", DELEGATE_CALL0(fdc->get_head_pos));
				RESET_DRQ;  /* XXX */
				fdc->status_register &= ~(STATUS_BUSY);
				SET_INTRQ;
				return;
			}
			if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
				log_hexdump_byte(log_wtrk_hex, fdc->data_register);
			if (fdc->status_register & STATUS_DRQ) {
				data = 0;
				fdc->status_register |= STATUS_LOST_DATA;
				if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_DATA)
					log_hexdump_flag(log_wtrk_hex);
			}
			SET_DRQ;
			if (IS_SINGLE_DENSITY) {
				/* Single density */
				if (data == 0xf5 || data == 0xf6) {
					LOG_DEBUG(3, "WD279X: Illegal value in single-density track write: %02x\n", data);
				}
				if (data == 0xf7) {
					VDRIVE_WRITE_CRC16;
					NEXT_STATE(WD279X_state_write_track_3, DELEGATE_CALL0(fdc->time_to_next_byte));
					return;
				}
				if (data >= 0xf8 && data <= 0xfb) {
					fdc->crc = CRC16_RESET;
					_vdrive_write(fdc, data);
					NEXT_STATE(WD279X_state_write_track_3, DELEGATE_CALL0(fdc->time_to_next_byte));
					return;
				}
				if (data == 0xfe) {
					LOG_DEBUG(3, "WD279X: IDAM at head_pos=%04x\n", DELEGATE_CALL0(fdc->get_head_pos));
					fdc->crc = CRC16_RESET;
					DELEGATE_CALL0(fdc->write_idam);
					fdc->crc = crc16_byte(fdc->crc, 0xfe);
					NEXT_STATE(WD279X_state_write_track_3, DELEGATE_CALL0(fdc->time_to_next_byte));
					return;
				}
				_vdrive_write(fdc, data);
				NEXT_STATE(WD279X_state_write_track_3, DELEGATE_CALL0(fdc->time_to_next_byte));
				return;
			}
			/* Double density */
			if (data == 0xf7) {
				VDRIVE_WRITE_CRC16;
				NEXT_STATE(WD279X_state_write_track_3, DELEGATE_CALL0(fdc->time_to_next_byte));
				return;
			}
			if (data == 0xfe) {
				LOG_DEBUG(3, "WD279X: IDAM at head_pos=%04x\n", DELEGATE_CALL0(fdc->get_head_pos));
				DELEGATE_CALL0(fdc->write_idam);
				fdc->crc = crc16_byte(fdc->crc, 0xfe);
				NEXT_STATE(WD279X_state_write_track_3, DELEGATE_CALL0(fdc->time_to_next_byte));
				return;
			}
			if (data == 0xf5) {
				fdc->crc = CRC16_RESET;
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_byte(fdc->crc, 0xa1);
				_vdrive_write(fdc, 0xa1);
				NEXT_STATE(WD279X_state_write_track_3, DELEGATE_CALL0(fdc->time_to_next_byte));
				return;
			}
			if (data == 0xf6) {
				data = 0xc2;
			}
			_vdrive_write(fdc, data);
			NEXT_STATE(WD279X_state_write_track_3, DELEGATE_CALL0(fdc->time_to_next_byte));
			return;

		default:
			return;

		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Debugging

static char const * const debug_state_name[] = {
	"accept_command",
	"type1_1",
	"type1_2",
	"type1_3",
	"verify_track_1",
	"verify_track_2",
	"type2_1",
	"type2_2",
	"read_sector_1",
	"read_sector_2",
	"read_sector_3",
	"write_sector_1",
	"write_sector_2",
	"write_sector_3",
	"write_sector_4",
	"write_sector_5",
	"write_sector_6",
	"type3_1",
	"read_address_1",
	"read_address_2",
	"read_address_3",
	"write_track_1",
	"write_track_2",
	"write_track_2b",
	"write_track_3",
};

static char const * const debug_command[] = {
	"restore",
	"seek",
	"step",
	"step",
	"step-in",
	"step-in",
	"step-out",
	"step-out",
	"read-sector",
	"read-sector",
	"write-sector",
	"write-sector",
	"read-address",
	"force-interrupt",
	"read-track N/A",
	"write-track",
};

static void debug_state(WD279X *fdc) {
	assert(fdc != NULL);
	assert((unsigned)fdc->state < WD279X_state_invalid);
	unsigned level = xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_STATE;
	if (level == 0)
		return;
	_Bool forced_interrupt = ((fdc->command_register & 0xf0) == 0xd0);
	if (fdc->state <= WD279X_state_accept_command || forced_interrupt) {
		// command (incl. forced interrupt)
		unsigned type = ((fdc->command_register) >> 4) & 15;
		LOG_PRINT("WD279X: CR=%02x ST=%02x TR=%02x SR=%02x DR=%02x state=%s [%s]\n", fdc->command_register, fdc->status_register, fdc->track_register, fdc->sector_register, fdc->data_register, debug_state_name[fdc->state], debug_command[type]);
	} else if (level >= 2) {
		// any other state
		LOG_PRINT("WD279X: CR=%02x ST=%02x TR=%02x SR=%02x DR=%02x state=%s\n", fdc->command_register, fdc->status_register, fdc->track_register, fdc->sector_register, fdc->data_register, debug_state_name[fdc->state]);
	}
}
