/*

Hitach HD6309 CPU tracing

Copyright 2012-2017 Ciaran Anscomb

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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "hd6309.h"
#include "hd6309_trace.h"

/* Instruction types.  PAGE0, PAGE2 and PAGE3 switch which page is selected. */

enum {
	PAGE0 = 0, PAGE2 = 1, PAGE3 = 2, ILLEGAL,
	INHERENT, WORD_IMMEDIATE, IMMEDIATE, EXTENDED,
	DIRECT, INDEXED, RELATIVE, LONG_RELATIVE,
	STACKS, STACKU, REGISTER, IRQVECTOR,
	QUAD_IMMEDIATE, MEMBIT,
	INMEM_DIRECT, INMEM_INDEXED, INMEM_EXTENDED,
	TFMPP, TFMMM, TFMP0, TFM0P
};

/* Three arrays of instructions, one for each of PAGE0, PAGE2 and PAGE3 */

static struct {
	const char *mnemonic;
	int type;
} const instructions[3][256] = {

	{
		// 0x00 - 0x0F
		{ "NEG", DIRECT },
		{ "OIM", INMEM_DIRECT },
		{ "AIM", INMEM_DIRECT },
		{ "COM", DIRECT },
		{ "LSR", DIRECT },
		{ "EIM", INMEM_DIRECT },
		{ "ROR", DIRECT },
		{ "ASR", DIRECT },
		{ "LSL", DIRECT },
		{ "ROL", DIRECT },
		{ "DEC", DIRECT },
		{ "TIM", INMEM_DIRECT },
		{ "INC", DIRECT },
		{ "TST", DIRECT },
		{ "JMP", DIRECT },
		{ "CLR", DIRECT },
		// 0x10 - 0x1F
		{ "*", PAGE2 },
		{ "*", PAGE3 },
		{ "NOP", INHERENT },
		{ "SYNC", INHERENT },
		{ "SEXW", INHERENT },
		{ "*", ILLEGAL },
		{ "LBRA", LONG_RELATIVE },
		{ "LBSR", LONG_RELATIVE },
		{ "*", ILLEGAL },
		{ "DAA", INHERENT },
		{ "ORCC", IMMEDIATE },
		{ "*", ILLEGAL },
		{ "ANDCC", IMMEDIATE },
		{ "SEX", INHERENT },
		{ "EXG", REGISTER },
		{ "TFR", REGISTER },
		// 0x20 - 0x2F
		{ "BRA", RELATIVE },
		{ "BRN", RELATIVE },
		{ "BHI", RELATIVE },
		{ "BLS", RELATIVE },
		{ "BCC", RELATIVE },
		{ "BCS", RELATIVE },
		{ "BNE", RELATIVE },
		{ "BEQ", RELATIVE },
		{ "BVC", RELATIVE },
		{ "BVS", RELATIVE },
		{ "BPL", RELATIVE },
		{ "BMI", RELATIVE },
		{ "BGE", RELATIVE },
		{ "BLT", RELATIVE },
		{ "BGT", RELATIVE },
		{ "BLE", RELATIVE },
		// 0x30 - 0x3F
		{ "LEAX", INDEXED },
		{ "LEAY", INDEXED },
		{ "LEAS", INDEXED },
		{ "LEAU", INDEXED },
		{ "PSHS", STACKS },
		{ "PULS", STACKS },
		{ "PSHU", STACKU },
		{ "PULU", STACKU },
		{ "*", ILLEGAL },
		{ "RTS", INHERENT },
		{ "ABX", INHERENT },
		{ "RTI", INHERENT },
		{ "CWAI", IMMEDIATE },
		{ "MUL", INHERENT },
		{ "*", ILLEGAL },
		{ "SWI", INHERENT },
		// 0x40 - 0x4F
		{ "NEGA", INHERENT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "COMA", INHERENT },
		{ "LSRA", INHERENT },
		{ "*", ILLEGAL },
		{ "RORA", INHERENT },
		{ "ASRA", INHERENT },
		{ "LSLA", INHERENT },
		{ "ROLA", INHERENT },
		{ "DECA", INHERENT },
		{ "*", ILLEGAL },
		{ "INCA", INHERENT },
		{ "TSTA", INHERENT },
		{ "*", ILLEGAL },
		{ "CLRA", INHERENT },
		// 0x50 - 0x5F
		{ "NEGB", INHERENT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "COMB", INHERENT },
		{ "LSRB", INHERENT },
		{ "*", ILLEGAL },
		{ "RORB", INHERENT },
		{ "ASRB", INHERENT },
		{ "LSLB", INHERENT },
		{ "ROLB", INHERENT },
		{ "DECB", INHERENT },
		{ "*", ILLEGAL },
		{ "INCB", INHERENT },
		{ "TSTB", INHERENT },
		{ "*", ILLEGAL },
		{ "CLRB", INHERENT },
		// 0x60 - 0x6F
		{ "NEG", INDEXED },
		{ "OIM", INMEM_INDEXED },
		{ "AIM", INMEM_INDEXED },
		{ "COM", INDEXED },
		{ "LSR", INDEXED },
		{ "EIM", INMEM_INDEXED },
		{ "ROR", INDEXED },
		{ "ASR", INDEXED },
		{ "LSL", INDEXED },
		{ "ROL", INDEXED },
		{ "DEC", INDEXED },
		{ "TIM", INMEM_INDEXED },
		{ "INC", INDEXED },
		{ "TST", INDEXED },
		{ "JMP", INDEXED },
		{ "CLR", INDEXED },
		// 0x70 - 0x7F
		{ "NEG", EXTENDED },
		{ "OIM", INMEM_EXTENDED },
		{ "AIM", INMEM_EXTENDED },
		{ "COM", EXTENDED },
		{ "LSR", EXTENDED },
		{ "EIM", INMEM_EXTENDED },
		{ "ROR", EXTENDED },
		{ "ASR", EXTENDED },
		{ "LSL", EXTENDED },
		{ "ROL", EXTENDED },
		{ "DEC", EXTENDED },
		{ "TIM", INMEM_EXTENDED },
		{ "INC", EXTENDED },
		{ "TST", EXTENDED },
		{ "JMP", EXTENDED },
		{ "CLR", EXTENDED },
		// 0x80 - 0x8F
		{ "SUBA", IMMEDIATE },
		{ "CMPA", IMMEDIATE },
		{ "SBCA", IMMEDIATE },
		{ "SUBD", WORD_IMMEDIATE },
		{ "ANDA", IMMEDIATE },
		{ "BITA", IMMEDIATE },
		{ "LDA", IMMEDIATE },
		{ "*", ILLEGAL },
		{ "EORA", IMMEDIATE },
		{ "ADCA", IMMEDIATE },
		{ "ORA", IMMEDIATE },
		{ "ADDA", IMMEDIATE },
		{ "CMPX", WORD_IMMEDIATE },
		{ "BSR", RELATIVE },
		{ "LDX", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		// 0x90 - 0x9F
		{ "SUBA", DIRECT },
		{ "CMPA", DIRECT },
		{ "SBCA", DIRECT },
		{ "SUBD", DIRECT },
		{ "ANDA", DIRECT },
		{ "BITA", DIRECT },
		{ "LDA", DIRECT },
		{ "STA", DIRECT },
		{ "EORA", DIRECT },
		{ "ADCA", DIRECT },
		{ "ORA", DIRECT },
		{ "ADDA", DIRECT },
		{ "CMPX", DIRECT },
		{ "JSR", DIRECT },
		{ "LDX", DIRECT },
		{ "STX", DIRECT },
		// 0xA0 - 0xAF
		{ "SUBA", INDEXED },
		{ "CMPA", INDEXED },
		{ "SBCA", INDEXED },
		{ "SUBD", INDEXED },
		{ "ANDA", INDEXED },
		{ "BITA", INDEXED },
		{ "LDA", INDEXED },
		{ "STA", INDEXED },
		{ "EORA", INDEXED },
		{ "ADCA", INDEXED },
		{ "ORA", INDEXED },
		{ "ADDA", INDEXED },
		{ "CMPX", INDEXED },
		{ "JSR", INDEXED },
		{ "LDX", INDEXED },
		{ "STX", INDEXED },
		// 0xB0 - 0xBF
		{ "SUBA", EXTENDED },
		{ "CMPA", EXTENDED },
		{ "SBCA", EXTENDED },
		{ "SUBD", EXTENDED },
		{ "ANDA", EXTENDED },
		{ "BITA", EXTENDED },
		{ "LDA", EXTENDED },
		{ "STA", EXTENDED },
		{ "EORA", EXTENDED },
		{ "ADCA", EXTENDED },
		{ "ORA", EXTENDED },
		{ "ADDA", EXTENDED },
		{ "CMPX", EXTENDED },
		{ "JSR", EXTENDED },
		{ "LDX", EXTENDED },
		{ "STX", EXTENDED },
		// 0xC0 - 0xCF
		{ "SUBB", IMMEDIATE },
		{ "CMPB", IMMEDIATE },
		{ "SBCB", IMMEDIATE },
		{ "ADDD", WORD_IMMEDIATE },
		{ "ANDB", IMMEDIATE },
		{ "BITB", IMMEDIATE },
		{ "LDB", IMMEDIATE },
		{ "*", ILLEGAL },
		{ "EORB", IMMEDIATE },
		{ "ADCB", IMMEDIATE },
		{ "ORB", IMMEDIATE },
		{ "ADDB", IMMEDIATE },
		{ "LDD", WORD_IMMEDIATE },
		{ "LDQ", QUAD_IMMEDIATE },
		{ "LDU", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		// 0xD0 - 0xDF
		{ "SUBB", DIRECT },
		{ "CMPB", DIRECT },
		{ "SBCB", DIRECT },
		{ "ADDD", DIRECT },
		{ "ANDB", DIRECT },
		{ "BITB", DIRECT },
		{ "LDB", DIRECT },
		{ "STB", DIRECT },
		{ "EORB", DIRECT },
		{ "ADCB", DIRECT },
		{ "ORB", DIRECT },
		{ "ADDB", DIRECT },
		{ "LDD", DIRECT },
		{ "STD", DIRECT },
		{ "LDU", DIRECT },
		{ "STU", DIRECT },
		// 0xE0 - 0xEF
		{ "SUBB", INDEXED },
		{ "CMPB", INDEXED },
		{ "SBCB", INDEXED },
		{ "ADDD", INDEXED },
		{ "ANDB", INDEXED },
		{ "BITB", INDEXED },
		{ "LDB", INDEXED },
		{ "STB", INDEXED },
		{ "EORB", INDEXED },
		{ "ADCB", INDEXED },
		{ "ORB", INDEXED },
		{ "ADDB", INDEXED },
		{ "LDD", INDEXED },
		{ "STD", INDEXED },
		{ "LDU", INDEXED },
		{ "STU", INDEXED },
		// 0xF0 - 0xFF
		{ "SUBB", EXTENDED },
		{ "CMPB", EXTENDED },
		{ "SBCB", EXTENDED },
		{ "ADDD", EXTENDED },
		{ "ANDB", EXTENDED },
		{ "BITB", EXTENDED },
		{ "LDB", EXTENDED },
		{ "STB", EXTENDED },
		{ "EORB", EXTENDED },
		{ "ADCB", EXTENDED },
		{ "ORB", EXTENDED },
		{ "ADDB", EXTENDED },
		{ "LDD", EXTENDED },
		{ "STD", EXTENDED },
		{ "LDU", EXTENDED },
		{ "STU", EXTENDED }
	}, {

		// 0x1000 - 0x100F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1010 - 0x101F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1020 - 0x102F
		{ "*", ILLEGAL },
		{ "LBRN", LONG_RELATIVE },
		{ "LBHI", LONG_RELATIVE },
		{ "LBLS", LONG_RELATIVE },
		{ "LBCC", LONG_RELATIVE },
		{ "LBCS", LONG_RELATIVE },
		{ "LBNE", LONG_RELATIVE },
		{ "LBEQ", LONG_RELATIVE },
		{ "LBVC", LONG_RELATIVE },
		{ "LBVS", LONG_RELATIVE },
		{ "LBPL", LONG_RELATIVE },
		{ "LBMI", LONG_RELATIVE },
		{ "LBGE", LONG_RELATIVE },
		{ "LBLT", LONG_RELATIVE },
		{ "LBGT", LONG_RELATIVE },
		{ "LBLE", LONG_RELATIVE },
		// 0x1030 - 0x103F
		{ "ADDR", REGISTER },
		{ "ADCR", REGISTER },
		{ "SUBR", REGISTER },
		{ "SBCR", REGISTER },
		{ "ANDR", REGISTER },
		{ "ORR", REGISTER },
		{ "EORR", REGISTER },
		{ "CMPR", REGISTER },
		{ "PSHSW", INHERENT },
		{ "PULSW", INHERENT },
		{ "PSHUW", INHERENT },
		{ "PULUW", INHERENT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "SWI2", INHERENT },
		// 0x1040 - 0x104F
		{ "NEGD", INHERENT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "COMD", INHERENT },
		{ "LSRD", INHERENT },
		{ "*", ILLEGAL },
		{ "RORD", INHERENT },
		{ "ASRD", INHERENT },
		{ "LSLD", INHERENT },
		{ "ROLD", INHERENT },
		{ "DECD", INHERENT },
		{ "*", ILLEGAL },
		{ "INCD", INHERENT },
		{ "TSTD", INHERENT },
		{ "*", ILLEGAL },
		{ "CLRD", INHERENT },
		// 0x1050 - 0x105F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "COMW", ILLEGAL },
		{ "LSRW", ILLEGAL },
		{ "*", ILLEGAL },
		{ "RORW", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "ROLW", ILLEGAL },
		{ "DECW", ILLEGAL },
		{ "*", ILLEGAL },
		{ "INCW", ILLEGAL },
		{ "TSTW", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CLRW", ILLEGAL },
		// 0x1060 - 0x106F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1070 - 0x107F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1080 - 0x108F
		{ "SUBW", WORD_IMMEDIATE },
		{ "CMPW", WORD_IMMEDIATE },
		{ "SBCD", WORD_IMMEDIATE },
		{ "CMPD", WORD_IMMEDIATE },
		{ "ANDD", WORD_IMMEDIATE },
		{ "BITD", WORD_IMMEDIATE },
		{ "LDW", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		{ "EORD", WORD_IMMEDIATE },
		{ "ADCD", WORD_IMMEDIATE },
		{ "ORD", WORD_IMMEDIATE },
		{ "ADDW", WORD_IMMEDIATE },
		{ "CMPY", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		{ "LDY", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		// 0x1090 - 0x109F
		{ "SUBW", DIRECT },
		{ "CMPW", DIRECT },
		{ "SBCD", DIRECT },
		{ "CMPD", DIRECT },
		{ "ANDD", DIRECT },
		{ "BITD", DIRECT },
		{ "LDW", DIRECT },
		{ "STW", DIRECT },
		{ "EORD", DIRECT },
		{ "ADCD", DIRECT },
		{ "ORD", DIRECT },
		{ "ADDW", DIRECT },
		{ "CMPY", DIRECT },
		{ "*", ILLEGAL },
		{ "LDY", DIRECT },
		{ "STY", DIRECT },
		// 0x10A0 - 0x10AF
		{ "SUBW", INDEXED },
		{ "CMPW", INDEXED },
		{ "SBCD", INDEXED },
		{ "CMPD", INDEXED },
		{ "ANDD", INDEXED },
		{ "BITD", INDEXED },
		{ "LDW", INDEXED },
		{ "STW", INDEXED },
		{ "EORD", INDEXED },
		{ "ADCD", INDEXED },
		{ "ORD", INDEXED },
		{ "ADDW", INDEXED },
		{ "CMPY", INDEXED },
		{ "*", ILLEGAL },
		{ "LDY", INDEXED },
		{ "STY", INDEXED },
		// 0x10B0 - 0x10BF
		{ "SUBW", EXTENDED },
		{ "CMPW", EXTENDED },
		{ "SBCD", EXTENDED },
		{ "CMPD", EXTENDED },
		{ "ANDD", EXTENDED },
		{ "BITD", EXTENDED },
		{ "LDW", EXTENDED },
		{ "STW", EXTENDED },
		{ "EORD", EXTENDED },
		{ "ADCD", EXTENDED },
		{ "ORD", EXTENDED },
		{ "ADDW", EXTENDED },
		{ "CMPY", EXTENDED },
		{ "*", ILLEGAL },
		{ "LDY", EXTENDED },
		{ "STY", EXTENDED },
		// 0x10C0 - 0x10CF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDS", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		// 0x10D0 - 0x10DF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDQ", DIRECT },
		{ "STQ", DIRECT },
		{ "LDS", DIRECT },
		{ "STS", DIRECT },
		// 0x10E0 - 0x10EF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDQ", INDEXED },
		{ "STQ", INDEXED },
		{ "LDS", INDEXED },
		{ "STS", INDEXED },
		// 0x10F0 - 0x10FF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDQ", EXTENDED },
		{ "STQ", EXTENDED },
		{ "LDS", EXTENDED },
		{ "STS", EXTENDED }
	}, {

		// 0x1100 - 0x110F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1110 - 0x111F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1120 - 0x112F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1130 - 0x113F
		{ "BAND", MEMBIT },
		{ "BIAND", MEMBIT },
		{ "BOR", MEMBIT },
		{ "BIOR", MEMBIT },
		{ "BEOR", MEMBIT },
		{ "BIEOR", MEMBIT },
		{ "LDBT", MEMBIT },
		{ "STBT", MEMBIT },
		{ "TFM", TFMPP },
		{ "TFM", TFMMM },
		{ "TFM", TFMP0 },
		{ "TFM", TFM0P },
		{ "BITMD", IMMEDIATE },
		{ "LDMD", IMMEDIATE },
		{ "*", ILLEGAL },
		{ "SWI3", INHERENT },
		// 0x1140 - 0x114F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "COME", INHERENT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "DECE", INHERENT },
		{ "*", ILLEGAL },
		{ "INCE", INHERENT },
		{ "TSTE", INHERENT },
		{ "*", ILLEGAL },
		{ "CLRE", INHERENT },
		// 0x1150 - 0x115F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "COMF", INHERENT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "DECF", INHERENT },
		{ "*", ILLEGAL },
		{ "INCF", INHERENT },
		{ "TSTF", INHERENT },
		{ "*", ILLEGAL },
		{ "CLRF", INHERENT },
		// 0x1160 - 0x116F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1170 - 0x117F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1180 - 0x118F
		{ "SUBE", IMMEDIATE },
		{ "CMPE", IMMEDIATE },
		{ "*", ILLEGAL },
		{ "CMPU", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDE", IMMEDIATE },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "ADDE", IMMEDIATE },
		{ "CMPS", WORD_IMMEDIATE },
		{ "DIVD", IMMEDIATE },
		{ "DIVQ", WORD_IMMEDIATE },
		{ "MULD", WORD_IMMEDIATE },
		// 0x1190 - 0x119F
		{ "SUBE", DIRECT },
		{ "CMPE", DIRECT },
		{ "*", ILLEGAL },
		{ "CMPU", DIRECT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDE", DIRECT },
		{ "STE", DIRECT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "ADDE", DIRECT },
		{ "CMPS", DIRECT },
		{ "DIVD", DIRECT },
		{ "DIVQ", DIRECT },
		{ "MULD", DIRECT },
		// 0x11A0 - 0x11AF
		{ "SUBE", INDEXED },
		{ "CMPE", INDEXED },
		{ "*", ILLEGAL },
		{ "CMPU", INDEXED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDE", INDEXED },
		{ "STE", INDEXED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "ADDE", INDEXED },
		{ "CMPS", INDEXED },
		{ "DIVD", INDEXED },
		{ "DIVQ", INDEXED },
		{ "MULD", INDEXED },
		// 0x11B0 - 0x11BF
		{ "SUBE", EXTENDED },
		{ "CMPE", EXTENDED },
		{ "*", ILLEGAL },
		{ "CMPU", EXTENDED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDE", EXTENDED },
		{ "STE", EXTENDED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "ADDE", EXTENDED },
		{ "CMPS", EXTENDED },
		{ "DIVD", EXTENDED },
		{ "DIVQ", EXTENDED },
		{ "MULD", EXTENDED },
		// 0x11C0 - 0x11CF
		{ "SUBF", IMMEDIATE },
		{ "CMPF", IMMEDIATE },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDF", IMMEDIATE },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "ADDF", IMMEDIATE },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x11D0 - 0x11DF
		{ "SUBF", DIRECT },
		{ "CMPF", DIRECT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDF", DIRECT },
		{ "STF", DIRECT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "ADDF", DIRECT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x11E0 - 0x11EF
		{ "SUBF", INDEXED },
		{ "CMPF", INDEXED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDF", INDEXED },
		{ "STF", INDEXED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "ADDF", INDEXED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x11F0 - 0x11FF
		{ "SUBF", EXTENDED },
		{ "CMPF", EXTENDED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDF", EXTENDED },
		{ "STF", EXTENDED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "ADDF", EXTENDED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
	}
};

/* The next byte is expected to be one of these, with special exceptions:
 * WANT_PRINT - expecting trace_print to be called
 * WANT_NOTHING - expecting a byte that is to be ignored */

enum {
	WANT_INSTRUCTION,
	WANT_IRQ_VECTOR,
	WANT_IDX_POSTBYTE,
	WANT_MEMBIT_POSTBYTE,
	WANT_VALUE,
	WANT_IM_VALUE,
	WANT_PRINT,
	WANT_NOTHING
};

/* Sequences of expected bytes */

static int const state_list_irq[] = { WANT_VALUE, WANT_PRINT };
static int const state_list_inherent[] = { WANT_PRINT };
static int const state_list_idx[] = { WANT_IDX_POSTBYTE };
static int const state_list_imm8[] = { WANT_VALUE, WANT_PRINT };
static int const state_list_imm16[] = { WANT_VALUE, WANT_VALUE, WANT_PRINT };
static int const state_list_imm32[] = { WANT_VALUE, WANT_VALUE, WANT_VALUE, WANT_VALUE, WANT_PRINT };
static int const state_list_mb[] = { WANT_MEMBIT_POSTBYTE, WANT_VALUE, WANT_PRINT };
static int const state_list_inmem_idx[] = { WANT_IM_VALUE, WANT_IDX_POSTBYTE };
static int const state_list_inmem8[] = { WANT_IM_VALUE, WANT_VALUE, WANT_PRINT };
static int const state_list_inmem16[] = { WANT_IM_VALUE, WANT_VALUE, WANT_VALUE, WANT_PRINT };

/* Indexed addressing modes */

enum {
	IDX_PI1, IDX_PI2, IDX_PD1, IDX_PD2,
	IDX_OFF0, IDX_OFFB, IDX_OFFA, IDX_OFFE,
	IDX_OFF8, IDX_OFF16, IDX_OFFF, IDX_OFFD,
	IDX_PCR8, IDX_PCR16, IDX_OFFW, IDX_EXT16,
	IDX_OFF5
};

/* Indexed mode format strings.  The leading and trailing %s account for the
 * optional brackets in indirect modes.  8-bit offsets include an extra %s to
 * indicate sign.  5-bit offsets are printed in decimal. */

static char const * const idx_fmts[17] = {
	"%s,%s+%s",
	"%s,%s++%s",
	"%s,-%s%s",
	"%s,--%s%s",
	"%s,%s%s",
	"%sB,%s%s",
	"%sA,%s%s",
	"%sE,%s%s",
	"%s%s$%02x,%s%s",
	"%s$%04x,%s%s",
	"%sF,%s%s",
	"%sD,%s%s",
	"%s%s$%02x,PCR%s",
	"%s$%04x,PCR%s",
	"%sW,%s%s",
	"%s$%04X%s",
	"%s%d,%s%s",
};

/* Indexed mode may well fetch more data after initial postbyte */

static int const * const idx_state_lists[17] = {
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_imm8,
	state_list_imm16,
	state_list_inherent,
	state_list_inherent,
	state_list_imm8,
	state_list_imm16,
	state_list_inherent,
	state_list_imm16,
	state_list_inherent,
};

/* TFM instruction format strings */

static char const * const tfm_fmts[4] = {
	"%s+,%s+",
	"%s-,%s-",
	"%s+,%s",
	"%s,%s+"
};

/* Names */

// Inter-register operation postbyte
static char const * const tfr_regs[16] = {
	"D", "X", "Y", "U", "S", "PC", "W", "V",
	"A", "B", "CC", "DP", "0", "0", "E", "F"
};

// Indexed addressing postbyte
static char const * const idx_regs[4] = { "X", "Y", "U", "S" };

// Memory with bit postbyte
static char const * const membit_regs[4] = { "CC", "A", "B", "*" };

// Interrupt vector names
static char const * const irq_names[8] = {
	"[ILLEGAL]", "[SWI3]", "[SWI2]", "[FIRQ]",
	"[IRQ]", "[SWI]", "[NMI]", "[RESET]"
};

/* Current state */

#define BYTES_BUF_SIZE 5

struct hd6309_trace {
	struct HD6309 *hcpu;

	int state;
	int page;
	uint16_t instr_pc;
	int bytes_count;
	uint8_t bytes_buf[BYTES_BUF_SIZE];

	const char *mnemonic;
	char operand_text[30];  // too large, but avoids gcc8 warnings

	int ins_type;
	const int *state_list;
	uint32_t value;
	int idx_mode;
	const char *idx_reg;
	_Bool idx_indirect;

	uint32_t im_value;
	const char *membit_reg;
	int membit_sbit;
	int membit_dbit;
	const char *tfm_fmt;
};

static void reset_state(struct hd6309_trace *tracer);
static void trace_print_short(struct hd6309_trace *tracer);

#define STACK_PRINT(t,r) do { \
		if (not_first) { strcat((t)->operand_text, "," r); } \
		else { strcat((t)->operand_text, r); not_first = 1; } \
	} while (0)

#define sex5(v) ((int)((v) & 0x0f) - (int)((v) & 0x10))
#define sex8(v) ((int8_t)(v))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct hd6309_trace *hd6309_trace_new(struct HD6309 *hcpu) {
	struct hd6309_trace *tracer = xmalloc(sizeof(*tracer));
	*tracer = (struct hd6309_trace){0};
	tracer->hcpu = hcpu;
	reset_state(tracer);
	return tracer;
}

void hd6309_trace_free(struct hd6309_trace *tracer) {
	free(tracer);
}

void hd6309_trace_reset(struct hd6309_trace *tracer) {
	reset_state(tracer);
	hd6309_trace_irq(tracer, 0xfffe);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void reset_state(struct hd6309_trace *tracer) {
	tracer->state = WANT_INSTRUCTION;
	tracer->page = PAGE0;
	tracer->bytes_count = 0;
	tracer->mnemonic = "*";
	strcpy(tracer->operand_text, "*");

	tracer->ins_type = PAGE0;
	tracer->state_list = NULL;
	tracer->idx_mode = 0;
	tracer->idx_reg = "";
	tracer->idx_indirect = 0;
	tracer->membit_reg = "";
	tracer->membit_sbit = 0;
	tracer->membit_dbit = 0;
	tracer->tfm_fmt = "";
}

/* Called for each memory read */

void hd6309_trace_byte(struct hd6309_trace *tracer, uint8_t byte, uint16_t pc) {

	// Record PC of instruction
	if (tracer->bytes_count == 0) {
		tracer->instr_pc = pc;
	}

	// Record byte if considered part of instruction
	if (tracer->bytes_count < BYTES_BUF_SIZE && tracer->state != WANT_PRINT && tracer->state != WANT_NOTHING) {
		tracer->bytes_buf[tracer->bytes_count++] = byte;
	}

	switch (tracer->state) {

		// Instruction fetch
		default:
		case WANT_INSTRUCTION:
			tracer->value = 0;
			tracer->im_value = 0;
			tracer->state_list = NULL;
			tracer->mnemonic = instructions[tracer->page][byte].mnemonic;
			tracer->ins_type = instructions[tracer->page][byte].type;
			switch (tracer->ins_type) {
				// Change page, stay in WANT_INSTRUCTION state
				case PAGE2: case PAGE3:
					tracer->page = tracer->ins_type;
					break;
				// Otherwise use an appropriate state list:
				default: case ILLEGAL: case INHERENT:
					tracer->state_list = state_list_inherent;
					break;
				case IMMEDIATE: case DIRECT: case RELATIVE:
				case STACKS: case STACKU: case REGISTER:
					tracer->state_list = state_list_imm8;
					break;
				case INDEXED:
					tracer->state_list = state_list_idx;
					break;
				case WORD_IMMEDIATE: case EXTENDED:
				case LONG_RELATIVE:
					tracer->state_list = state_list_imm16;
					break;
				case TFMPP: case TFMMM: case TFMP0: case TFM0P:
					tracer->tfm_fmt = tfm_fmts[byte & 3];
					tracer->state_list = state_list_imm8;
					break;
				case MEMBIT:
					tracer->state_list = state_list_mb;
					break;
				case QUAD_IMMEDIATE:
					tracer->state_list = state_list_imm32;
					break;
				case INMEM_DIRECT:
					tracer->state_list = state_list_inmem8;
					break;
				case INMEM_INDEXED:
					tracer->state_list = state_list_inmem_idx;
					break;
				case INMEM_EXTENDED:
					tracer->state_list = state_list_inmem16;
					break;
			}
			break;

		// First byte of an IRQ vector
		case WANT_IRQ_VECTOR:
			tracer->value = byte;
			tracer->ins_type = IRQVECTOR;
			tracer->state_list = state_list_irq;
			break;

		// Building a value byte by byte
		case WANT_VALUE:
			tracer->value = (tracer->value << 8) | byte;
			break;

		// Indexed postbyte - record relevant details
		case WANT_IDX_POSTBYTE:
			tracer->idx_reg = idx_regs[(byte>>5)&3];
			tracer->idx_indirect = byte & 0x10;
			tracer->idx_mode = byte & 0x0f;
			if ((byte & 0x80) == 0) {
				tracer->idx_indirect = 0;
				tracer->idx_mode = IDX_OFF5;
				tracer->value = byte & 0x1f;
			} else if (byte == 0x8f || byte == 0x90) {
				tracer->idx_reg = "W";
				tracer->idx_mode = IDX_OFF0;
			} else if (byte == 0xaf || byte == 0xb0) {
				tracer->idx_reg = "W";
				tracer->idx_mode = IDX_OFF16;
			} else if (byte == 0xcf || byte == 0xd0) {
				tracer->idx_reg = "W";
				tracer->idx_mode = IDX_PI2;
			} else if (byte == 0xef || byte == 0xf0) {
				tracer->idx_reg = "W";
				tracer->idx_mode = IDX_PD2;
			}
			tracer->state_list = idx_state_lists[tracer->idx_mode];
			break;

		// Postbyte for "memory with bit" instructions
		case WANT_MEMBIT_POSTBYTE:
			tracer->membit_reg = membit_regs[(byte>>6) & 3];
			tracer->membit_sbit = (byte>>3) & 7;
			tracer->membit_dbit = byte & 7;
			break;

		// Separate immediate value for "in memory" instructions
		case WANT_IM_VALUE:
			tracer->im_value = (tracer->im_value << 8) | byte;
			break;

		// Expecting CPU code to call trace_print
		case WANT_PRINT:
			tracer->state_list = NULL;
			return;

		// This byte is to be ignored (used following IRQ vector fetch)
		case WANT_NOTHING:
			break;
	}

	// Get next state from state list
	if (tracer->state_list)
		tracer->state = *(tracer->state_list++);

	if (tracer->state != WANT_PRINT)
		return;

	// If the next state is WANT_PRINT, we're done with the instruction, so
	// prep the operand text for printing.

	tracer->state_list = NULL;

	tracer->operand_text[0] = '\0';
	switch (tracer->ins_type) {
		case ILLEGAL: case INHERENT:
			break;

		case IMMEDIATE:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "#$%02x", tracer->value);
			break;

		case DIRECT:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "<$%02x", tracer->value);
			break;

		case WORD_IMMEDIATE:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "#$%04x", tracer->value);
			break;

		case EXTENDED:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "$%04x", tracer->value);
			break;

		case STACKS: {
			_Bool not_first = 0;
			if (tracer->value & 0x01) { STACK_PRINT(tracer, "CC"); }
			if (tracer->value & 0x02) { STACK_PRINT(tracer, "A"); }
			if (tracer->value & 0x04) { STACK_PRINT(tracer, "B"); }
			if (tracer->value & 0x08) { STACK_PRINT(tracer, "DP"); }
			if (tracer->value & 0x10) { STACK_PRINT(tracer, "X"); }
			if (tracer->value & 0x20) { STACK_PRINT(tracer, "Y"); }
			if (tracer->value & 0x40) { STACK_PRINT(tracer, "U"); }
			if (tracer->value & 0x80) { STACK_PRINT(tracer, "PC"); }
		} break;

		case STACKU: {
			_Bool not_first = 0;
			if (tracer->value & 0x01) { STACK_PRINT(tracer, "CC"); }
			if (tracer->value & 0x02) { STACK_PRINT(tracer, "A"); }
			if (tracer->value & 0x04) { STACK_PRINT(tracer, "B"); }
			if (tracer->value & 0x08) { STACK_PRINT(tracer, "DP"); }
			if (tracer->value & 0x10) { STACK_PRINT(tracer, "X"); }
			if (tracer->value & 0x20) { STACK_PRINT(tracer, "Y"); }
			if (tracer->value & 0x40) { STACK_PRINT(tracer, "S"); }
			if (tracer->value & 0x80) { STACK_PRINT(tracer, "PC"); }
		} break;

		case REGISTER:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "%s,%s",
					tfr_regs[(tracer->value>>4)&15],
					tfr_regs[tracer->value&15]);
			break;

		case INDEXED:
		case INMEM_INDEXED:
			{
			char tmp_text[19];
			const char *pre = tracer->idx_indirect ? "[" : "";
			const char *post = tracer->idx_indirect ? "]" : "";
			int value8 = sex8(tracer->value);
			int value5 = sex5(tracer->value);

			switch (tracer->idx_mode) {
			default:
			case IDX_PI1: case IDX_PI2: case IDX_PD1: case IDX_PD2:
			case IDX_OFF0: case IDX_OFFB: case IDX_OFFA: case IDX_OFFE:
			case IDX_OFFF: case IDX_OFFD: case IDX_OFFW:
				snprintf(tmp_text, sizeof(tmp_text), idx_fmts[tracer->idx_mode], pre, tracer->idx_reg, post);
				break;
			case IDX_OFF5:
				snprintf(tmp_text, sizeof(tmp_text), idx_fmts[tracer->idx_mode], pre, value5, tracer->idx_reg, post);
				break;
			case IDX_OFF8:
				snprintf(tmp_text, sizeof(tmp_text), idx_fmts[tracer->idx_mode], pre, (value8<0)?"-":"", (value8<0)?-value8:value8, tracer->idx_reg, post);
				break;
			case IDX_OFF16:
				snprintf(tmp_text, sizeof(tmp_text), idx_fmts[tracer->idx_mode], pre, tracer->value, tracer->idx_reg, post);
				break;
			case IDX_PCR8:
				snprintf(tmp_text, sizeof(tmp_text), idx_fmts[tracer->idx_mode], pre, (value8<0)?"-":"", (value8<0)?-value8:value8, post);
				break;
			case IDX_PCR16: case IDX_EXT16:
				snprintf(tmp_text, sizeof(tmp_text), idx_fmts[tracer->idx_mode], pre, tracer->value, post);
				break;
			}

			if (tracer->ins_type == INDEXED) {
				snprintf(tracer->operand_text, sizeof(tracer->operand_text), "%s", tmp_text);
			} else {
				snprintf(tracer->operand_text, sizeof(tracer->operand_text), "#$%02x,%s", tracer->im_value, tmp_text);
			}

			} break;

		case RELATIVE:
			pc = (pc + 1 + sex8(tracer->value)) & 0xffff;
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "$%04x", pc);
			break;

		case LONG_RELATIVE:
			pc = (pc + 1 + tracer->value) & 0xffff;
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "$%04x", pc);
			break;

		// CPU code will not call trace_print after IRQ vector fetch
		// and before the next instruction, therefore the state list
		// for IRQ vectors skips an expected dummy byte, and this
		// prints the trace line early.

		case IRQVECTOR:
			trace_print_short(tracer);
			printf("\n");
			fflush(stdout);
			break;

		case QUAD_IMMEDIATE:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "#$%08x", tracer->value);
			break;

		case INMEM_DIRECT:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "#$%02x,<$%02x", tracer->im_value, tracer->value);
			break;

		case INMEM_EXTENDED:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "#$%02x,$%04x", tracer->im_value, tracer->value);
			break;

		case TFMPP: case TFMMM: case TFMP0: case TFM0P:
			if ((tracer->value >> 4) > 4 || (tracer->value & 15) > 4)
				tracer->mnemonic = "TFM*";
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), tracer->tfm_fmt,
				 tfr_regs[(tracer->value>>4)&15],
				 tfr_regs[tracer->value&15]);
			break;

		case MEMBIT:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "%s,%d,%d,<$%02x",
					tracer->membit_reg, tracer->membit_sbit, tracer->membit_dbit, tracer->value);
			break;

		default:
			break;
	}
}

/* Called just before an IRQ vector fetch */

void hd6309_trace_irq(struct hd6309_trace *tracer, int vector) {
	reset_state(tracer);
	tracer->state = WANT_IRQ_VECTOR;
	tracer->bytes_count = 0;
	tracer->mnemonic = irq_names[(vector & 15) >> 1];
}

/* Called after each instruction */

void hd6309_trace_print(struct hd6309_trace *tracer) {
	struct HD6309 *hcpu = tracer->hcpu;
	struct MC6809 *cpu = &hcpu->mc6809;
	if (tracer->state != WANT_PRINT) return;
	trace_print_short(tracer);
	printf("cc=%02x a=%02x b=%02x e=%02x "
	       "f=%02x dp=%02x x=%04x y=%04x "
	       "u=%04x s=%04x v=%04x\n",
	       cpu->reg_cc, MC6809_REG_A(cpu), MC6809_REG_B(cpu), HD6309_REG_E(hcpu),
	       HD6309_REG_F(hcpu), cpu->reg_dp, cpu->reg_x, cpu->reg_y,
	       cpu->reg_u, cpu->reg_s, hcpu->reg_v);
	fflush(stdout);
	reset_state(tracer);
}

static void trace_print_short(struct hd6309_trace *tracer) {
	char bytes_string[(BYTES_BUF_SIZE*2)+1];
	if (tracer->bytes_count == 0) return;
	for (int i = 0; i < tracer->bytes_count; i++) {
		snprintf(bytes_string + i*2, 3, "%02x", tracer->bytes_buf[i]);
	}
	printf("%04x| %-12s%-8s%-20s", tracer->instr_pc, bytes_string, tracer->mnemonic, tracer->operand_text);
	reset_state(tracer);
}
