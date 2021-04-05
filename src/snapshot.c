/*

Snapshotting of emulated system

Copyright 2003-2016 Ciaran Anscomb

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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "c-strcase.h"
#include "xalloc.h"

#include "cart.h"
#include "fs.h"
#include "keyboard.h"
#include "hd6309.h"
#include "logging.h"
#include "machine.h"
#include "mc6809.h"
#include "mc6821.h"
#include "mc6847/mc6847.h"
#include "sam.h"
#include "snapshot.h"
#include "tape.h"
#include "vdisk.h"
#include "vdrive.h"
#include "xroar.h"

/* Write files in 'chunks', each with an identifying byte and a 16-bit length.
 * This should mean no changes are required that break the format.  */

/* Note: Setting up the correct ROM select for Dragon 64 depends on SAM
 * register update following PIA configuration. */

#define ID_REGISTER_DUMP (0)  // deprecated - part of ID_MC6809_STATE
#define ID_RAM_PAGE0     (1)
#define ID_PIA_REGISTERS (2)
#define ID_SAM_REGISTERS (3)
#define ID_MC6809_STATE  (4)
#define ID_KEYBOARD_MAP  (5)  // deprecated - part of ID_MACHINECONFIG
#define ID_ARCHITECTURE  (6)  // deprecated - part of ID_MACHINECONFIG
#define ID_RAM_PAGE1     (7)
#define ID_MACHINECONFIG (8)
#define ID_SNAPVERSION   (9)
#define ID_VDISK_FILE    (10)
#define ID_HD6309_STATE  (11)
#define ID_CART          (12)  // as of v1.8

#define SNAPSHOT_VERSION_MAJOR 1
#define SNAPSHOT_VERSION_MINOR 8

// Versions < 1.8 used a number for these (add 1 to index)
static const char *old_cart_type_names[] = {
	"dragondos",
	"rsdos",
	"delta",
};

static const char *pia_component_names[2] = { "PIA0", "PIA1" };

static char *read_string(FILE *fd, unsigned *size) {
	char *str = NULL;
	if (*size == 0) {
		return NULL;
	}
	int len = fs_read_uint8(fd);
	(*size)--;
	// For whatever reason, I chose to store len+1 as the size field
	// for strings.  Oh well, this means zero is invalid.
	if (len < 1) {
		return NULL;
	}
	if ((unsigned)(len-1) >= *size) {
		return NULL;
	}
	str = xzalloc(len);
	if (len > 1) {
		*size -= fread(str, 1, len-1, fd);
	}
	return str;
}

static void write_chunk_header(FILE *fd, unsigned id, int size) {
	assert(size >= 0);
	fs_write_uint8(fd, id);
	fs_write_uint16(fd, size);
}

static void write_mc6809(FILE *fd, struct MC6809 *cpu) {
	write_chunk_header(fd, ID_MC6809_STATE, 20);
	fs_write_uint8(fd, cpu->reg_cc);
	fs_write_uint8(fd, MC6809_REG_A(cpu));
	fs_write_uint8(fd, MC6809_REG_B(cpu));
	fs_write_uint8(fd, cpu->reg_dp);
	fs_write_uint16(fd, cpu->reg_x);
	fs_write_uint16(fd, cpu->reg_y);
	fs_write_uint16(fd, cpu->reg_u);
	fs_write_uint16(fd, cpu->reg_s);
	fs_write_uint16(fd, cpu->reg_pc);
	fs_write_uint8(fd, cpu->halt);
	fs_write_uint8(fd, cpu->nmi);
	fs_write_uint8(fd, cpu->firq);
	fs_write_uint8(fd, cpu->irq);
	fs_write_uint8(fd, cpu->state);
	fs_write_uint8(fd, cpu->nmi_armed);
}

static uint8_t tfm_reg(struct HD6309 *hcpu, uint16_t *ptr) {
	struct MC6809 *cpu = &hcpu->mc6809;
	if (ptr == &cpu->reg_d)
		return 0;
	if (ptr == &cpu->reg_x)
		return 1;
	if (ptr == &cpu->reg_y)
		return 2;
	if (ptr == &cpu->reg_u)
		return 3;
	if (ptr == &cpu->reg_s)
		return 4;
	return 15;
}

static void write_hd6309(FILE *fd, struct HD6309 *hcpu) {
	struct MC6809 *cpu = &hcpu->mc6809;
	write_chunk_header(fd, ID_HD6309_STATE, 27);
	fs_write_uint8(fd, cpu->reg_cc);
	fs_write_uint8(fd, MC6809_REG_A(cpu));
	fs_write_uint8(fd, MC6809_REG_B(cpu));
	fs_write_uint8(fd, cpu->reg_dp);
	fs_write_uint16(fd, cpu->reg_x);
	fs_write_uint16(fd, cpu->reg_y);
	fs_write_uint16(fd, cpu->reg_u);
	fs_write_uint16(fd, cpu->reg_s);
	fs_write_uint16(fd, cpu->reg_pc);
	fs_write_uint8(fd, cpu->halt);
	fs_write_uint8(fd, cpu->nmi);
	fs_write_uint8(fd, cpu->firq);
	fs_write_uint8(fd, cpu->irq);
	// 6309-specific state
	fs_write_uint8(fd, hcpu->state);
	fs_write_uint8(fd, cpu->nmi_armed);
	// 6309-specific extras:
	fs_write_uint8(fd, HD6309_REG_E(hcpu));
	fs_write_uint8(fd, HD6309_REG_F(hcpu));
	fs_write_uint16(fd, hcpu->reg_v);
	fs_write_uint8(fd, hcpu->reg_md);
	uint8_t tfm_src_dest = tfm_reg(hcpu, hcpu->tfm_src) << 4;
	tfm_src_dest |= tfm_reg(hcpu, hcpu->tfm_dest);
	fs_write_uint8(fd, tfm_src_dest);
	// lowest 4 bits of each of these is enough:
	uint8_t tfm_mod = (hcpu->tfm_src_mod & 15) << 4;
	tfm_mod |= (hcpu->tfm_dest_mod & 15);
	fs_write_uint8(fd, tfm_mod);
}

int write_snapshot(const char *filename) {
	FILE *fd;
	if (!(fd = fopen(filename, "wb")))
		return -1;
	fwrite("XRoar snapshot.\012\000", 17, 1, fd);
	// Snapshot version
	write_chunk_header(fd, ID_SNAPVERSION, 3);
	fs_write_uint8(fd, SNAPSHOT_VERSION_MAJOR);
	fs_write_uint16(fd, SNAPSHOT_VERSION_MINOR);
	// Machine running config
	write_chunk_header(fd, ID_MACHINECONFIG, 8);
	fs_write_uint8(fd, 0);  // xroar_machine_config->index;
	fs_write_uint8(fd, xroar_machine_config->architecture);
	fs_write_uint8(fd, xroar_machine_config->cpu);
	fs_write_uint8(fd, xroar_machine_config->keymap);
	fs_write_uint8(fd, xroar_machine_config->tv_standard);
	fs_write_uint8(fd, xroar_machine_config->ram);
	struct cart *cart = xroar_machine->get_interface(xroar_machine, "cart");
	if (cart) {
		// attempt to keep snapshots >= v1.8 loadable by older versions
		unsigned old_cart_type = 0;
		for (unsigned i = 0; i < ARRAY_N_ELEMENTS(old_cart_type_names); i++) {
			if (c_strcasecmp(cart->config->type, old_cart_type_names[i]) == 0) {
				old_cart_type = i + 1;
				break;
			}
		}
		fs_write_uint8(fd, old_cart_type);
	} else {
		fs_write_uint8(fd, 0);
	}
	fs_write_uint8(fd, xroar_machine_config->cross_colour_phase);
	// RAM page 0
	struct machine_memory *ram0 = xroar_machine->get_component(xroar_machine, "RAM0");
	write_chunk_header(fd, ID_RAM_PAGE0, ram0->size);
	fwrite(ram0->data, 1, ram0->size, fd);
	// RAM page 1
	struct machine_memory *ram1 = xroar_machine->get_component(xroar_machine, "RAM1");
	if (ram1->size > 0) {
		write_chunk_header(fd, ID_RAM_PAGE1, ram1->size);
		fwrite(ram1->data, 1, ram1->size, fd);
	}
	// PIA state written before CPU state because PIA may have
	// unacknowledged interrupts pending already cleared in the CPU state
	write_chunk_header(fd, ID_PIA_REGISTERS, 3 * 4);
	for (int i = 0; i < 2; i++) {
		struct MC6821 *pia = xroar_machine->get_component(xroar_machine, pia_component_names[i]);
		fs_write_uint8(fd, pia->a.direction_register);
		fs_write_uint8(fd, pia->a.output_register);
		fs_write_uint8(fd, pia->a.control_register);
		fs_write_uint8(fd, pia->b.direction_register);
		fs_write_uint8(fd, pia->b.output_register);
		fs_write_uint8(fd, pia->b.control_register);
	}
	// CPU state
	struct MC6809 *cpu = xroar_machine->get_component(xroar_machine, "CPU0");
	struct MC6883 *sam = xroar_machine->get_component(xroar_machine, "SAM0");
	switch (cpu->variant) {
	case MC6809_VARIANT_MC6809: default:
		write_mc6809(fd, cpu);
		break;
	case MC6809_VARIANT_HD6309:
		write_hd6309(fd, (struct HD6309 *)cpu);
		break;
	}
	// SAM
	write_chunk_header(fd, ID_SAM_REGISTERS, 2);
	fs_write_uint16(fd, sam_get_register(sam));

	// Cartridge
	if (cart) {
		struct cart_config *cc = cart->config;
		size_t name_len = cc->name ? strlen(cc->name) + 1 : 1;
		if (name_len > 255) name_len = 255;
		size_t desc_len = cc->description ? strlen(cc->description) + 1 : 1;
		if (desc_len > 255) desc_len = 255;
		size_t type_len = cc->type ? strlen(cc->type) + 1 : 1;
		if (type_len > 255) type_len = 255;
		size_t rom_len = cc->rom ? strlen(cc->rom) + 1: 1;
		if (rom_len > 255) rom_len = 255;
		size_t rom2_len = cc->rom2 ? strlen(cc->rom2) + 1: 1;
		if (rom2_len > 255) rom2_len = 255;
		int size = name_len + desc_len + type_len + rom_len + rom2_len + 2;
		write_chunk_header(fd, ID_CART, size);
		fs_write_uint8(fd, name_len);
		if (cc->name)
			fwrite(cc->name, 1, name_len-1, fd);
		fs_write_uint8(fd, desc_len);
		if (cc->description)
			fwrite(cc->description, 1, desc_len-1, fd);
		fs_write_uint8(fd, type_len);
		if (cc->type)
			fwrite(cc->type, 1, type_len-1, fd);
		fs_write_uint8(fd, rom_len);
		if (cc->rom)
			fwrite(cc->rom, 1, rom_len-1, fd);
		fs_write_uint8(fd, rom2_len);
		if (cc->rom2)
			fwrite(cc->rom2, 1, rom2_len-1, fd);
		fs_write_uint8(fd, cc->becker_port);
		fs_write_uint8(fd, cc->autorun);
	}

	// Attached virtual disk filenames
	{
		for (unsigned drive = 0; drive < VDRIVE_MAX_DRIVES; drive++) {
			struct vdisk *disk = vdrive_disk_in_drive(xroar_vdrive_interface, drive);
			if (disk != NULL && disk->filename != NULL) {
				int length = strlen(disk->filename) + 1;
				write_chunk_header(fd, ID_VDISK_FILE, 1 + length);
				fs_write_uint8(fd, drive);
				fwrite(disk->filename, 1, length, fd);
			}
		}
	}
	// Finish up
	fclose(fd);
	return 0;
}

static const int old_arch_mapping[4] = {
	MACHINE_DRAGON32,
	MACHINE_DRAGON64,
	MACHINE_TANO,
	MACHINE_COCOUS
};

static void old_set_registers(uint8_t *regs) {
	struct MC6809 *cpu = xroar_machine->get_component(xroar_machine, "CPU0");
	cpu->reg_cc = regs[0];
	MC6809_REG_A(cpu) = regs[1];
	MC6809_REG_B(cpu) = regs[2];
	cpu->reg_dp = regs[3];
	cpu->reg_x = regs[4] << 8 | regs[5];
	cpu->reg_y = regs[6] << 8 | regs[7];
	cpu->reg_u = regs[8] << 8 | regs[9];
	cpu->reg_s = regs[10] << 8 | regs[11];
	cpu->reg_pc = regs[12] << 8 | regs[13];
	cpu->halt = 0;
	cpu->nmi = 0;
	cpu->firq = 0;
	cpu->irq = 0;
	cpu->state = MC6809_COMPAT_STATE_NORMAL;
	cpu->nmi_armed = 0;
}

static uint16_t *tfm_reg_ptr(struct HD6309 *hcpu, unsigned reg) {
	struct MC6809 *cpu = &hcpu->mc6809;
	switch (reg >> 4) {
	case 0:
		return &cpu->reg_d;
	case 1:
		return &cpu->reg_x;
	case 2:
		return &cpu->reg_y;
	case 3:
		return &cpu->reg_u;
	case 4:
		return &cpu->reg_s;
	default:
		break;
	}
	return NULL;
}

#define sex4(v) (((uint16_t)(v) & 0x07) - ((uint16_t)(v) & 0x08))

int read_snapshot(const char *filename) {
	FILE *fd;
	uint8_t buffer[17];
	int section, tmp;
	int version_major = 1, version_minor = 0;
	if (filename == NULL)
		return -1;
	if (!(fd = fopen(filename, "rb")))
		return -1;
	if (fread(buffer, 17, 1, fd) < 1) {
		LOG_WARN("Snapshot format not recognised.\n");
		fclose(fd);
		return -1;
	}
	if (strncmp((char *)buffer, "XRoar snapshot.\012\000", 17)) {
		// Very old-style snapshot.  Register dump always came first.
		// Also, it used to be written out as only taking 12 bytes.
		if (buffer[0] != ID_REGISTER_DUMP || buffer[1] != 0
				|| (buffer[2] != 12 && buffer[2] != 14)) {
			LOG_WARN("Snapshot format not recognised.\n");
			fclose(fd);
			return -1;
		}
	}
	// Default to Dragon 64 for old snapshots
	struct machine_config *mc = machine_config_by_arch(ARCH_DRAGON64);
	xroar_configure_machine(mc);
	xroar_machine->reset(xroar_machine, RESET_HARD);
	// If old snapshot, buffer contains register dump
	if (buffer[0] != 'X') {
		old_set_registers(buffer + 3);
	}
	struct cart_config *cart_config = NULL;
	while ((section = fs_read_uint8(fd)) >= 0) {
		unsigned size = fs_read_uint16(fd);
		if (size == 0) size = 0x10000;
		LOG_DEBUG(2, "Snapshot read: chunk type %d, size %u\n", section, size);
		switch (section) {
			case ID_ARCHITECTURE:
				// Deprecated: Machine architecture
				if (size < 1) break;
				tmp = fs_read_uint8(fd);
				tmp %= 4;
				mc->architecture = old_arch_mapping[tmp];
				xroar_configure_machine(mc);
				xroar_machine->reset(xroar_machine, RESET_HARD);
				size--;
				break;
			case ID_KEYBOARD_MAP:
				// Deprecated: Keyboard map
				if (size < 1) break;
				tmp = fs_read_uint8(fd);
				xroar_set_keymap(1, tmp);
				size--;
				break;
			case ID_REGISTER_DUMP:
				// Deprecated
				if (size < 14) break;
				size -= fread(buffer, 1, 14, fd);
				old_set_registers(buffer);
				break;

			case ID_MC6809_STATE:
				{
					// MC6809 state
					if (size < 20) break;
					struct MC6809 *cpu = xroar_machine->get_component(xroar_machine, "CPU0");
					if (cpu->variant != MC6809_VARIANT_MC6809) {
						LOG_WARN("CPU mismatch - skipping MC6809 chunk\n");
						break;
					}
					cpu->reg_cc = fs_read_uint8(fd);
					MC6809_REG_A(cpu) = fs_read_uint8(fd);
					MC6809_REG_B(cpu) = fs_read_uint8(fd);
					cpu->reg_dp = fs_read_uint8(fd);
					cpu->reg_x = fs_read_uint16(fd);
					cpu->reg_y = fs_read_uint16(fd);
					cpu->reg_u = fs_read_uint16(fd);
					cpu->reg_s = fs_read_uint16(fd);
					cpu->reg_pc = fs_read_uint16(fd);
					cpu->halt = fs_read_uint8(fd);
					cpu->nmi = fs_read_uint8(fd);
					cpu->firq = fs_read_uint8(fd);
					cpu->irq = fs_read_uint8(fd);
					if (size == 21) {
						// Old style
						int wait_for_interrupt;
						int skip_register_push;
						wait_for_interrupt = fs_read_uint8(fd);
						skip_register_push = fs_read_uint8(fd);
						if (wait_for_interrupt && skip_register_push) {
							cpu->state = MC6809_COMPAT_STATE_CWAI;
						} else if (wait_for_interrupt) {
							cpu->state = MC6809_COMPAT_STATE_SYNC;
						} else {
							cpu->state = MC6809_COMPAT_STATE_NORMAL;
						}
						size--;
					} else {
						cpu->state = fs_read_uint8(fd);
						// Translate old otherwise-unused MC6809
						// states indicating instruction page.
						cpu->page = 0;
						if (cpu->state == mc6809_state_instruction_page_2) {
							cpu->page = 0x0200;
							cpu->state = mc6809_state_next_instruction;
						}
						if (cpu->state == mc6809_state_instruction_page_3) {
							cpu->page = 0x0300;
							cpu->state = mc6809_state_next_instruction;
						}
					}
					cpu->nmi_armed = fs_read_uint8(fd);
					size -= 20;
					if (size > 0) {
						// Skip 'halted'
						(void)fs_read_uint8(fd);
						size--;
					}
				}
				break;

			case ID_HD6309_STATE:
				{
					// HD6309 state
					if (size < 27) break;
					struct MC6809 *cpu = xroar_machine->get_component(xroar_machine, "CPU0");
					if (cpu->variant != MC6809_VARIANT_HD6309) {
						LOG_WARN("CPU mismatch - skipping HD6309 chunk\n");
						break;
					}
					struct HD6309 *hcpu = (struct HD6309 *)cpu;
					cpu->reg_cc = fs_read_uint8(fd);
					MC6809_REG_A(cpu) = fs_read_uint8(fd);
					MC6809_REG_B(cpu) = fs_read_uint8(fd);
					cpu->reg_dp = fs_read_uint8(fd);
					cpu->reg_x = fs_read_uint16(fd);
					cpu->reg_y = fs_read_uint16(fd);
					cpu->reg_u = fs_read_uint16(fd);
					cpu->reg_s = fs_read_uint16(fd);
					cpu->reg_pc = fs_read_uint16(fd);
					cpu->halt = fs_read_uint8(fd);
					cpu->nmi = fs_read_uint8(fd);
					cpu->firq = fs_read_uint8(fd);
					cpu->irq = fs_read_uint8(fd);
					hcpu->state = fs_read_uint8(fd);
					// Translate old otherwise-unused HD6309
					// states indicating instruction page.
					cpu->page = 0;
					if (hcpu->state == hd6309_state_instruction_page_2) {
						cpu->page = 0x0200;
						hcpu->state = hd6309_state_next_instruction;
					}
					if (hcpu->state == hd6309_state_instruction_page_3) {
						cpu->page = 0x0300;
						hcpu->state = hd6309_state_next_instruction;
					}
					cpu->nmi_armed = fs_read_uint8(fd);
					HD6309_REG_E(hcpu) = fs_read_uint8(fd);
					HD6309_REG_F(hcpu) = fs_read_uint8(fd);
					hcpu->reg_v = fs_read_uint16(fd);
					tmp = fs_read_uint8(fd);
					hcpu->reg_md = tmp;
					tmp = fs_read_uint8(fd);
					hcpu->tfm_src = tfm_reg_ptr(hcpu, tmp >> 4);
					hcpu->tfm_dest = tfm_reg_ptr(hcpu, tmp & 15);
					tmp = fs_read_uint8(fd);
					hcpu->tfm_src_mod = sex4(tmp >> 4);
					hcpu->tfm_dest_mod = sex4(tmp & 15);
					size -= 27;
				}
				break;

			case ID_MACHINECONFIG:
				// Machine running config
				if (size < 7) break;
				(void)fs_read_uint8(fd);  // requested_machine
				tmp = fs_read_uint8(fd);
				mc = machine_config_by_arch(tmp);
				tmp = fs_read_uint8(fd);  // was romset
				if (version_minor >= 7) {
					// old field not used any more, repurposed
					// in v1.7 to hold cpu type:
					mc->cpu = tmp;
				}
				mc->keymap = fs_read_uint8(fd);  // keymap
				mc->tv_standard = fs_read_uint8(fd);
				mc->ram = fs_read_uint8(fd);
				tmp = fs_read_uint8(fd);  // dos_type
				if (version_minor < 8) {
					// v1.8 adds a separate cart chunk
					xroar_set_dos(tmp);
				}
				size -= 7;
				if (size > 0) {
					mc->cross_colour_phase = fs_read_uint8(fd);
					size--;
				}
				xroar_configure_machine(mc);
				xroar_machine->reset(xroar_machine, RESET_HARD);
				break;

			case ID_PIA_REGISTERS:
				for (int i = 0; i < 2; i++) {
					struct MC6821 *pia = xroar_machine->get_component(xroar_machine, pia_component_names[i]);
					if (size < 3) break;
					pia->a.direction_register = fs_read_uint8(fd);
					pia->a.output_register = fs_read_uint8(fd);
					pia->a.control_register = fs_read_uint8(fd);
					size -= 3;
					if (size < 3) break;
					pia->b.direction_register = fs_read_uint8(fd);
					pia->b.output_register = fs_read_uint8(fd);
					pia->b.control_register = fs_read_uint8(fd);
					size -= 3;
					mc6821_update_state(pia);
				}
				break;

			case ID_RAM_PAGE0:
				{
					struct machine_memory *ram0 = xroar_machine->get_component(xroar_machine, "RAM0");
					assert(ram0 != NULL);
					ram0->size = (size < ram0->max_size) ? size : ram0->max_size;
					size -= fread(ram0->data, 1, ram0->size, fd);
				}
				break;
			case ID_RAM_PAGE1:
				{
					struct machine_memory *ram1 = xroar_machine->get_component(xroar_machine, "RAM1");
					assert(ram1 != NULL);
					ram1->size = (size < ram1->max_size) ? size : ram1->max_size;
					size -= fread(ram1->data, 1, ram1->size, fd);
				}
				break;
			case ID_SAM_REGISTERS:
				// SAM
				if (size < 2) break;
				tmp = fs_read_uint16(fd);
				size -= 2;
				{
					struct MC6883 *sam = xroar_machine->get_component(xroar_machine, "SAM0");
					sam_set_register(sam, tmp);
				}
				break;

			case ID_SNAPVERSION:
				// Snapshot version - abort if snapshot
				// contains stuff we don't understand
				if (size < 3) break;
				version_major = fs_read_uint8(fd);
				version_minor = fs_read_uint16(fd);
				size -= 3;
				if (version_major != SNAPSHOT_VERSION_MAJOR
				    || version_minor > SNAPSHOT_VERSION_MINOR) {
					LOG_WARN("Snapshot version %d.%d not supported.\n", version_major, version_minor);
					fclose(fd);
					return -1;
				}
				break;

			case ID_VDISK_FILE:
				// Attached virtual disk filenames
				{
					int drive;
					size--;
					drive = fs_read_uint8(fd);
					vdrive_eject_disk(xroar_vdrive_interface, drive);
					if (size > 0) {
						char *name = malloc(size);
						if (name != NULL) {
							size -= fread(name, 1, size, fd);
							vdrive_insert_disk(xroar_vdrive_interface, drive, vdisk_load(name));
						}
					}
				}
				break;

			case ID_CART:
				// Attached cartridge
				{
					char *name = read_string(fd, &size);
					// must have a name
					if (!name || size == 0) break;
					char *desc = read_string(fd, &size);
					if (size == 0) break;
					char *type = read_string(fd, &size);
					if (size == 0) break;
					char *rom = read_string(fd, &size);
					if (size == 0) break;
					char *rom2 = read_string(fd, &size);
					if (size < 2) break;
					cart_config = cart_config_by_name(name);
					if (!cart_config) {
						cart_config = cart_config_new();
					}
					if (cart_config->name)
						free(cart_config->name);
					cart_config->name = name;
					if (cart_config->description)
						free(cart_config->description);
					cart_config->description = desc;
					if (cart_config->type)
						free(cart_config->type);
					cart_config->type = type;
					if (cart_config->rom)
						free(cart_config->rom);
					cart_config->rom = rom;
					if (cart_config->rom2)
						free(cart_config->rom2);
					cart_config->rom2 = rom2;
					cart_config->becker_port = fs_read_uint8(fd);
					cart_config->autorun = fs_read_uint8(fd);
					size -= 2;
				}
				break;

			default:
				// Unknown chunk
				LOG_WARN("Unknown chunk in snaphot.\n");
				break;
		}
		if (size > 0) {
			LOG_WARN("Skipping extra bytes in snapshot chunk id=%d.\n", (int)section);
			for (; size; size--)
				(void)fs_read_uint8(fd);
		}
	}
	fclose(fd);
	if (cart_config) {
		// XXX really we need something to update the UI here, the
		// embedded cart config may have changed description.  more
		// importantly, the UI won't know about the id.
		xroar_set_cart(1, cart_config->name);
	}
	return 0;
}
