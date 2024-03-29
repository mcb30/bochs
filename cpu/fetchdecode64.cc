/////////////////////////////////////////////////////////////////////////
// $Id: fetchdecode64.cc,v 1.264 2010/05/18 07:28:04 sshwarts Exp $
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2001-2010  The Bochs Project
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA B 02110-1301 USA
//
/////////////////////////////////////////////////////////////////////////

#define NEED_CPU_REG_SHORTCUTS 1
#include "bochs.h"
#include "cpu.h"
#define LOG_THIS BX_CPU_THIS_PTR

#if BX_SUPPORT_X86_64

///////////////////////////
// prefix bytes
// opcode bytes
// modrm/sib
// address displacement
// immediate constant
///////////////////////////

// The table for 64-bit is slightly different from the
// table for 32-bit due to undefined opcodes, which
// were valid in 32-bit mode

#define X 0 /* undefined opcode */

static const Bit8u BxOpcodeHasModrm64[512] = {
  /*       0 1 2 3 4 5 6 7 8 9 a b c d e f          */
  /*       -------------------------------          */
  /* 00 */ 1,1,1,1,0,0,X,X,1,1,1,1,0,0,X,X,
  /* 10 */ 1,1,1,1,0,0,X,X,1,1,1,1,0,0,X,X,
  /* 20 */ 1,1,1,1,0,0,X,X,1,1,1,1,0,0,X,X,
  /* 30 */ 1,1,1,1,0,0,X,X,1,1,1,1,0,0,X,X,
  /* 40 */ X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
  /* 50 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  /* 60 */ X,X,X,1,X,X,X,X,0,1,0,1,0,0,0,0,
  /* 70 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  /* 80 */ 1,1,X,1,1,1,1,1,1,1,1,1,1,1,1,1,
  /* 90 */ 0,0,0,0,0,0,0,0,0,0,X,0,0,0,0,0,
  /* A0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  /* B0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  /* C0 */ 1,1,0,0,X,X,1,1,0,0,0,0,0,0,X,0,
  /* D0 */ 1,1,1,1,X,X,X,0,1,1,1,1,1,1,1,1,
  /* E0 */ 0,0,0,0,0,0,0,0,0,0,X,0,0,0,0,0,
  /* F0 */ X,0,X,X,0,0,1,1,0,0,0,0,0,0,1,1,
  /*       0 1 2 3 4 5 6 7 8 9 a b c d e f           */
  /*       -------------------------------           */
           1,1,1,1,X,0,0,0,0,0,X,0,X,1,0,1, /* 0F 00 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F 10 */
           1,1,1,1,X,X,X,X,1,1,1,1,1,1,1,1, /* 0F 20 */
           0,0,0,0,X,X,X,X,1,X,1,X,X,X,X,X, /* 0F 30 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F 40 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F 50 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F 60 */
           1,1,1,1,1,1,1,0,1,1,X,X,1,1,1,1, /* 0F 70 */
           0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0F 80 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F 90 */
           0,0,0,1,1,1,X,X,0,0,0,1,1,1,1,1, /* 0F A0 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F B0 */
           1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0, /* 0F C0 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F D0 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F E0 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,X  /* 0F F0 */
  /*       -------------------------------           */
  /*       0 1 2 3 4 5 6 7 8 9 a b c d e f           */
};

#undef X

/* *********** */
// LOCK PREFIX //
/* *********** */

/*
 *  The  LOCK  prefix  can be prepended only to the following instructions
 *  and  only  to  those  forms  of the instructions where the destination
 *  operand  is  a  memory operand: ADD, ADC, AND, BTC, BTR, BTS, CMPXCHG,
 *  CMPXCH8B,  DEC,  INC,  NEG, NOT, OR, SBB, SUB, XOR, XADD, and XCHG. If
 *  the  LOCK prefix is used with one of these instructions and the source
 *  operand  is a memory operand, an undefined opcode exception (#UD) will
 *  be  generated. An undefined opcode exception will also be generated if
 *  the  LOCK  prefix  is used with any instruction not in the above list.
 *  The XCHG instruction always asserts the LOCK# signal regardless of the
 *  presence or absence of the LOCK prefix.
 */

// Segment override prefixes
// -------------------------
// In 64-bit mode the CS, DS, ES, and SS segment overrides are ignored.

// decoding instructions; accessing seg reg's by index
static unsigned sreg_mod0_base32[16] = {
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_SS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS
};

static unsigned sreg_mod1or2_base32[16] = {
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_SS,
  BX_SEG_REG_SS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS,
  BX_SEG_REG_DS
};

// common fetchdecode32/64 opcode tables
#include "fetchdecode.h"

// table of all Bochs opcodes
extern struct bxIAOpcodeTable BxOpcodesTable[];

// 512*2 entries for 16bit operand size
// 512*2 entries for 32bit operand size
// 512*2 entries for 64bit operand size

static const BxOpcodeInfo_t BxOpcodeInfo64[512*3*2] = {
  // 512 (/r) + 512 (/m) entries for 16bit mode
  /* 00 /wr */ { BxArithDstRM, BX_IA_ADD_GbEbR },
  /* 00 /wm */ { BxLockable, BX_IA_ADD_EbGbM },
  /* 01 /wr */ { BxArithDstRM, BX_IA_ADD_GwEwR },
  /* 01 /wm */ { BxLockable, BX_IA_ADD_EwGwM },
  /* 02 /wr */ { 0, BX_IA_ADD_GbEbR },
  /* 02 /wm */ { 0, BX_IA_ADD_GbEbM },
  /* 03 /wr */ { 0, BX_IA_ADD_GwEwR },
  /* 03 /wm */ { 0, BX_IA_ADD_GwEwM },
  /* 04 /wr */ { BxImmediate_Ib, BX_IA_ADD_ALIb },
  /* 04 /wm */ { BxImmediate_Ib, BX_IA_ADD_ALIb },
  /* 05 /wr */ { BxImmediate_Iw, BX_IA_ADD_AXIw },
  /* 05 /wm */ { BxImmediate_Iw, BX_IA_ADD_AXIw },
  /* 06 /wr */ { 0, BX_IA_ERROR },
  /* 06 /wm */ { 0, BX_IA_ERROR },
  /* 07 /wr */ { 0, BX_IA_ERROR },
  /* 07 /wm */ { 0, BX_IA_ERROR },
  /* 08 /wr */ { BxArithDstRM, BX_IA_OR_GbEbR },
  /* 08 /wm */ { BxLockable, BX_IA_OR_EbGbM },
  /* 09 /wr */ { BxArithDstRM, BX_IA_OR_GwEwR },
  /* 09 /wm */ { BxLockable, BX_IA_OR_EwGwM },
  /* 0A /wr */ { 0, BX_IA_OR_GbEbR },
  /* 0A /wm */ { 0, BX_IA_OR_GbEbM },
  /* 0B /wr */ { 0, BX_IA_OR_GwEwR },
  /* 0B /wm */ { 0, BX_IA_OR_GwEwM },
  /* 0C /wr */ { BxImmediate_Ib, BX_IA_OR_ALIb },
  /* 0C /wm */ { BxImmediate_Ib, BX_IA_OR_ALIb },
  /* 0D /wr */ { BxImmediate_Iw, BX_IA_OR_AXIw },
  /* 0D /wm */ { BxImmediate_Iw, BX_IA_OR_AXIw },
  /* 0E /wr */ { 0, BX_IA_ERROR },
  /* 0E /wm */ { 0, BX_IA_ERROR },
  /* 0F /wr */ { 0, BX_IA_ERROR }, // 2-byte escape
  /* 0F /wm */ { 0, BX_IA_ERROR }, // 2-byte escape
  /* 10 /wr */ { BxArithDstRM, BX_IA_ADC_GbEbR },
  /* 10 /wm */ { BxLockable, BX_IA_ADC_EbGbM },
  /* 11 /wr */ { BxArithDstRM, BX_IA_ADC_GwEwR },
  /* 11 /wm */ { BxLockable, BX_IA_ADC_EwGwM },
  /* 12 /wr */ { 0, BX_IA_ADC_GbEbR },
  /* 12 /wm */ { 0, BX_IA_ADC_GbEbM },
  /* 13 /wr */ { 0, BX_IA_ADC_GwEwR },
  /* 13 /wm */ { 0, BX_IA_ADC_GwEwM },
  /* 14 /wr */ { BxImmediate_Ib, BX_IA_ADC_ALIb },
  /* 14 /wm */ { BxImmediate_Ib, BX_IA_ADC_ALIb },
  /* 15 /wr */ { BxImmediate_Iw, BX_IA_ADC_AXIw },
  /* 15 /wm */ { BxImmediate_Iw, BX_IA_ADC_AXIw },
  /* 16 /wr */ { 0, BX_IA_ERROR },
  /* 16 /wm */ { 0, BX_IA_ERROR },
  /* 17 /wr */ { 0, BX_IA_ERROR },
  /* 17 /wm */ { 0, BX_IA_ERROR },
  /* 18 /wr */ { BxArithDstRM, BX_IA_SBB_GbEbR },
  /* 18 /wm */ { BxLockable, BX_IA_SBB_EbGbM },
  /* 19 /wr */ { BxArithDstRM, BX_IA_SBB_GwEwR },
  /* 19 /wm */ { BxLockable, BX_IA_SBB_EwGwM },
  /* 1A /wr */ { 0, BX_IA_SBB_GbEbR },
  /* 1A /wm */ { 0, BX_IA_SBB_GbEbM },
  /* 1B /wr */ { 0, BX_IA_SBB_GwEwR },
  /* 1B /wm */ { 0, BX_IA_SBB_GwEwM },
  /* 1C /wr */ { BxImmediate_Ib, BX_IA_SBB_ALIb },
  /* 1C /wm */ { BxImmediate_Ib, BX_IA_SBB_ALIb },
  /* 1D /wr */ { BxImmediate_Iw, BX_IA_SBB_AXIw },
  /* 1D /wm */ { BxImmediate_Iw, BX_IA_SBB_AXIw },
  /* 1E /wr */ { 0, BX_IA_ERROR },
  /* 1E /wm */ { 0, BX_IA_ERROR },
  /* 1F /wr */ { 0, BX_IA_ERROR },
  /* 1F /wm */ { 0, BX_IA_ERROR },
  /* 20 /wr */ { BxArithDstRM, BX_IA_AND_GbEbR },
  /* 20 /wm */ { BxLockable, BX_IA_AND_EbGbM },
  /* 21 /wr */ { BxArithDstRM, BX_IA_AND_GwEwR },
  /* 21 /wm */ { BxLockable, BX_IA_AND_EwGwM },
  /* 22 /wr */ { 0, BX_IA_AND_GbEbR },
  /* 22 /wm */ { 0, BX_IA_AND_GbEbM },
  /* 23 /wr */ { 0, BX_IA_AND_GwEwR },
  /* 23 /wm */ { 0, BX_IA_AND_GwEwM },
  /* 24 /wr */ { BxImmediate_Ib, BX_IA_AND_ALIb },
  /* 24 /wm */ { BxImmediate_Ib, BX_IA_AND_ALIb },
  /* 25 /wr */ { BxImmediate_Iw, BX_IA_AND_AXIw },
  /* 25 /wm */ { BxImmediate_Iw, BX_IA_AND_AXIw },
  /* 26 /wr */ { 0, BX_IA_ERROR }, // ES:
  /* 26 /wm */ { 0, BX_IA_ERROR }, // ES:
  /* 27 /wr */ { 0, BX_IA_ERROR },
  /* 27 /wm */ { 0, BX_IA_ERROR },
  /* 28 /wr */ { BxArithDstRM, BX_IA_SUB_GbEbR },
  /* 28 /wm */ { BxLockable, BX_IA_SUB_EbGbM },
  /* 29 /wr */ { BxArithDstRM, BX_IA_SUB_GwEwR },
  /* 29 /wm */ { BxLockable, BX_IA_SUB_EwGwM },
  /* 2A /wr */ { 0, BX_IA_SUB_GbEbR },
  /* 2A /wm */ { 0, BX_IA_SUB_GbEbM },
  /* 2B /wr */ { 0, BX_IA_SUB_GwEwR },
  /* 2B /wm */ { 0, BX_IA_SUB_GwEwM },
  /* 2C /wr */ { BxImmediate_Ib, BX_IA_SUB_ALIb },
  /* 2C /wm */ { BxImmediate_Ib, BX_IA_SUB_ALIb },
  /* 2D /wr */ { BxImmediate_Iw, BX_IA_SUB_AXIw },
  /* 2D /wm */ { BxImmediate_Iw, BX_IA_SUB_AXIw },
  /* 2E /wr */ { 0, BX_IA_ERROR }, // CS:
  /* 2E /wm */ { 0, BX_IA_ERROR }, // CS:
  /* 2F /wr */ { 0, BX_IA_ERROR },
  /* 2F /wm */ { 0, BX_IA_ERROR },
  /* 30 /wr */ { BxArithDstRM, BX_IA_XOR_GbEbR },
  /* 30 /wm */ { BxLockable, BX_IA_XOR_EbGbM },
  /* 31 /wr */ { BxArithDstRM, BX_IA_XOR_GwEwR },
  /* 31 /wm */ { BxLockable, BX_IA_XOR_EwGwM },
  /* 32 /wr */ { 0, BX_IA_XOR_GbEbR },
  /* 32 /wm */ { 0, BX_IA_XOR_GbEbM },
  /* 33 /wr */ { 0, BX_IA_XOR_GwEwR },
  /* 33 /wm */ { 0, BX_IA_XOR_GwEwM },
  /* 34 /wr */ { BxImmediate_Ib, BX_IA_XOR_ALIb },
  /* 34 /wm */ { BxImmediate_Ib, BX_IA_XOR_ALIb },
  /* 35 /wr */ { BxImmediate_Iw, BX_IA_XOR_AXIw },
  /* 35 /wm */ { BxImmediate_Iw, BX_IA_XOR_AXIw },
  /* 36 /wr */ { 0, BX_IA_ERROR }, // SS:
  /* 36 /wm */ { 0, BX_IA_ERROR }, // SS:
  /* 37 /wr */ { 0, BX_IA_ERROR },
  /* 37 /wm */ { 0, BX_IA_ERROR },
  /* 38 /wr */ { BxArithDstRM, BX_IA_CMP_GbEbR },
  /* 38 /wm */ { 0, BX_IA_CMP_EbGbM },
  /* 39 /wr */ { BxArithDstRM, BX_IA_CMP_GwEwR },
  /* 39 /wm */ { 0, BX_IA_CMP_EwGwM },
  /* 3A /wr */ { 0, BX_IA_CMP_GbEbR },
  /* 3A /wm */ { 0, BX_IA_CMP_GbEbM },
  /* 3B /wr */ { 0, BX_IA_CMP_GwEwR },
  /* 3B /wm */ { 0, BX_IA_CMP_GwEwM },
  /* 3C /wr */ { BxImmediate_Ib, BX_IA_CMP_ALIb },
  /* 3C /wm */ { BxImmediate_Ib, BX_IA_CMP_ALIb },
  /* 3D /wr */ { BxImmediate_Iw, BX_IA_CMP_AXIw },
  /* 3D /wm */ { BxImmediate_Iw, BX_IA_CMP_AXIw },
  /* 3E /wr */ { 0, BX_IA_ERROR }, // DS:
  /* 3E /wm */ { 0, BX_IA_ERROR }, // DS:
  /* 3F /wr */ { 0, BX_IA_ERROR },
  /* 3F /wm */ { 0, BX_IA_ERROR },
  /* 40 /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 40 /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 41 /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 41 /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 42 /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 42 /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 43 /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 43 /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 44 /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 44 /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 45 /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 45 /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 46 /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 46 /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 47 /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 47 /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 48 /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 48 /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 49 /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 49 /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 4A /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 4A /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 4B /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 4B /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 4C /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 4C /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 4D /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 4D /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 4E /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 4E /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 4F /wr */ { 0, BX_IA_ERROR }, // REX:
  /* 4F /wm */ { 0, BX_IA_ERROR }, // REX:
  /* 50 /wr */ { 0, BX_IA_PUSH_RX },
  /* 50 /wm */ { 0, BX_IA_PUSH_RX },
  /* 51 /wr */ { 0, BX_IA_PUSH_RX },
  /* 51 /wm */ { 0, BX_IA_PUSH_RX },
  /* 52 /wr */ { 0, BX_IA_PUSH_RX },
  /* 52 /wm */ { 0, BX_IA_PUSH_RX },
  /* 53 /wr */ { 0, BX_IA_PUSH_RX },
  /* 53 /wm */ { 0, BX_IA_PUSH_RX },
  /* 54 /wr */ { 0, BX_IA_PUSH_RX },
  /* 54 /wm */ { 0, BX_IA_PUSH_RX },
  /* 55 /wr */ { 0, BX_IA_PUSH_RX },
  /* 55 /wm */ { 0, BX_IA_PUSH_RX },
  /* 56 /wr */ { 0, BX_IA_PUSH_RX },
  /* 56 /wm */ { 0, BX_IA_PUSH_RX },
  /* 57 /wr */ { 0, BX_IA_PUSH_RX },
  /* 57 /wm */ { 0, BX_IA_PUSH_RX },
  /* 58 /wr */ { 0, BX_IA_POP_RX },
  /* 58 /wm */ { 0, BX_IA_POP_RX },
  /* 59 /wr */ { 0, BX_IA_POP_RX },
  /* 59 /wm */ { 0, BX_IA_POP_RX },
  /* 5A /wr */ { 0, BX_IA_POP_RX },
  /* 5A /wm */ { 0, BX_IA_POP_RX },
  /* 5B /wr */ { 0, BX_IA_POP_RX },
  /* 5B /wm */ { 0, BX_IA_POP_RX },
  /* 5C /wr */ { 0, BX_IA_POP_RX },
  /* 5C /wm */ { 0, BX_IA_POP_RX },
  /* 5D /wr */ { 0, BX_IA_POP_RX },
  /* 5D /wm */ { 0, BX_IA_POP_RX },
  /* 5E /wr */ { 0, BX_IA_POP_RX },
  /* 5E /wm */ { 0, BX_IA_POP_RX },
  /* 5F /wr */ { 0, BX_IA_POP_RX },
  /* 5F /wm */ { 0, BX_IA_POP_RX },
  /* 60 /wr */ { 0, BX_IA_ERROR },
  /* 60 /wm */ { 0, BX_IA_ERROR },
  /* 61 /wr */ { 0, BX_IA_ERROR },
  /* 61 /wm */ { 0, BX_IA_ERROR },
  /* 62 /wr */ { 0, BX_IA_ERROR },
  /* 62 /wm */ { 0, BX_IA_ERROR },
  /* 63 /wr */ { 0, BX_IA_MOV_GwEwR }, // MOVSX_GwEw
  /* 63 /wm */ { 0, BX_IA_MOV_GwEwM }, // MOVSX_GwEw
  /* 64 /wr */ { 0, BX_IA_ERROR }, // FS:
  /* 64 /wm */ { 0, BX_IA_ERROR }, // FS:
  /* 65 /wr */ { 0, BX_IA_ERROR }, // GS:
  /* 65 /wm */ { 0, BX_IA_ERROR }, // GS:
  /* 66 /wr */ { 0, BX_IA_ERROR }, // OS:
  /* 66 /wm */ { 0, BX_IA_ERROR }, // OS:
  /* 67 /wr */ { 0, BX_IA_ERROR }, // AS:
  /* 67 /wm */ { 0, BX_IA_ERROR }, // AS:
  /* 68 /wr */ { BxImmediate_Iw, BX_IA_PUSH_Iw },
  /* 68 /wm */ { BxImmediate_Iw, BX_IA_PUSH_Iw },
  /* 69 /wr */ { BxImmediate_Iw, BX_IA_IMUL_GwEwIwR },
  /* 69 /wm */ { BxImmediate_Iw, BX_IA_IMUL_GwEwIwM },
  /* 6A /wr */ { BxImmediate_Ib_SE, BX_IA_PUSH_Iw },
  /* 6A /wm */ { BxImmediate_Ib_SE, BX_IA_PUSH_Iw },
  /* 6B /wr */ { BxImmediate_Ib_SE, BX_IA_IMUL_GwEwIwR },
  /* 6B /wm */ { BxImmediate_Ib_SE, BX_IA_IMUL_GwEwIwM },
  /* 6C /wr */ { 0, BX_IA_REP_INSB_YbDX },
  /* 6C /wm */ { 0, BX_IA_REP_INSB_YbDX },
  /* 6D /wr */ { 0, BX_IA_REP_INSW_YwDX },
  /* 6D /wm */ { 0, BX_IA_REP_INSW_YwDX },
  /* 6E /wr */ { 0, BX_IA_REP_OUTSB_DXXb },
  /* 6E /wm */ { 0, BX_IA_REP_OUTSB_DXXb },
  /* 6F /wr */ { 0, BX_IA_REP_OUTSW_DXXw },
  /* 6F /wm */ { 0, BX_IA_REP_OUTSW_DXXw },
  /* 70 /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JO_Jq },
  /* 70 /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JO_Jq },
  /* 71 /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 71 /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 72 /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JB_Jq },
  /* 72 /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JB_Jq },
  /* 73 /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 73 /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 74 /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 74 /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 75 /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 75 /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 76 /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 76 /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 77 /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 77 /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 78 /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JS_Jq },
  /* 78 /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JS_Jq },
  /* 79 /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 79 /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 7A /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JP_Jq },
  /* 7A /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JP_Jq },
  /* 7B /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 7B /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 7C /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JL_Jq },
  /* 7C /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JL_Jq },
  /* 7D /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 7D /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 7E /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 7E /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 7F /wr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 7F /wm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 80 /wr */ { BxGroup1 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG1EbIbR },
  /* 80 /wm */ { BxGroup1 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG1EbIbM },
  /* 81 /wr */ { BxGroup1 | BxImmediate_Iw, BX_IA_ERROR, BxOpcodeInfoG1EwR },
  /* 81 /wm */ { BxGroup1 | BxImmediate_Iw, BX_IA_ERROR, BxOpcodeInfoG1EwM },
  /* 82 /wr */ { 0, BX_IA_ERROR },
  /* 82 /wm */ { 0, BX_IA_ERROR },
  /* 83 /wr */ { BxGroup1 | BxImmediate_Ib_SE, BX_IA_ERROR, BxOpcodeInfoG1EwR },
  /* 83 /wm */ { BxGroup1 | BxImmediate_Ib_SE, BX_IA_ERROR, BxOpcodeInfoG1EwM },
  /* 84 /wr */ { 0, BX_IA_TEST_EbGbR },
  /* 84 /wm */ { 0, BX_IA_TEST_EbGbM },
  /* 85 /wr */ { 0, BX_IA_TEST_EwGwR },
  /* 85 /wm */ { 0, BX_IA_TEST_EwGwM },
  /* 86 /wr */ { 0, BX_IA_XCHG_EbGbR },
  /* 86 /wm */ { BxLockable, BX_IA_XCHG_EbGbM },
  /* 87 /wr */ { 0, BX_IA_XCHG_EwGwR },
  /* 87 /wm */ { BxLockable, BX_IA_XCHG_EwGwM },
  /* 88 /wr */ { BxArithDstRM, BX_IA_MOV_GbEbR },
  /* 88 /wm */ { 0, BX_IA_MOV_EbGbM },
  /* 89 /wr */ { BxArithDstRM, BX_IA_MOV_GwEwR },
  /* 89 /wm */ { 0, BX_IA_MOV_EwGwM },
  /* 8A /wr */ { 0, BX_IA_MOV_GbEbR },
  /* 8A /wm */ { 0, BX_IA_MOV_GbEbM },
  /* 8B /wr */ { 0, BX_IA_MOV_GwEwR },
  /* 8B /wm */ { 0, BX_IA_MOV_GwEwM },
  /* 8C /wr */ { 0, BX_IA_MOV_EwSwR },
  /* 8C /wm */ { 0, BX_IA_MOV_EwSwM },
  /* 8D /wr */ { 0, BX_IA_ERROR },      // LEA
  /* 8D /wm */ { 0, BX_IA_LEA_GwM },
  /* 8E /wr */ { 0, BX_IA_MOV_SwEw },
  /* 8E /wm */ { 0, BX_IA_MOV_SwEw },
  /* 8F /wr */ { BxGroup1A, BX_IA_ERROR, BxOpcodeInfoG1AEwR },
  /* 8F /wm */ { BxGroup1A, BX_IA_ERROR, BxOpcodeInfoG1AEwM },
  /* 90 /wr */ { 0, BX_IA_XCHG_RXAX }, // handles XCHG R8w, AX
  /* 90 /wm */ { 0, BX_IA_XCHG_RXAX }, // handles XCHG R8w, AX
  /* 91 /wr */ { 0, BX_IA_XCHG_RXAX },
  /* 91 /wm */ { 0, BX_IA_XCHG_RXAX },
  /* 92 /wr */ { 0, BX_IA_XCHG_RXAX },
  /* 92 /wm */ { 0, BX_IA_XCHG_RXAX },
  /* 93 /wr */ { 0, BX_IA_XCHG_RXAX },
  /* 93 /wm */ { 0, BX_IA_XCHG_RXAX },
  /* 94 /wr */ { 0, BX_IA_XCHG_RXAX },
  /* 94 /wm */ { 0, BX_IA_XCHG_RXAX },
  /* 95 /wr */ { 0, BX_IA_XCHG_RXAX },
  /* 95 /wm */ { 0, BX_IA_XCHG_RXAX },
  /* 96 /wr */ { 0, BX_IA_XCHG_RXAX },
  /* 96 /wm */ { 0, BX_IA_XCHG_RXAX },
  /* 97 /wr */ { 0, BX_IA_XCHG_RXAX },
  /* 97 /wm */ { 0, BX_IA_XCHG_RXAX },
  /* 98 /wr */ { 0, BX_IA_CBW },
  /* 98 /wm */ { 0, BX_IA_CBW },
  /* 99 /wr */ { 0, BX_IA_CWD },
  /* 99 /wm */ { 0, BX_IA_CWD },
  /* 9A /wr */ { 0, BX_IA_ERROR },
  /* 9A /wm */ { 0, BX_IA_ERROR },
  /* 9B /wr */ { 0, BX_IA_FWAIT },
  /* 9B /wm */ { 0, BX_IA_FWAIT },
  /* 9C /wr */ { 0, BX_IA_PUSHF_Fw },
  /* 9C /wm */ { 0, BX_IA_PUSHF_Fw },
  /* 9D /wr */ { 0, BX_IA_POPF_Fw },
  /* 9D /wm */ { 0, BX_IA_POPF_Fw },
  /* 9E /wr */ { 0, BX_IA_SAHF },
  /* 9E /wm */ { 0, BX_IA_SAHF },
  /* 9F /wr */ { 0, BX_IA_LAHF },
  /* 9F /wm */ { 0, BX_IA_LAHF },
  /* A0 /wr */ { BxImmediate_O, BX_IA_MOV_ALOq },
  /* A0 /wm */ { BxImmediate_O, BX_IA_MOV_ALOq },
  /* A1 /wr */ { BxImmediate_O, BX_IA_MOV_AXOq },
  /* A1 /wm */ { BxImmediate_O, BX_IA_MOV_AXOq },
  /* A2 /wr */ { BxImmediate_O, BX_IA_MOV_OqAL },
  /* A2 /wm */ { BxImmediate_O, BX_IA_MOV_OqAL },
  /* A3 /wr */ { BxImmediate_O, BX_IA_MOV_OqAX },
  /* A3 /wm */ { BxImmediate_O, BX_IA_MOV_OqAX },
  /* A4 /wr */ { 0, BX_IA_REP_MOVSB_XbYb },
  /* A4 /wm */ { 0, BX_IA_REP_MOVSB_XbYb },
  /* A5 /wr */ { 0, BX_IA_REP_MOVSW_XwYw },
  /* A5 /wm */ { 0, BX_IA_REP_MOVSW_XwYw },
  /* A6 /wr */ { 0, BX_IA_REP_CMPSB_XbYb },
  /* A6 /wm */ { 0, BX_IA_REP_CMPSB_XbYb },
  /* A7 /wr */ { 0, BX_IA_REP_CMPSW_XwYw },
  /* A7 /wm */ { 0, BX_IA_REP_CMPSW_XwYw },
  /* A8 /wr */ { BxImmediate_Ib, BX_IA_TEST_ALIb },
  /* A8 /wm */ { BxImmediate_Ib, BX_IA_TEST_ALIb },
  /* A9 /wr */ { BxImmediate_Iw, BX_IA_TEST_AXIw },
  /* A9 /wm */ { BxImmediate_Iw, BX_IA_TEST_AXIw },
  /* AA /wr */ { 0, BX_IA_REP_STOSB_YbAL },
  /* AA /wm */ { 0, BX_IA_REP_STOSB_YbAL },
  /* AB /wr */ { 0, BX_IA_REP_STOSW_YwAX },
  /* AB /wm */ { 0, BX_IA_REP_STOSW_YwAX },
  /* AC /wr */ { 0, BX_IA_REP_LODSB_ALXb },
  /* AC /wm */ { 0, BX_IA_REP_LODSB_ALXb },
  /* AD /wr */ { 0, BX_IA_REP_LODSW_AXXw },
  /* AD /wm */ { 0, BX_IA_REP_LODSW_AXXw },
  /* AE /wr */ { 0, BX_IA_REP_SCASB_ALXb },
  /* AE /wm */ { 0, BX_IA_REP_SCASB_ALXb },
  /* AF /wr */ { 0, BX_IA_REP_SCASW_AXXw },
  /* AF /wm */ { 0, BX_IA_REP_SCASW_AXXw },
  /* B0 /wr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B0 /wm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B1 /wr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B1 /wm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B2 /wr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B2 /wm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B3 /wr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B3 /wm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B4 /wr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B4 /wm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B5 /wr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B5 /wm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B6 /wr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B6 /wm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B7 /wr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B7 /wm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B8 /wr */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* B8 /wm */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* B9 /wr */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* B9 /wm */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BA /wr */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BA /wm */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BB /wr */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BB /wm */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BC /wr */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BC /wm */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BD /wr */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BD /wm */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BE /wr */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BE /wm */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BF /wr */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* BF /wm */ { BxImmediate_Iw, BX_IA_MOV_RXIw },
  /* C0 /wr */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG2EbR },
  /* C0 /wm */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG2EbM },
  /* C1 /wr */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG2EwR },
  /* C1 /wm */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG2EwM },
  /* C2 /wr */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETnear64_Iw },
  /* C2 /wm */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETnear64_Iw },
  /* C3 /wr */ { BxTraceEnd,                  BX_IA_RETnear64 },
  /* C3 /wm */ { BxTraceEnd,                  BX_IA_RETnear64 },
  /* C4 /wr */ { 0, BX_IA_ERROR },
  /* C4 /wm */ { 0, BX_IA_ERROR },
  /* C5 /wr */ { 0, BX_IA_ERROR },
  /* C5 /wm */ { 0, BX_IA_ERROR },
  /* C6 /wr */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfoG11EbR },
  /* C6 /wm */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfoG11EbM },
  /* C7 /wr */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfoG11EwR },
  /* C7 /wm */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfoG11EwM },
  /* C8 /wr */ { BxImmediate_IwIb, BX_IA_ENTER64_IwIb },
  /* C8 /wm */ { BxImmediate_IwIb, BX_IA_ENTER64_IwIb },
  /* C9 /wr */ { 0, BX_IA_LEAVE64 },
  /* C9 /wm */ { 0, BX_IA_LEAVE64 },
  /* CA /wr */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETfar16_Iw },
  /* CA /wm */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETfar16_Iw },
  /* CB /wr */ { BxTraceEnd,                  BX_IA_RETfar16 },
  /* CB /wm */ { BxTraceEnd,                  BX_IA_RETfar16 },
  /* CC /wr */ { BxTraceEnd, BX_IA_INT3 },
  /* CC /wm */ { BxTraceEnd, BX_IA_INT3 },
  /* CD /wr */ { BxImmediate_Ib | BxTraceEnd, BX_IA_INT_Ib },
  /* CD /wm */ { BxImmediate_Ib | BxTraceEnd, BX_IA_INT_Ib },
  /* CE /wr */ { 0, BX_IA_ERROR },
  /* CE /wm */ { 0, BX_IA_ERROR },
  /* CF /wr */ { BxTraceEnd, BX_IA_IRET64 },
  /* CF /wm */ { BxTraceEnd, BX_IA_IRET64 },
  /* D0 /wr */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfoG2EbR },
  /* D0 /wm */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfoG2EbM },
  /* D1 /wr */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfoG2EwR },
  /* D1 /wm */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfoG2EwM },
  /* D2 /wr */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfoG2EbR },
  /* D2 /wm */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfoG2EbM },
  /* D3 /wr */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfoG2EwR },
  /* D3 /wm */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfoG2EwM },
  /* D4 /wr */ { 0, BX_IA_ERROR },
  /* D4 /wm */ { 0, BX_IA_ERROR },
  /* D5 /wr */ { 0, BX_IA_ERROR },
  /* D5 /wm */ { 0, BX_IA_ERROR },
  /* D6 /wr */ { 0, BX_IA_ERROR },
  /* D6 /wm */ { 0, BX_IA_ERROR },
  /* D7 /wr */ { 0, BX_IA_XLAT },
  /* D7 /wm */ { 0, BX_IA_XLAT },
  /* D8 /wr */ { BxGroupN, BX_IA_ERROR, BxOpcodeInfo_FloatingPointD8 },
  /* D8 /wm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupD8 },
  /* D9 /wr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointD9 },
  /* D9 /wm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupD9 },
  /* DA /wr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDA },
  /* DA /wm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDA },
  /* DB /wr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDB },
  /* DB /wm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDB },
  /* DC /wr */ { BxGroupN, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDC },
  /* DC /wm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDC },
  /* DD /wr */ { BxGroupN, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDD },
  /* DD /wm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDD },
  /* DE /wr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDE },
  /* DE /wm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDE },
  /* DF /wr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDF },
  /* DF /wm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDF },
  /* E0 /wr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPNE64_Jb },
  /* E0 /wm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPNE64_Jb },
  /* E1 /wr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPE64_Jb },
  /* E1 /wm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPE64_Jb },
  /* E2 /wr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOP64_Jb },
  /* E2 /wm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOP64_Jb },
  /* E3 /wr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JRCXZ_Jb },
  /* E3 /wm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JRCXZ_Jb },
  /* E4 /wr */ { BxImmediate_Ib, BX_IA_IN_ALIb },
  /* E4 /wm */ { BxImmediate_Ib, BX_IA_IN_ALIb },
  /* E5 /wr */ { BxImmediate_Ib, BX_IA_IN_AXIb },
  /* E5 /wm */ { BxImmediate_Ib, BX_IA_IN_AXIb },
  /* E6 /wr */ { BxImmediate_Ib, BX_IA_OUT_IbAL },
  /* E6 /wm */ { BxImmediate_Ib, BX_IA_OUT_IbAL },
  /* E7 /wr */ { BxImmediate_Ib, BX_IA_OUT_IbAX },
  /* E7 /wm */ { BxImmediate_Ib, BX_IA_OUT_IbAX },
  /* E8 /wr */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_CALL_Jq },
  /* E8 /wm */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_CALL_Jq },
  /* E9 /wr */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_JMP_Jq },
  /* E9 /wm */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_JMP_Jq },
  /* EA /wr */ { 0, BX_IA_ERROR },
  /* EA /wm */ { 0, BX_IA_ERROR },
  /* EB /wr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JMP_Jq },
  /* EB /wm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JMP_Jq },
  /* EC /wr */ { 0, BX_IA_IN_ALDX },
  /* EC /wm */ { 0, BX_IA_IN_ALDX },
  /* ED /wr */ { 0, BX_IA_IN_AXDX },
  /* ED /wm */ { 0, BX_IA_IN_AXDX },
  /* EE /wr */ { 0, BX_IA_OUT_DXAL },
  /* EE /wm */ { 0, BX_IA_OUT_DXAL },
  /* EF /wr */ { 0, BX_IA_OUT_DXAX },
  /* EF /wm */ { 0, BX_IA_OUT_DXAX },
  /* F0 /wr */ { 0, BX_IA_ERROR }, // LOCK
  /* F0 /wm */ { 0, BX_IA_ERROR }, // LOCK
  /* F1 /wr */ { BxTraceEnd, BX_IA_INT1 },
  /* F1 /wm */ { BxTraceEnd, BX_IA_INT1 },
  /* F2 /wr */ { 0, BX_IA_ERROR }, // REPNE/REPNZ
  /* F2 /wm */ { 0, BX_IA_ERROR }, // REPNE/REPNZ
  /* F3 /wr */ { 0, BX_IA_ERROR }, // REP, REPE/REPZ
  /* F3 /wm */ { 0, BX_IA_ERROR }, // REP, REPE/REPZ
  /* F4 /wr */ { BxTraceEnd, BX_IA_HLT },
  /* F4 /wm */ { BxTraceEnd, BX_IA_HLT },
  /* F5 /wr */ { 0, BX_IA_CMC },
  /* F5 /wm */ { 0, BX_IA_CMC },
  /* F6 /wr */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfoG3EbR },
  /* F6 /wm */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfoG3EbM },
  /* F7 /wr */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfoG3EwR },
  /* F7 /wm */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfoG3EwM },
  /* F8 /wr */ { 0, BX_IA_CLC },
  /* F8 /wm */ { 0, BX_IA_CLC },
  /* F9 /wr */ { 0, BX_IA_STC },
  /* F9 /wm */ { 0, BX_IA_STC },
  /* FA /wr */ { 0, BX_IA_CLI },
  /* FA /wm */ { 0, BX_IA_CLI },
  /* FB /wr */ { 0, BX_IA_STI },
  /* FB /wm */ { 0, BX_IA_STI },
  /* FC /wr */ { 0, BX_IA_CLD },
  /* FC /wm */ { 0, BX_IA_CLD },
  /* FD /wr */ { 0, BX_IA_STD },
  /* FD /wm */ { 0, BX_IA_STD },
  /* FE /wr */ { BxGroup4, BX_IA_ERROR, BxOpcodeInfoG4R },
  /* FE /wm */ { BxGroup4, BX_IA_ERROR, BxOpcodeInfoG4M },
  /* FF /wr */ { BxGroup5, BX_IA_ERROR, BxOpcodeInfo64G5wR },
  /* FF /wm */ { BxGroup5, BX_IA_ERROR, BxOpcodeInfo64G5wM },

  /* 0F 00 /wr */ { BxGroup6, BX_IA_ERROR, BxOpcodeInfoG6 },
  /* 0F 00 /wm */ { BxGroup6, BX_IA_ERROR, BxOpcodeInfoG6 },
  /* 0F 01 /wr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfoG7R },
  /* 0F 01 /wm */ { BxGroup7, BX_IA_ERROR, BxOpcodeInfo64G7M },
  /* 0F 02 /wr */ { 0, BX_IA_LAR_GvEw },
  /* 0F 02 /wm */ { 0, BX_IA_LAR_GvEw },
  /* 0F 03 /wr */ { 0, BX_IA_LSL_GvEw },
  /* 0F 03 /wm */ { 0, BX_IA_LSL_GvEw },
  /* 0F 04 /wr */ { 0, BX_IA_ERROR },
  /* 0F 04 /wm */ { 0, BX_IA_ERROR },
  /* 0F 05 /wr */ { BxTraceEnd, BX_IA_SYSCALL },
  /* 0F 05 /wm */ { BxTraceEnd, BX_IA_SYSCALL },
  /* 0F 06 /wr */ { 0, BX_IA_CLTS },
  /* 0F 06 /wm */ { 0, BX_IA_CLTS },
  /* 0F 07 /wr */ { BxTraceEnd, BX_IA_SYSRET },
  /* 0F 07 /wm */ { BxTraceEnd, BX_IA_SYSRET },
  /* 0F 08 /wr */ { BxTraceEnd, BX_IA_INVD },
  /* 0F 08 /wm */ { BxTraceEnd, BX_IA_INVD },
  /* 0F 09 /wr */ { BxTraceEnd, BX_IA_WBINVD },
  /* 0F 09 /wm */ { BxTraceEnd, BX_IA_WBINVD },
  /* 0F 0A /wr */ { 0, BX_IA_ERROR },
  /* 0F 0A /wm */ { 0, BX_IA_ERROR },
  /* 0F 0B /wr */ { BxTraceEnd, BX_IA_UD2A },
  /* 0F 0B /wm */ { BxTraceEnd, BX_IA_UD2A },
  /* 0F 0C /wr */ { 0, BX_IA_ERROR },
  /* 0F 0C /wm */ { 0, BX_IA_ERROR },
  /* 0F 0D /wr */ { 0, BX_IA_PREFETCHW },       // 3DNow! PREFETCH on AMD, NOP on Intel
  /* 0F 0D /wm */ { 0, BX_IA_PREFETCHW },       // 3DNow! PREFETCH on AMD, NOP on Intel
  /* 0F 0E /wr */ { 0, BX_IA_FEMMS },           // 3DNow! FEMMS
  /* 0F 0E /wm */ { 0, BX_IA_FEMMS },           // 3DNow! FEMMS
#if BX_SUPPORT_3DNOW
  /* 0F 0F /wr */ { BxImmediate_Ib, BX_IA_ERROR },
  /* 0F 0F /wm */ { BxImmediate_Ib, BX_IA_ERROR },
#else
  /* 0F 0F /wr */ { 0, BX_IA_ERROR },
  /* 0F 0F /wm */ { 0, BX_IA_ERROR },
#endif
  /* 0F 10 /wr */ { BxPrefixSSE, BX_IA_MOVUPS_VpsWpsR, BxOpcodeGroupSSE_0f10R },
  /* 0F 10 /wm */ { BxPrefixSSE, BX_IA_MOVUPS_VpsWpsM, BxOpcodeGroupSSE_0f10M },
  /* 0F 11 /wr */ { BxPrefixSSE, BX_IA_MOVUPS_WpsVpsR, BxOpcodeGroupSSE_0f11R },
  /* 0F 11 /wm */ { BxPrefixSSE, BX_IA_MOVUPS_WpsVpsM, BxOpcodeGroupSSE_0f11M },
  /* 0F 12 /wr */ { BxPrefixSSE, BX_IA_MOVHLPS_VpsWpsR, BxOpcodeGroupSSE_0f12R },
  /* 0F 12 /wm */ { BxPrefixSSE, BX_IA_MOVLPS_VpsMq, BxOpcodeGroupSSE_0f12M },
  /* 0F 13 /wr */ { 0, BX_IA_ERROR },  // MOVLPS/PD SSE group
  /* 0F 13 /wm */ { BxPrefixSSE, BX_IA_MOVLPS_MqVps, BxOpcodeGroupSSE_0f13M },
  /* 0F 14 /wr */ { BxPrefixSSE, BX_IA_UNPCKLPS_VpsWdq, BxOpcodeGroupSSE_0f14 },
  /* 0F 14 /wm */ { BxPrefixSSE, BX_IA_UNPCKLPS_VpsWdq, BxOpcodeGroupSSE_0f14 },
  /* 0F 15 /wr */ { BxPrefixSSE, BX_IA_UNPCKHPS_VpsWdq, BxOpcodeGroupSSE_0f15 },
  /* 0F 15 /wm */ { BxPrefixSSE, BX_IA_UNPCKHPS_VpsWdq, BxOpcodeGroupSSE_0f15 },
  /* 0F 16 /wr */ { BxPrefixSSE, BX_IA_MOVLHPS_VpsWpsR, BxOpcodeGroupSSE_0f16R },
  /* 0F 16 /wm */ { BxPrefixSSE, BX_IA_MOVHPS_VpsMq, BxOpcodeGroupSSE_0f16M },
  /* 0F 17 /wr */ { 0, BX_IA_ERROR },  // MOHLPS/PD SSE group
  /* 0F 17 /wm */ { BxPrefixSSE, BX_IA_MOVHPS_MqVps, BxOpcodeGroupSSE_0f17M },
  /* 0F 18 /wr */ { 0, BX_IA_NOP },    // PREFETCH HINT
  /* 0F 18 /wm */ { 0, BX_IA_PREFETCH }, // opcode group G16, PREFETCH hints
  /* 0F 19 /wr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 19 /wm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1A /wr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1A /wm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1B /wr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1B /wm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1C /wr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1C /wm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1D /wr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1D /wm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1E /wr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1E /wm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1F /wr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1F /wm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 20 /wr */ { 0, BX_IA_MOV_RqCq },
  /* 0F 20 /wm */ { 0, BX_IA_MOV_RqCq },
  /* 0F 21 /wr */ { 0, BX_IA_MOV_RqDq },
  /* 0F 21 /wm */ { 0, BX_IA_MOV_RqDq },
  /* 0F 22 /wr */ { BxTraceEnd, BX_IA_MOV_CqRq },
  /* 0F 22 /wm */ { BxTraceEnd, BX_IA_MOV_CqRq },
  /* 0F 23 /wr */ { BxTraceEnd, BX_IA_MOV_DqRq },
  /* 0F 23 /wm */ { BxTraceEnd, BX_IA_MOV_DqRq },
  /* 0F 24 /wr */ { 0, BX_IA_ERROR },
  /* 0F 24 /wm */ { 0, BX_IA_ERROR },
  /* 0F 25 /wr */ { 0, BX_IA_ERROR },
  /* 0F 25 /wm */ { 0, BX_IA_ERROR },
  /* 0F 26 /wr */ { 0, BX_IA_ERROR },
  /* 0F 26 /wm */ { 0, BX_IA_ERROR },
  /* 0F 27 /wr */ { 0, BX_IA_ERROR },
  /* 0F 27 /wm */ { 0, BX_IA_ERROR },
  /* 0F 28 /wr */ { BxPrefixSSE, BX_IA_MOVAPS_VpsWpsR, BxOpcodeGroupSSE_0f28R },
  /* 0F 28 /wm */ { BxPrefixSSE, BX_IA_MOVAPS_VpsWpsM, BxOpcodeGroupSSE_0f28M },
  /* 0F 29 /wr */ { BxPrefixSSE, BX_IA_MOVAPS_WpsVpsR, BxOpcodeGroupSSE_0f29R },
  /* 0F 29 /wm */ { BxPrefixSSE, BX_IA_MOVAPS_WpsVpsM, BxOpcodeGroupSSE_0f29M },
  /* 0F 2A /wr */ { BxPrefixSSE, BX_IA_CVTPI2PS_VpsQqR, BxOpcodeGroupSSE_0f2aR },
  /* 0F 2A /wm */ { BxPrefixSSE, BX_IA_CVTPI2PS_VpsQqM, BxOpcodeGroupSSE_0f2aM },
  /* 0F 2B /wr */ { 0, BX_IA_ERROR }, // MOVNTPS/PD/SS/SD
  /* 0F 2B /wm */ { BxPrefixSSE, BX_IA_MOVNTPS_MpsVps, BxOpcodeGroupSSE_0f2bM },
  /* 0F 2C /wr */ { BxPrefixSSE, BX_IA_CVTTPS2PI_PqWps, BxOpcodeGroupSSE_0f2c },
  /* 0F 2C /wm */ { BxPrefixSSE, BX_IA_CVTTPS2PI_PqWps, BxOpcodeGroupSSE_0f2c },
  /* 0F 2D /wr */ { BxPrefixSSE, BX_IA_CVTPS2PI_PqWps, BxOpcodeGroupSSE_0f2d },
  /* 0F 2D /wm */ { BxPrefixSSE, BX_IA_CVTPS2PI_PqWps, BxOpcodeGroupSSE_0f2d },
  /* 0F 2E /wr */ { BxPrefixSSE, BX_IA_UCOMISS_VssWss, BxOpcodeGroupSSE_0f2e },
  /* 0F 2E /wm */ { BxPrefixSSE, BX_IA_UCOMISS_VssWss, BxOpcodeGroupSSE_0f2e },
  /* 0F 2F /wr */ { BxPrefixSSE, BX_IA_COMISS_VpsWps, BxOpcodeGroupSSE_0f2f },
  /* 0F 2F /wm */ { BxPrefixSSE, BX_IA_COMISS_VpsWps, BxOpcodeGroupSSE_0f2f },
  /* 0F 30 /wr */ { 0, BX_IA_WRMSR },
  /* 0F 30 /wm */ { 0, BX_IA_WRMSR },
  /* 0F 31 /wr */ { 0, BX_IA_RDTSC },
  /* 0F 31 /wm */ { 0, BX_IA_RDTSC },
  /* 0F 32 /wr */ { 0, BX_IA_RDMSR },
  /* 0F 32 /wm */ { 0, BX_IA_RDMSR },
  /* 0F 33 /wr */ { 0, BX_IA_RDPMC },
  /* 0F 33 /wm */ { 0, BX_IA_RDPMC },
  /* 0F 34 /wr */ { BxTraceEnd, BX_IA_SYSENTER },
  /* 0F 34 /wm */ { BxTraceEnd, BX_IA_SYSENTER },
  /* 0F 35 /wr */ { BxTraceEnd, BX_IA_SYSEXIT },
  /* 0F 35 /wm */ { BxTraceEnd, BX_IA_SYSEXIT },
  /* 0F 36 /wr */ { 0, BX_IA_ERROR },
  /* 0F 36 /wm */ { 0, BX_IA_ERROR },
  /* 0F 37 /wr */ { 0, BX_IA_ERROR },
  /* 0F 37 /wm */ { 0, BX_IA_ERROR },
  /* 0F 38 /wr */ { Bx3ByteOp, BX_IA_ERROR, BxOpcode3ByteTable0f38 }, // 3-byte escape
  /* 0F 38 /wm */ { Bx3ByteOp, BX_IA_ERROR, BxOpcode3ByteTable0f38 }, // 3-byte escape
  /* 0F 39 /wr */ { 0, BX_IA_ERROR },
  /* 0F 39 /wm */ { 0, BX_IA_ERROR },
  /* 0F 3A /wr */ { Bx3ByteOp | BxImmediate_Ib, BX_IA_ERROR, BxOpcode3ByteTable0f3a }, // 3-byte escape
  /* 0F 3A /wm */ { Bx3ByteOp | BxImmediate_Ib, BX_IA_ERROR, BxOpcode3ByteTable0f3a }, // 3-byte escape
  /* 0F 3B /wr */ { 0, BX_IA_ERROR },
  /* 0F 3B /wm */ { 0, BX_IA_ERROR },
  /* 0F 3C /wr */ { 0, BX_IA_ERROR },
  /* 0F 3C /wm */ { 0, BX_IA_ERROR },
  /* 0F 3D /wr */ { 0, BX_IA_ERROR },
  /* 0F 3D /wm */ { 0, BX_IA_ERROR },
  /* 0F 3E /wr */ { 0, BX_IA_ERROR },
  /* 0F 3E /wm */ { 0, BX_IA_ERROR },
  /* 0F 3F /wr */ { 0, BX_IA_ERROR },
  /* 0F 3F /wm */ { 0, BX_IA_ERROR },
  /* 0F 40 /wr */ { 0, BX_IA_CMOVO_GwEwR },
  /* 0F 40 /wm */ { 0, BX_IA_CMOVO_GwEwM },
  /* 0F 41 /wr */ { 0, BX_IA_CMOVNO_GwEwR },
  /* 0F 41 /wm */ { 0, BX_IA_CMOVNO_GwEwM },
  /* 0F 42 /wr */ { 0, BX_IA_CMOVB_GwEwR },
  /* 0F 42 /wm */ { 0, BX_IA_CMOVB_GwEwM },
  /* 0F 43 /wr */ { 0, BX_IA_CMOVNB_GwEwR },
  /* 0F 43 /wm */ { 0, BX_IA_CMOVNB_GwEwM },
  /* 0F 44 /wr */ { 0, BX_IA_CMOVZ_GwEwR },
  /* 0F 44 /wm */ { 0, BX_IA_CMOVZ_GwEwM },
  /* 0F 45 /wr */ { 0, BX_IA_CMOVNZ_GwEwR },
  /* 0F 45 /wm */ { 0, BX_IA_CMOVNZ_GwEwM },
  /* 0F 46 /wr */ { 0, BX_IA_CMOVBE_GwEwR },
  /* 0F 46 /wm */ { 0, BX_IA_CMOVBE_GwEwM },
  /* 0F 47 /wr */ { 0, BX_IA_CMOVNBE_GwEwR },
  /* 0F 47 /wm */ { 0, BX_IA_CMOVNBE_GwEwM },
  /* 0F 48 /wr */ { 0, BX_IA_CMOVS_GwEwR },
  /* 0F 48 /wm */ { 0, BX_IA_CMOVS_GwEwM },
  /* 0F 49 /wr */ { 0, BX_IA_CMOVNS_GwEwR },
  /* 0F 49 /wm */ { 0, BX_IA_CMOVNS_GwEwM },
  /* 0F 4A /wr */ { 0, BX_IA_CMOVP_GwEwR },
  /* 0F 4A /wm */ { 0, BX_IA_CMOVP_GwEwM },
  /* 0F 4B /wr */ { 0, BX_IA_CMOVNP_GwEwR },
  /* 0F 4B /wm */ { 0, BX_IA_CMOVNP_GwEwM },
  /* 0F 4C /wr */ { 0, BX_IA_CMOVL_GwEwR },
  /* 0F 4C /wm */ { 0, BX_IA_CMOVL_GwEwM },
  /* 0F 4D /wr */ { 0, BX_IA_CMOVNL_GwEwR },
  /* 0F 4D /wm */ { 0, BX_IA_CMOVNL_GwEwM },
  /* 0F 4E /wr */ { 0, BX_IA_CMOVLE_GwEwR },
  /* 0F 4E /wm */ { 0, BX_IA_CMOVLE_GwEwM },
  /* 0F 4F /wr */ { 0, BX_IA_CMOVNLE_GwEwR },
  /* 0F 4F /wm */ { 0, BX_IA_CMOVNLE_GwEwM },
  /* 0F 50 /wr */ { BxPrefixSSE, BX_IA_MOVMSKPS_GdVRps, BxOpcodeGroupSSE_0f50 },
  /* 0F 50 /wm */ { 0, BX_IA_ERROR }, // MOVMSKPS/PD
  /* 0F 51 /wr */ { BxPrefixSSE, BX_IA_SQRTPS_VpsWps, BxOpcodeGroupSSE_0f51 },
  /* 0F 51 /wm */ { BxPrefixSSE, BX_IA_SQRTPS_VpsWps, BxOpcodeGroupSSE_0f51 },
  /* 0F 52 /wr */ { BxPrefixSSE, BX_IA_RSQRTPS_VpsWps, BxOpcodeGroupSSE_0f52 },
  /* 0F 52 /wm */ { BxPrefixSSE, BX_IA_RSQRTPS_VpsWps, BxOpcodeGroupSSE_0f52 },
  /* 0F 53 /wr */ { BxPrefixSSE, BX_IA_RCPPS_VpsWps, BxOpcodeGroupSSE_0f53 },
  /* 0F 53 /wm */ { BxPrefixSSE, BX_IA_RCPPS_VpsWps, BxOpcodeGroupSSE_0f53 },
  /* 0F 54 /wr */ { BxPrefixSSE, BX_IA_ANDPS_VpsWps, BxOpcodeGroupSSE_0f54 },
  /* 0F 54 /wm */ { BxPrefixSSE, BX_IA_ANDPS_VpsWps, BxOpcodeGroupSSE_0f54 },
  /* 0F 55 /wr */ { BxPrefixSSE, BX_IA_ANDNPS_VpsWps, BxOpcodeGroupSSE_0f55 },
  /* 0F 55 /wm */ { BxPrefixSSE, BX_IA_ANDNPS_VpsWps, BxOpcodeGroupSSE_0f55 },
  /* 0F 56 /wr */ { BxPrefixSSE, BX_IA_ORPS_VpsWps, BxOpcodeGroupSSE_0f56 },
  /* 0F 56 /wm */ { BxPrefixSSE, BX_IA_ORPS_VpsWps, BxOpcodeGroupSSE_0f56 },
  /* 0F 57 /wr */ { BxPrefixSSE, BX_IA_XORPS_VpsWps, BxOpcodeGroupSSE_0f57 },
  /* 0F 57 /wm */ { BxPrefixSSE, BX_IA_XORPS_VpsWps, BxOpcodeGroupSSE_0f57 },
  /* 0F 58 /wr */ { BxPrefixSSE, BX_IA_ADDPS_VpsWps, BxOpcodeGroupSSE_0f58 },
  /* 0F 58 /wm */ { BxPrefixSSE, BX_IA_ADDPS_VpsWps, BxOpcodeGroupSSE_0f58 },
  /* 0F 59 /wr */ { BxPrefixSSE, BX_IA_MULPS_VpsWps, BxOpcodeGroupSSE_0f59 },
  /* 0F 59 /wm */ { BxPrefixSSE, BX_IA_MULPS_VpsWps, BxOpcodeGroupSSE_0f59 },
  /* 0F 5A /wr */ { BxPrefixSSE, BX_IA_CVTPS2PD_VpsWps, BxOpcodeGroupSSE_0f5a },
  /* 0F 5A /wm */ { BxPrefixSSE, BX_IA_CVTPS2PD_VpsWps, BxOpcodeGroupSSE_0f5a },
  /* 0F 5B /wr */ { BxPrefixSSE, BX_IA_CVTDQ2PS_VpsWdq, BxOpcodeGroupSSE_0f5b },
  /* 0F 5B /wm */ { BxPrefixSSE, BX_IA_CVTDQ2PS_VpsWdq, BxOpcodeGroupSSE_0f5b },
  /* 0F 5C /wr */ { BxPrefixSSE, BX_IA_SUBPS_VpsWps, BxOpcodeGroupSSE_0f5c },
  /* 0F 5C /wm */ { BxPrefixSSE, BX_IA_SUBPS_VpsWps, BxOpcodeGroupSSE_0f5c },
  /* 0F 5D /wr */ { BxPrefixSSE, BX_IA_MINPS_VpsWps, BxOpcodeGroupSSE_0f5d },
  /* 0F 5D /wm */ { BxPrefixSSE, BX_IA_MINPS_VpsWps, BxOpcodeGroupSSE_0f5d },
  /* 0F 5E /wr */ { BxPrefixSSE, BX_IA_DIVPS_VpsWps, BxOpcodeGroupSSE_0f5e },
  /* 0F 5E /wm */ { BxPrefixSSE, BX_IA_DIVPS_VpsWps, BxOpcodeGroupSSE_0f5e },
  /* 0F 5F /wr */ { BxPrefixSSE, BX_IA_MAXPS_VpsWps, BxOpcodeGroupSSE_0f5f },
  /* 0F 5F /wm */ { BxPrefixSSE, BX_IA_MAXPS_VpsWps, BxOpcodeGroupSSE_0f5f },
  /* 0F 60 /wr */ { BxPrefixSSE, BX_IA_PUNPCKLBW_PqQd, BxOpcodeGroupSSE_0f60 },
  /* 0F 60 /wm */ { BxPrefixSSE, BX_IA_PUNPCKLBW_PqQd, BxOpcodeGroupSSE_0f60 },
  /* 0F 61 /wr */ { BxPrefixSSE, BX_IA_PUNPCKLWD_PqQd, BxOpcodeGroupSSE_0f61 },
  /* 0F 61 /wm */ { BxPrefixSSE, BX_IA_PUNPCKLWD_PqQd, BxOpcodeGroupSSE_0f61 },
  /* 0F 62 /wr */ { BxPrefixSSE, BX_IA_PUNPCKLDQ_PqQd, BxOpcodeGroupSSE_0f62 },
  /* 0F 62 /wm */ { BxPrefixSSE, BX_IA_PUNPCKLDQ_PqQd, BxOpcodeGroupSSE_0f62 },
  /* 0F 63 /wr */ { BxPrefixSSE, BX_IA_PACKSSWB_PqQq, BxOpcodeGroupSSE_0f63 },
  /* 0F 63 /wm */ { BxPrefixSSE, BX_IA_PACKSSWB_PqQq, BxOpcodeGroupSSE_0f63 },
  /* 0F 64 /wr */ { BxPrefixSSE, BX_IA_PCMPGTB_PqQq, BxOpcodeGroupSSE_0f64 },
  /* 0F 64 /wm */ { BxPrefixSSE, BX_IA_PCMPGTB_PqQq, BxOpcodeGroupSSE_0f64 },
  /* 0F 65 /wr */ { BxPrefixSSE, BX_IA_PCMPGTW_PqQq, BxOpcodeGroupSSE_0f65 },
  /* 0F 65 /wm */ { BxPrefixSSE, BX_IA_PCMPGTW_PqQq, BxOpcodeGroupSSE_0f65 },
  /* 0F 66 /wr */ { BxPrefixSSE, BX_IA_PCMPGTD_PqQq, BxOpcodeGroupSSE_0f66 },
  /* 0F 66 /wm */ { BxPrefixSSE, BX_IA_PCMPGTD_PqQq, BxOpcodeGroupSSE_0f66 },
  /* 0F 67 /wr */ { BxPrefixSSE, BX_IA_PACKUSWB_PqQq, BxOpcodeGroupSSE_0f67 },
  /* 0F 67 /wm */ { BxPrefixSSE, BX_IA_PACKUSWB_PqQq, BxOpcodeGroupSSE_0f67 },
  /* 0F 68 /wr */ { BxPrefixSSE, BX_IA_PUNPCKHBW_PqQq, BxOpcodeGroupSSE_0f68 },
  /* 0F 68 /wm */ { BxPrefixSSE, BX_IA_PUNPCKHBW_PqQq, BxOpcodeGroupSSE_0f68 },
  /* 0F 69 /wr */ { BxPrefixSSE, BX_IA_PUNPCKHWD_PqQq, BxOpcodeGroupSSE_0f69 },
  /* 0F 69 /wm */ { BxPrefixSSE, BX_IA_PUNPCKHWD_PqQq, BxOpcodeGroupSSE_0f69 },
  /* 0F 6A /wr */ { BxPrefixSSE, BX_IA_PUNPCKHDQ_PqQq, BxOpcodeGroupSSE_0f6a },
  /* 0F 6A /wm */ { BxPrefixSSE, BX_IA_PUNPCKHDQ_PqQq, BxOpcodeGroupSSE_0f6a },
  /* 0F 6B /wr */ { BxPrefixSSE, BX_IA_PACKSSDW_PqQq, BxOpcodeGroupSSE_0f6b },
  /* 0F 6B /wm */ { BxPrefixSSE, BX_IA_PACKSSDW_PqQq, BxOpcodeGroupSSE_0f6b },
  /* 0F 6C /wr */ { BxPrefixSSE66, BX_IA_PUNPCKLQDQ_VdqWdq },
  /* 0F 6C /wm */ { BxPrefixSSE66, BX_IA_PUNPCKLQDQ_VdqWdq },
  /* 0F 6D /wr */ { BxPrefixSSE66, BX_IA_PUNPCKHQDQ_VdqWdq },
  /* 0F 6D /wm */ { BxPrefixSSE66, BX_IA_PUNPCKHQDQ_VdqWdq },
  /* 0F 6E /wr */ { BxPrefixSSE, BX_IA_MOVD_PqEdR, BxOpcodeGroupSSE_0f6eR },
  /* 0F 6E /wm */ { BxPrefixSSE, BX_IA_MOVD_PqEdM, BxOpcodeGroupSSE_0f6eM },
  /* 0F 6F /wr */ { BxPrefixSSE, BX_IA_MOVQ_PqQqR, BxOpcodeGroupSSE_0f6fR },
  /* 0F 6F /wm */ { BxPrefixSSE, BX_IA_MOVQ_PqQqM, BxOpcodeGroupSSE_0f6fM },
  /* 0F 70 /wr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PSHUFW_PqQqIb, BxOpcodeGroupSSE_0f70 },
  /* 0F 70 /wm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PSHUFW_PqQqIb, BxOpcodeGroupSSE_0f70 },
  /* 0F 71 /wr */ { BxGroup12, BX_IA_ERROR, BxOpcodeInfoG12R },
  /* 0F 71 /wm */ { 0, BX_IA_ERROR }, // SSE Group G12
  /* 0F 72 /wr */ { BxGroup13, BX_IA_ERROR, BxOpcodeInfoG13R },
  /* 0F 72 /wm */ { 0, BX_IA_ERROR }, // SSE Group G13
  /* 0F 73 /wr */ { BxGroup14, BX_IA_ERROR, BxOpcodeInfoG14R },
  /* 0F 73 /wm */ { 0, BX_IA_ERROR }, // SSE Group G14
  /* 0F 74 /wr */ { BxPrefixSSE, BX_IA_PCMPEQB_PqQq, BxOpcodeGroupSSE_0f74 },
  /* 0F 74 /wm */ { BxPrefixSSE, BX_IA_PCMPEQB_PqQq, BxOpcodeGroupSSE_0f74 },
  /* 0F 75 /wr */ { BxPrefixSSE, BX_IA_PCMPEQW_PqQq, BxOpcodeGroupSSE_0f75 },
  /* 0F 75 /wm */ { BxPrefixSSE, BX_IA_PCMPEQW_PqQq, BxOpcodeGroupSSE_0f75 },
  /* 0F 76 /wr */ { BxPrefixSSE, BX_IA_PCMPEQD_PqQq, BxOpcodeGroupSSE_0f76 },
  /* 0F 76 /wm */ { BxPrefixSSE, BX_IA_PCMPEQD_PqQq, BxOpcodeGroupSSE_0f76 },
  /* 0F 77 /wr */ { BxPrefixSSE, BX_IA_EMMS, BxOpcodeGroupSSE_ERR },
  /* 0F 77 /wm */ { BxPrefixSSE, BX_IA_EMMS, BxOpcodeGroupSSE_ERR },
  /* 0F 78 /wr */ { BxPrefixSSE, BX_IA_VMREAD_EqGq, BxOpcodeGroupSSE_ERR },
  /* 0F 78 /wm */ { BxPrefixSSE, BX_IA_VMREAD_EqGq, BxOpcodeGroupSSE_ERR },
  /* 0F 79 /wr */ { BxPrefixSSE, BX_IA_VMWRITE_GqEq, BxOpcodeGroupSSE_ERR },
  /* 0F 79 /wm */ { BxPrefixSSE, BX_IA_VMWRITE_GqEq, BxOpcodeGroupSSE_ERR },
  /* 0F 7A /wr */ { 0, BX_IA_ERROR },
  /* 0F 7A /wm */ { 0, BX_IA_ERROR },
  /* 0F 7B /wr */ { 0, BX_IA_ERROR },
  /* 0F 7B /wm */ { 0, BX_IA_ERROR },
  /* 0F 7C /wr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7c },
  /* 0F 7C /wm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7c },
  /* 0F 7D /wr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7d },
  /* 0F 7D /wm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7d },
  /* 0F 7E /wr */ { BxPrefixSSE, BX_IA_MOVD_EdPdR, BxOpcodeGroupSSE_0f7eR },
  /* 0F 7E /wm */ { BxPrefixSSE, BX_IA_MOVD_EdPdM, BxOpcodeGroupSSE_0f7eM },
  /* 0F 7F /wr */ { BxPrefixSSE, BX_IA_MOVQ_QqPqR, BxOpcodeGroupSSE_0f7fR },
  /* 0F 7F /wm */ { BxPrefixSSE, BX_IA_MOVQ_QqPqM, BxOpcodeGroupSSE_0f7fM },
  /* 0F 80 /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JO_Jq },
  /* 0F 80 /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JO_Jq },
  /* 0F 81 /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 0F 81 /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 0F 82 /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JB_Jq },
  /* 0F 82 /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JB_Jq },
  /* 0F 83 /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 0F 83 /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 0F 84 /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 0F 84 /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 0F 85 /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 0F 85 /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 0F 86 /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 0F 86 /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 0F 87 /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 0F 87 /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 0F 88 /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JS_Jq },
  /* 0F 88 /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JS_Jq },
  /* 0F 89 /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 0F 89 /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 0F 8A /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JP_Jq },
  /* 0F 8A /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JP_Jq },
  /* 0F 8B /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 0F 8B /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 0F 8C /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JL_Jq },
  /* 0F 8C /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JL_Jq },
  /* 0F 8D /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 0F 8D /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 0F 8E /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 0F 8E /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 0F 8F /wr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 0F 8F /wm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 0F 90 /wr */ { 0, BX_IA_SETO_EbR },
  /* 0F 90 /wm */ { 0, BX_IA_SETO_EbM },
  /* 0F 91 /wr */ { 0, BX_IA_SETNO_EbR },
  /* 0F 91 /wm */ { 0, BX_IA_SETNO_EbM },
  /* 0F 92 /wr */ { 0, BX_IA_SETB_EbR },
  /* 0F 92 /wm */ { 0, BX_IA_SETB_EbM },
  /* 0F 93 /wr */ { 0, BX_IA_SETNB_EbR },
  /* 0F 93 /wm */ { 0, BX_IA_SETNB_EbM },
  /* 0F 94 /wr */ { 0, BX_IA_SETZ_EbR },
  /* 0F 94 /wm */ { 0, BX_IA_SETZ_EbM },
  /* 0F 95 /wr */ { 0, BX_IA_SETNZ_EbR },
  /* 0F 95 /wm */ { 0, BX_IA_SETNZ_EbM },
  /* 0F 96 /wr */ { 0, BX_IA_SETBE_EbR },
  /* 0F 96 /wm */ { 0, BX_IA_SETBE_EbM },
  /* 0F 97 /wr */ { 0, BX_IA_SETNBE_EbR },
  /* 0F 97 /wm */ { 0, BX_IA_SETNBE_EbM },
  /* 0F 98 /wr */ { 0, BX_IA_SETS_EbR },
  /* 0F 98 /wm */ { 0, BX_IA_SETS_EbM },
  /* 0F 99 /wr */ { 0, BX_IA_SETNS_EbR },
  /* 0F 99 /wm */ { 0, BX_IA_SETNS_EbM },
  /* 0F 9A /wr */ { 0, BX_IA_SETP_EbR },
  /* 0F 9A /wm */ { 0, BX_IA_SETP_EbM },
  /* 0F 9B /wr */ { 0, BX_IA_SETNP_EbR },
  /* 0F 9B /wm */ { 0, BX_IA_SETNP_EbM },
  /* 0F 9C /wr */ { 0, BX_IA_SETL_EbR },
  /* 0F 9C /wm */ { 0, BX_IA_SETL_EbM },
  /* 0F 9D /wr */ { 0, BX_IA_SETNL_EbR },
  /* 0F 9D /wm */ { 0, BX_IA_SETNL_EbM },
  /* 0F 9E /wr */ { 0, BX_IA_SETLE_EbR },
  /* 0F 9E /wm */ { 0, BX_IA_SETLE_EbM },
  /* 0F 9F /wr */ { 0, BX_IA_SETNLE_EbR },
  /* 0F 9F /wm */ { 0, BX_IA_SETNLE_EbM },
  /* 0F A0 /wr */ { 0, BX_IA_PUSH16_FS },
  /* 0F A0 /wm */ { 0, BX_IA_PUSH16_FS },
  /* 0F A1 /wr */ { 0, BX_IA_POP16_FS },
  /* 0F A1 /wm */ { 0, BX_IA_POP16_FS },
  /* 0F A2 /wr */ { 0, BX_IA_CPUID },
  /* 0F A2 /wm */ { 0, BX_IA_CPUID },
  /* 0F A3 /wr */ { 0, BX_IA_BT_EwGwR },
  /* 0F A3 /wm */ { 0, BX_IA_BT_EwGwM },
  /* 0F A4 /wr */ { BxImmediate_Ib, BX_IA_SHLD_EwGwR },
  /* 0F A4 /wm */ { BxImmediate_Ib, BX_IA_SHLD_EwGwM },
  /* 0F A5 /wr */ { 0,              BX_IA_SHLD_EwGwR },
  /* 0F A5 /wm */ { 0,              BX_IA_SHLD_EwGwM },
  /* 0F A6 /wr */ { 0, BX_IA_ERROR },
  /* 0F A6 /wm */ { 0, BX_IA_ERROR },
  /* 0F A7 /wr */ { 0, BX_IA_ERROR },
  /* 0F A7 /wm */ { 0, BX_IA_ERROR },
  /* 0F A8 /wr */ { 0, BX_IA_PUSH16_GS },
  /* 0F A8 /wm */ { 0, BX_IA_PUSH16_GS },
  /* 0F A9 /wr */ { 0, BX_IA_POP16_GS },
  /* 0F A9 /wm */ { 0, BX_IA_POP16_GS },
  /* 0F AA /wr */ { BxTraceEnd, BX_IA_RSM },
  /* 0F AA /wm */ { BxTraceEnd, BX_IA_RSM },
  /* 0F AB /wr */ { 0, BX_IA_BTS_EwGwR },
  /* 0F AB /wm */ { BxLockable, BX_IA_BTS_EwGwM },
  /* 0F AC /wr */ { BxImmediate_Ib, BX_IA_SHRD_EwGwR },
  /* 0F AC /wm */ { BxImmediate_Ib, BX_IA_SHRD_EwGwM },
  /* 0F AD /wr */ { 0,              BX_IA_SHRD_EwGwR },
  /* 0F AD /wm */ { 0,              BX_IA_SHRD_EwGwM },
  /* 0F AE /wr */ { BxGroup15, BX_IA_ERROR, BxOpcodeInfoG15R },
  /* 0F AE /wm */ { BxGroup15, BX_IA_ERROR, BxOpcodeInfoG15M },
  /* 0F AF /wr */ { 0, BX_IA_IMUL_GwEwR },
  /* 0F AF /wm */ { 0, BX_IA_IMUL_GwEwM },
  /* 0F B0 /wr */ { 0, BX_IA_CMPXCHG_EbGbR },
  /* 0F B0 /wm */ { BxLockable, BX_IA_CMPXCHG_EbGbM },
  /* 0F B1 /wr */ { 0, BX_IA_CMPXCHG_EwGwR },
  /* 0F B1 /wm */ { BxLockable, BX_IA_CMPXCHG_EwGwM },
  /* 0F B2 /wr */ { 0, BX_IA_ERROR }, // LSS
  /* 0F B2 /wm */ { 0, BX_IA_LSS_GwMp },
  /* 0F B3 /wr */ { 0, BX_IA_BTR_EwGwR },
  /* 0F B3 /wm */ { BxLockable, BX_IA_BTR_EwGwM },
  /* 0F B4 /wr */ { 0, BX_IA_ERROR }, // LFS
  /* 0F B4 /wm */ { 0, BX_IA_LFS_GwMp },
  /* 0F B5 /wr */ { 0, BX_IA_ERROR }, // LGS
  /* 0F B5 /wm */ { 0, BX_IA_LGS_GwMp },
  /* 0F B6 /wr */ { 0, BX_IA_MOVZX_GwEbR },
  /* 0F B6 /wm */ { 0, BX_IA_MOVZX_GwEbM },
  /* 0F B7 /wr */ { 0, BX_IA_MOV_GwEwR }, // MOVZX_GwEw
  /* 0F B7 /wm */ { 0, BX_IA_MOV_GwEwM }, // MOVZX_GwEw
  /* 0F B8 /wr */ { BxPrefixSSEF3, BX_IA_POPCNT_GwEwR },
  /* 0F B8 /wm */ { BxPrefixSSEF3, BX_IA_POPCNT_GwEwM },
  /* 0F B9 /wr */ { BxTraceEnd, BX_IA_UD2B },
  /* 0F B9 /wm */ { BxTraceEnd, BX_IA_UD2B },
  /* 0F BA /wr */ { BxGroup8, BX_IA_ERROR, BxOpcodeInfoG8EwIbR },
  /* 0F BA /wm */ { BxGroup8, BX_IA_ERROR, BxOpcodeInfoG8EwIbM },
  /* 0F BB /wr */ { 0, BX_IA_BTC_EwGwR },
  /* 0F BB /wm */ { BxLockable, BX_IA_BTC_EwGwM },
  /* 0F BC /wr */ { 0, BX_IA_BSF_GwEwR },
  /* 0F BC /wm */ { 0, BX_IA_BSF_GwEwM },
  /* 0F BD /wr */ { 0, BX_IA_BSR_GwEwR },
  /* 0F BD /wm */ { 0, BX_IA_BSR_GwEwM },
  /* 0F BE /wr */ { 0, BX_IA_MOVSX_GwEbR },
  /* 0F BE /wm */ { 0, BX_IA_MOVSX_GwEbM },
  /* 0F BF /wr */ { 0, BX_IA_MOV_GwEwR }, // MOVSX_GwEw
  /* 0F BF /wm */ { 0, BX_IA_MOV_GwEwM }, // MOVSX_GwEw
  /* 0F C0 /wr */ { 0, BX_IA_XADD_EbGbR },
  /* 0F C0 /wm */ { BxLockable, BX_IA_XADD_EbGbM },
  /* 0F C1 /wr */ { 0, BX_IA_XADD_EwGwR },
  /* 0F C1 /wm */ { BxLockable, BX_IA_XADD_EwGwM },
  /* 0F C2 /wr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_CMPPS_VpsWpsIb, BxOpcodeGroupSSE_0fc2 },
  /* 0F C2 /wm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_CMPPS_VpsWpsIb, BxOpcodeGroupSSE_0fc2 },
  /* 0F C3 /wr */ { 0, BX_IA_ERROR }, // MOVNTI SSE group
  /* 0F C3 /wm */ { BxPrefixSSE, BX_IA_MOVNTI_MdGd, BxOpcodeGroupSSE_ERR },
  /* 0F C4 /wr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PINSRW_PqEwIb, BxOpcodeGroupSSE_0fc4 },
  /* 0F C4 /wm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PINSRW_PqEwIb, BxOpcodeGroupSSE_0fc4 },
  /* 0F C5 /wr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PEXTRW_GdPqIb, BxOpcodeGroupSSE_0fc5R },
  /* 0F C5 /wm */ { 0, BX_IA_ERROR }, // PEXTRW SSE group
  /* 0F C6 /wr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_SHUFPS_VpsWpsIb, BxOpcodeGroupSSE_0fc6 },
  /* 0F C6 /wm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_SHUFPS_VpsWpsIb, BxOpcodeGroupSSE_0fc6 },
  /* 0F C7 /wr */ { 0, BX_IA_ERROR }, // CMPXCHG8B G9 group
  /* 0F C7 /wm */ { BxGroup9, BX_IA_ERROR, BxOpcodeInfoG9M },
  /* 0F C8 /wr */ { 0, BX_IA_BSWAP_RX },
  /* 0F C8 /wm */ { 0, BX_IA_BSWAP_RX },
  /* 0F C9 /wr */ { 0, BX_IA_BSWAP_RX },
  /* 0F C9 /wm */ { 0, BX_IA_BSWAP_RX },
  /* 0F CA /wr */ { 0, BX_IA_BSWAP_RX },
  /* 0F CA /wm */ { 0, BX_IA_BSWAP_RX },
  /* 0F CB /wr */ { 0, BX_IA_BSWAP_RX },
  /* 0F CB /wm */ { 0, BX_IA_BSWAP_RX },
  /* 0F CC /wr */ { 0, BX_IA_BSWAP_RX },
  /* 0F CC /wm */ { 0, BX_IA_BSWAP_RX },
  /* 0F CD /wr */ { 0, BX_IA_BSWAP_RX },
  /* 0F CD /wm */ { 0, BX_IA_BSWAP_RX },
  /* 0F CE /wr */ { 0, BX_IA_BSWAP_RX },
  /* 0F CE /wm */ { 0, BX_IA_BSWAP_RX },
  /* 0F CF /wr */ { 0, BX_IA_BSWAP_RX },
  /* 0F CF /wm */ { 0, BX_IA_BSWAP_RX },
  /* 0F D0 /wr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fd0 },
  /* 0F D0 /wm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fd0 },
  /* 0F D1 /wr */ { BxPrefixSSE, BX_IA_PSRLW_PqQq, BxOpcodeGroupSSE_0fd1 },
  /* 0F D1 /wm */ { BxPrefixSSE, BX_IA_PSRLW_PqQq, BxOpcodeGroupSSE_0fd1 },
  /* 0F D2 /wr */ { BxPrefixSSE, BX_IA_PSRLD_PqQq, BxOpcodeGroupSSE_0fd2 },
  /* 0F D2 /wm */ { BxPrefixSSE, BX_IA_PSRLD_PqQq, BxOpcodeGroupSSE_0fd2 },
  /* 0F D3 /wr */ { BxPrefixSSE, BX_IA_PSRLQ_PqQq, BxOpcodeGroupSSE_0fd3 },
  /* 0F D3 /wm */ { BxPrefixSSE, BX_IA_PSRLQ_PqQq, BxOpcodeGroupSSE_0fd3 },
  /* 0F D4 /wr */ { BxPrefixSSE, BX_IA_PADDQ_PqQq, BxOpcodeGroupSSE_0fd4 },
  /* 0F D4 /wm */ { BxPrefixSSE, BX_IA_PADDQ_PqQq, BxOpcodeGroupSSE_0fd4 },
  /* 0F D5 /wr */ { BxPrefixSSE, BX_IA_PMULLW_PqQq, BxOpcodeGroupSSE_0fd5 },
  /* 0F D5 /wm */ { BxPrefixSSE, BX_IA_PMULLW_PqQq, BxOpcodeGroupSSE_0fd5 },
  /* 0F D6 /wr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fd6R },
  /* 0F D6 /wm */ { BxPrefixSSE66, BX_IA_MOVQ_WqVqM },
  /* 0F D7 /wr */ { BxPrefixSSE, BX_IA_PMOVMSKB_GdPRq, BxOpcodeGroupSSE_0fd7 },
  /* 0F D7 /wm */ { 0, BX_IA_ERROR }, // PMOVMSKB is reg/reg form only
  /* 0F D8 /wr */ { BxPrefixSSE, BX_IA_PSUBUSB_PqQq, BxOpcodeGroupSSE_0fd8 },
  /* 0F D8 /wm */ { BxPrefixSSE, BX_IA_PSUBUSB_PqQq, BxOpcodeGroupSSE_0fd8 },
  /* 0F D9 /wr */ { BxPrefixSSE, BX_IA_PSUBUSW_PqQq, BxOpcodeGroupSSE_0fd9 },
  /* 0F D9 /wm */ { BxPrefixSSE, BX_IA_PSUBUSW_PqQq, BxOpcodeGroupSSE_0fd9 },
  /* 0F DA /wr */ { BxPrefixSSE, BX_IA_PMINUB_PqQq, BxOpcodeGroupSSE_0fda },
  /* 0F DA /wm */ { BxPrefixSSE, BX_IA_PMINUB_PqQq, BxOpcodeGroupSSE_0fda },
  /* 0F DB /wr */ { BxPrefixSSE, BX_IA_PAND_PqQq, BxOpcodeGroupSSE_0fdb },
  /* 0F DB /wm */ { BxPrefixSSE, BX_IA_PAND_PqQq, BxOpcodeGroupSSE_0fdb },
  /* 0F DC /wr */ { BxPrefixSSE, BX_IA_PADDUSB_PqQq, BxOpcodeGroupSSE_0fdc },
  /* 0F DC /wm */ { BxPrefixSSE, BX_IA_PADDUSB_PqQq, BxOpcodeGroupSSE_0fdc },
  /* 0F DD /wr */ { BxPrefixSSE, BX_IA_PADDUSW_PqQq, BxOpcodeGroupSSE_0fdd },
  /* 0F DD /wm */ { BxPrefixSSE, BX_IA_PADDUSW_PqQq, BxOpcodeGroupSSE_0fdd },
  /* 0F DE /wr */ { BxPrefixSSE, BX_IA_PMAXUB_PqQq, BxOpcodeGroupSSE_0fde },
  /* 0F DE /wm */ { BxPrefixSSE, BX_IA_PMAXUB_PqQq, BxOpcodeGroupSSE_0fde },
  /* 0F DF /wr */ { BxPrefixSSE, BX_IA_PANDN_PqQq, BxOpcodeGroupSSE_0fdf },
  /* 0F DF /wm */ { BxPrefixSSE, BX_IA_PANDN_PqQq, BxOpcodeGroupSSE_0fdf },
  /* 0F E0 /wr */ { BxPrefixSSE, BX_IA_PAVGB_PqQq, BxOpcodeGroupSSE_0fe0 },
  /* 0F E0 /wm */ { BxPrefixSSE, BX_IA_PAVGB_PqQq, BxOpcodeGroupSSE_0fe0 },
  /* 0F E1 /wr */ { BxPrefixSSE, BX_IA_PSRAW_PqQq, BxOpcodeGroupSSE_0fe1 },
  /* 0F E1 /wm */ { BxPrefixSSE, BX_IA_PSRAW_PqQq, BxOpcodeGroupSSE_0fe1 },
  /* 0F E2 /wr */ { BxPrefixSSE, BX_IA_PSRAD_PqQq, BxOpcodeGroupSSE_0fe2 },
  /* 0F E2 /wm */ { BxPrefixSSE, BX_IA_PSRAD_PqQq, BxOpcodeGroupSSE_0fe2 },
  /* 0F E3 /wr */ { BxPrefixSSE, BX_IA_PAVGW_PqQq, BxOpcodeGroupSSE_0fe3 },
  /* 0F E3 /wm */ { BxPrefixSSE, BX_IA_PAVGW_PqQq, BxOpcodeGroupSSE_0fe3 },
  /* 0F E4 /wr */ { BxPrefixSSE, BX_IA_PMULHUW_PqQq, BxOpcodeGroupSSE_0fe4 },
  /* 0F E4 /wm */ { BxPrefixSSE, BX_IA_PMULHUW_PqQq, BxOpcodeGroupSSE_0fe4 },
  /* 0F E5 /wr */ { BxPrefixSSE, BX_IA_PMULHW_PqQq, BxOpcodeGroupSSE_0fe5 },
  /* 0F E5 /wm */ { BxPrefixSSE, BX_IA_PMULHW_PqQq, BxOpcodeGroupSSE_0fe5 },
  /* 0F E6 /wr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fe6 },
  /* 0F E6 /wm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fe6 },
  /* 0F E7 /wr */ { 0, BX_IA_ERROR }, // MOVNTQ/MOVNTDQ SSE group
  /* 0F E7 /wm */ { BxPrefixSSE, BX_IA_MOVNTQ_MqPq, BxOpcodeGroupSSE_0fe7M },
  /* 0F E8 /wr */ { BxPrefixSSE, BX_IA_PSUBSB_PqQq, BxOpcodeGroupSSE_0fe8 },
  /* 0F E8 /wm */ { BxPrefixSSE, BX_IA_PSUBSB_PqQq, BxOpcodeGroupSSE_0fe8 },
  /* 0F E9 /wr */ { BxPrefixSSE, BX_IA_PSUBSW_PqQq, BxOpcodeGroupSSE_0fe9 },
  /* 0F E9 /wm */ { BxPrefixSSE, BX_IA_PSUBSW_PqQq, BxOpcodeGroupSSE_0fe9 },
  /* 0F EA /wr */ { BxPrefixSSE, BX_IA_PMINSW_PqQq, BxOpcodeGroupSSE_0fea },
  /* 0F EA /wm */ { BxPrefixSSE, BX_IA_PMINSW_PqQq, BxOpcodeGroupSSE_0fea },
  /* 0F EB /wr */ { BxPrefixSSE, BX_IA_POR_PqQq, BxOpcodeGroupSSE_0feb },
  /* 0F EB /wm */ { BxPrefixSSE, BX_IA_POR_PqQq, BxOpcodeGroupSSE_0feb },
  /* 0F EC /wr */ { BxPrefixSSE, BX_IA_PADDSB_PqQq, BxOpcodeGroupSSE_0fec },
  /* 0F EC /wm */ { BxPrefixSSE, BX_IA_PADDSB_PqQq, BxOpcodeGroupSSE_0fec },
  /* 0F ED /wr */ { BxPrefixSSE, BX_IA_PADDSW_PqQq, BxOpcodeGroupSSE_0fed },
  /* 0F ED /wm */ { BxPrefixSSE, BX_IA_PADDSW_PqQq, BxOpcodeGroupSSE_0fed },
  /* 0F EE /wr */ { BxPrefixSSE, BX_IA_PMAXSW_PqQq, BxOpcodeGroupSSE_0fee },
  /* 0F EE /wm */ { BxPrefixSSE, BX_IA_PMAXSW_PqQq, BxOpcodeGroupSSE_0fee },
  /* 0F EF /wr */ { BxPrefixSSE, BX_IA_PXOR_PqQq, BxOpcodeGroupSSE_0fef },
  /* 0F EF /wm */ { BxPrefixSSE, BX_IA_PXOR_PqQq, BxOpcodeGroupSSE_0fef },
  /* 0F F0 /wr */ { 0, BX_IA_ERROR }, // LDDQU SSE group
  /* 0F F0 /wm */ { BxPrefixSSEF2, BX_IA_LDDQU_VdqMdq },
  /* 0F F1 /wr */ { BxPrefixSSE, BX_IA_PSLLW_PqQq, BxOpcodeGroupSSE_0ff1 },
  /* 0F F1 /wm */ { BxPrefixSSE, BX_IA_PSLLW_PqQq, BxOpcodeGroupSSE_0ff1 },
  /* 0F F2 /wr */ { BxPrefixSSE, BX_IA_PSLLD_PqQq, BxOpcodeGroupSSE_0ff2 },
  /* 0F F2 /wm */ { BxPrefixSSE, BX_IA_PSLLD_PqQq, BxOpcodeGroupSSE_0ff2 },
  /* 0F F3 /wr */ { BxPrefixSSE, BX_IA_PSLLQ_PqQq, BxOpcodeGroupSSE_0ff3 },
  /* 0F F3 /wm */ { BxPrefixSSE, BX_IA_PSLLQ_PqQq, BxOpcodeGroupSSE_0ff3 },
  /* 0F F4 /wr */ { BxPrefixSSE, BX_IA_PMULUDQ_PqQq, BxOpcodeGroupSSE_0ff4 },
  /* 0F F4 /wm */ { BxPrefixSSE, BX_IA_PMULUDQ_PqQq, BxOpcodeGroupSSE_0ff4 },
  /* 0F F5 /wr */ { BxPrefixSSE, BX_IA_PMADDWD_PqQq, BxOpcodeGroupSSE_0ff5 },
  /* 0F F5 /wm */ { BxPrefixSSE, BX_IA_PMADDWD_PqQq, BxOpcodeGroupSSE_0ff5 },
  /* 0F F6 /wr */ { BxPrefixSSE, BX_IA_PSADBW_PqQq, BxOpcodeGroupSSE_0ff6 },
  /* 0F F6 /wm */ { BxPrefixSSE, BX_IA_PSADBW_PqQq, BxOpcodeGroupSSE_0ff6 },
  /* 0F F7 /wr */ { BxPrefixSSE, BX_IA_MASKMOVQ_PqPRq, BxOpcodeGroupSSE_0ff7R },
  /* 0F F7 /wm */ { 0, BX_IA_ERROR }, // MASKMOV SSE group
  /* 0F F8 /wr */ { BxPrefixSSE, BX_IA_PSUBB_PqQq, BxOpcodeGroupSSE_0ff8 },
  /* 0F F8 /wm */ { BxPrefixSSE, BX_IA_PSUBB_PqQq, BxOpcodeGroupSSE_0ff8 },
  /* 0F F9 /wr */ { BxPrefixSSE, BX_IA_PSUBW_PqQq, BxOpcodeGroupSSE_0ff9 },
  /* 0F F9 /wm */ { BxPrefixSSE, BX_IA_PSUBW_PqQq, BxOpcodeGroupSSE_0ff9 },
  /* 0F FA /wr */ { BxPrefixSSE, BX_IA_PSUBD_PqQq, BxOpcodeGroupSSE_0ffa },
  /* 0F FA /wm */ { BxPrefixSSE, BX_IA_PSUBD_PqQq, BxOpcodeGroupSSE_0ffa },
  /* 0F FB /wr */ { BxPrefixSSE, BX_IA_PSUBQ_PqQq, BxOpcodeGroupSSE_0ffb },
  /* 0F FB /wm */ { BxPrefixSSE, BX_IA_PSUBQ_PqQq, BxOpcodeGroupSSE_0ffb },
  /* 0F FC /wr */ { BxPrefixSSE, BX_IA_PADDB_PqQq, BxOpcodeGroupSSE_0ffc },
  /* 0F FC /wm */ { BxPrefixSSE, BX_IA_PADDB_PqQq, BxOpcodeGroupSSE_0ffc },
  /* 0F FD /wr */ { BxPrefixSSE, BX_IA_PADDW_PqQq, BxOpcodeGroupSSE_0ffd },
  /* 0F FD /wm */ { BxPrefixSSE, BX_IA_PADDW_PqQq, BxOpcodeGroupSSE_0ffd },
  /* 0F FE /wr */ { BxPrefixSSE, BX_IA_PADDD_PqQq, BxOpcodeGroupSSE_0ffe },
  /* 0F FE /wm */ { BxPrefixSSE, BX_IA_PADDD_PqQq, BxOpcodeGroupSSE_0ffe },
  /* 0F FF /wr */ { 0, BX_IA_ERROR },
  /* 0F FF /wm */ { 0, BX_IA_ERROR },

  // 512 (/r) + 512 (/m) entries for 32bit mode
  /* 00 /dr */ { BxArithDstRM, BX_IA_ADD_GbEbR },
  /* 00 /dm */ { BxLockable, BX_IA_ADD_EbGbM },
  /* 01 /dr */ { BxArithDstRM, BX_IA_ADD_GdEdR },
  /* 01 /dm */ { BxLockable, BX_IA_ADD_EdGdM },
  /* 02 /dr */ { 0, BX_IA_ADD_GbEbR },
  /* 02 /dm */ { 0, BX_IA_ADD_GbEbM },
  /* 03 /dr */ { 0, BX_IA_ADD_GdEdR },
  /* 03 /dm */ { 0, BX_IA_ADD_GdEdM },
  /* 04 /dr */ { BxImmediate_Ib, BX_IA_ADD_ALIb },
  /* 04 /dm */ { BxImmediate_Ib, BX_IA_ADD_ALIb },
  /* 05 /dr */ { BxImmediate_Id, BX_IA_ADD_EAXId },
  /* 05 /dm */ { BxImmediate_Id, BX_IA_ADD_EAXId },
  /* 06 /dr */ { 0, BX_IA_ERROR },
  /* 06 /dm */ { 0, BX_IA_ERROR },
  /* 07 /dr */ { 0, BX_IA_ERROR },
  /* 07 /dm */ { 0, BX_IA_ERROR },
  /* 08 /dr */ { BxArithDstRM, BX_IA_OR_GbEbR },
  /* 08 /dm */ { BxLockable, BX_IA_OR_EbGbM },
  /* 09 /dr */ { BxArithDstRM, BX_IA_OR_GdEdR },
  /* 09 /dm */ { BxLockable, BX_IA_OR_EdGdM },
  /* 0A /dr */ { 0, BX_IA_OR_GbEbR },
  /* 0A /dm */ { 0, BX_IA_OR_GbEbM },
  /* 0B /dr */ { 0, BX_IA_OR_GdEdR },
  /* 0B /dm */ { 0, BX_IA_OR_GdEdM },
  /* 0C /dr */ { BxImmediate_Ib, BX_IA_OR_ALIb },
  /* 0C /dm */ { BxImmediate_Ib, BX_IA_OR_ALIb },
  /* 0D /dr */ { BxImmediate_Id, BX_IA_OR_EAXId },
  /* 0D /dm */ { BxImmediate_Id, BX_IA_OR_EAXId },
  /* 0E /dr */ { 0, BX_IA_ERROR },
  /* 0E /dm */ { 0, BX_IA_ERROR },
  /* 0F /dr */ { 0, BX_IA_ERROR }, // 2-byte escape
  /* 0F /dm */ { 0, BX_IA_ERROR }, // 2-byte escape
  /* 10 /dr */ { BxArithDstRM, BX_IA_ADC_GbEbR },
  /* 10 /dm */ { BxLockable, BX_IA_ADC_EbGbM },
  /* 11 /dr */ { BxArithDstRM, BX_IA_ADC_GdEdR },
  /* 11 /dm */ { BxLockable, BX_IA_ADC_EdGdM },
  /* 12 /dr */ { 0, BX_IA_ADC_GbEbR },
  /* 12 /dm */ { 0, BX_IA_ADC_GbEbM },
  /* 13 /dr */ { 0, BX_IA_ADC_GdEdR },
  /* 13 /dm */ { 0, BX_IA_ADC_GdEdM },
  /* 14 /dr */ { BxImmediate_Ib, BX_IA_ADC_ALIb },
  /* 14 /dm */ { BxImmediate_Ib, BX_IA_ADC_ALIb },
  /* 15 /dr */ { BxImmediate_Id, BX_IA_ADC_EAXId },
  /* 15 /dm */ { BxImmediate_Id, BX_IA_ADC_EAXId },
  /* 16 /dr */ { 0, BX_IA_ERROR },
  /* 16 /dm */ { 0, BX_IA_ERROR },
  /* 17 /dr */ { 0, BX_IA_ERROR },
  /* 17 /dm */ { 0, BX_IA_ERROR },
  /* 18 /dr */ { BxArithDstRM, BX_IA_SBB_GbEbR },
  /* 18 /dm */ { BxLockable, BX_IA_SBB_EbGbM },
  /* 19 /dr */ { BxArithDstRM, BX_IA_SBB_GdEdR },
  /* 19 /dm */ { BxLockable, BX_IA_SBB_EdGdM },
  /* 1A /dr */ { 0, BX_IA_SBB_GbEbR },
  /* 1A /dm */ { 0, BX_IA_SBB_GbEbM },
  /* 1B /dr */ { 0, BX_IA_SBB_GdEdR },
  /* 1B /dm */ { 0, BX_IA_SBB_GdEdM },
  /* 1C /dr */ { BxImmediate_Ib, BX_IA_SBB_ALIb },
  /* 1C /dm */ { BxImmediate_Ib, BX_IA_SBB_ALIb },
  /* 1D /dr */ { BxImmediate_Id, BX_IA_SBB_EAXId },
  /* 1D /dm */ { BxImmediate_Id, BX_IA_SBB_EAXId },
  /* 1E /dr */ { 0, BX_IA_ERROR },
  /* 1E /dm */ { 0, BX_IA_ERROR },
  /* 1F /dr */ { 0, BX_IA_ERROR },
  /* 1F /dm */ { 0, BX_IA_ERROR },
  /* 20 /dr */ { BxArithDstRM, BX_IA_AND_GbEbR },
  /* 20 /dm */ { BxLockable, BX_IA_AND_EbGbM },
  /* 21 /dr */ { BxArithDstRM, BX_IA_AND_GdEdR },
  /* 21 /dm */ { BxLockable, BX_IA_AND_EdGdM },
  /* 22 /dr */ { 0, BX_IA_AND_GbEbR },
  /* 22 /dm */ { 0, BX_IA_AND_GbEbM },
  /* 23 /dr */ { 0, BX_IA_AND_GdEdR },
  /* 23 /dm */ { 0, BX_IA_AND_GdEdM },
  /* 24 /dr */ { BxImmediate_Ib, BX_IA_AND_ALIb },
  /* 24 /dm */ { BxImmediate_Ib, BX_IA_AND_ALIb },
  /* 25 /dr */ { BxImmediate_Id, BX_IA_AND_EAXId },
  /* 25 /dm */ { BxImmediate_Id, BX_IA_AND_EAXId },
  /* 26 /dr */ { 0, BX_IA_ERROR }, // ES:
  /* 26 /dm */ { 0, BX_IA_ERROR }, // ES:
  /* 27 /dr */ { 0, BX_IA_ERROR },
  /* 27 /dm */ { 0, BX_IA_ERROR },
  /* 28 /dr */ { BxArithDstRM, BX_IA_SUB_GbEbR },
  /* 28 /dm */ { BxLockable, BX_IA_SUB_EbGbM },
  /* 29 /dr */ { BxArithDstRM, BX_IA_SUB_GdEdR },
  /* 29 /dm */ { BxLockable, BX_IA_SUB_EdGdM },
  /* 2A /dr */ { 0, BX_IA_SUB_GbEbR },
  /* 2A /dm */ { 0, BX_IA_SUB_GbEbM },
  /* 2B /dr */ { 0, BX_IA_SUB_GdEdR },
  /* 2B /dm */ { 0, BX_IA_SUB_GdEdM },
  /* 2C /dr */ { BxImmediate_Ib, BX_IA_SUB_ALIb },
  /* 2C /dm */ { BxImmediate_Ib, BX_IA_SUB_ALIb },
  /* 2D /dr */ { BxImmediate_Id, BX_IA_SUB_EAXId },
  /* 2D /dm */ { BxImmediate_Id, BX_IA_SUB_EAXId },
  /* 2E /dr */ { 0, BX_IA_ERROR }, // CS:
  /* 2E /dm */ { 0, BX_IA_ERROR }, // CS:
  /* 2F /dr */ { 0, BX_IA_ERROR },
  /* 2F /dm */ { 0, BX_IA_ERROR },
  /* 30 /dr */ { BxArithDstRM, BX_IA_XOR_GbEbR },
  /* 30 /dm */ { BxLockable, BX_IA_XOR_EbGbM },
  /* 31 /dr */ { BxArithDstRM, BX_IA_XOR_GdEdR },
  /* 31 /dm */ { BxLockable, BX_IA_XOR_EdGdM },
  /* 32 /dr */ { 0, BX_IA_XOR_GbEbR },
  /* 32 /dm */ { 0, BX_IA_XOR_GbEbM },
  /* 33 /dr */ { 0, BX_IA_XOR_GdEdR },
  /* 33 /dm */ { 0, BX_IA_XOR_GdEdM },
  /* 34 /dr */ { BxImmediate_Ib, BX_IA_XOR_ALIb },
  /* 34 /dm */ { BxImmediate_Ib, BX_IA_XOR_ALIb },
  /* 35 /dr */ { BxImmediate_Id, BX_IA_XOR_EAXId },
  /* 35 /dm */ { BxImmediate_Id, BX_IA_XOR_EAXId },
  /* 36 /dr */ { 0, BX_IA_ERROR }, // SS:
  /* 36 /dm */ { 0, BX_IA_ERROR }, // SS:
  /* 37 /dr */ { 0, BX_IA_ERROR },
  /* 37 /dm */ { 0, BX_IA_ERROR },
  /* 38 /dr */ { BxArithDstRM, BX_IA_CMP_GbEbR },
  /* 38 /dm */ { 0, BX_IA_CMP_EbGbM },
  /* 39 /dr */ { BxArithDstRM, BX_IA_CMP_GdEdR },
  /* 39 /dm */ { 0, BX_IA_CMP_EdGdM },
  /* 3A /dr */ { 0, BX_IA_CMP_GbEbR },
  /* 3A /dm */ { 0, BX_IA_CMP_GbEbM },
  /* 3B /dr */ { 0, BX_IA_CMP_GdEdR },
  /* 3B /dm */ { 0, BX_IA_CMP_GdEdM },
  /* 3C /dr */ { BxImmediate_Ib, BX_IA_CMP_ALIb },
  /* 3C /dm */ { BxImmediate_Ib, BX_IA_CMP_ALIb },
  /* 3D /dr */ { BxImmediate_Id, BX_IA_CMP_EAXId },
  /* 3D /dm */ { BxImmediate_Id, BX_IA_CMP_EAXId },
  /* 3E /dr */ { 0, BX_IA_ERROR }, // DS:
  /* 3E /dm */ { 0, BX_IA_ERROR }, // DS:
  /* 3F /dr */ { 0, BX_IA_ERROR },
  /* 3F /dm */ { 0, BX_IA_ERROR },
  /* 40 /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 40 /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 41 /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 41 /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 42 /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 42 /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 43 /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 43 /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 44 /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 44 /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 45 /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 45 /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 46 /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 46 /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 47 /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 47 /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 48 /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 48 /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 49 /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 49 /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 4A /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 4A /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 4B /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 4B /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 4C /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 4C /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 4D /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 4D /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 4E /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 4E /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 4F /dr */ { 0, BX_IA_ERROR }, // REX:
  /* 4F /dm */ { 0, BX_IA_ERROR }, // REX:
  /* 50 /dr */ { 0, BX_IA_PUSH_RRX },
  /* 50 /dm */ { 0, BX_IA_PUSH_RRX },
  /* 51 /dr */ { 0, BX_IA_PUSH_RRX },
  /* 51 /dm */ { 0, BX_IA_PUSH_RRX },
  /* 52 /dr */ { 0, BX_IA_PUSH_RRX },
  /* 52 /dm */ { 0, BX_IA_PUSH_RRX },
  /* 53 /dr */ { 0, BX_IA_PUSH_RRX },
  /* 53 /dm */ { 0, BX_IA_PUSH_RRX },
  /* 54 /dr */ { 0, BX_IA_PUSH_RRX },
  /* 54 /dm */ { 0, BX_IA_PUSH_RRX },
  /* 55 /dr */ { 0, BX_IA_PUSH_RRX },
  /* 55 /dm */ { 0, BX_IA_PUSH_RRX },
  /* 56 /dr */ { 0, BX_IA_PUSH_RRX },
  /* 56 /dm */ { 0, BX_IA_PUSH_RRX },
  /* 57 /dr */ { 0, BX_IA_PUSH_RRX },
  /* 57 /dm */ { 0, BX_IA_PUSH_RRX },
  /* 58 /dr */ { 0, BX_IA_POP_RRX },
  /* 58 /dm */ { 0, BX_IA_POP_RRX },
  /* 59 /dr */ { 0, BX_IA_POP_RRX },
  /* 59 /dm */ { 0, BX_IA_POP_RRX },
  /* 5A /dr */ { 0, BX_IA_POP_RRX },
  /* 5A /dm */ { 0, BX_IA_POP_RRX },
  /* 5B /dr */ { 0, BX_IA_POP_RRX },
  /* 5B /dm */ { 0, BX_IA_POP_RRX },
  /* 5C /dr */ { 0, BX_IA_POP_RRX },
  /* 5C /dm */ { 0, BX_IA_POP_RRX },
  /* 5D /dr */ { 0, BX_IA_POP_RRX },
  /* 5D /dm */ { 0, BX_IA_POP_RRX },
  /* 5E /dr */ { 0, BX_IA_POP_RRX },
  /* 5E /dm */ { 0, BX_IA_POP_RRX },
  /* 5F /dr */ { 0, BX_IA_POP_RRX },
  /* 5F /dm */ { 0, BX_IA_POP_RRX },
  /* 60 /dr */ { 0, BX_IA_ERROR },
  /* 60 /dm */ { 0, BX_IA_ERROR },
  /* 61 /dr */ { 0, BX_IA_ERROR },
  /* 61 /dm */ { 0, BX_IA_ERROR },
  /* 62 /dr */ { 0, BX_IA_ERROR },
  /* 62 /dm */ { 0, BX_IA_ERROR },
  /* 63 /dr */ { 0, BX_IA_MOV_GdEdR }, // MOVSX_GdEd
  /* 63 /dm */ { 0, BX_IA_MOV64_GdEdM }, // MOVSX_GdEd
  /* 64 /dr */ { 0, BX_IA_ERROR }, // FS:
  /* 64 /dm */ { 0, BX_IA_ERROR }, // FS:
  /* 65 /dr */ { 0, BX_IA_ERROR }, // GS:
  /* 65 /dm */ { 0, BX_IA_ERROR }, // GS:
  /* 66 /dr */ { 0, BX_IA_ERROR }, // OS:
  /* 66 /dm */ { 0, BX_IA_ERROR }, // OS:
  /* 67 /dr */ { 0, BX_IA_ERROR }, // AS:
  /* 67 /dm */ { 0, BX_IA_ERROR }, // AS:
  /* 68 /dr */ { BxImmediate_Id, BX_IA_PUSH64_Id },
  /* 68 /dm */ { BxImmediate_Id, BX_IA_PUSH64_Id },
  /* 69 /dr */ { BxImmediate_Id, BX_IA_IMUL_GdEdIdR },
  /* 69 /dm */ { BxImmediate_Id, BX_IA_IMUL_GdEdIdM },
  /* 6A /dr */ { BxImmediate_Ib_SE, BX_IA_PUSH64_Id },
  /* 6A /dm */ { BxImmediate_Ib_SE, BX_IA_PUSH64_Id },
  /* 6B /dr */ { BxImmediate_Ib_SE, BX_IA_IMUL_GdEdIdR },
  /* 6B /dm */ { BxImmediate_Ib_SE, BX_IA_IMUL_GdEdIdM },
  /* 6C /dr */ { 0, BX_IA_REP_INSB_YbDX },
  /* 6C /dm */ { 0, BX_IA_REP_INSB_YbDX },
  /* 6D /dr */ { 0, BX_IA_REP_INSD_YdDX },
  /* 6D /dm */ { 0, BX_IA_REP_INSD_YdDX },
  /* 6E /dr */ { 0, BX_IA_REP_OUTSB_DXXb },
  /* 6E /dm */ { 0, BX_IA_REP_OUTSB_DXXb },
  /* 6F /dr */ { 0, BX_IA_REP_OUTSD_DXXd },
  /* 6F /dm */ { 0, BX_IA_REP_OUTSD_DXXd },
  /* 70 /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JO_Jq },
  /* 70 /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JO_Jq },
  /* 71 /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 71 /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 72 /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JB_Jq },
  /* 72 /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JB_Jq },
  /* 73 /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 73 /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 74 /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 74 /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 75 /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 75 /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 76 /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 76 /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 77 /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 77 /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 78 /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JS_Jq },
  /* 78 /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JS_Jq },
  /* 79 /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 79 /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 7A /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JP_Jq },
  /* 7A /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JP_Jq },
  /* 7B /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 7B /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 7C /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JL_Jq },
  /* 7C /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JL_Jq },
  /* 7D /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 7D /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 7E /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 7E /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 7F /dr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 7F /dm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 80 /dr */ { BxGroup1 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG1EbIbR },
  /* 80 /dm */ { BxGroup1 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG1EbIbM },
  /* 81 /dr */ { BxGroup5 | BxImmediate_Id, BX_IA_ERROR, BxOpcodeInfoG1EdR },
  /* 81 /dm */ { BxGroup1 | BxImmediate_Id, BX_IA_ERROR, BxOpcodeInfoG1EdM },
  /* 82 /dr */ { 0, BX_IA_ERROR },
  /* 82 /dm */ { 0, BX_IA_ERROR },
  /* 83 /dr */ { BxGroup5 | BxImmediate_Ib_SE, BX_IA_ERROR, BxOpcodeInfoG1EdR },
  /* 83 /dm */ { BxGroup1 | BxImmediate_Ib_SE, BX_IA_ERROR, BxOpcodeInfoG1EdM },
  /* 84 /dr */ { 0, BX_IA_TEST_EbGbR },
  /* 84 /dm */ { 0, BX_IA_TEST_EbGbM },
  /* 85 /dr */ { 0, BX_IA_TEST_EdGdR },
  /* 85 /dm */ { 0, BX_IA_TEST_EdGdM },
  /* 86 /dr */ { 0, BX_IA_XCHG_EbGbR },
  /* 86 /dm */ { BxLockable, BX_IA_XCHG_EbGbM },
  /* 87 /dr */ { 0, BX_IA_XCHG_EdGdR },
  /* 87 /dm */ { BxLockable, BX_IA_XCHG_EdGdM },
  /* 88 /dr */ { BxArithDstRM, BX_IA_MOV_GbEbR },
  /* 88 /dm */ { 0, BX_IA_MOV_EbGbM },
  /* 89 /dr */ { BxArithDstRM, BX_IA_MOV_GdEdR },
  /* 89 /dm */ { 0, BX_IA_MOV64_EdGdM },
  /* 8A /dr */ { 0, BX_IA_MOV_GbEbR },
  /* 8A /dm */ { 0, BX_IA_MOV_GbEbM },
  /* 8B /dr */ { 0, BX_IA_MOV_GdEdR },
  /* 8B /dm */ { 0, BX_IA_MOV64_GdEdM },
  /* 8C /dr */ { 0, BX_IA_MOV_EwSwR },
  /* 8C /dm */ { 0, BX_IA_MOV_EwSwM },
  /* 8D /dr */ { 0, BX_IA_ERROR },       // LEA
  /* 8D /dm */ { 0, BX_IA_LEA_GdM },
  /* 8E /dr */ { 0, BX_IA_MOV_SwEw },
  /* 8E /dm */ { 0, BX_IA_MOV_SwEw },
  /* 8F /dr */ { BxGroup1A, BX_IA_ERROR, BxOpcodeInfo64G1AEqR },
  /* 8F /dm */ { BxGroup1A, BX_IA_ERROR, BxOpcodeInfo64G1AEqM },
  /* 90 /dr */ { 0, BX_IA_XCHG_ERXEAX }, // handles XCHG R8d, EAX
  /* 90 /dm */ { 0, BX_IA_XCHG_ERXEAX }, // handles XCHG R8d, EAX
  /* 91 /dr */ { 0, BX_IA_XCHG_ERXEAX },
  /* 91 /dm */ { 0, BX_IA_XCHG_ERXEAX },
  /* 92 /dr */ { 0, BX_IA_XCHG_ERXEAX },
  /* 92 /dm */ { 0, BX_IA_XCHG_ERXEAX },
  /* 93 /dr */ { 0, BX_IA_XCHG_ERXEAX },
  /* 93 /dm */ { 0, BX_IA_XCHG_ERXEAX },
  /* 94 /dr */ { 0, BX_IA_XCHG_ERXEAX },
  /* 94 /dm */ { 0, BX_IA_XCHG_ERXEAX },
  /* 95 /dr */ { 0, BX_IA_XCHG_ERXEAX },
  /* 95 /dm */ { 0, BX_IA_XCHG_ERXEAX },
  /* 96 /dr */ { 0, BX_IA_XCHG_ERXEAX },
  /* 96 /dm */ { 0, BX_IA_XCHG_ERXEAX },
  /* 97 /dr */ { 0, BX_IA_XCHG_ERXEAX },
  /* 97 /dm */ { 0, BX_IA_XCHG_ERXEAX },
  /* 98 /dr */ { 0, BX_IA_CWDE },
  /* 98 /dm */ { 0, BX_IA_CWDE },
  /* 99 /dr */ { 0, BX_IA_CDQ },
  /* 99 /dm */ { 0, BX_IA_CDQ },
  /* 9A /dr */ { 0, BX_IA_ERROR },
  /* 9A /dm */ { 0, BX_IA_ERROR },
  /* 9B /dr */ { 0, BX_IA_FWAIT },
  /* 9B /dm */ { 0, BX_IA_FWAIT },
  /* 9C /dr */ { 0, BX_IA_PUSHF_Fq },
  /* 9C /dm */ { 0, BX_IA_PUSHF_Fq },
  /* 9D /dr */ { 0, BX_IA_POPF_Fq },
  /* 9D /dm */ { 0, BX_IA_POPF_Fq },
  /* 9E /dr */ { 0, BX_IA_SAHF },
  /* 9E /dm */ { 0, BX_IA_SAHF },
  /* 9F /dr */ { 0, BX_IA_LAHF },
  /* 9F /dm */ { 0, BX_IA_LAHF },
  /* A0 /dr */ { BxImmediate_O, BX_IA_MOV_ALOq },
  /* A0 /dm */ { BxImmediate_O, BX_IA_MOV_ALOq },
  /* A1 /dr */ { BxImmediate_O, BX_IA_MOV_EAXOq },
  /* A1 /dm */ { BxImmediate_O, BX_IA_MOV_EAXOq },
  /* A2 /dr */ { BxImmediate_O, BX_IA_MOV_OqAL },
  /* A2 /dm */ { BxImmediate_O, BX_IA_MOV_OqAL },
  /* A3 /dr */ { BxImmediate_O, BX_IA_MOV_OqEAX },
  /* A3 /dm */ { BxImmediate_O, BX_IA_MOV_OqEAX },
  /* A4 /dr */ { 0, BX_IA_REP_MOVSB_XbYb },
  /* A4 /dm */ { 0, BX_IA_REP_MOVSB_XbYb },
  /* A5 /dr */ { 0, BX_IA_REP_MOVSD_XdYd },
  /* A5 /dm */ { 0, BX_IA_REP_MOVSD_XdYd },
  /* A6 /dr */ { 0, BX_IA_REP_CMPSB_XbYb },
  /* A6 /dm */ { 0, BX_IA_REP_CMPSB_XbYb },
  /* A7 /dr */ { 0, BX_IA_REP_CMPSD_XdYd },
  /* A7 /dm */ { 0, BX_IA_REP_CMPSD_XdYd },
  /* A8 /dr */ { BxImmediate_Ib, BX_IA_TEST_ALIb },
  /* A8 /dm */ { BxImmediate_Ib, BX_IA_TEST_ALIb },
  /* A9 /dr */ { BxImmediate_Id, BX_IA_TEST_EAXId },
  /* A9 /dm */ { BxImmediate_Id, BX_IA_TEST_EAXId },
  /* AA /dr */ { 0, BX_IA_REP_STOSB_YbAL },
  /* AA /dm */ { 0, BX_IA_REP_STOSB_YbAL },
  /* AB /dr */ { 0, BX_IA_REP_STOSD_YdEAX },
  /* AB /dm */ { 0, BX_IA_REP_STOSD_YdEAX },
  /* AC /dr */ { 0, BX_IA_REP_LODSB_ALXb },
  /* AC /dm */ { 0, BX_IA_REP_LODSB_ALXb },
  /* AD /dr */ { 0, BX_IA_REP_LODSD_EAXXd },
  /* AD /dm */ { 0, BX_IA_REP_LODSD_EAXXd },
  /* AE /dr */ { 0, BX_IA_REP_SCASB_ALXb  },
  /* AE /dm */ { 0, BX_IA_REP_SCASB_ALXb  },
  /* AF /dr */ { 0, BX_IA_REP_SCASD_EAXXd },
  /* AF /dm */ { 0, BX_IA_REP_SCASD_EAXXd },
  /* B0 /dr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B0 /dm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B1 /dr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B1 /dm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B2 /dr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B2 /dm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B3 /dr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B3 /dm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B4 /dr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B4 /dm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B5 /dr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B5 /dm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B6 /dr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B6 /dm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B7 /dr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B7 /dm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B8 /dr */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* B8 /dm */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* B9 /dr */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* B9 /dm */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BA /dr */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BA /dm */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BB /dr */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BB /dm */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BC /dr */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BC /dm */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BD /dr */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BD /dm */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BE /dr */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BE /dm */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BF /dr */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* BF /dm */ { BxImmediate_Id, BX_IA_MOV_ERXId },
  /* C0 /dr */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG2EbR },
  /* C0 /dm */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG2EbM },
  /* C1 /dr */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG2EdR },
  /* C1 /dm */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG2EdM },
  /* C2 /dr */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETnear64_Iw },
  /* C2 /dm */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETnear64_Iw },
  /* C3 /dr */ { BxTraceEnd,                  BX_IA_RETnear64 },
  /* C3 /dm */ { BxTraceEnd,                  BX_IA_RETnear64 },
  /* C4 /dr */ { 0, BX_IA_ERROR },
  /* C4 /dm */ { 0, BX_IA_ERROR },
  /* C5 /dr */ { 0, BX_IA_ERROR },
  /* C5 /dm */ { 0, BX_IA_ERROR },
  /* C6 /dr */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfoG11EbR },
  /* C6 /dm */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfoG11EbM },
  /* C7 /dr */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfoG11EdR },
  /* C7 /dm */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfoG11EdM },
  /* C8 /dr */ { BxImmediate_IwIb, BX_IA_ENTER64_IwIb },
  /* C8 /dm */ { BxImmediate_IwIb, BX_IA_ENTER64_IwIb },
  /* C9 /dr */ { 0, BX_IA_LEAVE64 },
  /* C9 /dm */ { 0, BX_IA_LEAVE64 },
  /* CA /dr */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETfar32_Iw },
  /* CA /dm */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETfar32_Iw },
  /* CB /dr */ { BxTraceEnd,                  BX_IA_RETfar32 },
  /* CB /dm */ { BxTraceEnd,                  BX_IA_RETfar32 },
  /* CC /dr */ { BxTraceEnd, BX_IA_INT3 },
  /* CC /dm */ { BxTraceEnd, BX_IA_INT3 },
  /* CD /dr */ { BxImmediate_Ib | BxTraceEnd, BX_IA_INT_Ib },
  /* CD /dm */ { BxImmediate_Ib | BxTraceEnd, BX_IA_INT_Ib },
  /* CE /dr */ { 0, BX_IA_ERROR },
  /* CE /dm */ { 0, BX_IA_ERROR },
  /* CF /dr */ { BxTraceEnd, BX_IA_IRET64 },
  /* CF /dm */ { BxTraceEnd, BX_IA_IRET64 },
  /* D0 /dr */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfoG2EbR },
  /* D0 /dm */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfoG2EbM },
  /* D1 /dr */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfoG2EdR },
  /* D1 /dm */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfoG2EdM },
  /* D2 /dr */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfoG2EbR },
  /* D2 /dm */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfoG2EbM },
  /* D3 /dr */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfoG2EdR },
  /* D3 /dm */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfoG2EdM },
  /* D4 /dr */ { 0, BX_IA_ERROR },
  /* D4 /dm */ { 0, BX_IA_ERROR },
  /* D5 /dr */ { 0, BX_IA_ERROR },
  /* D5 /dm */ { 0, BX_IA_ERROR },
  /* D6 /dr */ { 0, BX_IA_ERROR },
  /* D6 /dm */ { 0, BX_IA_ERROR },
  /* D7 /dr */ { 0, BX_IA_XLAT },
  /* D7 /dm */ { 0, BX_IA_XLAT },
  /* D8 /dr */ { BxGroupN, BX_IA_ERROR, BxOpcodeInfo_FloatingPointD8 },
  /* D8 /dm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupD8 },
  /* D9 /dr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointD9 },
  /* D9 /dm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupD9 },
  /* DA /dr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDA },
  /* DA /dm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDA },
  /* DB /dr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDB },
  /* DB /dm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDB },
  /* DC /dr */ { BxGroupN, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDC },
  /* DC /dm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDC },
  /* DD /dr */ { BxGroupN, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDD },
  /* DD /dm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDD },
  /* DE /dr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDE },
  /* DE /dm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDE },
  /* DF /dr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDF },
  /* DF /dm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDF },
  /* E0 /dr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPNE64_Jb },
  /* E0 /dm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPNE64_Jb },
  /* E1 /dr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPE64_Jb },
  /* E1 /dm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPE64_Jb },
  /* E2 /dr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOP64_Jb },
  /* E2 /dm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOP64_Jb },
  /* E3 /dr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JRCXZ_Jb },
  /* E3 /dm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JRCXZ_Jb },
  /* E4 /dr */ { BxImmediate_Ib, BX_IA_IN_ALIb },
  /* E4 /dm */ { BxImmediate_Ib, BX_IA_IN_ALIb },
  /* E5 /dr */ { BxImmediate_Ib, BX_IA_IN_EAXIb },
  /* E5 /dm */ { BxImmediate_Ib, BX_IA_IN_EAXIb },
  /* E6 /dr */ { BxImmediate_Ib, BX_IA_OUT_IbAL },
  /* E6 /dm */ { BxImmediate_Ib, BX_IA_OUT_IbAL },
  /* E7 /dr */ { BxImmediate_Ib, BX_IA_OUT_IbEAX },
  /* E7 /dm */ { BxImmediate_Ib, BX_IA_OUT_IbEAX },
  /* E8 /dr */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_CALL_Jq },
  /* E8 /dm */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_CALL_Jq },
  /* E9 /dr */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_JMP_Jq },
  /* E9 /dm */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_JMP_Jq },
  /* EA /dr */ { 0, BX_IA_ERROR },
  /* EA /dm */ { 0, BX_IA_ERROR },
  /* EB /dr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JMP_Jq },
  /* EB /dm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JMP_Jq },
  /* EC /dr */ { 0, BX_IA_IN_ALDX },
  /* EC /dm */ { 0, BX_IA_IN_ALDX },
  /* ED /dr */ { 0, BX_IA_IN_EAXDX },
  /* ED /dm */ { 0, BX_IA_IN_EAXDX },
  /* EE /dr */ { 0, BX_IA_OUT_DXAL },
  /* EE /dm */ { 0, BX_IA_OUT_DXAL },
  /* EF /dr */ { 0, BX_IA_OUT_DXEAX },
  /* EF /dm */ { 0, BX_IA_OUT_DXEAX },
  /* F0 /dr */ { 0, BX_IA_ERROR }, // LOCK:
  /* F0 /dm */ { 0, BX_IA_ERROR }, // LOCK:
  /* F1 /dr */ { BxTraceEnd, BX_IA_INT1 },
  /* F1 /dm */ { BxTraceEnd, BX_IA_INT1 },
  /* F2 /dr */ { 0, BX_IA_ERROR }, // REPNE/REPNZ
  /* F2 /dm */ { 0, BX_IA_ERROR }, // REPNE/REPNZ
  /* F3 /dr */ { 0, BX_IA_ERROR }, // REP,REPE/REPZ
  /* F3 /dm */ { 0, BX_IA_ERROR }, // REP,REPE/REPZ
  /* F4 /dr */ { BxTraceEnd, BX_IA_HLT },
  /* F4 /dm */ { BxTraceEnd, BX_IA_HLT },
  /* F5 /dr */ { 0, BX_IA_CMC },
  /* F5 /dm */ { 0, BX_IA_CMC },
  /* F6 /dr */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfoG3EbR },
  /* F6 /dm */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfoG3EbM },
  /* F7 /dr */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfoG3EdR },
  /* F7 /dm */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfoG3EdM },
  /* F8 /dr */ { 0, BX_IA_CLC },
  /* F8 /dm */ { 0, BX_IA_CLC },
  /* F9 /dr */ { 0, BX_IA_STC },
  /* F9 /dm */ { 0, BX_IA_STC },
  /* FA /dr */ { 0, BX_IA_CLI },
  /* FA /dm */ { 0, BX_IA_CLI },
  /* FB /dr */ { 0, BX_IA_STI },
  /* FB /dm */ { 0, BX_IA_STI },
  /* FC /dr */ { 0, BX_IA_CLD },
  /* FC /dm */ { 0, BX_IA_CLD },
  /* FD /dr */ { 0, BX_IA_STD },
  /* FD /dm */ { 0, BX_IA_STD },
  /* FE /dr */ { BxGroup4, BX_IA_ERROR, BxOpcodeInfoG4R },
  /* FE /dm */ { BxGroup4, BX_IA_ERROR, BxOpcodeInfoG4M },
  /* FF /dr */ { BxGroup5, BX_IA_ERROR, BxOpcodeInfo64G5dR },
  /* FF /dm */ { BxGroup5, BX_IA_ERROR, BxOpcodeInfo64G5dM },

  /* 0F 00 /dr */ { BxGroup6, BX_IA_ERROR, BxOpcodeInfoG6 },
  /* 0F 00 /dm */ { BxGroup6, BX_IA_ERROR, BxOpcodeInfoG6 },
  /* 0F 01 /dr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfoG7R },
  /* 0F 01 /dm */ { BxGroup7, BX_IA_ERROR, BxOpcodeInfo64G7M },
  /* 0F 02 /dr */ { 0, BX_IA_LAR_GvEw },
  /* 0F 02 /dm */ { 0, BX_IA_LAR_GvEw },
  /* 0F 03 /dr */ { 0, BX_IA_LSL_GvEw },
  /* 0F 03 /dm */ { 0, BX_IA_LSL_GvEw },
  /* 0F 04 /dr */ { 0, BX_IA_ERROR },
  /* 0F 04 /dm */ { 0, BX_IA_ERROR },
  /* 0F 05 /dr */ { BxTraceEnd, BX_IA_SYSCALL },
  /* 0F 05 /dm */ { BxTraceEnd, BX_IA_SYSCALL },
  /* 0F 06 /dr */ { 0, BX_IA_CLTS },
  /* 0F 06 /dm */ { 0, BX_IA_CLTS },
  /* 0F 07 /dr */ { BxTraceEnd, BX_IA_SYSRET },
  /* 0F 07 /dm */ { BxTraceEnd, BX_IA_SYSRET },
  /* 0F 08 /dr */ { BxTraceEnd, BX_IA_INVD },
  /* 0F 08 /dm */ { BxTraceEnd, BX_IA_INVD },
  /* 0F 09 /dr */ { BxTraceEnd, BX_IA_WBINVD },
  /* 0F 09 /dm */ { BxTraceEnd, BX_IA_WBINVD },
  /* 0F 0A /dr */ { 0, BX_IA_ERROR },
  /* 0F 0A /dm */ { 0, BX_IA_ERROR },
  /* 0F 0B /dr */ { BxTraceEnd, BX_IA_UD2A },
  /* 0F 0B /dm */ { BxTraceEnd, BX_IA_UD2A },
  /* 0F 0C /dr */ { 0, BX_IA_ERROR },
  /* 0F 0C /dm */ { 0, BX_IA_ERROR },
  /* 0F 0D /dr */ { 0, BX_IA_PREFETCHW },       // 3DNow! PREFETCH on AMD, NOP on Intel
  /* 0F 0D /dm */ { 0, BX_IA_PREFETCHW },       // 3DNow! PREFETCH on AMD, NOP on Intel
  /* 0F 0E /dr */ { 0, BX_IA_FEMMS },           // 3DNow! FEMMS
  /* 0F 0E /dm */ { 0, BX_IA_FEMMS },           // 3DNow! FEMMS
#if BX_SUPPORT_3DNOW
  /* 0F 0F /dr */ { BxImmediate_Ib, BX_IA_ERROR },
  /* 0F 0F /dm */ { BxImmediate_Ib, BX_IA_ERROR },
#else
  /* 0F 0F /dr */ { 0, BX_IA_ERROR },
  /* 0F 0F /dm */ { 0, BX_IA_ERROR },
#endif
  /* 0F 10 /dr */ { BxPrefixSSE, BX_IA_MOVUPS_VpsWpsR, BxOpcodeGroupSSE_0f10R },
  /* 0F 10 /dm */ { BxPrefixSSE, BX_IA_MOVUPS_VpsWpsM, BxOpcodeGroupSSE_0f10M },
  /* 0F 11 /dr */ { BxPrefixSSE, BX_IA_MOVUPS_WpsVpsR, BxOpcodeGroupSSE_0f11R },
  /* 0F 11 /dm */ { BxPrefixSSE, BX_IA_MOVUPS_WpsVpsM, BxOpcodeGroupSSE_0f11M },
  /* 0F 12 /dr */ { BxPrefixSSE, BX_IA_MOVHLPS_VpsWpsR, BxOpcodeGroupSSE_0f12R },
  /* 0F 12 /dm */ { BxPrefixSSE, BX_IA_MOVLPS_VpsMq, BxOpcodeGroupSSE_0f12M },
  /* 0F 13 /dr */ { 0, BX_IA_ERROR },  // MOVLPS/PD SSE group
  /* 0F 13 /dm */ { BxPrefixSSE, BX_IA_MOVLPS_MqVps, BxOpcodeGroupSSE_0f13M },
  /* 0F 14 /dr */ { BxPrefixSSE, BX_IA_UNPCKLPS_VpsWdq, BxOpcodeGroupSSE_0f14 },
  /* 0F 14 /dm */ { BxPrefixSSE, BX_IA_UNPCKLPS_VpsWdq, BxOpcodeGroupSSE_0f14 },
  /* 0F 15 /dr */ { BxPrefixSSE, BX_IA_UNPCKHPS_VpsWdq, BxOpcodeGroupSSE_0f15 },
  /* 0F 15 /dm */ { BxPrefixSSE, BX_IA_UNPCKHPS_VpsWdq, BxOpcodeGroupSSE_0f15 },
  /* 0F 16 /dr */ { BxPrefixSSE, BX_IA_MOVLHPS_VpsWpsR, BxOpcodeGroupSSE_0f16R },
  /* 0F 16 /dm */ { BxPrefixSSE, BX_IA_MOVHPS_VpsMq, BxOpcodeGroupSSE_0f16M },
  /* 0F 17 /dr */ { 0, BX_IA_ERROR },  // MOHLPS/PD SSE group
  /* 0F 17 /dm */ { BxPrefixSSE, BX_IA_MOVHPS_MqVps, BxOpcodeGroupSSE_0f17M },
  /* 0F 18 /dr */ { 0, BX_IA_NOP },    // PREFETCH HINT
  /* 0F 18 /dm */ { 0, BX_IA_PREFETCH }, // opcode group G16, PREFETCH hints
  /* 0F 19 /dr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 19 /dm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1A /dr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1A /dm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1B /dr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1B /dm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1C /dr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1C /dm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1D /dr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1D /dm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1E /dr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1E /dm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1F /dr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1F /dm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 20 /dr */ { 0, BX_IA_MOV_RqCq },
  /* 0F 20 /dm */ { 0, BX_IA_MOV_RqCq },
  /* 0F 21 /dr */ { 0, BX_IA_MOV_RqDq },
  /* 0F 21 /dm */ { 0, BX_IA_MOV_RqDq },
  /* 0F 22 /dr */ { BxTraceEnd, BX_IA_MOV_CqRq },
  /* 0F 22 /dm */ { BxTraceEnd, BX_IA_MOV_CqRq },
  /* 0F 23 /dr */ { BxTraceEnd, BX_IA_MOV_DqRq },
  /* 0F 23 /dm */ { BxTraceEnd, BX_IA_MOV_DqRq },
  /* 0F 24 /dr */ { 0, BX_IA_ERROR },
  /* 0F 24 /dm */ { 0, BX_IA_ERROR },
  /* 0F 25 /dr */ { 0, BX_IA_ERROR },
  /* 0F 25 /dm */ { 0, BX_IA_ERROR },
  /* 0F 26 /dr */ { 0, BX_IA_ERROR },
  /* 0F 26 /dm */ { 0, BX_IA_ERROR },
  /* 0F 27 /dr */ { 0, BX_IA_ERROR },
  /* 0F 27 /dm */ { 0, BX_IA_ERROR },
  /* 0F 28 /dr */ { BxPrefixSSE, BX_IA_MOVAPS_VpsWpsR, BxOpcodeGroupSSE_0f28R },
  /* 0F 28 /dm */ { BxPrefixSSE, BX_IA_MOVAPS_VpsWpsM, BxOpcodeGroupSSE_0f28M },
  /* 0F 29 /dr */ { BxPrefixSSE, BX_IA_MOVAPS_WpsVpsR, BxOpcodeGroupSSE_0f29R },
  /* 0F 29 /dm */ { BxPrefixSSE, BX_IA_MOVAPS_WpsVpsM, BxOpcodeGroupSSE_0f29M },
  /* 0F 2A /dr */ { BxPrefixSSE, BX_IA_CVTPI2PS_VpsQqR, BxOpcodeGroupSSE_0f2aR },
  /* 0F 2A /dm */ { BxPrefixSSE, BX_IA_CVTPI2PS_VpsQqM, BxOpcodeGroupSSE_0f2aM },
  /* 0F 2B /dr */ { 0, BX_IA_ERROR }, // MOVNTPS/PD/SS/SD
  /* 0F 2B /dm */ { BxPrefixSSE, BX_IA_MOVNTPS_MpsVps, BxOpcodeGroupSSE_0f2bM },
  /* 0F 2C /dr */ { BxPrefixSSE, BX_IA_CVTTPS2PI_PqWps, BxOpcodeGroupSSE_0f2c },
  /* 0F 2C /dm */ { BxPrefixSSE, BX_IA_CVTTPS2PI_PqWps, BxOpcodeGroupSSE_0f2c },
  /* 0F 2D /dr */ { BxPrefixSSE, BX_IA_CVTPS2PI_PqWps, BxOpcodeGroupSSE_0f2d },
  /* 0F 2D /dm */ { BxPrefixSSE, BX_IA_CVTPS2PI_PqWps, BxOpcodeGroupSSE_0f2d },
  /* 0F 2E /dr */ { BxPrefixSSE, BX_IA_UCOMISS_VssWss, BxOpcodeGroupSSE_0f2e },
  /* 0F 2E /dm */ { BxPrefixSSE, BX_IA_UCOMISS_VssWss, BxOpcodeGroupSSE_0f2e },
  /* 0F 2F /dr */ { BxPrefixSSE, BX_IA_COMISS_VpsWps, BxOpcodeGroupSSE_0f2f },
  /* 0F 2F /dm */ { BxPrefixSSE, BX_IA_COMISS_VpsWps, BxOpcodeGroupSSE_0f2f },
  /* 0F 30 /dr */ { 0, BX_IA_WRMSR },
  /* 0F 30 /dm */ { 0, BX_IA_WRMSR },
  /* 0F 31 /dr */ { 0, BX_IA_RDTSC },
  /* 0F 31 /dm */ { 0, BX_IA_RDTSC },
  /* 0F 32 /dr */ { 0, BX_IA_RDMSR },
  /* 0F 32 /dm */ { 0, BX_IA_RDMSR },
  /* 0F 33 /dr */ { 0, BX_IA_RDPMC },
  /* 0F 33 /dm */ { 0, BX_IA_RDPMC },
  /* 0F 34 /dr */ { BxTraceEnd, BX_IA_SYSENTER },
  /* 0F 34 /dm */ { BxTraceEnd, BX_IA_SYSENTER },
  /* 0F 35 /dr */ { BxTraceEnd, BX_IA_SYSEXIT },
  /* 0F 35 /dm */ { BxTraceEnd, BX_IA_SYSEXIT },
  /* 0F 36 /dr */ { 0, BX_IA_ERROR },
  /* 0F 36 /dm */ { 0, BX_IA_ERROR },
  /* 0F 37 /dr */ { 0, BX_IA_ERROR },
  /* 0F 37 /dm */ { 0, BX_IA_ERROR },
  /* 0F 38 /dr */ { Bx3ByteOp, BX_IA_ERROR, BxOpcode3ByteTable0f38 }, // 3-byte escape
  /* 0F 38 /dm */ { Bx3ByteOp, BX_IA_ERROR, BxOpcode3ByteTable0f38 }, // 3-byte escape
  /* 0F 39 /dr */ { 0, BX_IA_ERROR },
  /* 0F 39 /dm */ { 0, BX_IA_ERROR },
  /* 0F 3A /dr */ { Bx3ByteOp | BxImmediate_Ib, BX_IA_ERROR, BxOpcode3ByteTable0f3a }, // 3-byte escape
  /* 0F 3A /dm */ { Bx3ByteOp | BxImmediate_Ib, BX_IA_ERROR, BxOpcode3ByteTable0f3a }, // 3-byte escape
  /* 0F 3B /dr */ { 0, BX_IA_ERROR },
  /* 0F 3B /dm */ { 0, BX_IA_ERROR },
  /* 0F 3C /dr */ { 0, BX_IA_ERROR },
  /* 0F 3C /dm */ { 0, BX_IA_ERROR },
  /* 0F 3D /dr */ { 0, BX_IA_ERROR },
  /* 0F 3D /dm */ { 0, BX_IA_ERROR },
  /* 0F 3E /dr */ { 0, BX_IA_ERROR },
  /* 0F 3E /dm */ { 0, BX_IA_ERROR },
  /* 0F 3F /dr */ { 0, BX_IA_ERROR },
  /* 0F 3F /dm */ { 0, BX_IA_ERROR },
  /* 0F 40 /dr */ { 0, BX_IA_CMOVO_GdEdR },
  /* 0F 40 /dm */ { 0, BX_IA_CMOVO_GdEdM },
  /* 0F 41 /dr */ { 0, BX_IA_CMOVNO_GdEdR },
  /* 0F 41 /dm */ { 0, BX_IA_CMOVNO_GdEdM },
  /* 0F 42 /dr */ { 0, BX_IA_CMOVB_GdEdR },
  /* 0F 42 /dm */ { 0, BX_IA_CMOVB_GdEdM },
  /* 0F 43 /dr */ { 0, BX_IA_CMOVNB_GdEdR },
  /* 0F 43 /dm */ { 0, BX_IA_CMOVNB_GdEdM },
  /* 0F 44 /dr */ { 0, BX_IA_CMOVZ_GdEdR },
  /* 0F 44 /dm */ { 0, BX_IA_CMOVZ_GdEdM },
  /* 0F 45 /dr */ { 0, BX_IA_CMOVNZ_GdEdR },
  /* 0F 45 /dm */ { 0, BX_IA_CMOVNZ_GdEdM },
  /* 0F 46 /dr */ { 0, BX_IA_CMOVBE_GdEdR },
  /* 0F 46 /dm */ { 0, BX_IA_CMOVBE_GdEdM },
  /* 0F 47 /dr */ { 0, BX_IA_CMOVNBE_GdEdR },
  /* 0F 47 /dm */ { 0, BX_IA_CMOVNBE_GdEdM },
  /* 0F 48 /dr */ { 0, BX_IA_CMOVS_GdEdR },
  /* 0F 48 /dm */ { 0, BX_IA_CMOVS_GdEdM },
  /* 0F 49 /dr */ { 0, BX_IA_CMOVNS_GdEdR },
  /* 0F 49 /dm */ { 0, BX_IA_CMOVNS_GdEdM },
  /* 0F 4A /dr */ { 0, BX_IA_CMOVP_GdEdR },
  /* 0F 4A /dm */ { 0, BX_IA_CMOVP_GdEdM },
  /* 0F 4B /dr */ { 0, BX_IA_CMOVNP_GdEdR },
  /* 0F 4B /dm */ { 0, BX_IA_CMOVNP_GdEdM },
  /* 0F 4C /dr */ { 0, BX_IA_CMOVL_GdEdR },
  /* 0F 4C /dm */ { 0, BX_IA_CMOVL_GdEdM },
  /* 0F 4D /dr */ { 0, BX_IA_CMOVNL_GdEdR },
  /* 0F 4D /dm */ { 0, BX_IA_CMOVNL_GdEdM },
  /* 0F 4E /dr */ { 0, BX_IA_CMOVLE_GdEdR },
  /* 0F 4E /dm */ { 0, BX_IA_CMOVLE_GdEdM },
  /* 0F 4F /dr */ { 0, BX_IA_CMOVNLE_GdEdR },
  /* 0F 4F /dm */ { 0, BX_IA_CMOVNLE_GdEdM },
  /* 0F 50 /dr */ { BxPrefixSSE, BX_IA_MOVMSKPS_GdVRps, BxOpcodeGroupSSE_0f50 },
  /* 0F 50 /dm */ { 0, BX_IA_ERROR }, // MOVMSKPS/PD
  /* 0F 51 /dr */ { BxPrefixSSE, BX_IA_SQRTPS_VpsWps, BxOpcodeGroupSSE_0f51 },
  /* 0F 51 /dm */ { BxPrefixSSE, BX_IA_SQRTPS_VpsWps, BxOpcodeGroupSSE_0f51 },
  /* 0F 52 /dr */ { BxPrefixSSE, BX_IA_RSQRTPS_VpsWps, BxOpcodeGroupSSE_0f52 },
  /* 0F 52 /dm */ { BxPrefixSSE, BX_IA_RSQRTPS_VpsWps, BxOpcodeGroupSSE_0f52 },
  /* 0F 53 /dr */ { BxPrefixSSE, BX_IA_RCPPS_VpsWps, BxOpcodeGroupSSE_0f53 },
  /* 0F 53 /dm */ { BxPrefixSSE, BX_IA_RCPPS_VpsWps, BxOpcodeGroupSSE_0f53 },
  /* 0F 54 /dr */ { BxPrefixSSE, BX_IA_ANDPS_VpsWps, BxOpcodeGroupSSE_0f54 },
  /* 0F 54 /dm */ { BxPrefixSSE, BX_IA_ANDPS_VpsWps, BxOpcodeGroupSSE_0f54 },
  /* 0F 55 /dr */ { BxPrefixSSE, BX_IA_ANDNPS_VpsWps, BxOpcodeGroupSSE_0f55 },
  /* 0F 55 /dm */ { BxPrefixSSE, BX_IA_ANDNPS_VpsWps, BxOpcodeGroupSSE_0f55 },
  /* 0F 56 /dr */ { BxPrefixSSE, BX_IA_ORPS_VpsWps, BxOpcodeGroupSSE_0f56 },
  /* 0F 56 /dm */ { BxPrefixSSE, BX_IA_ORPS_VpsWps, BxOpcodeGroupSSE_0f56 },
  /* 0F 57 /dr */ { BxPrefixSSE, BX_IA_XORPS_VpsWps, BxOpcodeGroupSSE_0f57 },
  /* 0F 57 /dm */ { BxPrefixSSE, BX_IA_XORPS_VpsWps, BxOpcodeGroupSSE_0f57 },
  /* 0F 58 /dr */ { BxPrefixSSE, BX_IA_ADDPS_VpsWps, BxOpcodeGroupSSE_0f58 },
  /* 0F 58 /dm */ { BxPrefixSSE, BX_IA_ADDPS_VpsWps, BxOpcodeGroupSSE_0f58 },
  /* 0F 59 /dr */ { BxPrefixSSE, BX_IA_MULPS_VpsWps, BxOpcodeGroupSSE_0f59 },
  /* 0F 59 /dm */ { BxPrefixSSE, BX_IA_MULPS_VpsWps, BxOpcodeGroupSSE_0f59 },
  /* 0F 5A /dr */ { BxPrefixSSE, BX_IA_CVTPS2PD_VpsWps, BxOpcodeGroupSSE_0f5a },
  /* 0F 5A /dm */ { BxPrefixSSE, BX_IA_CVTPS2PD_VpsWps, BxOpcodeGroupSSE_0f5a },
  /* 0F 5B /dr */ { BxPrefixSSE, BX_IA_CVTDQ2PS_VpsWdq, BxOpcodeGroupSSE_0f5b },
  /* 0F 5B /dm */ { BxPrefixSSE, BX_IA_CVTDQ2PS_VpsWdq, BxOpcodeGroupSSE_0f5b },
  /* 0F 5C /dr */ { BxPrefixSSE, BX_IA_SUBPS_VpsWps, BxOpcodeGroupSSE_0f5c },
  /* 0F 5C /dm */ { BxPrefixSSE, BX_IA_SUBPS_VpsWps, BxOpcodeGroupSSE_0f5c },
  /* 0F 5D /dr */ { BxPrefixSSE, BX_IA_MINPS_VpsWps, BxOpcodeGroupSSE_0f5d },
  /* 0F 5D /dm */ { BxPrefixSSE, BX_IA_MINPS_VpsWps, BxOpcodeGroupSSE_0f5d },
  /* 0F 5E /dr */ { BxPrefixSSE, BX_IA_DIVPS_VpsWps, BxOpcodeGroupSSE_0f5e },
  /* 0F 5E /dm */ { BxPrefixSSE, BX_IA_DIVPS_VpsWps, BxOpcodeGroupSSE_0f5e },
  /* 0F 5F /dr */ { BxPrefixSSE, BX_IA_MAXPS_VpsWps, BxOpcodeGroupSSE_0f5f },
  /* 0F 5F /dm */ { BxPrefixSSE, BX_IA_MAXPS_VpsWps, BxOpcodeGroupSSE_0f5f },
  /* 0F 60 /dr */ { BxPrefixSSE, BX_IA_PUNPCKLBW_PqQd, BxOpcodeGroupSSE_0f60 },
  /* 0F 60 /dm */ { BxPrefixSSE, BX_IA_PUNPCKLBW_PqQd, BxOpcodeGroupSSE_0f60 },
  /* 0F 61 /dr */ { BxPrefixSSE, BX_IA_PUNPCKLWD_PqQd, BxOpcodeGroupSSE_0f61 },
  /* 0F 61 /dm */ { BxPrefixSSE, BX_IA_PUNPCKLWD_PqQd, BxOpcodeGroupSSE_0f61 },
  /* 0F 62 /dr */ { BxPrefixSSE, BX_IA_PUNPCKLDQ_PqQd, BxOpcodeGroupSSE_0f62 },
  /* 0F 62 /dm */ { BxPrefixSSE, BX_IA_PUNPCKLDQ_PqQd, BxOpcodeGroupSSE_0f62 },
  /* 0F 63 /dr */ { BxPrefixSSE, BX_IA_PACKSSWB_PqQq, BxOpcodeGroupSSE_0f63 },
  /* 0F 63 /dm */ { BxPrefixSSE, BX_IA_PACKSSWB_PqQq, BxOpcodeGroupSSE_0f63 },
  /* 0F 64 /dr */ { BxPrefixSSE, BX_IA_PCMPGTB_PqQq, BxOpcodeGroupSSE_0f64 },
  /* 0F 64 /dm */ { BxPrefixSSE, BX_IA_PCMPGTB_PqQq, BxOpcodeGroupSSE_0f64 },
  /* 0F 65 /dr */ { BxPrefixSSE, BX_IA_PCMPGTW_PqQq, BxOpcodeGroupSSE_0f65 },
  /* 0F 65 /dm */ { BxPrefixSSE, BX_IA_PCMPGTW_PqQq, BxOpcodeGroupSSE_0f65 },
  /* 0F 66 /dr */ { BxPrefixSSE, BX_IA_PCMPGTD_PqQq, BxOpcodeGroupSSE_0f66 },
  /* 0F 66 /dm */ { BxPrefixSSE, BX_IA_PCMPGTD_PqQq, BxOpcodeGroupSSE_0f66 },
  /* 0F 67 /dr */ { BxPrefixSSE, BX_IA_PACKUSWB_PqQq, BxOpcodeGroupSSE_0f67 },
  /* 0F 67 /dm */ { BxPrefixSSE, BX_IA_PACKUSWB_PqQq, BxOpcodeGroupSSE_0f67 },
  /* 0F 68 /dr */ { BxPrefixSSE, BX_IA_PUNPCKHBW_PqQq, BxOpcodeGroupSSE_0f68 },
  /* 0F 68 /dm */ { BxPrefixSSE, BX_IA_PUNPCKHBW_PqQq, BxOpcodeGroupSSE_0f68 },
  /* 0F 69 /dr */ { BxPrefixSSE, BX_IA_PUNPCKHWD_PqQq, BxOpcodeGroupSSE_0f69 },
  /* 0F 69 /dm */ { BxPrefixSSE, BX_IA_PUNPCKHWD_PqQq, BxOpcodeGroupSSE_0f69 },
  /* 0F 6A /dr */ { BxPrefixSSE, BX_IA_PUNPCKHDQ_PqQq, BxOpcodeGroupSSE_0f6a },
  /* 0F 6A /dm */ { BxPrefixSSE, BX_IA_PUNPCKHDQ_PqQq, BxOpcodeGroupSSE_0f6a },
  /* 0F 6B /dr */ { BxPrefixSSE, BX_IA_PACKSSDW_PqQq, BxOpcodeGroupSSE_0f6b },
  /* 0F 6B /dm */ { BxPrefixSSE, BX_IA_PACKSSDW_PqQq, BxOpcodeGroupSSE_0f6b },
  /* 0F 6C /dr */ { BxPrefixSSE66, BX_IA_PUNPCKLQDQ_VdqWdq },
  /* 0F 6C /dm */ { BxPrefixSSE66, BX_IA_PUNPCKLQDQ_VdqWdq },
  /* 0F 6D /dr */ { BxPrefixSSE66, BX_IA_PUNPCKHQDQ_VdqWdq },
  /* 0F 6D /dm */ { BxPrefixSSE66, BX_IA_PUNPCKHQDQ_VdqWdq },
  /* 0F 6E /dr */ { BxPrefixSSE, BX_IA_MOVD_PqEdR, BxOpcodeGroupSSE_0f6eR },
  /* 0F 6E /dm */ { BxPrefixSSE, BX_IA_MOVD_PqEdM, BxOpcodeGroupSSE_0f6eM },
  /* 0F 6F /dr */ { BxPrefixSSE, BX_IA_MOVQ_PqQqR, BxOpcodeGroupSSE_0f6fR },
  /* 0F 6F /dm */ { BxPrefixSSE, BX_IA_MOVQ_PqQqM, BxOpcodeGroupSSE_0f6fM },
  /* 0F 70 /dr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PSHUFW_PqQqIb, BxOpcodeGroupSSE_0f70 },
  /* 0F 70 /dm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PSHUFW_PqQqIb, BxOpcodeGroupSSE_0f70 },
  /* 0F 71 /dr */ { BxGroup12, BX_IA_ERROR, BxOpcodeInfoG12R },
  /* 0F 71 /dm */ { 0, BX_IA_ERROR }, // SSE Group G12
  /* 0F 72 /dr */ { BxGroup13, BX_IA_ERROR, BxOpcodeInfoG13R },
  /* 0F 72 /dm */ { 0, BX_IA_ERROR }, // SSE Group G13
  /* 0F 73 /dr */ { BxGroup14, BX_IA_ERROR, BxOpcodeInfoG14R },
  /* 0F 73 /dm */ { 0, BX_IA_ERROR }, // SSE Group G14
  /* 0F 74 /dr */ { BxPrefixSSE, BX_IA_PCMPEQB_PqQq, BxOpcodeGroupSSE_0f74 },
  /* 0F 74 /dm */ { BxPrefixSSE, BX_IA_PCMPEQB_PqQq, BxOpcodeGroupSSE_0f74 },
  /* 0F 75 /dr */ { BxPrefixSSE, BX_IA_PCMPEQW_PqQq, BxOpcodeGroupSSE_0f75 },
  /* 0F 75 /dm */ { BxPrefixSSE, BX_IA_PCMPEQW_PqQq, BxOpcodeGroupSSE_0f75 },
  /* 0F 76 /dr */ { BxPrefixSSE, BX_IA_PCMPEQD_PqQq, BxOpcodeGroupSSE_0f76 },
  /* 0F 76 /dm */ { BxPrefixSSE, BX_IA_PCMPEQD_PqQq, BxOpcodeGroupSSE_0f76 },
  /* 0F 77 /dr */ { BxPrefixSSE, BX_IA_EMMS, BxOpcodeGroupSSE_ERR },
  /* 0F 77 /dm */ { BxPrefixSSE, BX_IA_EMMS, BxOpcodeGroupSSE_ERR },
  /* 0F 78 /dr */ { BxPrefixSSE, BX_IA_VMREAD_EqGq, BxOpcodeGroupSSE_ERR },
  /* 0F 78 /dm */ { BxPrefixSSE, BX_IA_VMREAD_EqGq, BxOpcodeGroupSSE_ERR },
  /* 0F 79 /dr */ { BxPrefixSSE, BX_IA_VMWRITE_GqEq, BxOpcodeGroupSSE_ERR },
  /* 0F 79 /dm */ { BxPrefixSSE, BX_IA_VMWRITE_GqEq, BxOpcodeGroupSSE_ERR },
  /* 0F 7A /dr */ { 0, BX_IA_ERROR },
  /* 0F 7A /dm */ { 0, BX_IA_ERROR },
  /* 0F 7B /dr */ { 0, BX_IA_ERROR },
  /* 0F 7B /dm */ { 0, BX_IA_ERROR },
  /* 0F 7C /dr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7c },
  /* 0F 7C /dm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7c },
  /* 0F 7D /dr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7d },
  /* 0F 7D /dm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7d },
  /* 0F 7E /dr */ { BxPrefixSSE, BX_IA_MOVD_EdPdR, BxOpcodeGroupSSE_0f7eR },
  /* 0F 7E /dm */ { BxPrefixSSE, BX_IA_MOVD_EdPdM, BxOpcodeGroupSSE_0f7eM },
  /* 0F 7F /dr */ { BxPrefixSSE, BX_IA_MOVQ_QqPqR, BxOpcodeGroupSSE_0f7fR },
  /* 0F 7F /dm */ { BxPrefixSSE, BX_IA_MOVQ_QqPqM, BxOpcodeGroupSSE_0f7fM },
  /* 0F 80 /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JO_Jq },
  /* 0F 80 /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JO_Jq },
  /* 0F 81 /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 0F 81 /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 0F 82 /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JB_Jq },
  /* 0F 82 /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JB_Jq },
  /* 0F 83 /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 0F 83 /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 0F 84 /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 0F 84 /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 0F 85 /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 0F 85 /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 0F 86 /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 0F 86 /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 0F 87 /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 0F 87 /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 0F 88 /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JS_Jq },
  /* 0F 88 /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JS_Jq },
  /* 0F 89 /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 0F 89 /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 0F 8A /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JP_Jq },
  /* 0F 8A /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JP_Jq },
  /* 0F 8B /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 0F 8B /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 0F 8C /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JL_Jq },
  /* 0F 8C /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JL_Jq },
  /* 0F 8D /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 0F 8D /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 0F 8E /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 0F 8E /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 0F 8F /dr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 0F 8F /dm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 0F 90 /dr */ { 0, BX_IA_SETO_EbR },
  /* 0F 90 /dm */ { 0, BX_IA_SETO_EbM },
  /* 0F 91 /dr */ { 0, BX_IA_SETNO_EbR },
  /* 0F 91 /dm */ { 0, BX_IA_SETNO_EbM },
  /* 0F 92 /dr */ { 0, BX_IA_SETB_EbR },
  /* 0F 92 /dm */ { 0, BX_IA_SETB_EbM },
  /* 0F 93 /dr */ { 0, BX_IA_SETNB_EbR },
  /* 0F 93 /dm */ { 0, BX_IA_SETNB_EbM },
  /* 0F 94 /dr */ { 0, BX_IA_SETZ_EbR },
  /* 0F 94 /dm */ { 0, BX_IA_SETZ_EbM },
  /* 0F 95 /dr */ { 0, BX_IA_SETNZ_EbR },
  /* 0F 95 /dm */ { 0, BX_IA_SETNZ_EbM },
  /* 0F 96 /dr */ { 0, BX_IA_SETBE_EbR },
  /* 0F 96 /dm */ { 0, BX_IA_SETBE_EbM },
  /* 0F 97 /dr */ { 0, BX_IA_SETNBE_EbR },
  /* 0F 97 /dm */ { 0, BX_IA_SETNBE_EbM },
  /* 0F 98 /dr */ { 0, BX_IA_SETS_EbR },
  /* 0F 98 /dm */ { 0, BX_IA_SETS_EbM },
  /* 0F 99 /dr */ { 0, BX_IA_SETNS_EbR },
  /* 0F 99 /dm */ { 0, BX_IA_SETNS_EbM },
  /* 0F 9A /dr */ { 0, BX_IA_SETP_EbR },
  /* 0F 9A /dm */ { 0, BX_IA_SETP_EbM },
  /* 0F 9B /dr */ { 0, BX_IA_SETNP_EbR },
  /* 0F 9B /dm */ { 0, BX_IA_SETNP_EbM },
  /* 0F 9C /dr */ { 0, BX_IA_SETL_EbR },
  /* 0F 9C /dm */ { 0, BX_IA_SETL_EbM },
  /* 0F 9D /dr */ { 0, BX_IA_SETNL_EbR },
  /* 0F 9D /dm */ { 0, BX_IA_SETNL_EbM },
  /* 0F 9E /dr */ { 0, BX_IA_SETLE_EbR },
  /* 0F 9E /dm */ { 0, BX_IA_SETLE_EbM },
  /* 0F 9F /dr */ { 0, BX_IA_SETNLE_EbR },
  /* 0F 9F /dm */ { 0, BX_IA_SETNLE_EbM },
  /* 0F A0 /dr */ { 0, BX_IA_PUSH64_FS },
  /* 0F A0 /dm */ { 0, BX_IA_PUSH64_FS },
  /* 0F A1 /dr */ { 0, BX_IA_POP64_FS },
  /* 0F A1 /dm */ { 0, BX_IA_POP64_FS },
  /* 0F A2 /dr */ { 0, BX_IA_CPUID },
  /* 0F A2 /dm */ { 0, BX_IA_CPUID },
  /* 0F A3 /dr */ { 0, BX_IA_BT_EdGdR },
  /* 0F A3 /dm */ { 0, BX_IA_BT_EdGdM },
  /* 0F A4 /dr */ { BxImmediate_Ib, BX_IA_SHLD_EdGdR },
  /* 0F A4 /dm */ { BxImmediate_Ib, BX_IA_SHLD_EdGdM },
  /* 0F A5 /dr */ { 0,              BX_IA_SHLD_EdGdR },
  /* 0F A5 /dm */ { 0,              BX_IA_SHLD_EdGdM },
  /* 0F A6 /dr */ { 0, BX_IA_ERROR },
  /* 0F A6 /dm */ { 0, BX_IA_ERROR },
  /* 0F A7 /dr */ { 0, BX_IA_ERROR },
  /* 0F A7 /dm */ { 0, BX_IA_ERROR },
  /* 0F A8 /dr */ { 0, BX_IA_PUSH64_GS },
  /* 0F A8 /dm */ { 0, BX_IA_PUSH64_GS },
  /* 0F A9 /dr */ { 0, BX_IA_POP64_GS },
  /* 0F A9 /dm */ { 0, BX_IA_POP64_GS },
  /* 0F AA /dr */ { BxTraceEnd, BX_IA_RSM },
  /* 0F AA /dm */ { BxTraceEnd, BX_IA_RSM },
  /* 0F AB /dr */ { 0, BX_IA_BTS_EdGdR },
  /* 0F AB /dm */ { BxLockable, BX_IA_BTS_EdGdM },
  /* 0F AC /dr */ { BxImmediate_Ib, BX_IA_SHRD_EdGdR },
  /* 0F AC /dm */ { BxImmediate_Ib, BX_IA_SHRD_EdGdM },
  /* 0F AD /dr */ { 0,              BX_IA_SHRD_EdGdR },
  /* 0F AD /dm */ { 0,              BX_IA_SHRD_EdGdM },
  /* 0F AE /dr */ { BxGroup15, BX_IA_ERROR, BxOpcodeInfoG15R },
  /* 0F AE /dm */ { BxGroup15, BX_IA_ERROR, BxOpcodeInfoG15M },
  /* 0F AF /dr */ { 0, BX_IA_IMUL_GdEdR },
  /* 0F AF /dm */ { 0, BX_IA_IMUL_GdEdM },
  /* 0F B0 /dr */ { 0, BX_IA_CMPXCHG_EbGbR },
  /* 0F B0 /dm */ { BxLockable, BX_IA_CMPXCHG_EbGbM },
  /* 0F B1 /dr */ { 0, BX_IA_CMPXCHG_EdGdR },
  /* 0F B1 /dm */ { BxLockable, BX_IA_CMPXCHG_EdGdM },
  /* 0F B2 /dr */ { 0, BX_IA_ERROR }, // LSS
  /* 0F B2 /dm */ { 0, BX_IA_LSS_GdMp },
  /* 0F B3 /dr */ { 0, BX_IA_BTR_EdGdR },
  /* 0F B3 /dm */ { BxLockable, BX_IA_BTR_EdGdM },
  /* 0F B4 /dr */ { 0, BX_IA_ERROR }, // LFS
  /* 0F B4 /dm */ { 0, BX_IA_LFS_GdMp },
  /* 0F B5 /dr */ { 0, BX_IA_ERROR }, // LFS
  /* 0F B5 /dm */ { 0, BX_IA_LGS_GdMp },
  /* 0F B6 /dr */ { 0, BX_IA_MOVZX_GdEbR },
  /* 0F B6 /dm */ { 0, BX_IA_MOVZX_GdEbM },
  /* 0F B7 /dr */ { 0, BX_IA_MOVZX_GdEwR },
  /* 0F B7 /dm */ { 0, BX_IA_MOVZX_GdEwM },
  /* 0F B8 /dr */ { BxPrefixSSEF3, BX_IA_POPCNT_GdEdR },
  /* 0F B8 /dm */ { BxPrefixSSEF3, BX_IA_POPCNT_GdEdM },
  /* 0F B9 /dr */ { BxTraceEnd, BX_IA_UD2B },
  /* 0F B9 /dm */ { BxTraceEnd, BX_IA_UD2B },
  /* 0F BA /dr */ { BxGroup8, BX_IA_ERROR, BxOpcodeInfoG8EdIbR },
  /* 0F BA /dm */ { BxGroup8, BX_IA_ERROR, BxOpcodeInfoG8EdIbM },
  /* 0F BB /dr */ { 0, BX_IA_BTC_EdGdR },
  /* 0F BB /dm */ { BxLockable, BX_IA_BTC_EdGdM },
  /* 0F BC /dr */ { 0, BX_IA_BSF_GdEdR },
  /* 0F BC /dm */ { 0, BX_IA_BSF_GdEdM },
  /* 0F BD /dr */ { 0, BX_IA_BSR_GdEdR },
  /* 0F BD /dm */ { 0, BX_IA_BSR_GdEdM },
  /* 0F BE /dr */ { 0, BX_IA_MOVSX_GdEbR },
  /* 0F BE /dm */ { 0, BX_IA_MOVSX_GdEbM },
  /* 0F BF /dr */ { 0, BX_IA_MOVSX_GdEwR },
  /* 0F BF /dm */ { 0, BX_IA_MOVSX_GdEwM },
  /* 0F C0 /dr */ { 0, BX_IA_XADD_EbGbR },
  /* 0F C0 /dm */ { BxLockable, BX_IA_XADD_EbGbM },
  /* 0F C1 /dr */ { 0, BX_IA_XADD_EdGdR },
  /* 0F C1 /dm */ { BxLockable, BX_IA_XADD_EdGdM },
  /* 0F C2 /dr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_CMPPS_VpsWpsIb, BxOpcodeGroupSSE_0fc2 },
  /* 0F C2 /dm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_CMPPS_VpsWpsIb, BxOpcodeGroupSSE_0fc2 },
  /* 0F C3 /dr */ { 0, BX_IA_ERROR }, // MOVNTI SSE group
  /* 0F C3 /dm */ { BxPrefixSSE, BX_IA_MOVNTI_MdGd, BxOpcodeGroupSSE_ERR },
  /* 0F C4 /dr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PINSRW_PqEwIb, BxOpcodeGroupSSE_0fc4 },
  /* 0F C4 /dm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PINSRW_PqEwIb, BxOpcodeGroupSSE_0fc4 },
  /* 0F C5 /dr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PEXTRW_GdPqIb, BxOpcodeGroupSSE_0fc5R },
  /* 0F C5 /dm */ { 0, BX_IA_ERROR }, // PEXTRW SSE group
  /* 0F C6 /dr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_SHUFPS_VpsWpsIb, BxOpcodeGroupSSE_0fc6 },
  /* 0F C6 /dm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_SHUFPS_VpsWpsIb, BxOpcodeGroupSSE_0fc6 },
  /* 0F C7 /dr */ { 0, BX_IA_ERROR }, // CMPXCHG8B G9 group
  /* 0F C7 /dm */ { BxGroup9, BX_IA_ERROR, BxOpcodeInfoG9M },
  /* 0F C8 /dr */ { 0, BX_IA_BSWAP_ERX },
  /* 0F C8 /dm */ { 0, BX_IA_BSWAP_ERX },
  /* 0F C9 /dr */ { 0, BX_IA_BSWAP_ERX },
  /* 0F C9 /dm */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CA /dr */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CA /dm */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CB /dr */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CB /dm */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CC /dr */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CC /dm */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CD /dr */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CD /dm */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CE /dr */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CE /dm */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CF /dr */ { 0, BX_IA_BSWAP_ERX },
  /* 0F CF /dm */ { 0, BX_IA_BSWAP_ERX },
  /* 0F D0 /dr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fd0 },
  /* 0F D0 /dm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fd0 },
  /* 0F D1 /dr */ { BxPrefixSSE, BX_IA_PSRLW_PqQq, BxOpcodeGroupSSE_0fd1 },
  /* 0F D1 /dm */ { BxPrefixSSE, BX_IA_PSRLW_PqQq, BxOpcodeGroupSSE_0fd1 },
  /* 0F D2 /dr */ { BxPrefixSSE, BX_IA_PSRLD_PqQq, BxOpcodeGroupSSE_0fd2 },
  /* 0F D2 /dm */ { BxPrefixSSE, BX_IA_PSRLD_PqQq, BxOpcodeGroupSSE_0fd2 },
  /* 0F D3 /dr */ { BxPrefixSSE, BX_IA_PSRLQ_PqQq, BxOpcodeGroupSSE_0fd3 },
  /* 0F D3 /dm */ { BxPrefixSSE, BX_IA_PSRLQ_PqQq, BxOpcodeGroupSSE_0fd3 },
  /* 0F D4 /dr */ { BxPrefixSSE, BX_IA_PADDQ_PqQq, BxOpcodeGroupSSE_0fd4 },
  /* 0F D4 /dm */ { BxPrefixSSE, BX_IA_PADDQ_PqQq, BxOpcodeGroupSSE_0fd4 },
  /* 0F D5 /dr */ { BxPrefixSSE, BX_IA_PMULLW_PqQq, BxOpcodeGroupSSE_0fd5 },
  /* 0F D5 /dm */ { BxPrefixSSE, BX_IA_PMULLW_PqQq, BxOpcodeGroupSSE_0fd5 },
  /* 0F D6 /dr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fd6R },
  /* 0F D6 /dm */ { BxPrefixSSE66, BX_IA_MOVQ_WqVqM },
  /* 0F D7 /dr */ { BxPrefixSSE, BX_IA_PMOVMSKB_GdPRq, BxOpcodeGroupSSE_0fd7 },
  /* 0F D7 /dm */ { 0, BX_IA_ERROR }, // PMOVMSKB is reg/reg form only
  /* 0F D8 /dr */ { BxPrefixSSE, BX_IA_PSUBUSB_PqQq, BxOpcodeGroupSSE_0fd8 },
  /* 0F D8 /dm */ { BxPrefixSSE, BX_IA_PSUBUSB_PqQq, BxOpcodeGroupSSE_0fd8 },
  /* 0F D9 /dr */ { BxPrefixSSE, BX_IA_PSUBUSW_PqQq, BxOpcodeGroupSSE_0fd9 },
  /* 0F D9 /dm */ { BxPrefixSSE, BX_IA_PSUBUSW_PqQq, BxOpcodeGroupSSE_0fd9 },
  /* 0F DA /dr */ { BxPrefixSSE, BX_IA_PMINUB_PqQq, BxOpcodeGroupSSE_0fda },
  /* 0F DA /dm */ { BxPrefixSSE, BX_IA_PMINUB_PqQq, BxOpcodeGroupSSE_0fda },
  /* 0F DB /dr */ { BxPrefixSSE, BX_IA_PAND_PqQq, BxOpcodeGroupSSE_0fdb },
  /* 0F DB /dm */ { BxPrefixSSE, BX_IA_PAND_PqQq, BxOpcodeGroupSSE_0fdb },
  /* 0F DC /dr */ { BxPrefixSSE, BX_IA_PADDUSB_PqQq, BxOpcodeGroupSSE_0fdc },
  /* 0F DC /dm */ { BxPrefixSSE, BX_IA_PADDUSB_PqQq, BxOpcodeGroupSSE_0fdc },
  /* 0F DD /dr */ { BxPrefixSSE, BX_IA_PADDUSW_PqQq, BxOpcodeGroupSSE_0fdd },
  /* 0F DD /dm */ { BxPrefixSSE, BX_IA_PADDUSW_PqQq, BxOpcodeGroupSSE_0fdd },
  /* 0F DE /dr */ { BxPrefixSSE, BX_IA_PMAXUB_PqQq, BxOpcodeGroupSSE_0fde },
  /* 0F DE /dm */ { BxPrefixSSE, BX_IA_PMAXUB_PqQq, BxOpcodeGroupSSE_0fde },
  /* 0F DF /dr */ { BxPrefixSSE, BX_IA_PANDN_PqQq, BxOpcodeGroupSSE_0fdf },
  /* 0F DF /dm */ { BxPrefixSSE, BX_IA_PANDN_PqQq, BxOpcodeGroupSSE_0fdf },
  /* 0F E0 /dr */ { BxPrefixSSE, BX_IA_PAVGB_PqQq, BxOpcodeGroupSSE_0fe0 },
  /* 0F E0 /dm */ { BxPrefixSSE, BX_IA_PAVGB_PqQq, BxOpcodeGroupSSE_0fe0 },
  /* 0F E1 /dr */ { BxPrefixSSE, BX_IA_PSRAW_PqQq, BxOpcodeGroupSSE_0fe1 },
  /* 0F E1 /dm */ { BxPrefixSSE, BX_IA_PSRAW_PqQq, BxOpcodeGroupSSE_0fe1 },
  /* 0F E2 /dr */ { BxPrefixSSE, BX_IA_PSRAD_PqQq, BxOpcodeGroupSSE_0fe2 },
  /* 0F E2 /dm */ { BxPrefixSSE, BX_IA_PSRAD_PqQq, BxOpcodeGroupSSE_0fe2 },
  /* 0F E3 /dr */ { BxPrefixSSE, BX_IA_PAVGW_PqQq, BxOpcodeGroupSSE_0fe3 },
  /* 0F E3 /dm */ { BxPrefixSSE, BX_IA_PAVGW_PqQq, BxOpcodeGroupSSE_0fe3 },
  /* 0F E4 /dr */ { BxPrefixSSE, BX_IA_PMULHUW_PqQq, BxOpcodeGroupSSE_0fe4 },
  /* 0F E4 /dm */ { BxPrefixSSE, BX_IA_PMULHUW_PqQq, BxOpcodeGroupSSE_0fe4 },
  /* 0F E5 /dr */ { BxPrefixSSE, BX_IA_PMULHW_PqQq, BxOpcodeGroupSSE_0fe5 },
  /* 0F E5 /dm */ { BxPrefixSSE, BX_IA_PMULHW_PqQq, BxOpcodeGroupSSE_0fe5 },
  /* 0F E6 /dr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fe6 },
  /* 0F E6 /dm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fe6 },
  /* 0F E7 /dr */ { 0, BX_IA_ERROR }, // MOVNTQ/MOVNTDQ SSE group
  /* 0F E7 /dm */ { BxPrefixSSE, BX_IA_MOVNTQ_MqPq, BxOpcodeGroupSSE_0fe7M },
  /* 0F E8 /dr */ { BxPrefixSSE, BX_IA_PSUBSB_PqQq, BxOpcodeGroupSSE_0fe8 },
  /* 0F E8 /dm */ { BxPrefixSSE, BX_IA_PSUBSB_PqQq, BxOpcodeGroupSSE_0fe8 },
  /* 0F E9 /dr */ { BxPrefixSSE, BX_IA_PSUBSW_PqQq, BxOpcodeGroupSSE_0fe9 },
  /* 0F E9 /dm */ { BxPrefixSSE, BX_IA_PSUBSW_PqQq, BxOpcodeGroupSSE_0fe9 },
  /* 0F EA /dr */ { BxPrefixSSE, BX_IA_PMINSW_PqQq, BxOpcodeGroupSSE_0fea },
  /* 0F EA /dm */ { BxPrefixSSE, BX_IA_PMINSW_PqQq, BxOpcodeGroupSSE_0fea },
  /* 0F EB /dr */ { BxPrefixSSE, BX_IA_POR_PqQq, BxOpcodeGroupSSE_0feb },
  /* 0F EB /dm */ { BxPrefixSSE, BX_IA_POR_PqQq, BxOpcodeGroupSSE_0feb },
  /* 0F EC /dr */ { BxPrefixSSE, BX_IA_PADDSB_PqQq, BxOpcodeGroupSSE_0fec },
  /* 0F EC /dm */ { BxPrefixSSE, BX_IA_PADDSB_PqQq, BxOpcodeGroupSSE_0fec },
  /* 0F ED /dr */ { BxPrefixSSE, BX_IA_PADDSW_PqQq, BxOpcodeGroupSSE_0fed },
  /* 0F ED /dm */ { BxPrefixSSE, BX_IA_PADDSW_PqQq, BxOpcodeGroupSSE_0fed },
  /* 0F EE /dr */ { BxPrefixSSE, BX_IA_PMAXSW_PqQq, BxOpcodeGroupSSE_0fee },
  /* 0F EE /dm */ { BxPrefixSSE, BX_IA_PMAXSW_PqQq, BxOpcodeGroupSSE_0fee },
  /* 0F EF /dr */ { BxPrefixSSE, BX_IA_PXOR_PqQq, BxOpcodeGroupSSE_0fef },
  /* 0F EF /dm */ { BxPrefixSSE, BX_IA_PXOR_PqQq, BxOpcodeGroupSSE_0fef },
  /* 0F F0 /dr */ { 0, BX_IA_ERROR }, // LDDQU SSE group
  /* 0F F0 /wm */ { BxPrefixSSEF2, BX_IA_LDDQU_VdqMdq },
  /* 0F F1 /dr */ { BxPrefixSSE, BX_IA_PSLLW_PqQq, BxOpcodeGroupSSE_0ff1 },
  /* 0F F1 /dm */ { BxPrefixSSE, BX_IA_PSLLW_PqQq, BxOpcodeGroupSSE_0ff1 },
  /* 0F F2 /dr */ { BxPrefixSSE, BX_IA_PSLLD_PqQq, BxOpcodeGroupSSE_0ff2 },
  /* 0F F2 /dm */ { BxPrefixSSE, BX_IA_PSLLD_PqQq, BxOpcodeGroupSSE_0ff2 },
  /* 0F F3 /dr */ { BxPrefixSSE, BX_IA_PSLLQ_PqQq, BxOpcodeGroupSSE_0ff3 },
  /* 0F F3 /dm */ { BxPrefixSSE, BX_IA_PSLLQ_PqQq, BxOpcodeGroupSSE_0ff3 },
  /* 0F F4 /dr */ { BxPrefixSSE, BX_IA_PMULUDQ_PqQq, BxOpcodeGroupSSE_0ff4 },
  /* 0F F4 /dm */ { BxPrefixSSE, BX_IA_PMULUDQ_PqQq, BxOpcodeGroupSSE_0ff4 },
  /* 0F F5 /dr */ { BxPrefixSSE, BX_IA_PMADDWD_PqQq, BxOpcodeGroupSSE_0ff5 },
  /* 0F F5 /dm */ { BxPrefixSSE, BX_IA_PMADDWD_PqQq, BxOpcodeGroupSSE_0ff5 },
  /* 0F F6 /dr */ { BxPrefixSSE, BX_IA_PSADBW_PqQq, BxOpcodeGroupSSE_0ff6 },
  /* 0F F6 /dm */ { BxPrefixSSE, BX_IA_PSADBW_PqQq, BxOpcodeGroupSSE_0ff6 },
  /* 0F F7 /dr */ { BxPrefixSSE, BX_IA_MASKMOVQ_PqPRq, BxOpcodeGroupSSE_0ff7R },
  /* 0F F7 /dm */ { 0, BX_IA_ERROR }, // MASKMOV SSE group
  /* 0F F8 /dr */ { BxPrefixSSE, BX_IA_PSUBB_PqQq, BxOpcodeGroupSSE_0ff8 },
  /* 0F F8 /dm */ { BxPrefixSSE, BX_IA_PSUBB_PqQq, BxOpcodeGroupSSE_0ff8 },
  /* 0F F9 /dr */ { BxPrefixSSE, BX_IA_PSUBW_PqQq, BxOpcodeGroupSSE_0ff9 },
  /* 0F F9 /dm */ { BxPrefixSSE, BX_IA_PSUBW_PqQq, BxOpcodeGroupSSE_0ff9 },
  /* 0F FA /dr */ { BxPrefixSSE, BX_IA_PSUBD_PqQq, BxOpcodeGroupSSE_0ffa },
  /* 0F FA /dm */ { BxPrefixSSE, BX_IA_PSUBD_PqQq, BxOpcodeGroupSSE_0ffa },
  /* 0F FB /dr */ { BxPrefixSSE, BX_IA_PSUBQ_PqQq, BxOpcodeGroupSSE_0ffb },
  /* 0F FB /dm */ { BxPrefixSSE, BX_IA_PSUBQ_PqQq, BxOpcodeGroupSSE_0ffb },
  /* 0F FC /dr */ { BxPrefixSSE, BX_IA_PADDB_PqQq, BxOpcodeGroupSSE_0ffc },
  /* 0F FC /dm */ { BxPrefixSSE, BX_IA_PADDB_PqQq, BxOpcodeGroupSSE_0ffc },
  /* 0F FD /dr */ { BxPrefixSSE, BX_IA_PADDW_PqQq, BxOpcodeGroupSSE_0ffd },
  /* 0F FD /dm */ { BxPrefixSSE, BX_IA_PADDW_PqQq, BxOpcodeGroupSSE_0ffd },
  /* 0F FE /dr */ { BxPrefixSSE, BX_IA_PADDD_PqQq, BxOpcodeGroupSSE_0ffe },
  /* 0F FE /dm */ { BxPrefixSSE, BX_IA_PADDD_PqQq, BxOpcodeGroupSSE_0ffe },
  /* 0F FF /dr */ { 0, BX_IA_ERROR },
  /* 0F FF /dm */ { 0, BX_IA_ERROR },

  // 512 (/r) + 512 (/m) entries for 64bit mode
  /* 00 /qr */ { BxArithDstRM, BX_IA_ADD_GbEbR },
  /* 00 /qm */ { BxLockable, BX_IA_ADD_EbGbM },
  /* 01 /qr */ { BxArithDstRM, BX_IA_ADD_GqEqR },
  /* 01 /qm */ { BxLockable, BX_IA_ADD_EqGqM },
  /* 02 /qr */ { 0, BX_IA_ADD_GbEbR },
  /* 02 /qm */ { 0, BX_IA_ADD_GbEbM },
  /* 03 /qr */ { 0, BX_IA_ADD_GqEqR },
  /* 03 /qm */ { 0, BX_IA_ADD_GqEqM },
  /* 04 /qr */ { BxImmediate_Ib, BX_IA_ADD_ALIb },
  /* 04 /qm */ { BxImmediate_Ib, BX_IA_ADD_ALIb },
  /* 05 /qr */ { BxImmediate_Id, BX_IA_ADD_RAXId },
  /* 05 /qm */ { BxImmediate_Id, BX_IA_ADD_RAXId },
  /* 06 /qr */ { 0, BX_IA_ERROR },
  /* 06 /qm */ { 0, BX_IA_ERROR },
  /* 07 /qr */ { 0, BX_IA_ERROR },
  /* 07 /qm */ { 0, BX_IA_ERROR },
  /* 08 /qr */ { BxArithDstRM, BX_IA_OR_GbEbR },
  /* 08 /qm */ { BxLockable, BX_IA_OR_EbGbM },
  /* 09 /qr */ { BxArithDstRM, BX_IA_OR_GqEqR },
  /* 09 /qm */ { BxLockable, BX_IA_OR_EqGqM },
  /* 0A /qr */ { 0, BX_IA_OR_GbEbR },
  /* 0A /qm */ { 0, BX_IA_OR_GbEbM },
  /* 0B /qr */ { 0, BX_IA_OR_GqEqR },
  /* 0B /qm */ { 0, BX_IA_OR_GqEqM },
  /* 0C /qr */ { BxImmediate_Ib, BX_IA_OR_ALIb },
  /* 0C /qm */ { BxImmediate_Ib, BX_IA_OR_ALIb },
  /* 0D /qr */ { BxImmediate_Id, BX_IA_OR_RAXId },
  /* 0D /qm */ { BxImmediate_Id, BX_IA_OR_RAXId },
  /* 0E /qr */ { 0, BX_IA_ERROR },
  /* 0E /qm */ { 0, BX_IA_ERROR },
  /* 0F /qr */ { 0, BX_IA_ERROR }, // 2-byte escape
  /* 0F /qm */ { 0, BX_IA_ERROR }, // 2-byte escape
  /* 10 /qr */ { BxArithDstRM, BX_IA_ADC_GbEbR },
  /* 10 /qm */ { BxLockable, BX_IA_ADC_EbGbM },
  /* 11 /qr */ { BxArithDstRM, BX_IA_ADC_GqEqR },
  /* 11 /qm */ { BxLockable, BX_IA_ADC_EqGqM },
  /* 12 /qr */ { 0, BX_IA_ADC_GbEbR },
  /* 12 /qm */ { 0, BX_IA_ADC_GbEbM },
  /* 13 /qr */ { 0, BX_IA_ADC_GqEqR },
  /* 13 /qm */ { 0, BX_IA_ADC_GqEqM },
  /* 14 /qr */ { BxImmediate_Ib, BX_IA_ADC_ALIb },
  /* 14 /qm */ { BxImmediate_Ib, BX_IA_ADC_ALIb },
  /* 15 /qr */ { BxImmediate_Id, BX_IA_ADC_RAXId },
  /* 15 /qm */ { BxImmediate_Id, BX_IA_ADC_RAXId },
  /* 16 /qr */ { 0, BX_IA_ERROR },
  /* 16 /qm */ { 0, BX_IA_ERROR },
  /* 17 /qr */ { 0, BX_IA_ERROR },
  /* 17 /qm */ { 0, BX_IA_ERROR },
  /* 18 /qr */ { BxArithDstRM, BX_IA_SBB_GbEbR },
  /* 18 /qm */ { BxLockable, BX_IA_SBB_EbGbM },
  /* 19 /qr */ { BxArithDstRM, BX_IA_SBB_GqEqR },
  /* 19 /qm */ { BxLockable, BX_IA_SBB_EqGqM },
  /* 1A /qr */ { 0, BX_IA_SBB_GbEbR },
  /* 1A /qm */ { 0, BX_IA_SBB_GbEbM },
  /* 1B /qr */ { 0, BX_IA_SBB_GqEqR },
  /* 1B /qm */ { 0, BX_IA_SBB_GqEqM },
  /* 1C /qr */ { BxImmediate_Ib, BX_IA_SBB_ALIb },
  /* 1C /qm */ { BxImmediate_Ib, BX_IA_SBB_ALIb },
  /* 1D /qr */ { BxImmediate_Id, BX_IA_SBB_RAXId },
  /* 1D /qm */ { BxImmediate_Id, BX_IA_SBB_RAXId },
  /* 1E /qr */ { 0, BX_IA_ERROR },
  /* 1E /qm */ { 0, BX_IA_ERROR },
  /* 1F /qr */ { 0, BX_IA_ERROR },
  /* 1F /qm */ { 0, BX_IA_ERROR },
  /* 20 /qr */ { BxArithDstRM, BX_IA_AND_GbEbR },
  /* 20 /qm */ { BxLockable, BX_IA_AND_EbGbM },
  /* 21 /qr */ { BxArithDstRM, BX_IA_AND_GqEqR },
  /* 21 /qm */ { BxLockable, BX_IA_AND_EqGqM },
  /* 22 /qr */ { 0, BX_IA_AND_GbEbR },
  /* 22 /qm */ { 0, BX_IA_AND_GbEbM },
  /* 23 /qr */ { 0, BX_IA_AND_GqEqR },
  /* 23 /qm */ { 0, BX_IA_AND_GqEqM },
  /* 24 /qr */ { BxImmediate_Ib, BX_IA_AND_ALIb },
  /* 24 /qm */ { BxImmediate_Ib, BX_IA_AND_ALIb },
  /* 25 /qr */ { BxImmediate_Id, BX_IA_AND_RAXId },
  /* 25 /qm */ { BxImmediate_Id, BX_IA_AND_RAXId },
  /* 26 /qr */ { 0, BX_IA_ERROR }, // ES:
  /* 26 /qm */ { 0, BX_IA_ERROR }, // ES:
  /* 27 /qr */ { 0, BX_IA_ERROR },
  /* 27 /qm */ { 0, BX_IA_ERROR },
  /* 28 /qr */ { BxArithDstRM, BX_IA_SUB_GbEbR },
  /* 28 /qm */ { BxLockable, BX_IA_SUB_EbGbM },
  /* 29 /qr */ { BxArithDstRM, BX_IA_SUB_GqEqR },
  /* 29 /qm */ { BxLockable, BX_IA_SUB_EqGqM },
  /* 2A /qr */ { 0, BX_IA_SUB_GbEbR },
  /* 2A /qm */ { 0, BX_IA_SUB_GbEbM },
  /* 2B /qr */ { 0, BX_IA_SUB_GqEqR },
  /* 2B /qm */ { 0, BX_IA_SUB_GqEqM },
  /* 2C /qr */ { BxImmediate_Ib, BX_IA_SUB_ALIb },
  /* 2C /qm */ { BxImmediate_Ib, BX_IA_SUB_ALIb },
  /* 2D /qr */ { BxImmediate_Id, BX_IA_SUB_RAXId },
  /* 2D /qm */ { BxImmediate_Id, BX_IA_SUB_RAXId },
  /* 2E /qr */ { 0, BX_IA_ERROR }, // CS:
  /* 2E /qm */ { 0, BX_IA_ERROR }, // CS:
  /* 2F /qr */ { 0, BX_IA_ERROR },
  /* 2F /qm */ { 0, BX_IA_ERROR },
  /* 30 /qr */ { BxArithDstRM, BX_IA_XOR_GbEbR },
  /* 30 /qm */ { BxLockable, BX_IA_XOR_EbGbM },
  /* 31 /qr */ { BxArithDstRM, BX_IA_XOR_GqEqR },
  /* 31 /qm */ { BxLockable, BX_IA_XOR_EqGqM },
  /* 32 /qr */ { 0, BX_IA_XOR_GbEbR },
  /* 32 /qm */ { 0, BX_IA_XOR_GbEbM },
  /* 33 /qr */ { 0, BX_IA_XOR_GqEqR },
  /* 33 /qm */ { 0, BX_IA_XOR_GqEqM },
  /* 34 /qr */ { BxImmediate_Ib, BX_IA_XOR_ALIb },
  /* 34 /qm */ { BxImmediate_Ib, BX_IA_XOR_ALIb },
  /* 35 /qr */ { BxImmediate_Id, BX_IA_XOR_RAXId },
  /* 35 /qm */ { BxImmediate_Id, BX_IA_XOR_RAXId },
  /* 36 /qr */ { 0, BX_IA_ERROR }, // SS:
  /* 36 /qm */ { 0, BX_IA_ERROR }, // SS:
  /* 37 /qr */ { 0, BX_IA_ERROR },
  /* 37 /qm */ { 0, BX_IA_ERROR },
  /* 38 /qr */ { BxArithDstRM, BX_IA_CMP_GbEbR },
  /* 38 /qm */ { 0, BX_IA_CMP_EbGbM },
  /* 39 /qr */ { BxArithDstRM, BX_IA_CMP_GqEqR },
  /* 39 /qm */ { 0, BX_IA_CMP_EqGqM },
  /* 3A /qr */ { 0, BX_IA_CMP_GbEbR },
  /* 3A /qm */ { 0, BX_IA_CMP_GbEbM },
  /* 3B /qr */ { 0, BX_IA_CMP_GqEqR },
  /* 3B /qm */ { 0, BX_IA_CMP_GqEqM },
  /* 3C /qr */ { BxImmediate_Ib, BX_IA_CMP_ALIb },
  /* 3C /qm */ { BxImmediate_Ib, BX_IA_CMP_ALIb },
  /* 3D /qr */ { BxImmediate_Id, BX_IA_CMP_RAXId },
  /* 3D /qm */ { BxImmediate_Id, BX_IA_CMP_RAXId },
  /* 3E /qr */ { 0, BX_IA_ERROR }, // DS:
  /* 3E /qm */ { 0, BX_IA_ERROR }, // DS:
  /* 3F /qr */ { 0, BX_IA_ERROR },
  /* 3F /qm */ { 0, BX_IA_ERROR },
  /* 40 /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 40 /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 41 /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 41 /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 42 /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 42 /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 43 /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 43 /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 44 /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 44 /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 45 /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 45 /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 46 /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 46 /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 47 /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 47 /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 48 /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 48 /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 49 /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 49 /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 4A /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 4A /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 4B /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 4B /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 4C /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 4C /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 4D /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 4D /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 4E /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 4E /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 4F /qr */ { 0, BX_IA_ERROR }, // REX:
  /* 4F /qm */ { 0, BX_IA_ERROR }, // REX:
  /* 50 /qr */ { 0, BX_IA_PUSH_RRX },
  /* 50 /qm */ { 0, BX_IA_PUSH_RRX },
  /* 51 /qr */ { 0, BX_IA_PUSH_RRX },
  /* 51 /qm */ { 0, BX_IA_PUSH_RRX },
  /* 52 /qr */ { 0, BX_IA_PUSH_RRX },
  /* 52 /qm */ { 0, BX_IA_PUSH_RRX },
  /* 53 /qr */ { 0, BX_IA_PUSH_RRX },
  /* 53 /qm */ { 0, BX_IA_PUSH_RRX },
  /* 54 /qr */ { 0, BX_IA_PUSH_RRX },
  /* 54 /qm */ { 0, BX_IA_PUSH_RRX },
  /* 55 /qr */ { 0, BX_IA_PUSH_RRX },
  /* 55 /qm */ { 0, BX_IA_PUSH_RRX },
  /* 56 /qr */ { 0, BX_IA_PUSH_RRX },
  /* 56 /qm */ { 0, BX_IA_PUSH_RRX },
  /* 57 /qr */ { 0, BX_IA_PUSH_RRX },
  /* 57 /qm */ { 0, BX_IA_PUSH_RRX },
  /* 58 /qr */ { 0, BX_IA_POP_RRX },
  /* 58 /qm */ { 0, BX_IA_POP_RRX },
  /* 59 /qr */ { 0, BX_IA_POP_RRX },
  /* 59 /qm */ { 0, BX_IA_POP_RRX },
  /* 5A /qr */ { 0, BX_IA_POP_RRX },
  /* 5A /qm */ { 0, BX_IA_POP_RRX },
  /* 5B /qr */ { 0, BX_IA_POP_RRX },
  /* 5B /qm */ { 0, BX_IA_POP_RRX },
  /* 5C /qr */ { 0, BX_IA_POP_RRX },
  /* 5C /qm */ { 0, BX_IA_POP_RRX },
  /* 5D /qr */ { 0, BX_IA_POP_RRX },
  /* 5D /qm */ { 0, BX_IA_POP_RRX },
  /* 5E /qr */ { 0, BX_IA_POP_RRX },
  /* 5E /qm */ { 0, BX_IA_POP_RRX },
  /* 5F /qr */ { 0, BX_IA_POP_RRX },
  /* 5F /qm */ { 0, BX_IA_POP_RRX },
  /* 60 /qr */ { 0, BX_IA_ERROR },
  /* 60 /qm */ { 0, BX_IA_ERROR },
  /* 61 /qr */ { 0, BX_IA_ERROR },
  /* 61 /qm */ { 0, BX_IA_ERROR },
  /* 62 /qr */ { 0, BX_IA_ERROR },
  /* 62 /qm */ { 0, BX_IA_ERROR },
  /* 63 /qr */ { 0, BX_IA_MOVSX_GqEdR },
  /* 63 /qm */ { 0, BX_IA_MOVSX_GqEdM },
  /* 64 /qr */ { 0, BX_IA_ERROR }, // FS:
  /* 64 /qm */ { 0, BX_IA_ERROR }, // FS:
  /* 65 /qr */ { 0, BX_IA_ERROR }, // GS:
  /* 65 /qm */ { 0, BX_IA_ERROR }, // GS:
  /* 66 /qr */ { 0, BX_IA_ERROR }, // OS:
  /* 66 /qm */ { 0, BX_IA_ERROR }, // OS:
  /* 67 /qr */ { 0, BX_IA_ERROR }, // AS:
  /* 67 /qm */ { 0, BX_IA_ERROR }, // AS:
  /* 68 /qr */ { BxImmediate_Id, BX_IA_PUSH64_Id },
  /* 68 /qm */ { BxImmediate_Id, BX_IA_PUSH64_Id },
  /* 69 /qr */ { BxImmediate_Id, BX_IA_IMUL_GqEqIdR },
  /* 69 /qm */ { BxImmediate_Id, BX_IA_IMUL_GqEqIdM },
  /* 6A /qr */ { BxImmediate_Ib_SE, BX_IA_PUSH64_Id },
  /* 6A /qm */ { BxImmediate_Ib_SE, BX_IA_PUSH64_Id },
  /* 6B /qr */ { BxImmediate_Ib_SE, BX_IA_IMUL_GqEqIdR },
  /* 6B /qm */ { BxImmediate_Ib_SE, BX_IA_IMUL_GqEqIdM },
  /* 6C /qr */ { 0, BX_IA_REP_INSB_YbDX },
  /* 6C /qm */ { 0, BX_IA_REP_INSB_YbDX },
  /* 6D /qr */ { 0, BX_IA_REP_INSD_YdDX },
  /* 6D /qm */ { 0, BX_IA_REP_INSD_YdDX },
  /* 6E /qr */ { 0, BX_IA_REP_OUTSB_DXXb },
  /* 6E /qm */ { 0, BX_IA_REP_OUTSB_DXXb },
  /* 6F /qr */ { 0, BX_IA_REP_OUTSD_DXXd },
  /* 6F /qm */ { 0, BX_IA_REP_OUTSD_DXXd },
  /* 70 /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JO_Jq },
  /* 70 /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JO_Jq },
  /* 71 /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 71 /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 72 /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JB_Jq },
  /* 72 /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JB_Jq },
  /* 73 /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 73 /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 74 /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 74 /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 75 /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 75 /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 76 /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 76 /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 77 /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 77 /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 78 /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JS_Jq },
  /* 78 /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JS_Jq },
  /* 79 /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 79 /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 7A /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JP_Jq },
  /* 7A /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JP_Jq },
  /* 7B /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 7B /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 7C /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JL_Jq },
  /* 7C /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JL_Jq },
  /* 7D /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 7D /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 7E /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 7E /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 7F /qr */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 7F /qm */ { BxImmediate_BrOff8 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 80 /qr */ { BxGroup1 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG1EbIbR },
  /* 80 /qm */ { BxGroup1 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG1EbIbM },
  /* 81 /qr */ { BxGroup1 | BxImmediate_Id, BX_IA_ERROR, BxOpcodeInfo64G1EqR },
  /* 81 /qm */ { BxGroup1 | BxImmediate_Id, BX_IA_ERROR, BxOpcodeInfo64G1EqM },
  /* 82 /qr */ { 0, BX_IA_ERROR },
  /* 82 /qm */ { 0, BX_IA_ERROR },
  /* 83 /qr */ { BxGroup1 | BxImmediate_Ib_SE, BX_IA_ERROR, BxOpcodeInfo64G1EqR },
  /* 83 /qm */ { BxGroup1 | BxImmediate_Ib_SE, BX_IA_ERROR, BxOpcodeInfo64G1EqM },
  /* 84 /qr */ { 0, BX_IA_TEST_EbGbR },
  /* 84 /qm */ { 0, BX_IA_TEST_EbGbM },
  /* 85 /qr */ { 0, BX_IA_TEST_EqGqR },
  /* 85 /qm */ { 0, BX_IA_TEST_EqGqM },
  /* 86 /qr */ { 0, BX_IA_XCHG_EbGbR },
  /* 86 /qm */ { BxLockable, BX_IA_XCHG_EbGbM },
  /* 87 /qr */ { 0, BX_IA_XCHG_EqGqR },
  /* 87 /qm */ { BxLockable, BX_IA_XCHG_EqGqM },
  /* 88 /qr */ { BxArithDstRM, BX_IA_MOV_GbEbR },
  /* 88 /qm */ { 0, BX_IA_MOV_EbGbM },
  /* 89 /qr */ { BxArithDstRM, BX_IA_MOV_GqEqR },
  /* 89 /qm */ { 0, BX_IA_MOV_EqGqM },
  /* 8A /qr */ { 0, BX_IA_MOV_GbEbR },
  /* 8A /qm */ { 0, BX_IA_MOV_GbEbM },
  /* 8B /qr */ { 0, BX_IA_MOV_GqEqR },
  /* 8B /qm */ { 0, BX_IA_MOV_GqEqM },
  /* 8C /qr */ { 0, BX_IA_MOV_EwSwR },
  /* 8C /qm */ { 0, BX_IA_MOV_EwSwM },
  /* 8D /qr */ { 0, BX_IA_ERROR },       // LEA
  /* 8D /qm */ { 0, BX_IA_LEA_GqM },
  /* 8E /qr */ { 0, BX_IA_MOV_SwEw },
  /* 8E /qm */ { 0, BX_IA_MOV_SwEw },
  /* 8F /qr */ { BxGroup1A, BX_IA_ERROR, BxOpcodeInfo64G1AEqR },
  /* 8F /qm */ { BxGroup1A, BX_IA_ERROR, BxOpcodeInfo64G1AEqM },
  /* 90 /qr */ { 0, BX_IA_XCHG_RRXRAX }, // handles XCHG R8, RAX
  /* 90 /qm */ { 0, BX_IA_XCHG_RRXRAX }, // handles XCHG R8, RAX
  /* 91 /qr */ { 0, BX_IA_XCHG_RRXRAX },
  /* 91 /qm */ { 0, BX_IA_XCHG_RRXRAX },
  /* 92 /qr */ { 0, BX_IA_XCHG_RRXRAX },
  /* 92 /qm */ { 0, BX_IA_XCHG_RRXRAX },
  /* 93 /qr */ { 0, BX_IA_XCHG_RRXRAX },
  /* 93 /qm */ { 0, BX_IA_XCHG_RRXRAX },
  /* 94 /qr */ { 0, BX_IA_XCHG_RRXRAX },
  /* 94 /qm */ { 0, BX_IA_XCHG_RRXRAX },
  /* 95 /qr */ { 0, BX_IA_XCHG_RRXRAX },
  /* 95 /qm */ { 0, BX_IA_XCHG_RRXRAX },
  /* 96 /qr */ { 0, BX_IA_XCHG_RRXRAX },
  /* 96 /qm */ { 0, BX_IA_XCHG_RRXRAX },
  /* 97 /qr */ { 0, BX_IA_XCHG_RRXRAX },
  /* 97 /qm */ { 0, BX_IA_XCHG_RRXRAX },
  /* 98 /qr */ { 0, BX_IA_CDQE },
  /* 98 /qm */ { 0, BX_IA_CDQE },
  /* 99 /qr */ { 0, BX_IA_CQO },
  /* 99 /qm */ { 0, BX_IA_CQO },
  /* 9A /qr */ { 0, BX_IA_ERROR },
  /* 9A /qm */ { 0, BX_IA_ERROR },
  /* 9B /qr */ { 0, BX_IA_FWAIT },
  /* 9B /qm */ { 0, BX_IA_FWAIT },
  /* 9C /qr */ { 0, BX_IA_PUSHF_Fq },
  /* 9C /qm */ { 0, BX_IA_PUSHF_Fq },
  /* 9D /qr */ { 0, BX_IA_POPF_Fq },
  /* 9D /qm */ { 0, BX_IA_POPF_Fq },
  /* 9E /qr */ { 0, BX_IA_SAHF },
  /* 9E /qm */ { 0, BX_IA_SAHF },
  /* 9F /qr */ { 0, BX_IA_LAHF },
  /* 9F /qm */ { 0, BX_IA_LAHF },
  /* A0 /qr */ { BxImmediate_O, BX_IA_MOV_ALOq },
  /* A0 /qm */ { BxImmediate_O, BX_IA_MOV_ALOq },
  /* A1 /qr */ { BxImmediate_O, BX_IA_MOV_RAXOq },
  /* A1 /qm */ { BxImmediate_O, BX_IA_MOV_RAXOq },
  /* A2 /qr */ { BxImmediate_O, BX_IA_MOV_OqAL },
  /* A2 /qm */ { BxImmediate_O, BX_IA_MOV_OqAL },
  /* A3 /qr */ { BxImmediate_O, BX_IA_MOV_OqRAX },
  /* A3 /qm */ { BxImmediate_O, BX_IA_MOV_OqRAX },
  /* A4 /qr */ { 0, BX_IA_REP_MOVSB_XbYb },
  /* A4 /qm */ { 0, BX_IA_REP_MOVSB_XbYb },
  /* A5 /qr */ { 0, BX_IA_REP_MOVSQ_XqYq },
  /* A5 /qm */ { 0, BX_IA_REP_MOVSQ_XqYq },
  /* A6 /qr */ { 0, BX_IA_REP_CMPSB_XbYb },
  /* A6 /qm */ { 0, BX_IA_REP_CMPSB_XbYb },
  /* A7 /qr */ { 0, BX_IA_REP_CMPSQ_XqYq },
  /* A7 /qm */ { 0, BX_IA_REP_CMPSQ_XqYq },
  /* A8 /qr */ { BxImmediate_Ib, BX_IA_TEST_ALIb },
  /* A8 /qm */ { BxImmediate_Ib, BX_IA_TEST_ALIb },
  /* A9 /qr */ { BxImmediate_Id, BX_IA_TEST_RAXId },
  /* A9 /qm */ { BxImmediate_Id, BX_IA_TEST_RAXId },
  /* AA /qr */ { 0, BX_IA_REP_STOSB_YbAL },
  /* AA /qm */ { 0, BX_IA_REP_STOSB_YbAL },
  /* AB /qr */ { 0, BX_IA_REP_STOSQ_YqRAX },
  /* AB /qm */ { 0, BX_IA_REP_STOSQ_YqRAX },
  /* AC /qr */ { 0, BX_IA_REP_LODSB_ALXb },
  /* AC /qm */ { 0, BX_IA_REP_LODSB_ALXb },
  /* AD /qr */ { 0, BX_IA_REP_LODSQ_RAXXq },
  /* AD /qm */ { 0, BX_IA_REP_LODSQ_RAXXq },
  /* AE /qr */ { 0, BX_IA_REP_SCASB_ALXb  },
  /* AE /qm */ { 0, BX_IA_REP_SCASB_ALXb  },
  /* AF /qr */ { 0, BX_IA_REP_SCASQ_RAXXq },
  /* AF /qm */ { 0, BX_IA_REP_SCASQ_RAXXq },
  /* B0 /qr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B0 /qm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B1 /qr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B1 /qm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B2 /qr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B2 /qm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B3 /qr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B3 /qm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B4 /qr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B4 /qm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B5 /qr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B5 /qm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B6 /qr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B6 /qm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B7 /qr */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B7 /qm */ { BxImmediate_Ib, BX_IA_MOV_RLIb },
  /* B8 /qr */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* B8 /qm */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* B9 /qr */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* B9 /qm */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BA /qr */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BA /qm */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BB /qr */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BB /qm */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BC /qr */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BC /qm */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BD /qr */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BD /qm */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BE /qr */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BE /qm */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BF /qr */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* BF /qm */ { BxImmediate_Iq, BX_IA_MOV_RRXIq },
  /* C0 /qr */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG2EbR },
  /* C0 /qm */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfoG2EbM },
  /* C1 /qr */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfo64G2EqR },
  /* C1 /qm */ { BxGroup2 | BxImmediate_Ib, BX_IA_ERROR, BxOpcodeInfo64G2EqM },
  /* C2 /qr */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETnear64_Iw },
  /* C2 /qm */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETnear64_Iw },
  /* C3 /qr */ { BxTraceEnd,                  BX_IA_RETnear64 },
  /* C3 /qm */ { BxTraceEnd,                  BX_IA_RETnear64 },
  /* C4 /qr */ { 0, BX_IA_ERROR },
  /* C4 /qm */ { 0, BX_IA_ERROR },
  /* C5 /qr */ { 0, BX_IA_ERROR },
  /* C5 /qm */ { 0, BX_IA_ERROR },
  /* C6 /qr */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfoG11EbR },
  /* C6 /qm */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfoG11EbM },
  /* C7 /qr */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfo64G11EqR },
  /* C7 /qm */ { BxGroup11, BX_IA_ERROR, BxOpcodeInfo64G11EqM },
  /* C8 /qr */ { BxImmediate_IwIb, BX_IA_ENTER64_IwIb },
  /* C8 /qm */ { BxImmediate_IwIb, BX_IA_ENTER64_IwIb },
  /* C9 /qr */ { 0, BX_IA_LEAVE64 },
  /* C9 /qm */ { 0, BX_IA_LEAVE64 },
  /* CA /qr */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETfar64_Iw },
  /* CA /qm */ { BxImmediate_Iw | BxTraceEnd, BX_IA_RETfar64_Iw },
  /* CB /qr */ { BxTraceEnd,                  BX_IA_RETfar64 },
  /* CB /qm */ { BxTraceEnd,                  BX_IA_RETfar64 },
  /* CC /qr */ { BxTraceEnd, BX_IA_INT3 },
  /* CC /qm */ { BxTraceEnd, BX_IA_INT3 },
  /* CD /qr */ { BxImmediate_Ib | BxTraceEnd, BX_IA_INT_Ib },
  /* CD /qm */ { BxImmediate_Ib | BxTraceEnd, BX_IA_INT_Ib },
  /* CE /qr */ { 0, BX_IA_ERROR },
  /* CE /qm */ { 0, BX_IA_ERROR },
  /* CF /qr */ { BxTraceEnd, BX_IA_IRET64 },
  /* CF /qm */ { BxTraceEnd, BX_IA_IRET64 },
  /* D0 /qr */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfoG2EbR },
  /* D0 /qm */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfoG2EbM },
  /* D1 /qr */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfo64G2EqR },
  /* D1 /qm */ { BxGroup2 | BxImmediate_I1, BX_IA_ERROR, BxOpcodeInfo64G2EqM },
  /* D2 /qr */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfoG2EbR },
  /* D2 /qm */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfoG2EbM },
  /* D3 /qr */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfo64G2EqR },
  /* D3 /qm */ { BxGroup2, BX_IA_ERROR, BxOpcodeInfo64G2EqM },
  /* D4 /qr */ { 0, BX_IA_ERROR },
  /* D4 /qm */ { 0, BX_IA_ERROR },
  /* D5 /qr */ { 0, BX_IA_ERROR },
  /* D5 /qm */ { 0, BX_IA_ERROR },
  /* D6 /qr */ { 0, BX_IA_ERROR },
  /* D6 /qm */ { 0, BX_IA_ERROR },
  /* D7 /qr */ { 0, BX_IA_XLAT },
  /* D7 /qm */ { 0, BX_IA_XLAT },
  /* D8 /qr */ { BxGroupN, BX_IA_ERROR, BxOpcodeInfo_FloatingPointD8 },
  /* D8 /qm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupD8 },
  /* D9 /qr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointD9 },
  /* D9 /qm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupD9 },
  /* DA /qr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDA },
  /* DA /qm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDA },
  /* DB /qr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDB },
  /* DB /qm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDB },
  /* DC /qr */ { BxGroupN, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDC },
  /* DC /qm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDC },
  /* DD /qr */ { BxGroupN, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDD },
  /* DD /qm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDD },
  /* DE /qr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDE },
  /* DE /qm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDE },
  /* DF /qr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfo_FloatingPointDF },
  /* DF /qm */ { BxGroupFP, BX_IA_ERROR, BxOpcodeInfo_FPGroupDF },
  /* E0 /qr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPNE64_Jb },
  /* E0 /qm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPNE64_Jb },
  /* E1 /qr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPE64_Jb },
  /* E1 /qm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOPE64_Jb },
  /* E2 /qr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOP64_Jb },
  /* E2 /qm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_LOOP64_Jb },
  /* E3 /qr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JRCXZ_Jb },
  /* E3 /qm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JRCXZ_Jb },
  /* E4 /qr */ { BxImmediate_Ib, BX_IA_IN_ALIb },
  /* E4 /qm */ { BxImmediate_Ib, BX_IA_IN_ALIb },
  /* E5 /qr */ { BxImmediate_Ib, BX_IA_IN_EAXIb },
  /* E5 /qm */ { BxImmediate_Ib, BX_IA_IN_EAXIb },
  /* E6 /qr */ { BxImmediate_Ib, BX_IA_OUT_IbAL },
  /* E6 /qm */ { BxImmediate_Ib, BX_IA_OUT_IbAL },
  /* E7 /qr */ { BxImmediate_Ib, BX_IA_OUT_IbEAX },
  /* E7 /qm */ { BxImmediate_Ib, BX_IA_OUT_IbEAX },
  /* E8 /qr */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_CALL_Jq },
  /* E8 /qm */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_CALL_Jq },
  /* E9 /qr */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_JMP_Jq },
  /* E9 /qm */ { BxImmediate_BrOff32 | BxTraceEnd, BX_IA_JMP_Jq },
  /* EA /qr */ { 0, BX_IA_ERROR },
  /* EA /qm */ { 0, BX_IA_ERROR },
  /* EB /qr */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JMP_Jq },
  /* EB /qm */ { BxImmediate_BrOff8 | BxTraceEnd, BX_IA_JMP_Jq },
  /* EC /qr */ { 0, BX_IA_IN_ALDX },
  /* EC /qm */ { 0, BX_IA_IN_ALDX },
  /* ED /qr */ { 0, BX_IA_IN_EAXDX },
  /* ED /qm */ { 0, BX_IA_IN_EAXDX },
  /* EE /qr */ { 0, BX_IA_OUT_DXAL },
  /* EE /qm */ { 0, BX_IA_OUT_DXAL },
  /* EF /qr */ { 0, BX_IA_OUT_DXEAX },
  /* EF /qm */ { 0, BX_IA_OUT_DXEAX },
  /* F0 /qr */ { 0, BX_IA_ERROR }, // LOCK:
  /* F0 /qm */ { 0, BX_IA_ERROR }, // LOCK:
  /* F1 /qr */ { BxTraceEnd, BX_IA_INT1 },
  /* F1 /qm */ { BxTraceEnd, BX_IA_INT1 },
  /* F2 /qr */ { 0, BX_IA_ERROR }, // REPNE/REPNZ
  /* F2 /qm */ { 0, BX_IA_ERROR }, // REPNE/REPNZ
  /* F3 /qr */ { 0, BX_IA_ERROR }, // REP,REPE/REPZ
  /* F3 /qm */ { 0, BX_IA_ERROR }, // REP,REPE/REPZ
  /* F4 /qr */ { BxTraceEnd, BX_IA_HLT },
  /* F4 /qm */ { BxTraceEnd, BX_IA_HLT },
  /* F5 /qr */ { 0, BX_IA_CMC },
  /* F5 /qm */ { 0, BX_IA_CMC },
  /* F6 /qr */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfoG3EbR },
  /* F6 /qm */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfoG3EbM },
  /* F7 /qr */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfo64G3EqR },
  /* F7 /qm */ { BxGroup3, BX_IA_ERROR, BxOpcodeInfo64G3EqM },
  /* F8 /qr */ { 0, BX_IA_CLC },
  /* F8 /qm */ { 0, BX_IA_CLC },
  /* F9 /qr */ { 0, BX_IA_STC },
  /* F9 /qm */ { 0, BX_IA_STC },
  /* FA /qr */ { 0, BX_IA_CLI },
  /* FA /qm */ { 0, BX_IA_CLI },
  /* FB /qr */ { 0, BX_IA_STI },
  /* FB /qm */ { 0, BX_IA_STI },
  /* FC /qr */ { 0, BX_IA_CLD },
  /* FC /qm */ { 0, BX_IA_CLD },
  /* FD /qr */ { 0, BX_IA_STD },
  /* FD /qm */ { 0, BX_IA_STD },
  /* FE /qr */ { BxGroup4, BX_IA_ERROR, BxOpcodeInfoG4R },
  /* FE /qm */ { BxGroup4, BX_IA_ERROR, BxOpcodeInfoG4M },
  /* FF /qr */ { BxGroup5, BX_IA_ERROR, BxOpcodeInfo64G5qR },
  /* FF /qm */ { BxGroup5, BX_IA_ERROR, BxOpcodeInfo64G5qM },

  /* 0F 00 /qr */ { BxGroup6, BX_IA_ERROR, BxOpcodeInfoG6 },
  /* 0F 00 /qm */ { BxGroup6, BX_IA_ERROR, BxOpcodeInfoG6 },
  /* 0F 01 /qr */ { BxFPEscape, BX_IA_ERROR, BxOpcodeInfoG7R },
  /* 0F 01 /qm */ { BxGroup7, BX_IA_ERROR, BxOpcodeInfo64G7M },
  /* 0F 02 /qr */ { 0, BX_IA_LAR_GvEw },
  /* 0F 02 /qm */ { 0, BX_IA_LAR_GvEw },
  /* 0F 03 /qr */ { 0, BX_IA_LSL_GvEw },
  /* 0F 03 /qm */ { 0, BX_IA_LSL_GvEw },
  /* 0F 04 /qr */ { 0, BX_IA_ERROR },
  /* 0F 04 /qm */ { 0, BX_IA_ERROR },
  /* 0F 05 /qr */ { BxTraceEnd, BX_IA_SYSCALL },
  /* 0F 05 /qm */ { BxTraceEnd, BX_IA_SYSCALL },
  /* 0F 06 /qr */ { 0, BX_IA_CLTS },
  /* 0F 06 /qm */ { 0, BX_IA_CLTS },
  /* 0F 07 /qr */ { BxTraceEnd, BX_IA_SYSRET },
  /* 0F 07 /qm */ { BxTraceEnd, BX_IA_SYSRET },
  /* 0F 08 /qr */ { BxTraceEnd, BX_IA_INVD },
  /* 0F 08 /qm */ { BxTraceEnd, BX_IA_INVD },
  /* 0F 09 /qr */ { BxTraceEnd, BX_IA_WBINVD },
  /* 0F 09 /qm */ { BxTraceEnd, BX_IA_WBINVD },
  /* 0F 0A /qr */ { 0, BX_IA_ERROR },
  /* 0F 0A /qm */ { 0, BX_IA_ERROR },
  /* 0F 0B /qr */ { BxTraceEnd, BX_IA_UD2A },
  /* 0F 0B /qm */ { BxTraceEnd, BX_IA_UD2A },
  /* 0F 0C /qr */ { 0, BX_IA_ERROR },
  /* 0F 0C /qm */ { 0, BX_IA_ERROR },
  /* 0F 0D /qr */ { 0, BX_IA_PREFETCHW },       // 3DNow! PREFETCH on AMD, NOP on Intel
  /* 0F 0D /qm */ { 0, BX_IA_PREFETCHW },       // 3DNow! PREFETCH on AMD, NOP on Intel
  /* 0F 0E /qr */ { 0, BX_IA_FEMMS },           // 3DNow! FEMMS
  /* 0F 0E /qm */ { 0, BX_IA_FEMMS },           // 3DNow! FEMMS
#if BX_SUPPORT_3DNOW
  /* 0F 0F /qr */ { BxImmediate_Ib, BX_IA_ERROR },
  /* 0F 0F /qm */ { BxImmediate_Ib, BX_IA_ERROR },
#else
  /* 0F 0F /qr */ { 0, BX_IA_ERROR },
  /* 0F 0F /qm */ { 0, BX_IA_ERROR },
#endif
  /* 0F 10 /qr */ { BxPrefixSSE, BX_IA_MOVUPS_VpsWpsR, BxOpcodeGroupSSE_0f10R },
  /* 0F 10 /qm */ { BxPrefixSSE, BX_IA_MOVUPS_VpsWpsM, BxOpcodeGroupSSE_0f10M },
  /* 0F 11 /qr */ { BxPrefixSSE, BX_IA_MOVUPS_WpsVpsR, BxOpcodeGroupSSE_0f11R },
  /* 0F 11 /qm */ { BxPrefixSSE, BX_IA_MOVUPS_WpsVpsM, BxOpcodeGroupSSE_0f11M },
  /* 0F 12 /qr */ { BxPrefixSSE, BX_IA_MOVHLPS_VpsWpsR, BxOpcodeGroupSSE_0f12R },
  /* 0F 12 /qm */ { BxPrefixSSE, BX_IA_MOVLPS_VpsMq, BxOpcodeGroupSSE_0f12M },
  /* 0F 13 /qr */ { 0, BX_IA_ERROR },  // MOVLPS/PD SSE group
  /* 0F 13 /qm */ { BxPrefixSSE, BX_IA_MOVLPS_MqVps, BxOpcodeGroupSSE_0f13M },
  /* 0F 14 /qr */ { BxPrefixSSE, BX_IA_UNPCKLPS_VpsWdq, BxOpcodeGroupSSE_0f14 },
  /* 0F 14 /qm */ { BxPrefixSSE, BX_IA_UNPCKLPS_VpsWdq, BxOpcodeGroupSSE_0f14 },
  /* 0F 15 /qr */ { BxPrefixSSE, BX_IA_UNPCKHPS_VpsWdq, BxOpcodeGroupSSE_0f15 },
  /* 0F 15 /qm */ { BxPrefixSSE, BX_IA_UNPCKHPS_VpsWdq, BxOpcodeGroupSSE_0f15 },
  /* 0F 16 /qr */ { BxPrefixSSE, BX_IA_MOVLHPS_VpsWpsR, BxOpcodeGroupSSE_0f16R },
  /* 0F 16 /qm */ { BxPrefixSSE, BX_IA_MOVHPS_VpsMq, BxOpcodeGroupSSE_0f16M },
  /* 0F 17 /qr */ { 0, BX_IA_ERROR },  // MOHLPS/PD SSE group
  /* 0F 17 /qm */ { BxPrefixSSE, BX_IA_MOVHPS_MqVps, BxOpcodeGroupSSE_0f17M },
  /* 0F 18 /qr */ { 0, BX_IA_NOP },    // PREFETCH HINT
  /* 0F 18 /qm */ { 0, BX_IA_PREFETCH }, // opcode group G16, PREFETCH hints
  /* 0F 19 /qr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 19 /qm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1A /qr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1A /qm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1B /qr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1B /qm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1C /qr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1C /qm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1D /qr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1D /qm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1E /qr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1E /qm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 1F /qr */ { 0, BX_IA_NOP },    // multi-byte NOP
  /* 0F 1F /qm */ { 0, BX_IA_NOP },      // multi-byte NOP
  /* 0F 20 /qr */ { 0, BX_IA_MOV_RqCq },
  /* 0F 20 /qm */ { 0, BX_IA_MOV_RqCq },
  /* 0F 21 /qr */ { 0, BX_IA_MOV_RqDq },
  /* 0F 21 /qm */ { 0, BX_IA_MOV_RqDq },
  /* 0F 22 /qr */ { BxTraceEnd, BX_IA_MOV_CqRq },
  /* 0F 22 /qm */ { BxTraceEnd, BX_IA_MOV_CqRq },
  /* 0F 23 /qr */ { BxTraceEnd, BX_IA_MOV_DqRq },
  /* 0F 23 /qm */ { BxTraceEnd, BX_IA_MOV_DqRq },
  /* 0F 24 /qr */ { 0, BX_IA_ERROR },
  /* 0F 24 /qm */ { 0, BX_IA_ERROR },
  /* 0F 25 /qr */ { 0, BX_IA_ERROR },
  /* 0F 25 /qm */ { 0, BX_IA_ERROR },
  /* 0F 26 /qr */ { 0, BX_IA_ERROR },
  /* 0F 26 /qm */ { 0, BX_IA_ERROR },
  /* 0F 27 /qr */ { 0, BX_IA_ERROR },
  /* 0F 27 /qm */ { 0, BX_IA_ERROR },
  /* 0F 28 /qr */ { BxPrefixSSE, BX_IA_MOVAPS_VpsWpsR, BxOpcodeGroupSSE_0f28R },
  /* 0F 28 /qm */ { BxPrefixSSE, BX_IA_MOVAPS_VpsWpsM, BxOpcodeGroupSSE_0f28M },
  /* 0F 29 /qr */ { BxPrefixSSE, BX_IA_MOVAPS_WpsVpsR, BxOpcodeGroupSSE_0f29R },
  /* 0F 29 /qm */ { BxPrefixSSE, BX_IA_MOVAPS_WpsVpsM, BxOpcodeGroupSSE_0f29M },
  /* 0F 2A /qr */ { BxPrefixSSE, BX_IA_CVTPI2PS_VpsQqR, BxOpcodeGroupSSE_0f2aR },
  /* 0F 2A /qm */ { BxPrefixSSE, BX_IA_CVTPI2PS_VpsQqM, BxOpcodeGroupSSE_0f2aM },
  /* 0F 2B /qr */ { 0, BX_IA_ERROR }, // MOVNTPS/PD/SS/SD
  /* 0F 2B /qm */ { BxPrefixSSE, BX_IA_MOVNTPS_MpsVps, BxOpcodeGroupSSE_0f2bM },
  /* 0F 2C /qr */ { BxPrefixSSE, BX_IA_CVTTPS2PI_PqWps, BxOpcodeGroupSSE_0f2c },
  /* 0F 2C /qm */ { BxPrefixSSE, BX_IA_CVTTPS2PI_PqWps, BxOpcodeGroupSSE_0f2c },
  /* 0F 2D /qr */ { BxPrefixSSE, BX_IA_CVTPS2PI_PqWps, BxOpcodeGroupSSE_0f2d },
  /* 0F 2D /qm */ { BxPrefixSSE, BX_IA_CVTPS2PI_PqWps, BxOpcodeGroupSSE_0f2d },
  /* 0F 2E /qr */ { BxPrefixSSE, BX_IA_UCOMISS_VssWss, BxOpcodeGroupSSE_0f2e },
  /* 0F 2E /qm */ { BxPrefixSSE, BX_IA_UCOMISS_VssWss, BxOpcodeGroupSSE_0f2e },
  /* 0F 2F /qr */ { BxPrefixSSE, BX_IA_COMISS_VpsWps, BxOpcodeGroupSSE_0f2f },
  /* 0F 2F /qm */ { BxPrefixSSE, BX_IA_COMISS_VpsWps, BxOpcodeGroupSSE_0f2f },
  /* 0F 30 /qr */ { 0, BX_IA_WRMSR },
  /* 0F 30 /qm */ { 0, BX_IA_WRMSR },
  /* 0F 31 /qr */ { 0, BX_IA_RDTSC },
  /* 0F 31 /qm */ { 0, BX_IA_RDTSC },
  /* 0F 32 /qr */ { 0, BX_IA_RDMSR },
  /* 0F 32 /qm */ { 0, BX_IA_RDMSR },
  /* 0F 33 /qr */ { 0, BX_IA_RDPMC },
  /* 0F 33 /qm */ { 0, BX_IA_RDPMC },
  /* 0F 34 /qr */ { BxTraceEnd, BX_IA_SYSENTER },
  /* 0F 34 /qm */ { BxTraceEnd, BX_IA_SYSENTER },
  /* 0F 35 /qr */ { BxTraceEnd, BX_IA_SYSEXIT },
  /* 0F 35 /qm */ { BxTraceEnd, BX_IA_SYSEXIT },
  /* 0F 36 /qr */ { 0, BX_IA_ERROR },
  /* 0F 36 /qm */ { 0, BX_IA_ERROR },
  /* 0F 37 /qr */ { 0, BX_IA_ERROR },
  /* 0F 37 /qm */ { 0, BX_IA_ERROR },
  /* 0F 38 /qr */ { Bx3ByteOp, BX_IA_ERROR, BxOpcode3ByteTable0f38 }, // 3-byte escape
  /* 0F 38 /qm */ { Bx3ByteOp, BX_IA_ERROR, BxOpcode3ByteTable0f38 }, // 3-byte escape
  /* 0F 39 /qr */ { 0, BX_IA_ERROR },
  /* 0F 39 /qm */ { 0, BX_IA_ERROR },
  /* 0F 3A /qr */ { Bx3ByteOp | BxImmediate_Ib, BX_IA_ERROR, BxOpcode3ByteTable0f3a }, // 3-byte escape
  /* 0F 3A /qm */ { Bx3ByteOp | BxImmediate_Ib, BX_IA_ERROR, BxOpcode3ByteTable0f3a }, // 3-byte escape
  /* 0F 3B /qr */ { 0, BX_IA_ERROR },
  /* 0F 3B /qm */ { 0, BX_IA_ERROR },
  /* 0F 3C /qr */ { 0, BX_IA_ERROR },
  /* 0F 3C /qm */ { 0, BX_IA_ERROR },
  /* 0F 3D /qr */ { 0, BX_IA_ERROR },
  /* 0F 3D /qm */ { 0, BX_IA_ERROR },
  /* 0F 3E /qr */ { 0, BX_IA_ERROR },
  /* 0F 3E /qm */ { 0, BX_IA_ERROR },
  /* 0F 3F /qr */ { 0, BX_IA_ERROR },
  /* 0F 3F /qm */ { 0, BX_IA_ERROR },
  /* 0F 40 /qr */ { 0, BX_IA_CMOVO_GqEqR },
  /* 0F 40 /qm */ { 0, BX_IA_CMOVO_GqEqM },
  /* 0F 41 /qr */ { 0, BX_IA_CMOVNO_GqEqR },
  /* 0F 41 /qm */ { 0, BX_IA_CMOVNO_GqEqM },
  /* 0F 42 /qr */ { 0, BX_IA_CMOVB_GqEqR },
  /* 0F 42 /qm */ { 0, BX_IA_CMOVB_GqEqM },
  /* 0F 43 /qr */ { 0, BX_IA_CMOVNB_GqEqR },
  /* 0F 43 /qm */ { 0, BX_IA_CMOVNB_GqEqM },
  /* 0F 44 /qr */ { 0, BX_IA_CMOVZ_GqEqR },
  /* 0F 44 /qm */ { 0, BX_IA_CMOVZ_GqEqM },
  /* 0F 45 /qr */ { 0, BX_IA_CMOVNZ_GqEqR },
  /* 0F 45 /qm */ { 0, BX_IA_CMOVNZ_GqEqM },
  /* 0F 46 /qr */ { 0, BX_IA_CMOVBE_GqEqR },
  /* 0F 46 /qm */ { 0, BX_IA_CMOVBE_GqEqM },
  /* 0F 47 /qr */ { 0, BX_IA_CMOVNBE_GqEqR },
  /* 0F 47 /qm */ { 0, BX_IA_CMOVNBE_GqEqM },
  /* 0F 48 /qr */ { 0, BX_IA_CMOVS_GqEqR },
  /* 0F 48 /qm */ { 0, BX_IA_CMOVS_GqEqM },
  /* 0F 49 /qr */ { 0, BX_IA_CMOVNS_GqEqR },
  /* 0F 49 /qm */ { 0, BX_IA_CMOVNS_GqEqM },
  /* 0F 4A /qr */ { 0, BX_IA_CMOVP_GqEqR },
  /* 0F 4A /qm */ { 0, BX_IA_CMOVP_GqEqM },
  /* 0F 4B /qr */ { 0, BX_IA_CMOVNP_GqEqR },
  /* 0F 4B /qm */ { 0, BX_IA_CMOVNP_GqEqM },
  /* 0F 4C /qr */ { 0, BX_IA_CMOVL_GqEqR },
  /* 0F 4C /qm */ { 0, BX_IA_CMOVL_GqEqM },
  /* 0F 4D /qr */ { 0, BX_IA_CMOVNL_GqEqR },
  /* 0F 4D /qm */ { 0, BX_IA_CMOVNL_GqEqM },
  /* 0F 4E /qr */ { 0, BX_IA_CMOVLE_GqEqR },
  /* 0F 4E /qm */ { 0, BX_IA_CMOVLE_GqEqM },
  /* 0F 4F /qr */ { 0, BX_IA_CMOVNLE_GqEqR },
  /* 0F 4F /qm */ { 0, BX_IA_CMOVNLE_GqEqM },
  /* 0F 50 /qr */ { BxPrefixSSE, BX_IA_MOVMSKPS_GdVRps, BxOpcodeGroupSSE_0f50 },
  /* 0F 50 /qm */ { 0, BX_IA_ERROR }, // MOVMSKPS/PD
  /* 0F 51 /qr */ { BxPrefixSSE, BX_IA_SQRTPS_VpsWps, BxOpcodeGroupSSE_0f51 },
  /* 0F 51 /qm */ { BxPrefixSSE, BX_IA_SQRTPS_VpsWps, BxOpcodeGroupSSE_0f51 },
  /* 0F 52 /qr */ { BxPrefixSSE, BX_IA_RSQRTPS_VpsWps, BxOpcodeGroupSSE_0f52 },
  /* 0F 52 /qm */ { BxPrefixSSE, BX_IA_RSQRTPS_VpsWps, BxOpcodeGroupSSE_0f52 },
  /* 0F 53 /qr */ { BxPrefixSSE, BX_IA_RCPPS_VpsWps, BxOpcodeGroupSSE_0f53 },
  /* 0F 53 /qm */ { BxPrefixSSE, BX_IA_RCPPS_VpsWps, BxOpcodeGroupSSE_0f53 },
  /* 0F 54 /qr */ { BxPrefixSSE, BX_IA_ANDPS_VpsWps, BxOpcodeGroupSSE_0f54 },
  /* 0F 54 /qm */ { BxPrefixSSE, BX_IA_ANDPS_VpsWps, BxOpcodeGroupSSE_0f54 },
  /* 0F 55 /qr */ { BxPrefixSSE, BX_IA_ANDNPS_VpsWps, BxOpcodeGroupSSE_0f55 },
  /* 0F 55 /qm */ { BxPrefixSSE, BX_IA_ANDNPS_VpsWps, BxOpcodeGroupSSE_0f55 },
  /* 0F 56 /qr */ { BxPrefixSSE, BX_IA_ORPS_VpsWps, BxOpcodeGroupSSE_0f56 },
  /* 0F 56 /qm */ { BxPrefixSSE, BX_IA_ORPS_VpsWps, BxOpcodeGroupSSE_0f56 },
  /* 0F 57 /qr */ { BxPrefixSSE, BX_IA_XORPS_VpsWps, BxOpcodeGroupSSE_0f57 },
  /* 0F 57 /qm */ { BxPrefixSSE, BX_IA_XORPS_VpsWps, BxOpcodeGroupSSE_0f57 },
  /* 0F 58 /qr */ { BxPrefixSSE, BX_IA_ADDPS_VpsWps, BxOpcodeGroupSSE_0f58 },
  /* 0F 58 /qm */ { BxPrefixSSE, BX_IA_ADDPS_VpsWps, BxOpcodeGroupSSE_0f58 },
  /* 0F 59 /qr */ { BxPrefixSSE, BX_IA_MULPS_VpsWps, BxOpcodeGroupSSE_0f59 },
  /* 0F 59 /qm */ { BxPrefixSSE, BX_IA_MULPS_VpsWps, BxOpcodeGroupSSE_0f59 },
  /* 0F 5A /qr */ { BxPrefixSSE, BX_IA_CVTPS2PD_VpsWps, BxOpcodeGroupSSE_0f5a },
  /* 0F 5A /qm */ { BxPrefixSSE, BX_IA_CVTPS2PD_VpsWps, BxOpcodeGroupSSE_0f5a },
  /* 0F 5B /qr */ { BxPrefixSSE, BX_IA_CVTDQ2PS_VpsWdq, BxOpcodeGroupSSE_0f5b },
  /* 0F 5B /qm */ { BxPrefixSSE, BX_IA_CVTDQ2PS_VpsWdq, BxOpcodeGroupSSE_0f5b },
  /* 0F 5C /qr */ { BxPrefixSSE, BX_IA_SUBPS_VpsWps, BxOpcodeGroupSSE_0f5c },
  /* 0F 5C /qm */ { BxPrefixSSE, BX_IA_SUBPS_VpsWps, BxOpcodeGroupSSE_0f5c },
  /* 0F 5D /qr */ { BxPrefixSSE, BX_IA_MINPS_VpsWps, BxOpcodeGroupSSE_0f5d },
  /* 0F 5D /qm */ { BxPrefixSSE, BX_IA_MINPS_VpsWps, BxOpcodeGroupSSE_0f5d },
  /* 0F 5E /qr */ { BxPrefixSSE, BX_IA_DIVPS_VpsWps, BxOpcodeGroupSSE_0f5e },
  /* 0F 5E /qm */ { BxPrefixSSE, BX_IA_DIVPS_VpsWps, BxOpcodeGroupSSE_0f5e },
  /* 0F 5F /qr */ { BxPrefixSSE, BX_IA_MAXPS_VpsWps, BxOpcodeGroupSSE_0f5f },
  /* 0F 5F /qm */ { BxPrefixSSE, BX_IA_MAXPS_VpsWps, BxOpcodeGroupSSE_0f5f },
  /* 0F 60 /qr */ { BxPrefixSSE, BX_IA_PUNPCKLBW_PqQd, BxOpcodeGroupSSE_0f60 },
  /* 0F 60 /qm */ { BxPrefixSSE, BX_IA_PUNPCKLBW_PqQd, BxOpcodeGroupSSE_0f60 },
  /* 0F 61 /qr */ { BxPrefixSSE, BX_IA_PUNPCKLWD_PqQd, BxOpcodeGroupSSE_0f61 },
  /* 0F 61 /qm */ { BxPrefixSSE, BX_IA_PUNPCKLWD_PqQd, BxOpcodeGroupSSE_0f61 },
  /* 0F 62 /qr */ { BxPrefixSSE, BX_IA_PUNPCKLDQ_PqQd, BxOpcodeGroupSSE_0f62 },
  /* 0F 62 /qm */ { BxPrefixSSE, BX_IA_PUNPCKLDQ_PqQd, BxOpcodeGroupSSE_0f62 },
  /* 0F 63 /qr */ { BxPrefixSSE, BX_IA_PACKSSWB_PqQq, BxOpcodeGroupSSE_0f63 },
  /* 0F 63 /qm */ { BxPrefixSSE, BX_IA_PACKSSWB_PqQq, BxOpcodeGroupSSE_0f63 },
  /* 0F 64 /qr */ { BxPrefixSSE, BX_IA_PCMPGTB_PqQq, BxOpcodeGroupSSE_0f64 },
  /* 0F 64 /qm */ { BxPrefixSSE, BX_IA_PCMPGTB_PqQq, BxOpcodeGroupSSE_0f64 },
  /* 0F 65 /qr */ { BxPrefixSSE, BX_IA_PCMPGTW_PqQq, BxOpcodeGroupSSE_0f65 },
  /* 0F 65 /qm */ { BxPrefixSSE, BX_IA_PCMPGTW_PqQq, BxOpcodeGroupSSE_0f65 },
  /* 0F 66 /qr */ { BxPrefixSSE, BX_IA_PCMPGTD_PqQq, BxOpcodeGroupSSE_0f66 },
  /* 0F 66 /qm */ { BxPrefixSSE, BX_IA_PCMPGTD_PqQq, BxOpcodeGroupSSE_0f66 },
  /* 0F 67 /qr */ { BxPrefixSSE, BX_IA_PACKUSWB_PqQq, BxOpcodeGroupSSE_0f67 },
  /* 0F 67 /qm */ { BxPrefixSSE, BX_IA_PACKUSWB_PqQq, BxOpcodeGroupSSE_0f67 },
  /* 0F 68 /qr */ { BxPrefixSSE, BX_IA_PUNPCKHBW_PqQq, BxOpcodeGroupSSE_0f68 },
  /* 0F 68 /qm */ { BxPrefixSSE, BX_IA_PUNPCKHBW_PqQq, BxOpcodeGroupSSE_0f68 },
  /* 0F 69 /qr */ { BxPrefixSSE, BX_IA_PUNPCKHWD_PqQq, BxOpcodeGroupSSE_0f69 },
  /* 0F 69 /qm */ { BxPrefixSSE, BX_IA_PUNPCKHWD_PqQq, BxOpcodeGroupSSE_0f69 },
  /* 0F 6A /qr */ { BxPrefixSSE, BX_IA_PUNPCKHDQ_PqQq, BxOpcodeGroupSSE_0f6a },
  /* 0F 6A /qm */ { BxPrefixSSE, BX_IA_PUNPCKHDQ_PqQq, BxOpcodeGroupSSE_0f6a },
  /* 0F 6B /qr */ { BxPrefixSSE, BX_IA_PACKSSDW_PqQq, BxOpcodeGroupSSE_0f6b },
  /* 0F 6B /qm */ { BxPrefixSSE, BX_IA_PACKSSDW_PqQq, BxOpcodeGroupSSE_0f6b },
  /* 0F 6C /qr */ { BxPrefixSSE66, BX_IA_PUNPCKLQDQ_VdqWdq },
  /* 0F 6C /qm */ { BxPrefixSSE66, BX_IA_PUNPCKLQDQ_VdqWdq },
  /* 0F 6D /qr */ { BxPrefixSSE66, BX_IA_PUNPCKHQDQ_VdqWdq },
  /* 0F 6D /qm */ { BxPrefixSSE66, BX_IA_PUNPCKHQDQ_VdqWdq },
  /* 0F 6E /qr */ { BxPrefixSSE, BX_IA_MOVQ_PqEqR, BxOpcodeGroupSSE_0f6eQR },
  /* 0F 6E /qm */ { BxPrefixSSE, BX_IA_MOVQ_PqEqM, BxOpcodeGroupSSE_0f6eQM },
  /* 0F 6F /qr */ { BxPrefixSSE, BX_IA_MOVQ_PqQqR, BxOpcodeGroupSSE_0f6fR },
  /* 0F 6F /qm */ { BxPrefixSSE, BX_IA_MOVQ_PqQqM, BxOpcodeGroupSSE_0f6fM },
  /* 0F 70 /qr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PSHUFW_PqQqIb, BxOpcodeGroupSSE_0f70 },
  /* 0F 70 /qm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PSHUFW_PqQqIb, BxOpcodeGroupSSE_0f70 },
  /* 0F 71 /qr */ { BxGroup12, BX_IA_ERROR, BxOpcodeInfoG12R },
  /* 0F 71 /qm */ { 0, BX_IA_ERROR }, // SSE Group G12
  /* 0F 72 /qr */ { BxGroup13, BX_IA_ERROR, BxOpcodeInfoG13R },
  /* 0F 72 /qm */ { 0, BX_IA_ERROR }, // SSE Group G13
  /* 0F 73 /qr */ { BxGroup14, BX_IA_ERROR, BxOpcodeInfoG14R },
  /* 0F 73 /qm */ { 0, BX_IA_ERROR }, // SSE Group G14
  /* 0F 74 /qr */ { BxPrefixSSE, BX_IA_PCMPEQB_PqQq, BxOpcodeGroupSSE_0f74 },
  /* 0F 74 /qm */ { BxPrefixSSE, BX_IA_PCMPEQB_PqQq, BxOpcodeGroupSSE_0f74 },
  /* 0F 75 /qr */ { BxPrefixSSE, BX_IA_PCMPEQW_PqQq, BxOpcodeGroupSSE_0f75 },
  /* 0F 75 /qm */ { BxPrefixSSE, BX_IA_PCMPEQW_PqQq, BxOpcodeGroupSSE_0f75 },
  /* 0F 76 /qr */ { BxPrefixSSE, BX_IA_PCMPEQD_PqQq, BxOpcodeGroupSSE_0f76 },
  /* 0F 76 /qm */ { BxPrefixSSE, BX_IA_PCMPEQD_PqQq, BxOpcodeGroupSSE_0f76 },
  /* 0F 77 /qr */ { BxPrefixSSE, BX_IA_EMMS, BxOpcodeGroupSSE_ERR },
  /* 0F 77 /qm */ { BxPrefixSSE, BX_IA_EMMS, BxOpcodeGroupSSE_ERR },
  /* 0F 78 /qr */ { BxPrefixSSE, BX_IA_VMREAD_EqGq, BxOpcodeGroupSSE_ERR },
  /* 0F 78 /qm */ { BxPrefixSSE, BX_IA_VMREAD_EqGq, BxOpcodeGroupSSE_ERR },
  /* 0F 79 /qr */ { BxPrefixSSE, BX_IA_VMWRITE_GqEq, BxOpcodeGroupSSE_ERR },
  /* 0F 79 /qm */ { BxPrefixSSE, BX_IA_VMWRITE_GqEq, BxOpcodeGroupSSE_ERR },
  /* 0F 7A /qr */ { 0, BX_IA_ERROR },
  /* 0F 7A /qm */ { 0, BX_IA_ERROR },
  /* 0F 7B /qr */ { 0, BX_IA_ERROR },
  /* 0F 7B /qm */ { 0, BX_IA_ERROR },
  /* 0F 7C /qr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7c },
  /* 0F 7C /qm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7c },
  /* 0F 7D /qr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7d },
  /* 0F 7D /qm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0f7d },
  /* 0F 7E /qr */ { BxPrefixSSE, BX_IA_MOVQ_EqPqR, BxOpcodeGroupSSE_0f7eQR },
  /* 0F 7E /qm */ { BxPrefixSSE, BX_IA_MOVQ_EqPqM, BxOpcodeGroupSSE_0f7eQM },
  /* 0F 7F /qr */ { BxPrefixSSE, BX_IA_MOVQ_QqPqR, BxOpcodeGroupSSE_0f7fR },
  /* 0F 7F /qm */ { BxPrefixSSE, BX_IA_MOVQ_QqPqM, BxOpcodeGroupSSE_0f7fM },
  /* 0F 80 /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JO_Jq },
  /* 0F 80 /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JO_Jq },
  /* 0F 81 /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 0F 81 /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNO_Jq },
  /* 0F 82 /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JB_Jq },
  /* 0F 82 /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JB_Jq },
  /* 0F 83 /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 0F 83 /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNB_Jq },
  /* 0F 84 /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 0F 84 /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JZ_Jq },
  /* 0F 85 /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 0F 85 /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNZ_Jq },
  /* 0F 86 /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 0F 86 /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JBE_Jq },
  /* 0F 87 /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 0F 87 /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNBE_Jq },
  /* 0F 88 /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JS_Jq },
  /* 0F 88 /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JS_Jq },
  /* 0F 89 /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 0F 89 /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNS_Jq },
  /* 0F 8A /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JP_Jq },
  /* 0F 8A /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JP_Jq },
  /* 0F 8B /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 0F 8B /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNP_Jq },
  /* 0F 8C /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JL_Jq },
  /* 0F 8C /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JL_Jq },
  /* 0F 8D /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 0F 8D /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNL_Jq },
  /* 0F 8E /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 0F 8E /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JLE_Jq },
  /* 0F 8F /qr */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 0F 8F /qm */ { BxImmediate_BrOff32 | BxTraceJCC, BX_IA_JNLE_Jq },
  /* 0F 90 /qr */ { 0, BX_IA_SETO_EbR },
  /* 0F 90 /qm */ { 0, BX_IA_SETO_EbM },
  /* 0F 91 /qr */ { 0, BX_IA_SETNO_EbR },
  /* 0F 91 /qm */ { 0, BX_IA_SETNO_EbM },
  /* 0F 92 /qr */ { 0, BX_IA_SETB_EbR },
  /* 0F 92 /qm */ { 0, BX_IA_SETB_EbM },
  /* 0F 93 /qr */ { 0, BX_IA_SETNB_EbR },
  /* 0F 93 /qm */ { 0, BX_IA_SETNB_EbM },
  /* 0F 94 /qr */ { 0, BX_IA_SETZ_EbR },
  /* 0F 94 /qm */ { 0, BX_IA_SETZ_EbM },
  /* 0F 95 /qr */ { 0, BX_IA_SETNZ_EbR },
  /* 0F 95 /qm */ { 0, BX_IA_SETNZ_EbM },
  /* 0F 96 /qr */ { 0, BX_IA_SETBE_EbR },
  /* 0F 96 /qm */ { 0, BX_IA_SETBE_EbM },
  /* 0F 97 /qr */ { 0, BX_IA_SETNBE_EbR },
  /* 0F 97 /qm */ { 0, BX_IA_SETNBE_EbM },
  /* 0F 98 /qr */ { 0, BX_IA_SETS_EbR },
  /* 0F 98 /qm */ { 0, BX_IA_SETS_EbM },
  /* 0F 99 /qr */ { 0, BX_IA_SETNS_EbR },
  /* 0F 99 /qm */ { 0, BX_IA_SETNS_EbM },
  /* 0F 9A /qr */ { 0, BX_IA_SETP_EbR },
  /* 0F 9A /qm */ { 0, BX_IA_SETP_EbM },
  /* 0F 9B /qr */ { 0, BX_IA_SETNP_EbR },
  /* 0F 9B /qm */ { 0, BX_IA_SETNP_EbM },
  /* 0F 9C /qr */ { 0, BX_IA_SETL_EbR },
  /* 0F 9C /qm */ { 0, BX_IA_SETL_EbM },
  /* 0F 9D /qr */ { 0, BX_IA_SETNL_EbR },
  /* 0F 9D /qm */ { 0, BX_IA_SETNL_EbM },
  /* 0F 9E /qr */ { 0, BX_IA_SETLE_EbR },
  /* 0F 9E /qm */ { 0, BX_IA_SETLE_EbM },
  /* 0F 9F /qr */ { 0, BX_IA_SETNLE_EbR },
  /* 0F 9F /qm */ { 0, BX_IA_SETNLE_EbM },
  /* 0F A0 /qr */ { 0, BX_IA_PUSH64_FS },
  /* 0F A0 /qm */ { 0, BX_IA_PUSH64_FS },
  /* 0F A1 /qr */ { 0, BX_IA_POP64_FS },
  /* 0F A1 /qm */ { 0, BX_IA_POP64_FS },
  /* 0F A2 /qr */ { 0, BX_IA_CPUID },
  /* 0F A2 /qm */ { 0, BX_IA_CPUID },
  /* 0F A3 /qr */ { 0, BX_IA_BT_EqGqR },
  /* 0F A3 /qm */ { 0, BX_IA_BT_EqGqM },
  /* 0F A4 /qr */ { BxImmediate_Ib, BX_IA_SHLD_EqGqR },
  /* 0F A4 /qm */ { BxImmediate_Ib, BX_IA_SHLD_EqGqM },
  /* 0F A5 /qr */ { 0,              BX_IA_SHLD_EqGqR },
  /* 0F A5 /qm */ { 0,              BX_IA_SHLD_EqGqM },
  /* 0F A6 /qr */ { 0, BX_IA_ERROR },
  /* 0F A6 /qm */ { 0, BX_IA_ERROR },
  /* 0F A7 /qr */ { 0, BX_IA_ERROR },
  /* 0F A7 /qm */ { 0, BX_IA_ERROR },
  /* 0F A8 /qr */ { 0, BX_IA_PUSH64_GS },
  /* 0F A8 /qm */ { 0, BX_IA_PUSH64_GS },
  /* 0F A9 /qr */ { 0, BX_IA_POP64_GS },
  /* 0F A9 /qm */ { 0, BX_IA_POP64_GS },
  /* 0F AA /qr */ { BxTraceEnd, BX_IA_RSM },
  /* 0F AA /qm */ { BxTraceEnd, BX_IA_RSM },
  /* 0F AB /qr */ { 0, BX_IA_BTS_EqGqR },
  /* 0F AB /qm */ { BxLockable, BX_IA_BTS_EqGqM },
  /* 0F AC /qr */ { BxImmediate_Ib, BX_IA_SHRD_EqGqR },
  /* 0F AC /qm */ { BxImmediate_Ib, BX_IA_SHRD_EqGqM },
  /* 0F AD /qr */ { 0,              BX_IA_SHRD_EqGqR },
  /* 0F AD /qm */ { 0,              BX_IA_SHRD_EqGqM },
  /* 0F AE /qr */ { BxGroup15, BX_IA_ERROR, BxOpcodeInfoG15R },
  /* 0F AE /qm */ { BxGroup15, BX_IA_ERROR, BxOpcodeInfoG15M },
  /* 0F AF /qr */ { 0, BX_IA_IMUL_GqEqR },
  /* 0F AF /qm */ { 0, BX_IA_IMUL_GqEqM },
  /* 0F B0 /qr */ { 0, BX_IA_CMPXCHG_EbGbR },
  /* 0F B0 /qm */ { BxLockable, BX_IA_CMPXCHG_EbGbM },
  /* 0F B1 /qr */ { 0, BX_IA_CMPXCHG_EqGqR },
  /* 0F B1 /qm */ { BxLockable, BX_IA_CMPXCHG_EqGqM },
  /* 0F B2 /qr */ { 0, BX_IA_ERROR }, // LSS
  /* 0F B2 /qm */ { 0, BX_IA_LSS_GqMp }, // TODO: LSS_GdMp for AMD CPU
  /* 0F B3 /qr */ { 0, BX_IA_BTR_EqGqR },
  /* 0F B3 /qm */ { BxLockable, BX_IA_BTR_EqGqM },
  /* 0F B4 /qr */ { 0, BX_IA_ERROR }, // LFS
  /* 0F B4 /qm */ { 0, BX_IA_LFS_GqMp }, // TODO: LFS_GdMp for AMD CPU
  /* 0F B5 /qr */ { 0, BX_IA_ERROR }, // LGS
  /* 0F B5 /qm */ { 0, BX_IA_LGS_GqMp }, // TODO: LGS_GdMp for AMD CPU
  /* 0F B6 /qr */ { 0, BX_IA_MOVZX_GqEbR },
  /* 0F B6 /qm */ { 0, BX_IA_MOVZX_GqEbM },
  /* 0F B7 /qr */ { 0, BX_IA_MOVZX_GqEwR },
  /* 0F B7 /qm */ { 0, BX_IA_MOVZX_GqEwM },
  /* 0F B8 /qr */ { BxPrefixSSEF3, BX_IA_POPCNT_GqEqR },
  /* 0F B8 /qm */ { BxPrefixSSEF3, BX_IA_POPCNT_GqEqM },
  /* 0F B9 /qr */ { BxTraceEnd, BX_IA_UD2B },
  /* 0F B9 /qm */ { BxTraceEnd, BX_IA_UD2B },
  /* 0F BA /qr */ { BxGroup8, BX_IA_ERROR, BxOpcodeInfo64G8EqIbR },
  /* 0F BA /qm */ { BxGroup8, BX_IA_ERROR, BxOpcodeInfo64G8EqIbM },
  /* 0F BB /qr */ { 0, BX_IA_BTC_EqGqR },
  /* 0F BB /qm */ { BxLockable, BX_IA_BTC_EqGqM },
  /* 0F BC /qr */ { 0, BX_IA_BSF_GqEqR },
  /* 0F BC /qm */ { 0, BX_IA_BSF_GqEqM },
  /* 0F BD /qr */ { 0, BX_IA_BSR_GqEqR },
  /* 0F BD /qm */ { 0, BX_IA_BSR_GqEqM },
  /* 0F BE /qr */ { 0, BX_IA_MOVSX_GqEbR },
  /* 0F BE /qm */ { 0, BX_IA_MOVSX_GqEbM },
  /* 0F BF /qr */ { 0, BX_IA_MOVSX_GqEwR },
  /* 0F BF /qm */ { 0, BX_IA_MOVSX_GqEwM },
  /* 0F C0 /qr */ { 0, BX_IA_XADD_EbGbR },
  /* 0F C0 /qm */ { BxLockable, BX_IA_XADD_EbGbM },
  /* 0F C1 /qr */ { 0, BX_IA_XADD_EqGqR },
  /* 0F C1 /qm */ { BxLockable, BX_IA_XADD_EqGqM },
  /* 0F C2 /qr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_CMPPS_VpsWpsIb, BxOpcodeGroupSSE_0fc2 },
  /* 0F C2 /qm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_CMPPS_VpsWpsIb, BxOpcodeGroupSSE_0fc2 },
  /* 0F C3 /qr */ { 0, BX_IA_ERROR }, // MOVNTI SSE group
  /* 0F C3 /qm */ { BxPrefixSSE, BX_IA_MOVNTI_MqGq, BxOpcodeGroupSSE_ERR },
  /* 0F C4 /qr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PINSRW_PqEwIb, BxOpcodeGroupSSE_0fc4 },
  /* 0F C4 /qm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PINSRW_PqEwIb, BxOpcodeGroupSSE_0fc4 },
  /* 0F C5 /qr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_PEXTRW_GdPqIb, BxOpcodeGroupSSE_0fc5R },
  /* 0F C5 /qm */ { 0, BX_IA_ERROR }, // PEXTRW SSE group
  /* 0F C6 /qr */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_SHUFPS_VpsWpsIb, BxOpcodeGroupSSE_0fc6 },
  /* 0F C6 /qm */ { BxPrefixSSE | BxImmediate_Ib, BX_IA_SHUFPS_VpsWpsIb, BxOpcodeGroupSSE_0fc6 },
  /* 0F C7 /qr */ { 0, BX_IA_ERROR }, // CMPXCHG16B G9 group
  /* 0F C7 /qm */ { BxGroup9, BX_IA_ERROR, BxOpcodeInfo64G9qM },
  /* 0F C8 /qr */ { 0, BX_IA_BSWAP_RRX },
  /* 0F C8 /qm */ { 0, BX_IA_BSWAP_RRX },
  /* 0F C9 /qr */ { 0, BX_IA_BSWAP_RRX },
  /* 0F C9 /qm */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CA /qr */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CA /qm */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CB /qr */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CB /qm */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CC /qr */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CC /qm */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CD /qr */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CD /qm */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CE /qr */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CE /qm */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CF /qr */ { 0, BX_IA_BSWAP_RRX },
  /* 0F CF /qm */ { 0, BX_IA_BSWAP_RRX },
  /* 0F D0 /qr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fd0 },
  /* 0F D0 /qm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fd0 },
  /* 0F D1 /qr */ { BxPrefixSSE, BX_IA_PSRLW_PqQq, BxOpcodeGroupSSE_0fd1 },
  /* 0F D1 /qm */ { BxPrefixSSE, BX_IA_PSRLW_PqQq, BxOpcodeGroupSSE_0fd1 },
  /* 0F D2 /qr */ { BxPrefixSSE, BX_IA_PSRLD_PqQq, BxOpcodeGroupSSE_0fd2 },
  /* 0F D2 /qm */ { BxPrefixSSE, BX_IA_PSRLD_PqQq, BxOpcodeGroupSSE_0fd2 },
  /* 0F D3 /qr */ { BxPrefixSSE, BX_IA_PSRLQ_PqQq, BxOpcodeGroupSSE_0fd3 },
  /* 0F D3 /qm */ { BxPrefixSSE, BX_IA_PSRLQ_PqQq, BxOpcodeGroupSSE_0fd3 },
  /* 0F D4 /qr */ { BxPrefixSSE, BX_IA_PADDQ_PqQq, BxOpcodeGroupSSE_0fd4 },
  /* 0F D4 /qm */ { BxPrefixSSE, BX_IA_PADDQ_PqQq, BxOpcodeGroupSSE_0fd4 },
  /* 0F D5 /qr */ { BxPrefixSSE, BX_IA_PMULLW_PqQq, BxOpcodeGroupSSE_0fd5 },
  /* 0F D5 /qm */ { BxPrefixSSE, BX_IA_PMULLW_PqQq, BxOpcodeGroupSSE_0fd5 },
  /* 0F D6 /qr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fd6R },
  /* 0F D6 /qm */ { BxPrefixSSE66, BX_IA_MOVQ_WqVqM },
  /* 0F D7 /qr */ { BxPrefixSSE, BX_IA_PMOVMSKB_GdPRq, BxOpcodeGroupSSE_0fd7 },
  /* 0F D7 /qm */ { 0, BX_IA_ERROR }, // PMOVMSKB is reg/reg form only
  /* 0F D8 /qr */ { BxPrefixSSE, BX_IA_PSUBUSB_PqQq, BxOpcodeGroupSSE_0fd8 },
  /* 0F D8 /qm */ { BxPrefixSSE, BX_IA_PSUBUSB_PqQq, BxOpcodeGroupSSE_0fd8 },
  /* 0F D9 /qr */ { BxPrefixSSE, BX_IA_PSUBUSW_PqQq, BxOpcodeGroupSSE_0fd9 },
  /* 0F D9 /qm */ { BxPrefixSSE, BX_IA_PSUBUSW_PqQq, BxOpcodeGroupSSE_0fd9 },
  /* 0F DA /qr */ { BxPrefixSSE, BX_IA_PMINUB_PqQq, BxOpcodeGroupSSE_0fda },
  /* 0F DA /qm */ { BxPrefixSSE, BX_IA_PMINUB_PqQq, BxOpcodeGroupSSE_0fda },
  /* 0F DB /qr */ { BxPrefixSSE, BX_IA_PAND_PqQq, BxOpcodeGroupSSE_0fdb },
  /* 0F DB /qm */ { BxPrefixSSE, BX_IA_PAND_PqQq, BxOpcodeGroupSSE_0fdb },
  /* 0F DC /qr */ { BxPrefixSSE, BX_IA_PADDUSB_PqQq, BxOpcodeGroupSSE_0fdc },
  /* 0F DC /qm */ { BxPrefixSSE, BX_IA_PADDUSB_PqQq, BxOpcodeGroupSSE_0fdc },
  /* 0F DD /qr */ { BxPrefixSSE, BX_IA_PADDUSW_PqQq, BxOpcodeGroupSSE_0fdd },
  /* 0F DD /qm */ { BxPrefixSSE, BX_IA_PADDUSW_PqQq, BxOpcodeGroupSSE_0fdd },
  /* 0F DE /qr */ { BxPrefixSSE, BX_IA_PMAXUB_PqQq, BxOpcodeGroupSSE_0fde },
  /* 0F DE /qm */ { BxPrefixSSE, BX_IA_PMAXUB_PqQq, BxOpcodeGroupSSE_0fde },
  /* 0F DF /qr */ { BxPrefixSSE, BX_IA_PANDN_PqQq, BxOpcodeGroupSSE_0fdf },
  /* 0F DF /qm */ { BxPrefixSSE, BX_IA_PANDN_PqQq, BxOpcodeGroupSSE_0fdf },
  /* 0F E0 /qr */ { BxPrefixSSE, BX_IA_PAVGB_PqQq, BxOpcodeGroupSSE_0fe0 },
  /* 0F E0 /qm */ { BxPrefixSSE, BX_IA_PAVGB_PqQq, BxOpcodeGroupSSE_0fe0 },
  /* 0F E1 /qr */ { BxPrefixSSE, BX_IA_PSRAW_PqQq, BxOpcodeGroupSSE_0fe1 },
  /* 0F E1 /qm */ { BxPrefixSSE, BX_IA_PSRAW_PqQq, BxOpcodeGroupSSE_0fe1 },
  /* 0F E2 /qr */ { BxPrefixSSE, BX_IA_PSRAD_PqQq, BxOpcodeGroupSSE_0fe2 },
  /* 0F E2 /qm */ { BxPrefixSSE, BX_IA_PSRAD_PqQq, BxOpcodeGroupSSE_0fe2 },
  /* 0F E3 /qr */ { BxPrefixSSE, BX_IA_PAVGW_PqQq, BxOpcodeGroupSSE_0fe3 },
  /* 0F E3 /qm */ { BxPrefixSSE, BX_IA_PAVGW_PqQq, BxOpcodeGroupSSE_0fe3 },
  /* 0F E4 /qr */ { BxPrefixSSE, BX_IA_PMULHUW_PqQq, BxOpcodeGroupSSE_0fe4 },
  /* 0F E4 /qm */ { BxPrefixSSE, BX_IA_PMULHUW_PqQq, BxOpcodeGroupSSE_0fe4 },
  /* 0F E5 /qr */ { BxPrefixSSE, BX_IA_PMULHW_PqQq, BxOpcodeGroupSSE_0fe5 },
  /* 0F E5 /qm */ { BxPrefixSSE, BX_IA_PMULHW_PqQq, BxOpcodeGroupSSE_0fe5 },
  /* 0F E6 /qr */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fe6 },
  /* 0F E6 /qm */ { BxPrefixSSE, BX_IA_ERROR, BxOpcodeGroupSSE_0fe6 },
  /* 0F E7 /qr */ { 0, BX_IA_ERROR }, // MOVNTQ/MOVNTDQ SSE group
  /* 0F E7 /qm */ { BxPrefixSSE, BX_IA_MOVNTQ_MqPq, BxOpcodeGroupSSE_0fe7M },
  /* 0F E8 /qr */ { BxPrefixSSE, BX_IA_PSUBSB_PqQq, BxOpcodeGroupSSE_0fe8 },
  /* 0F E8 /qm */ { BxPrefixSSE, BX_IA_PSUBSB_PqQq, BxOpcodeGroupSSE_0fe8 },
  /* 0F E9 /qr */ { BxPrefixSSE, BX_IA_PSUBSW_PqQq, BxOpcodeGroupSSE_0fe9 },
  /* 0F E9 /qm */ { BxPrefixSSE, BX_IA_PSUBSW_PqQq, BxOpcodeGroupSSE_0fe9 },
  /* 0F EA /qr */ { BxPrefixSSE, BX_IA_PMINSW_PqQq, BxOpcodeGroupSSE_0fea },
  /* 0F EA /qm */ { BxPrefixSSE, BX_IA_PMINSW_PqQq, BxOpcodeGroupSSE_0fea },
  /* 0F EB /qr */ { BxPrefixSSE, BX_IA_POR_PqQq, BxOpcodeGroupSSE_0feb },
  /* 0F EB /qm */ { BxPrefixSSE, BX_IA_POR_PqQq, BxOpcodeGroupSSE_0feb },
  /* 0F EC /qr */ { BxPrefixSSE, BX_IA_PADDSB_PqQq, BxOpcodeGroupSSE_0fec },
  /* 0F EC /qm */ { BxPrefixSSE, BX_IA_PADDSB_PqQq, BxOpcodeGroupSSE_0fec },
  /* 0F ED /qr */ { BxPrefixSSE, BX_IA_PADDSW_PqQq, BxOpcodeGroupSSE_0fed },
  /* 0F ED /qm */ { BxPrefixSSE, BX_IA_PADDSW_PqQq, BxOpcodeGroupSSE_0fed },
  /* 0F EE /qr */ { BxPrefixSSE, BX_IA_PMAXSW_PqQq, BxOpcodeGroupSSE_0fee },
  /* 0F EE /qm */ { BxPrefixSSE, BX_IA_PMAXSW_PqQq, BxOpcodeGroupSSE_0fee },
  /* 0F EF /qr */ { BxPrefixSSE, BX_IA_PXOR_PqQq, BxOpcodeGroupSSE_0fef },
  /* 0F EF /qm */ { BxPrefixSSE, BX_IA_PXOR_PqQq, BxOpcodeGroupSSE_0fef },
  /* 0F F0 /qr */ { 0, BX_IA_ERROR }, // LDDQU SSE group
  /* 0F F0 /wm */ { BxPrefixSSEF2, BX_IA_LDDQU_VdqMdq },
  /* 0F F1 /qr */ { BxPrefixSSE, BX_IA_PSLLW_PqQq, BxOpcodeGroupSSE_0ff1 },
  /* 0F F1 /qm */ { BxPrefixSSE, BX_IA_PSLLW_PqQq, BxOpcodeGroupSSE_0ff1 },
  /* 0F F2 /qr */ { BxPrefixSSE, BX_IA_PSLLD_PqQq, BxOpcodeGroupSSE_0ff2 },
  /* 0F F2 /qm */ { BxPrefixSSE, BX_IA_PSLLD_PqQq, BxOpcodeGroupSSE_0ff2 },
  /* 0F F3 /qr */ { BxPrefixSSE, BX_IA_PSLLQ_PqQq, BxOpcodeGroupSSE_0ff3 },
  /* 0F F3 /qm */ { BxPrefixSSE, BX_IA_PSLLQ_PqQq, BxOpcodeGroupSSE_0ff3 },
  /* 0F F4 /qr */ { BxPrefixSSE, BX_IA_PMULUDQ_PqQq, BxOpcodeGroupSSE_0ff4 },
  /* 0F F4 /qm */ { BxPrefixSSE, BX_IA_PMULUDQ_PqQq, BxOpcodeGroupSSE_0ff4 },
  /* 0F F5 /qr */ { BxPrefixSSE, BX_IA_PMADDWD_PqQq, BxOpcodeGroupSSE_0ff5 },
  /* 0F F5 /qm */ { BxPrefixSSE, BX_IA_PMADDWD_PqQq, BxOpcodeGroupSSE_0ff5 },
  /* 0F F6 /qr */ { BxPrefixSSE, BX_IA_PSADBW_PqQq, BxOpcodeGroupSSE_0ff6 },
  /* 0F F6 /qm */ { BxPrefixSSE, BX_IA_PSADBW_PqQq, BxOpcodeGroupSSE_0ff6 },
  /* 0F F7 /qr */ { BxPrefixSSE, BX_IA_MASKMOVQ_PqPRq, BxOpcodeGroupSSE_0ff7R },
  /* 0F F7 /qm */ { 0, BX_IA_ERROR }, // MASKMOV SSE group
  /* 0F F8 /qr */ { BxPrefixSSE, BX_IA_PSUBB_PqQq, BxOpcodeGroupSSE_0ff8 },
  /* 0F F8 /qm */ { BxPrefixSSE, BX_IA_PSUBB_PqQq, BxOpcodeGroupSSE_0ff8 },
  /* 0F F9 /qr */ { BxPrefixSSE, BX_IA_PSUBW_PqQq, BxOpcodeGroupSSE_0ff9 },
  /* 0F F9 /qm */ { BxPrefixSSE, BX_IA_PSUBW_PqQq, BxOpcodeGroupSSE_0ff9 },
  /* 0F FA /qr */ { BxPrefixSSE, BX_IA_PSUBD_PqQq, BxOpcodeGroupSSE_0ffa },
  /* 0F FA /qm */ { BxPrefixSSE, BX_IA_PSUBD_PqQq, BxOpcodeGroupSSE_0ffa },
  /* 0F FB /qr */ { BxPrefixSSE, BX_IA_PSUBQ_PqQq, BxOpcodeGroupSSE_0ffb },
  /* 0F FB /qm */ { BxPrefixSSE, BX_IA_PSUBQ_PqQq, BxOpcodeGroupSSE_0ffb },
  /* 0F FC /qr */ { BxPrefixSSE, BX_IA_PADDB_PqQq, BxOpcodeGroupSSE_0ffc },
  /* 0F FC /qm */ { BxPrefixSSE, BX_IA_PADDB_PqQq, BxOpcodeGroupSSE_0ffc },
  /* 0F FD /qr */ { BxPrefixSSE, BX_IA_PADDW_PqQq, BxOpcodeGroupSSE_0ffd },
  /* 0F FD /qm */ { BxPrefixSSE, BX_IA_PADDW_PqQq, BxOpcodeGroupSSE_0ffd },
  /* 0F FE /qr */ { BxPrefixSSE, BX_IA_PADDD_PqQq, BxOpcodeGroupSSE_0ffe },
  /* 0F FE /qm */ { BxPrefixSSE, BX_IA_PADDD_PqQq, BxOpcodeGroupSSE_0ffe },
  /* 0F FF /qr */ { 0, BX_IA_ERROR },
  /* 0F FF /qm */ { 0, BX_IA_ERROR }
};

  int BX_CPP_AttrRegparmN(3)
BX_CPU_C::fetchDecode64(const Bit8u *iptr, bxInstruction_c *i, unsigned remainingInPage)
{
  // remain must be at least 1
  unsigned remain = (remainingInPage < 15) ? remainingInPage : 15;

  unsigned b1, b2, ilen=0, attr, lock=0, ia_opcode = 0;
  unsigned imm_mode, offset = 512, rex_r = 0, rex_x = 0, rex_b = 0;
  unsigned rm = 0, mod = 0, nnn = 0, index = 0;
  unsigned seg = BX_SEG_REG_DS, seg_override = BX_SEG_REG_NULL;

#define SSE_PREFIX_NONE 0
#define SSE_PREFIX_66   1
#define SSE_PREFIX_F2   2
#define SSE_PREFIX_F3   3
  unsigned sse_prefix = SSE_PREFIX_NONE;
  unsigned rex_prefix = 0;

  i->ResolveModrm = 0;
  i->init(/*os32*/ 1,  // operand size 32 override defaults to 1
          /*as32*/ 1,  // address size 32 override defaults to 1
          /*os64*/ 0,  // operand size 64 override defaults to 0
          /*as64*/ 1); // address size 64 override defaults to 1

fetch_b1:
  b1 = *iptr++;
  ilen++;

  switch (b1) {
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4A:
    case 0x4B:
    case 0x4C:
    case 0x4D:
    case 0x4E:
    case 0x4F:
      rex_prefix = b1;
      if (ilen < remain) {
        goto fetch_b1;
      }
      return(-1);
    case 0x0f: // 2 byte escape
      if (ilen < remain) {
        ilen++;
        b1 = 0x100 | *iptr++;
        break;
      }
      return(-1);
    case 0xf2: // REPNE/REPNZ
    case 0xf3: // REP/REPE/REPZ
      rex_prefix = 0;
      sse_prefix = b1 & 3;
      i->setRepUsed(b1 & 3);
      if (ilen < remain) {
        goto fetch_b1;
      }
      return(-1);
    case 0x2e: // CS:
    case 0x26: // ES:
    case 0x36: // SS:
    case 0x3e: // DS:
      /* ignore segment override prefix */
      rex_prefix = 0;
      if (ilen < remain) {
        goto fetch_b1;
      }
      return(-1);
    case 0x64: // FS:
    case 0x65: // GS:
      rex_prefix = 0;
      seg_override = b1 & 0xf;
      if (ilen < remain) {
        goto fetch_b1;
      }
      return(-1);
    case 0x66: // OpSize
      rex_prefix = 0;
      if(!sse_prefix) sse_prefix = SSE_PREFIX_66;
      i->setOs32B(0);
      offset = 0;
      if (ilen < remain) {
        goto fetch_b1;
      }
      return(-1);
    case 0x67: // AddrSize
      rex_prefix = 0;
      i->setAs64B(0);
      if (ilen < remain) {
        goto fetch_b1;
      }
      return(-1);
    case 0xf0: // LOCK:
      rex_prefix = 0;
      lock = 1;
      if (ilen < remain) {
        goto fetch_b1;
      }
      return(-1);
    default:
      break;
  }

  if (rex_prefix) {
    i->assertExtend8bit();
    if (rex_prefix & 0x8) {
      i->assertOs64();
      i->assertOs32();
      offset = 512*2;
    }
    rex_r = ((rex_prefix & 0x4) << 1);
    rex_x = ((rex_prefix & 0x2) << 2);
    rex_b = ((rex_prefix & 0x1) << 3);
  }

  index = (b1+offset) << 1;

  attr = BxOpcodeInfo64[index].Attr;

  if (BxOpcodeHasModrm64[b1]) {

    unsigned b3 = 0;
    // handle 3-byte escape
    if ((attr & BxGroupX) == Bx3ByteOp) {
      if (ilen < remain) {
        ilen++;
        b3 = *iptr++;
      }
      else
        return(-1);
    }

    // opcode requires modrm byte
    if (ilen < remain) {
      ilen++;
      b2 = *iptr++;
    }
    else
      return(-1);

    // Parse mod-nnn-rm and related bytes
    mod = b2 & 0xc0;
    nnn = ((b2 >> 3) & 0x7) | rex_r;
    rm  = (b2 & 0x7) | rex_b;

    // MOVs with CRx and DRx always use register ops and ignore the mod field.
    if ((b1 & ~3) == 0x120)
      mod = 0xc0;

    i->setModRM(b2);
    i->setNnn(nnn);

    if (mod == 0xc0) { // mod == 11b
      i->setRm(rm);
      i->assertModC0();
      goto modrm_done;
    }

    index++;

    i->setRm(BX_TMP_REGISTER);
    i->setSibBase(rm);      // initialize with rm to use BxResolve32Base
    i->setSibIndex(BX_NIL_REGISTER);
    // initialize displ32 with zero to include cases with no diplacement
    i->modRMForm.displ32u = 0;

    if (i->as64L()) {
      // 64-bit addressing modes; note that mod==11b handled above
      i->ResolveModrm = &BX_CPU_C::BxResolve64Base;
      if ((rm & 0x7) != 4) { // no s-i-b byte
        if (mod == 0x00) { // mod == 00b
          if ((rm & 0x7) == 5) {
            i->setSibBase(BX_64BIT_REG_RIP);
            goto get_32bit_displ;
          }
          // mod==00b, rm!=4, rm!=5
          goto modrm_done;
        }
        // (mod == 0x40), mod==01b or (mod == 0x80), mod==10b
        seg = sreg_mod1or2_base32[rm];
      }
      else { // mod!=11b, rm==4, s-i-b byte follows
        unsigned sib, base, index, scale;
        if (ilen < remain) {
          sib = *iptr++;
          ilen++;
        }
        else {
          return(-1);
        }
        base  = (sib & 0x7) | rex_b; sib >>= 3;
        index = (sib & 0x7) | rex_x; sib >>= 3;
        scale =  sib;
        i->setSibScale(scale);
        i->setSibBase(base);
        if (index != 4) {
          i->ResolveModrm = &BX_CPU_C::BxResolve64BaseIndex;
          i->setSibIndex(index);
        }
        if (mod == 0x00) { // mod==00b, rm==4
          seg = sreg_mod0_base32[base];
          if ((base & 0x7) == 5) {
            i->setSibBase(BX_NIL_REGISTER);
            goto get_32bit_displ;
          }
          // mod==00b, rm==4, base!=5
          goto modrm_done;
        }
        // (mod == 0x40), mod==01b or (mod == 0x80), mod==10b
        seg = sreg_mod1or2_base32[base];
      }
    }
    else {
      // 32-bit addressing modes; note that mod==11b handled above
      i->ResolveModrm = &BX_CPU_C::BxResolve32Base;
      if ((rm & 0x7) != 4) { // no s-i-b byte
        if (mod == 0x00) { // mod == 00b
          if ((rm & 0x7) == 5) {
            i->setSibBase(BX_32BIT_REG_EIP);
            goto get_32bit_displ;
          }
          // mod==00b, rm!=4, rm!=5
          goto modrm_done;
        }
        // (mod == 0x40), mod==01b or (mod == 0x80), mod==10b
        seg = sreg_mod1or2_base32[rm];
      }
      else { // mod!=11b, rm==4, s-i-b byte follows
        unsigned sib, base, index, scale;
        if (ilen < remain) {
          sib = *iptr++;
          ilen++;
        }
        else {
          return(-1);
        }
        base  = (sib & 0x7) | rex_b; sib >>= 3;
        index = (sib & 0x7) | rex_x; sib >>= 3;
        scale =  sib;
        i->setSibBase(base);
        i->setSibScale(scale);
        if (index != 4) {
          i->ResolveModrm = &BX_CPU_C::BxResolve32BaseIndex;
          i->setSibIndex(index);
        }
        if (mod == 0x00) { // mod==00b, rm==4
          seg = sreg_mod0_base32[base];
          if ((base & 0x7) == 5) {
            i->setSibBase(BX_NIL_REGISTER);
            goto get_32bit_displ;
          }
          // mod==00b, rm==4, base!=5
          goto modrm_done;
        }
        // (mod == 0x40), mod==01b or (mod == 0x80), mod==10b
        seg = sreg_mod1or2_base32[base];
      }
    }

    // (mod == 0x40), mod==01b
    if (mod == 0x40) {
      if (ilen < remain) {
        // 8 sign extended to 32
        i->modRMForm.displ32u = (Bit8s) *iptr++;
        ilen++;
      }
      else {
        return(-1);
      }
    }
    else {

get_32bit_displ:

      // (mod == 0x80), mod==10b
      if ((ilen+3) < remain) {
        i->modRMForm.displ32u = FetchDWORD(iptr);
        iptr += 4;
        ilen += 4;
      }
      else {
        return(-1);
      }
    }

modrm_done:

    // Resolve ExecutePtr and additional opcode Attr
    const BxOpcodeInfo_t *OpcodeInfoPtr = &(BxOpcodeInfo64[index]);
    attr = BxOpcodeInfo64[index].Attr;

    while(attr & BxGroupX) {
      Bit32u Group = attr & BxGroupX;
      attr &= ~BxGroupX;

      switch(Group) {
        case BxGroupN:
          OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[nnn & 0x7]);
          break;
        case Bx3ByteOp:
          OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[b3]);
          break;
        case BxOSizeGrp:
          OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[offset >> 8]);
          break;
        case BxPrefixSSE:
          /* For SSE opcodes look into another table
                     with the opcode prefixes (NONE, 0x66, 0xF2, 0xF3) */
          if (sse_prefix) {
            OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[sse_prefix-1]);
            break;
          }
          continue;
        case BxPrefixSSE66:
          /* For SSE opcodes with prefix 66 only */
          if (sse_prefix != SSE_PREFIX_66) {
            OpcodeInfoPtr = &BxOpcodeGroupSSE_ERR[0]; // BX_IA_ERROR
          }
          continue;
        case BxPrefixSSEF2:
          /* For SSE opcodes with prefix F2 only */
          if (sse_prefix != SSE_PREFIX_F2) {
            OpcodeInfoPtr = &BxOpcodeGroupSSE_ERR[0]; // BX_IA_ERROR
          }
          continue;
        case BxPrefixSSEF3:
          /* For SSE opcodes with prefix F3 only */
          if (sse_prefix != SSE_PREFIX_F3) {
            OpcodeInfoPtr = &BxOpcodeGroupSSE_ERR[0]; // BX_IA_ERROR
          }
          continue;
        case BxFPEscape:
          OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[b2 & 0x3f]);
          break;
        default:
          BX_PANIC(("fetchdecode: Unknown opcode group"));
      }

      /* get additional attributes from group table */
      attr |= OpcodeInfoPtr->Attr;
    }

    ia_opcode = OpcodeInfoPtr->IA;
  }
  else {
    // Opcode does not require a MODRM byte.
    // Note that a 2-byte opcode (0F XX) will jump to before
    // the if() above after fetching the 2nd byte, so this path is
    // taken in all cases if a modrm byte is NOT required.

    const BxOpcodeInfo_t *OpcodeInfoPtr = &(BxOpcodeInfo64[index]);

    if (b1 == 0x90 && sse_prefix == SSE_PREFIX_F3) {
      ia_opcode = BX_IA_PAUSE;
    }
    else {
      unsigned group = attr & BxGroupX;
      BX_ASSERT(group == BxPrefixSSE || group == 0);
      if (group == BxPrefixSSE && sse_prefix)
        OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[sse_prefix-1]);

      ia_opcode = OpcodeInfoPtr->IA;
      i->setOpcodeReg((b1 & 7) | rex_b);
    }
  }

  if (lock) { // lock prefix invalid opcode
    // lock prefix not allowed or destination operand is not memory
    // mod == 0xc0 can't be BxLockable in fetchdecode tables
    if (/*(mod == 0xc0) ||*/ !(attr & BxLockable)) {
      BX_INFO(("LOCK prefix unallowed (op1=0x%x, mod=%u, nnn=%u)", b1, mod, nnn));
      // replace execution function with undefined-opcode
      ia_opcode = BX_IA_ERROR;
    }
  }

  i->modRMForm.Id = 0;
  imm_mode = attr & BxImmediate;
  if (imm_mode) {
    switch (imm_mode) {
      case BxImmediate_I1:
        i->modRMForm.Ib = 1;
        break;
      case BxImmediate_Ib:
        if (ilen < remain) {
          i->modRMForm.Ib = *iptr;
          ilen++;
        }
        else {
          return(-1);
        }
        break;
      case BxImmediate_Ib_SE: // Sign extend to OS size
        if (ilen < remain) {
          Bit8s temp8s = *iptr;
          if (i->os32L())
            i->modRMForm.Id = (Bit32s) temp8s;
          else
            i->modRMForm.Iw = (Bit16s) temp8s;
          ilen++;
        }
        else {
          return(-1);
        }
        break;
      case BxImmediate_Iw:
        if ((ilen+1) < remain) {
          i->modRMForm.Iw = FetchWORD(iptr);
          ilen += 2;
        }
        else {
          return(-1);
        }
        break;
      case BxImmediate_Id:
        if ((ilen+3) < remain) {
          i->modRMForm.Id = FetchDWORD(iptr);
          ilen += 4;
        }
        else {
          return(-1);
        }
        break;
      case BxImmediate_Iq: // MOV Rx,imm64
        if ((ilen+7) < remain) {
          i->IqForm.Iq = FetchQWORD(iptr);
          ilen += 8;
        }
        else {
          return(-1);
        }
        break;
      case BxImmediate_BrOff8:
        if (ilen < remain) {
          i->modRMForm.Id = (Bit8s) (*iptr);
          ilen++;
        }
        else {
          return(-1);
        }
        break;
      case BxImmediate_IwIb:
        if ((ilen+2) < remain) {
          i->IxIxForm.Iw = FetchWORD(iptr);
          iptr += 2;
          i->IxIxForm.Ib2 = *iptr;
          ilen += 3;
        }
        else return(-1);
        break;
      case BxImmediate_O:
        // For is which embed the address in the opcode.  Note
        // there is only 64/32-bit addressing available in long-mode.
        if (i->as64L()) {
          if ((ilen+7) < remain) {
            i->IqForm.Iq = FetchQWORD(iptr);
            ilen += 8;
          }
          else return(-1);
        }
        else { // as32
          if ((ilen+3) < remain) {
            i->IqForm.Iq = (Bit64u) FetchDWORD(iptr);
            ilen += 4;
          }
          else return(-1);
        }
        break;
      default:
        BX_INFO(("b1 was %x", b1));
        BX_PANIC(("fetchdecode: imm_mode = %u", imm_mode));
    }
  }

#if BX_SUPPORT_3DNOW
  if(b1 == 0x10f)
     ia_opcode = Bx3DNowOpcode[i->modRMForm.Ib];
#endif

  if (! BX_NULL_SEG_REG(seg_override))
     seg = seg_override;
  i->setSeg(seg);

  if (attr & BxArithDstRM) {
    i->setRm(nnn);
    i->setNnn(rm);
  }

  i->execute  = BxOpcodesTable[ia_opcode].execute1;
  i->execute2 = BxOpcodesTable[ia_opcode].execute2;

  i->setB1(b1);
  i->setILen(ilen);
  i->setIaOpcode(ia_opcode);

#if BX_SUPPORT_TRACE_CACHE
  if ((attr & BxTraceEnd) || ia_opcode == BX_IA_ERROR)
     return(1);
#endif

  return(0);
}

#endif /* if BX_SUPPORT_X86_64 */
