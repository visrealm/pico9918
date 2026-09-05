/**
 * \file
 * \brief pico9918-core - TMS9900 CPU interpreter (portable C)
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 *
 * This is a full reimplementation of JasonACT's RP2040 thumb assembly core
 * (thumb9900_m0.S / thumb9900_m33.S) intended for non-ARM targets. It aims
 * to be functionally identical where it matters to the GPU: the status flag layout
 * matches the assembly core (LGT=0x80, AGT=0x40, EQ=0x20, C=0x10, OV=0x08, P=0x04).
 * CRU (LDCR, STCR, SBO, SBZ, TB) and CKON/CKOF/LREX are no-ops in both cores.
 *
 * Memory layout follows the existing GPU glue: a flat 64 KiB byte array that
 * stores TMS9900 words in big-endian order. The workspace pointer (WP) is a
 * byte address into that array and register access uses big-endian word
 * loads/stores.
 *
 * The interpreter stops when bit0 of the control byte at regx38 is cleared,
 * or when an IDLE instruction is executed, returning the current PC just like
 * the assembly core. Behavior of auto-increment and index modes matches the
 * original core (increments are 1 for byte ops, 2 for word ops).
 */

#include "tms9900.h"

#include <stddef.h>


/*
 * Parity table for byte operations: P (bit 2) is set when the byte has an odd number
 * of 1-bits. Matches the PARITY table in thumb9900_m0.S.
 */
static const uint8_t parity_tbl[256] = {
  0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0, 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
  4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4, 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
  4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4, 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
  0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0, 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
  4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4, 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
  0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0, 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
  0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0, 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
  4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4, 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0};

/*
 * Flag helpers
 */
static inline void set_flags_word(Tms9900Cpu* cpu, uint16_t v)
{
  cpu->st &= 0x1E; /* preserve C/OV/P only (assembly uses AND #0x1E) */
  int16_t sv = (int16_t)v;
  if (v == 0)
  {
    cpu->st |= TMS_ST_EQ;
    return;
  }
  if (sv > 0)
  {
    cpu->st |= (TMS_ST_LGT | TMS_ST_AGT);
  }
  else
  {
    cpu->st |= TMS_ST_LGT;
  }
}

static inline void set_flags_byte(Tms9900Cpu* cpu, uint8_t v)
{
  /* Assembly OP_COMP_B: sets LGT/AGT/EQ + parity from PARITY table */
  cpu->st   = (uint16_t)((cpu->st & ~(TMS_ST_LGT | TMS_ST_AGT | TMS_ST_EQ | TMS_ST_P)) | parity_tbl[v]);
  int8_t sv = (int8_t)v;
  if (v == 0)
  {
    cpu->st |= TMS_ST_EQ;
    return;
  }
  if (sv > 0)
    cpu->st |= (TMS_ST_LGT | TMS_ST_AGT);
  else
    cpu->st |= TMS_ST_LGT;
}

/*
 * Memory helpers (big-endian words)
 */
static inline uint8_t rd8(uint8_t* m, uint16_t a)
{
  return m[a];
}

static inline void wr8(uint8_t* m, uint16_t a, uint8_t v)
{
  m[a] = v;
}

static inline uint16_t rd16(uint8_t* m, uint16_t a)
{
  return (uint16_t)((m[a] << 8) | m[(uint16_t)(a + 1)]);
}

static inline void wr16(uint8_t* m, uint16_t a, uint16_t v)
{
  m[a]                 = (uint8_t)(v >> 8);
  m[(uint16_t)(a + 1)] = (uint8_t)(v & 0xFF);
}

/* Report a write to an address the program chose - see Tms9900Cpu::onWrite, which
   also carries what this deliberately does not cover. */
#if defined(TMS9900_WATCH_WRITES)
static inline void watch_write(Tms9900Cpu* cpu, uint32_t addr)
{
  if (cpu->onWrite) cpu->onWrite(cpu->mem, addr);
}
#else
#define watch_write(cpu, addr) ((void)0)
#endif

/* Workspace address: WP + r*2 as uint32_t to handle WP=0xFFFE overflow
 * past the 64KB boundary into the workspace overflow area */
static inline uint32_t wp_addr(Tms9900Cpu* cpu, uint8_t r)
{
  return (uint32_t)cpu->wp + ((uint32_t)r << 1);
}

static inline uint16_t get_reg(Tms9900Cpu* cpu, uint8_t r)
{
  uint32_t a = wp_addr(cpu, r);
  return (uint16_t)((cpu->mem[a] << 8) | cpu->mem[a + 1]);
}

static inline void set_reg(Tms9900Cpu* cpu, uint8_t r, uint16_t v)
{
  uint32_t a      = wp_addr(cpu, r);
  cpu->mem[a]     = (uint8_t)(v >> 8);
  cpu->mem[a + 1] = (uint8_t)(v & 0xFF);
}

/* Operand addressing */
typedef struct Operand
{
  uint16_t addr; /* effective address for memory targets */
  uint16_t val;  /* value loaded */
  uint8_t reg;   /* register index */
  uint8_t mode;  /* 0=reg,1=indirect,2=indexed,3=auto-inc */
  uint8_t is_byte;
} Operand;

static inline uint16_t fetchw(Tms9900Cpu* cpu)
{
  uint32_t a = cpu->pc;
  uint16_t v = (uint16_t)((cpu->mem[a] << 8) | cpu->mem[a + 1]);
  cpu->pc    = (a + 2) & 0xFFFF;
  return v;
}

static Operand decode_operand(Tms9900Cpu* cpu, uint8_t field, uint8_t is_byte)
{
  Operand o = {0};
  o.mode    = (field >> 4) & 0x3;
  o.reg     = field & 0xF;
  o.is_byte = is_byte;

  switch (o.mode)
  {
  case 0: /* register direct */
    if (is_byte) o.addr = (uint16_t)wp_addr(cpu, o.reg); /* high byte address */
    o.val = is_byte ? cpu->mem[wp_addr(cpu, o.reg)] : get_reg(cpu, o.reg);
    break;
  case 1: /* indirect - assembly word-aligns effective address for word ops */
    o.addr = get_reg(cpu, o.reg);
    if (!is_byte) o.addr &= 0xFFFE;
    o.val = is_byte ? rd8(cpu->mem, o.addr) : rd16(cpu->mem, o.addr);
    break;
  case 2:
  { /* indexed - when reg==0, address is the immediate offset only (absolute) */
    uint16_t offset = fetchw(cpu);
    uint16_t ea;
    if (o.reg == 0)
      ea = offset; /* @address - no register added (assembly: CMP R5,#0; BEQ skip_add) */
    else
      ea = (uint16_t)(get_reg(cpu, o.reg) + offset);
    o.addr = is_byte ? ea : (ea & 0xFFFE);
    o.val  = is_byte ? rd8(cpu->mem, o.addr) : rd16(cpu->mem, o.addr);
    break;
  }
  case 3:
  { /* auto-increment: effective address = old register value (word-aligned for word ops),
       * register is updated to old+inc (assembly does this before returning the address) */
    uint16_t raw = get_reg(cpu, o.reg);
    uint16_t inc = is_byte ? 1u : 2u;
    set_reg(cpu, o.reg, (uint16_t)(raw + inc));
    o.addr = is_byte ? raw : (raw & 0xFFFE);
    o.val  = is_byte ? rd8(cpu->mem, o.addr) : rd16(cpu->mem, o.addr);
    break;
  }
  }
  return o;
}

static void store_operand(Tms9900Cpu* cpu, const Operand* o, uint16_t v)
{
  if (o->mode == 0)
  {
    if (o->is_byte)
    {
      cpu->mem[wp_addr(cpu, o->reg)] = (uint8_t)v;
    }
    else
    {
      set_reg(cpu, o->reg, v);
    }
  }
  else
  {
    if (o->is_byte)
    {
      wr8(cpu->mem, o->addr, (uint8_t)v);
    }
    else
    {
      wr16(cpu->mem, o->addr, v);
    }
    watch_write(cpu, o->addr);
  }
}

/* ALU helpers */
static inline uint16_t add16(Tms9900Cpu* cpu, uint16_t a, uint16_t b)
{
  uint32_t res = (uint32_t)a + (uint32_t)b;
  uint16_t r16 = (uint16_t)res;
  cpu->st &= 0x06; /* preserve only parity bit; clear LGT/AGT/EQ/OV/C */
  if (res & 0x10000) cpu->st |= TMS_ST_C;
  /* overflow: sign(a)==sign(b) and sign differs from result */
  if (((a ^ b) & 0x8000) == 0 && ((a ^ r16) & 0x8000)) cpu->st |= TMS_ST_OV;
  set_flags_word(cpu, r16);
  return r16;
}

static inline uint16_t sub16(Tms9900Cpu* cpu, uint16_t a, uint16_t b)
{
  uint32_t res = (uint32_t)a - (uint32_t)b;
  uint16_t r16 = (uint16_t)res;
  cpu->st &= 0x06;
  /* Assembly "borrow NOT" convention: carry=1 means no borrow (src==0 or dst>=result) */
  if (b == 0 || a >= r16) cpu->st |= TMS_ST_C;
  /* overflow: sign(a)!=sign(b) and sign differs from result */
  if (((a ^ b) & 0x8000) && ((a ^ r16) & 0x8000)) cpu->st |= TMS_ST_OV;
  set_flags_word(cpu, r16);
  return r16;
}

static inline uint8_t add8(Tms9900Cpu* cpu, uint8_t a, uint8_t b)
{
  uint16_t res = (uint16_t)a + (uint16_t)b;
  uint8_t r8   = (uint8_t)res;
  cpu->st &= 0x06;
  if (res & 0x100) cpu->st |= TMS_ST_C;
  if (((a ^ b) & 0x80) == 0 && ((a ^ r8) & 0x80)) cpu->st |= TMS_ST_OV;
  set_flags_byte(cpu, r8);
  return r8;
}

static inline uint8_t sub8(Tms9900Cpu* cpu, uint8_t a, uint8_t b)
{
  uint16_t res = (uint16_t)a - (uint16_t)b;
  uint8_t r8   = (uint8_t)res;
  cpu->st &= 0x06;
  /* Assembly "borrow NOT" convention: carry=1 means no borrow (src==0 or dst>=result) */
  if (b == 0 || a >= r8) cpu->st |= TMS_ST_C;
  if (((a ^ b) & 0x80) && ((a ^ r8) & 0x80)) cpu->st |= TMS_ST_OV;
  set_flags_byte(cpu, r8);
  return r8;
}

/* cmp16: compare src against dst (first operand vs second operand in TMS9900 convention).
 * Assembly I_C: CMP R5,R2 where R5=src, R2=dst. LGT set when src > dst (unsigned).
 * Assembly I_CI: CMP R2,R5 where R2=dst, R5=imm. LGT set when dst > imm (unsigned).
 * Both callers must pass (first_operand, second_operand) in the correct TMS9900 order. */
static inline void cmp16(Tms9900Cpu* cpu, uint16_t first, uint16_t second)
{
  /* Compare sets LGT/AGT/EQ only; C and OV are preserved */
  cpu->st &= 0x1E;
  if (first == second)
  {
    cpu->st |= TMS_ST_EQ;
    return;
  }
  if (first > second) cpu->st |= TMS_ST_LGT;                   /* unsigned greater */
  if ((int16_t)first > (int16_t)second) cpu->st |= TMS_ST_AGT; /* signed greater */
}

static inline void cmp8(Tms9900Cpu* cpu, uint8_t src, uint8_t dst)
{
  /* Assembly I_CB: preserves C/OV, sets parity of SOURCE, then sets LGT/AGT/EQ */
  cpu->st = (uint16_t)((cpu->st & 0x1A) | parity_tbl[src]);
  if (src == dst)
  {
    cpu->st |= TMS_ST_EQ;
    return;
  }
  if (src > dst) cpu->st |= TMS_ST_LGT;
  if ((int8_t)src > (int8_t)dst) cpu->st |= TMS_ST_AGT;
}

static inline uint16_t slx16(Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  cpu->st &= 0x06;
  if (count == 0) count = 16;
  uint32_t vv          = (uint32_t)v;
  uint32_t mask_change = 0;
  /* detect overflow like assembly: if sign changes during shift */
  for (uint8_t i = 0; i < count; ++i)
  {
    uint32_t msb = vv & 0x8000;
    vv <<= 1;
    if (msb != (vv & 0x8000)) mask_change = 1;
  }
  if (vv & 0x10000) cpu->st |= TMS_ST_C;
  uint16_t r = (uint16_t)vv;
  if (mask_change) cpu->st |= TMS_ST_OV;
  set_flags_word(cpu, r);
  return r;
}

static inline uint16_t sra16(Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  /* Assembly: MOVS R4,#0x0E; ANDS R1,R4 - keeps OV/P/bit1, clears C (and LGT/AGT/EQ) */
  cpu->st &= 0x0E;
  if (count == 0) count = 16;
  int32_t vv     = (int32_t)(int16_t)v;
  uint16_t carry = 0;
  for (uint8_t i = 0; i < count; ++i)
  {
    carry = (uint16_t)((uint32_t)vv & 1u);
    vv >>= 1; /* arithmetic right shift on signed preserves sign bit */
  }
  if (carry) cpu->st |= TMS_ST_C;
  uint16_t r = (uint16_t)vv;
  set_flags_word(cpu, r);
  return r;
}

static inline uint16_t srl16(Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  /* Assembly: MOVS R4,#0x0E; ANDS R1,R4 - keeps OV/P/bit1, clears C */
  cpu->st &= 0x0E;
  if (count == 0) count = 16;
  uint32_t vv    = v;
  uint16_t carry = 0;
  for (uint8_t i = 0; i < count; ++i)
  {
    carry = (uint16_t)(vv & 1u);
    vv >>= 1;
  }
  if (carry) cpu->st |= TMS_ST_C;
  uint16_t r = (uint16_t)vv;
  set_flags_word(cpu, r);
  return r;
}

static inline uint16_t src16(Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  /* Assembly: MOVS R4,#0x0E; ANDS R1,R4 - keeps OV/P/bit1, clears C */
  cpu->st &= 0x0E;
  if (count == 0) count = 16;
  /* Assembly: ORRS R0 = (v<<16)|v, then RORS by count. Lower 16 bits = v rotated right.
   * Carry = bit(count-1) of v (last bit shifted out). Use 32-bit to avoid shift-by-16 UB. */
  uint32_t vv    = v;
  uint16_t r     = (uint16_t)((vv >> count) | (vv << (16u - count)));
  uint16_t carry = (uint16_t)((vv >> (count - 1u)) & 1u);
  if (carry) cpu->st |= TMS_ST_C;
  set_flags_word(cpu, r);
  return r;
}

static inline uint16_t slc16(Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  /* Assembly: MOVS R4,#0x0E; ANDS R1,R4 - keeps OV/P/bit1, clears C */
  cpu->st &= 0x0E;
  if (count == 0) count = 16;
  count &= 0x1F;
  uint32_t vv  = v;
  uint32_t rot = (vv << count) | (vv >> (16 - count));
  uint16_t r   = (uint16_t)rot;
  /* Carry = bit just before wrap (count-1) */
  uint16_t carry = (uint16_t)((vv << (count - 1)) & 0x8000u);
  if (carry) cpu->st |= TMS_ST_C;
  set_flags_word(cpu, r);
  return r;
}

static inline uint16_t src_through_c(Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  cpu->st &= 0x06;
  if (count == 0) count = 16;
  uint32_t vv = ((uint32_t)v << 1) | ((cpu->st & TMS_ST_C) ? 1u : 0u);
  for (uint8_t i = 0; i < count; ++i)
  {
    uint32_t c = vv & 1u;
    vv >>= 1;
    if (c) vv |= 0x8000u;
    cpu->st = (cpu->st & ~TMS_ST_C) | (c ? TMS_ST_C : 0);
  }
  uint16_t r = (uint16_t)vv;
  set_flags_word(cpu, r);
  return r;
}

/* Forward declarations (needed for X instruction dispatch) */
static inline void handle_two_operand(Tms9900Cpu* cpu, uint16_t inst);
static inline void handle_format9(Tms9900Cpu* cpu, uint16_t inst);
static inline int handle_branch_group(Tms9900Cpu* cpu, uint16_t inst);
static inline void handle_shift_rotate(Tms9900Cpu* cpu, uint16_t inst);
static inline void handle_f18a_stack(Tms9900Cpu* cpu, uint16_t inst);

/* Function:  handle_immediate_system
 * ----------------------------------------
 * Execute op group 0 (immediate/system). Returns 0 to stop on IDLE.
 */
static inline int handle_immediate_system(Tms9900Cpu* cpu, uint16_t inst)
{
  /* Sub-opcode is (inst >> 5) & 0x1F; register is inst & 0xF */
  uint8_t sub      = (uint8_t)((inst >> 5) & 0x1F);
  uint8_t dest_reg = inst & 0xF;

  switch (sub)
  {
  case 0x10: /* LI - 0x0200 */
  {
    uint16_t imm = fetchw(cpu);
    set_reg(cpu, dest_reg, imm);
    cpu->st &= 0x1E;
    set_flags_word(cpu, imm);
    break;
  }
  case 0x11: /* AI - 0x0220 */
  {
    uint16_t imm = fetchw(cpu);
    uint16_t res = add16(cpu, get_reg(cpu, dest_reg), imm);
    set_reg(cpu, dest_reg, res);
    break;
  }
  case 0x12: /* ANDI - 0x0240 */
  {
    uint16_t imm = fetchw(cpu);
    uint16_t res = get_reg(cpu, dest_reg) & imm;
    set_reg(cpu, dest_reg, res);
    cpu->st &= 0x1E;
    set_flags_word(cpu, res);
    break;
  }
  case 0x13: /* ORI - 0x0260 */
  {
    uint16_t imm = fetchw(cpu);
    uint16_t res = get_reg(cpu, dest_reg) | imm;
    set_reg(cpu, dest_reg, res);
    cpu->st &= 0x1E;
    set_flags_word(cpu, res);
    break;
  }
  case 0x14: /* CI - 0x0280 */
  {
    uint16_t imm = fetchw(cpu);
    cmp16(cpu, get_reg(cpu, dest_reg), imm);
    break;
  }
  case 0x15: /* STWP - 0x02A0 - store WP into dest register */ set_reg(cpu, dest_reg, cpu->wp); break;
  case 0x16: /* STST - 0x02C0 - store ST into dest register */
    set_reg(cpu, dest_reg, (uint16_t)cpu->st << 8);
    break;
  case 0x17: /* LWPI - 0x02E0 - load WP immediate (word-aligned) */ cpu->wp = fetchw(cpu) & 0xFFFE; break;
  case 0x18:     /* LIMI - 0x0300 - load interrupt mask (skip imm word) */
    fetchw(cpu); /* consume immediate, ignore (no interrupt mask in this core) */
    break;
  case 0x1A: /* IDLE - 0x0340 - stop execution */ return 0;
  case 0x1C: /* RTWP - 0x0380 */
  {
    /* Restore ST/PC/WP from R15/R14/R13 (offsets 30/28/26) of current workspace */
    uint32_t owp = (uint32_t)cpu->wp;
    cpu->st      = cpu->mem[owp + 30];
    cpu->pc      = (uint16_t)((cpu->mem[owp + 28] << 8) | cpu->mem[owp + 29]) & 0xFFFE;
    cpu->wp      = (uint16_t)((cpu->mem[owp + 26] << 8) | cpu->mem[owp + 27]) & 0xFFFE;
    break;
  }
  case 0x1D: /* CKON - 0x03A0 */
  case 0x1E: /* CKOF - 0x03C0 */
  case 0x1F: /* LREX - 0x03E0 */ break;
  default: break;
  }
  return 1;
}

/* Function:  handle_jump_single
 * ----------------------------------------
 * Execute single-operand instructions (opcodes 0x0400-0x07FF).
 * Sub-opcode is bits 10:6 of the instruction word.
 */
static inline void handle_jump_single(Tms9900Cpu* cpu, uint16_t inst)
{
  uint8_t sub = (inst >> 6) & 0x1F; /* bits 10:6 */
  switch (sub)
  {
  case 0x10: /* BLWP */
  {
    Operand s = decode_operand(cpu, inst & 0x3F, 0);
    uint32_t src_addr = (s.mode == 0) ? wp_addr(cpu, inst & 0xF) : (uint32_t)s.addr;
    uint16_t new_wp   = (uint16_t)((cpu->mem[src_addr] << 8) | cpu->mem[src_addr + 1]) & 0xFFFE;
    uint16_t old_wp = cpu->wp;
    uint16_t old_pc = cpu->pc;
    uint16_t old_st = cpu->st;
    cpu->wp         = new_wp;
    /* new workspace is always in normal address space (not overflow) */
    wr16(cpu->mem, (uint16_t)(new_wp + 26), old_wp);
    wr16(cpu->mem, (uint16_t)(new_wp + 28), old_pc);
    wr16(cpu->mem, (uint16_t)(new_wp + 30), (uint16_t)old_st << 8); /* ST→high byte, low byte=0 */
    cpu->pc = (uint16_t)((cpu->mem[src_addr + 2] << 8) | cpu->mem[src_addr + 3]) & 0xFFFE;
    /* Assembly does NOT clear ST - new context inherits caller's flags */
    break;
  }
  case 0x11: /* B - branch to source operand address */
  {
    Operand s       = decode_operand(cpu, inst & 0x3F, 0);
    uint32_t target = (s.mode == 0) ? wp_addr(cpu, inst & 0xF) : (uint32_t)s.addr;
    cpu->pc         = target & 0xFFFE;
    break;
  }
  case 0x12: /* X - execute instruction at source */
  {
    Operand s       = decode_operand(cpu, inst & 0x3F, 0);
    uint16_t x_inst = (s.mode == 0) ? s.val : rd16(cpu->mem, s.addr);
    /* Dispatch the fetched instruction (PC is NOT advanced by X itself) */
    uint8_t x_hi = (uint8_t)(x_inst >> 8);
    if (x_hi >= 0x40)
      handle_two_operand(cpu, x_inst);
    else if (x_hi >= 0x20)
      handle_format9(cpu, x_inst);
    else if (x_hi >= 0x10)
      handle_branch_group(cpu, x_inst);
    else if (x_hi >= 0x0C)
    {
      if (x_hi == 0x0E)
        handle_shift_rotate(cpu, x_inst);
      else
        handle_f18a_stack(cpu, x_inst);
    }
    else if (x_hi >= 0x08)
      handle_shift_rotate(cpu, x_inst);
    else if (x_hi >= 0x04)
      handle_jump_single(cpu, x_inst);
    else
      handle_immediate_system(cpu, x_inst);
    break;
  }
  case 0x13: /* CLR - no flag update (assembly does not touch ST) */
  {
    Operand d = decode_operand(cpu, inst & 0x3F, 0);
    store_operand(cpu, &d, 0);
    break;
  }
  case 0x14: /* NEG */
  {
    Operand d = decode_operand(cpu, inst & 0x3F, 0);
    cpu->st &= 0x06;
    if (d.val == 0x8000u)
    {
      cpu->st |= TMS_ST_OV;
      set_flags_word(cpu, d.val); /* flags on 0x8000 = LGT only */
    }
    else
    {
      uint16_t res = (uint16_t)(0u - d.val);
      if (res == 0) cpu->st |= TMS_ST_C;
      store_operand(cpu, &d, res);
      set_flags_word(cpu, res);
    }
    break;
  }
  case 0x15: /* INV */
  {
    Operand d    = decode_operand(cpu, inst & 0x3F, 0);
    uint16_t res = (uint16_t)~d.val;
    store_operand(cpu, &d, res);
    cpu->st &= 0x1E;
    set_flags_word(cpu, res);
    break;
  }
  case 0x16: /* INC */
  {
    Operand d    = decode_operand(cpu, inst & 0x3F, 0);
    uint16_t res = add16(cpu, d.val, 1);
    store_operand(cpu, &d, res);
    break;
  }
  case 0x17: /* INCT */
  {
    Operand d    = decode_operand(cpu, inst & 0x3F, 0);
    uint16_t res = add16(cpu, d.val, 2);
    store_operand(cpu, &d, res);
    break;
  }
  case 0x18: /* DEC */
  {
    Operand d    = decode_operand(cpu, inst & 0x3F, 0);
    uint16_t res = sub16(cpu, d.val, 1);
    store_operand(cpu, &d, res);
    break;
  }
  case 0x19: /* DECT */
  {
    Operand d    = decode_operand(cpu, inst & 0x3F, 0);
    uint16_t res = sub16(cpu, d.val, 2);
    store_operand(cpu, &d, res);
    break;
  }
  case 0x1A: /* BL - branch and link, save PC to R11 */
  {
    Operand s = decode_operand(cpu, inst & 0x3F, 0);
    set_reg(cpu, 11, (uint16_t)cpu->pc);
    uint32_t target = (s.mode == 0) ? wp_addr(cpu, inst & 0xF) : (uint32_t)s.addr;
    cpu->pc         = target & 0xFFFE;
    break;
  }
  case 0x1B: /* SWPB */
  {
    Operand d    = decode_operand(cpu, inst & 0x3F, 0);
    uint16_t res = (uint16_t)((d.val << 8) | (d.val >> 8));
    store_operand(cpu, &d, res);
    break;
  }
  case 0x1C: /* SETO - no flag update (assembly does not touch ST) */
  {
    Operand d = decode_operand(cpu, inst & 0x3F, 0);
    store_operand(cpu, &d, 0xFFFF);
    break;
  }
  case 0x1D: /* ABS */
  {
    Operand d  = decode_operand(cpu, inst & 0x3F, 0);
    int16_t sv = (int16_t)d.val;
    /* Assembly: ST &= 0x06 (clears C and OV, keeps only P/bit1) */
    cpu->st &= 0x06;
    if (d.val == 0x8000u)
    {
      /* Overflow case: can't negate 0x8000; set OV, leave value unchanged */
      cpu->st |= TMS_ST_OV;
      set_flags_word(cpu, d.val);
    }
    else if (sv < 0)
    {
      uint16_t res = (uint16_t)(0u - d.val);
      store_operand(cpu, &d, res);
      set_flags_word(cpu, d.val);
    }
    else
    {
      /* Positive/zero: flags only, no store */
      set_flags_word(cpu, d.val);
    }
    break;
  }
  case 0x1E: /* LDCR - CRU not emulated */
  case 0x1F: /* STCR - CRU not emulated */
  default: break;
  }
}

/* Function:  handle_branch_group
 * ----------------------------------------
 * Execute conditional/unconditional jumps (opcodes 0x1000-0x1FFF).
 * Displacement is a signed byte in bits 7:0, in word units (×2).
 * Returns 0 if JMP self-loop detected (signals stop like IDLE), else 1.
 */
static inline int handle_branch_group(Tms9900Cpu* cpu, uint16_t inst)
{
  int16_t disp = (int16_t)((int8_t)(inst & 0xFF)) * 2;
  uint8_t cond = (inst >> 8) & 0xF; /* 0x0=JMP, 0x1=JLT, ... */

  switch (cond)
  {
  case 0x0: /* JMP - unconditional */
    /* Assembly: self-jump (disp==-2, i.e. JMP $) treated as IDLE - exit emulation */
    if (disp == -2)
    {
      cpu->pc = (cpu->pc - 2) & 0xFFFF;
      return 0;
    }
    cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0x1: /* JLT - signed < (AGT=0 and EQ=0) */
    if ((cpu->st & (TMS_ST_AGT | TMS_ST_EQ)) == 0) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0x2: /* JLE - unsigned ≤ (LGT=0 or EQ=1) */
    if (!(cpu->st & TMS_ST_LGT) || (cpu->st & TMS_ST_EQ)) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0x3: /* JEQ */
    if (cpu->st & TMS_ST_EQ) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0x4: /* JHE - unsigned ≥ (LGT=1 or EQ=1) */
    if (cpu->st & (TMS_ST_LGT | TMS_ST_EQ)) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0x5: /* JGT - signed > (AGT=1) */
    if (cpu->st & TMS_ST_AGT) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0x6: /* JNE */
    if (!(cpu->st & TMS_ST_EQ)) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0x7: /* JNC - no carry */
    if (!(cpu->st & TMS_ST_C)) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0x8: /* JOC - carry set */
    if (cpu->st & TMS_ST_C) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0x9: /* JNO - no overflow */
    if (!(cpu->st & TMS_ST_OV)) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0xA: /* JL - unsigned < (LGT=0 and EQ=0) */
    if (!(cpu->st & (TMS_ST_LGT | TMS_ST_EQ))) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0xB: /* JH - unsigned > (LGT=1 and EQ=0) */
    if ((cpu->st & TMS_ST_LGT) && !(cpu->st & TMS_ST_EQ)) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  case 0xC: /* JOP - parity */
    if (cpu->st & TMS_ST_P) cpu->pc = (cpu->pc + disp) & 0xFFFF;
    break;
  /* 0xD=SBO, 0xE=SBZ: CRU ops, NOP */
  case 0xF: /* TB - CRU test, clears EQ */ cpu->st &= ~TMS_ST_EQ; break;
  default: break;
  }
  return 1;
}

/* Function:  handle_cru_single_bit
 * ----------------------------------------
 * Execute op group 3 (CRU single-bit) - treated as NOP here.
 */
static inline void handle_cru_single_bit(void) {}

/* Function:  handle_shift_rotate
 * ----------------------------------------
 * Execute op group 4 (shift/rotate).
 */
static inline void handle_shift_rotate(Tms9900Cpu* cpu, uint16_t inst)
{
  uint8_t sub = (inst >> 8) & 0xF;
  uint8_t count = (inst >> 4) & 0xF;
  uint8_t reg   = inst & 0xF;
  if (count == 0)
  {
    /* count=0: take low nibble of R0; if still 0, use 16 */
    count = get_reg(cpu, 0) & 0xF;
    if (count == 0) count = 16;
  }
  uint16_t v   = get_reg(cpu, reg);
  uint16_t res = v;
  switch (sub)
  {
  case 0x8: res = sra16(cpu, v, count); break; /* SRA */
  case 0x9: res = srl16(cpu, v, count); break; /* SRL */
  case 0xA: res = slx16(cpu, v, count); break; /* SLA */
  case 0xB: res = src16(cpu, v, count); break; /* SRC */
  case 0xE: res = slc16(cpu, v, count); break; /* SLC (F18A) */
  default: break;
  }
  set_reg(cpu, reg, res);
}

/* Handle COC/CZC/XOR/MPY/DIV (opcodes 0x2000-0x3FFF) */
static inline void handle_format9(Tms9900Cpu* cpu, uint16_t inst)
{
  /* Bits 13:10 identify the instruction group (0x2000>>10=8, 0x2400>>10=9, etc.) */
  uint8_t opcode = (uint8_t)((inst >> 10) & 0xF);
  /* Dest register in bits 9:6, source operand in bits 5:0 */
  uint8_t dreg = (inst >> 6) & 0xF;
  Operand src  = decode_operand(cpu, inst & 0x3F, 0);

  switch (opcode)
  {
  case 0x8: /* 0x2000 COC - EQ if (src & dst) == src */
  {
    uint16_t d = get_reg(cpu, dreg);
    if ((d & src.val) == src.val)
      cpu->st |= TMS_ST_EQ;
    else
      cpu->st &= (uint16_t)~TMS_ST_EQ;
    break;
  }
  case 0x9: /* 0x2400 CZC - EQ if (src & dst) == 0 */
  {
    uint16_t d = get_reg(cpu, dreg);
    if ((d & src.val) == 0)
      cpu->st |= TMS_ST_EQ;
    else
      cpu->st &= (uint16_t)~TMS_ST_EQ;
    break;
  }
  case 0xA: /* 0x2800 XOR */
  {
    uint16_t res = get_reg(cpu, dreg) ^ src.val;
    set_reg(cpu, dreg, res);
    cpu->st &= 0x1E;
    set_flags_word(cpu, res);
    break;
  }
  case 0xB: /* 0x2C00-0x2FFF: XOP/F18A PIX */
  {
    static const uint8_t pix_mask[]  = {0xC0, 0x30, 0x0C, 0x03};
    static const uint8_t pix_shift[] = {6, 4, 2, 0};

    uint16_t flags = get_reg(cpu, dreg);
    uint16_t xy    = src.val;
    uint8_t x      = (uint8_t)(xy >> 8);
    uint8_t y      = (uint8_t)(xy & 0xFF);

    if (flags & 0x8000) /* PIX_M: BM mode */
    {
      /* Calculate pattern name table byte offset from X,Y (E/A 335-336) */
      uint16_t r = (uint16_t)(((uint16_t)y << 5) | y);
      r &= (uint16_t)~0xF8;
      r |= (uint16_t)(x & 0xF8);

      uint8_t vr04 = cpu->mem[0x6004];
      r |= (uint16_t)((vr04 & 0x04) << 11);

      set_reg(cpu, dreg, r);
    }
    else /* BL mode */
    {
      uint8_t vr35   = cpu->mem[0x6023];
      uint16_t width = (vr35 == 0) ? 256u : (uint16_t)vr35;

      /* Four pixels a byte, so the row stride rounds up, and the address wraps in 16KB. */
      uint16_t stride = (uint16_t)((width + 3) >> 2);
      uint8_t vr32    = cpu->mem[0x6020];
      uint16_t a      = (uint16_t)((((uint16_t)vr32 << 6) + y * stride + (x >> 2)) & 0x3FFF);

      if (flags & 0x4000) /* PIX_A: address only */
      {
        set_reg(cpu, dreg, a);
        break;
      }

      uint8_t s    = (uint8_t)(x & 0x03);
      uint8_t b    = cpu->mem[a];
      uint8_t pixv = (uint8_t)((b & pix_mask[s]) >> pix_shift[s]);

      /* Write logic */
      int do_write = 0;
      if (!(flags & 0x0400)) /* bit 10 clear: writes allowed */
      {
        if (!(flags & 0x0200)) /* bit 9 clear: unconditional write */
        {
          do_write = 1;
        }
        else /* conditional write */
        {
          uint8_t pp_cmp = (uint8_t)((flags >> 4) & 0x03);
          if (flags & 0x0100) /* PIX_E set: not-equal test */
          {
            if (pixv == pp_cmp) do_write = 1;
          }
          else /* equal test */
          {
            if (pixv != pp_cmp) do_write = 1;
          }
        }
      }

      if (do_write)
      {
        uint8_t pp_wr = (uint8_t)(flags & 0x03);
        b             = (uint8_t)((b & ~pix_mask[s]) | (pp_wr << pix_shift[s]));
        cpu->mem[a]   = b;
        watch_write(cpu, a);
      }

      if (flags & 0x0800) /* PIX_R: read back pixel into dest reg */
      {
        flags = (uint16_t)((flags & ~0x03) | pixv);
        set_reg(cpu, dreg, flags);
      }
    }
    break;
  }
  case 0xE: /* 0x3800 MPY */
  {
    uint32_t prod = (uint32_t)get_reg(cpu, dreg) * (uint32_t)src.val;
    set_reg(cpu, dreg, (uint16_t)(prod >> 16));
    set_reg(cpu, (uint8_t)(dreg + 1), (uint16_t)(prod & 0xFFFF));
    break;
  }
  case 0xF: /* 0x3C00 DIV */
  {
    uint32_t dividend = ((uint32_t)get_reg(cpu, dreg) << 16) | get_reg(cpu, (uint8_t)(dreg + 1));
    if (src.val == 0 || (dividend >> 16) >= src.val)
    {
      cpu->st |= TMS_ST_OV;
      break;
    }
    uint16_t quo = (uint16_t)(dividend / src.val);
    uint16_t rem = (uint16_t)(dividend % src.val);
    set_reg(cpu, dreg, quo);
    set_reg(cpu, (uint8_t)(dreg + 1), rem);
    cpu->st &= (uint16_t)~TMS_ST_OV;
    break;
  }
  default: break;
  }
}

/* Handle F18A stack ops: RET/CALL/PUSH/POP (opcodes 0x0C00-0x0DFF, 0x0F00-0x0FFF) */
static inline void handle_f18a_stack(Tms9900Cpu* cpu, uint16_t inst)
{
  uint8_t hi = (inst >> 8) & 0xF; /* C=RET/CALL, D=PUSH, F=POP */
  switch (hi)
  {
  case 0xC:
  {
    /* RET=0x0C00 (bits 6:0 of inst are 0), CALL=0x0C40+ (bit 6 set) */
    if (inst & 0xC0)
    { /* CALL - push PC at OLD R15, pre-decrement R15 by 2, branch to source */
      Operand s       = decode_operand(cpu, inst & 0x3F, 0);
      uint16_t old_sp = get_reg(cpu, 15) & 0xFFFE;
      uint16_t new_sp = (uint16_t)(old_sp - 2);
      set_reg(cpu, 15, new_sp);
      wr16(cpu->mem, old_sp, (uint16_t)cpu->pc); /* write at OLD sp, not new sp */
      uint32_t target = (s.mode == 0) ? wp_addr(cpu, inst & 0xF) : (uint32_t)s.addr;
      cpu->pc         = target & 0xFFFE;
    }
    else
    { /* RET - read PC from R15+2 (OLD R15), then post-increment R15 by 2 */
      /* Assembly: ADD R4,R8; LDR R5,[R4,#2]; ... R15 += 2 */
      uint16_t sp = get_reg(cpu, 15) & 0xFFFE;
      cpu->pc     = rd16(cpu->mem, (uint16_t)(sp + 2)) & 0xFFFE;
      set_reg(cpu, 15, (uint16_t)(sp + 2));
    }
    break;
  }
  case 0xD: /* PUSH - write value at OLD R15, decrement R15 by 2 */
  {
    /* Assembly: R4=old_sp; R2=old_sp-2; store R2 as new R15; write at R4 (old_sp) */
    Operand s       = decode_operand(cpu, inst & 0x3F, 0);
    uint16_t old_sp = get_reg(cpu, 15) & 0xFFFE;
    set_reg(cpu, 15, (uint16_t)(old_sp - 2));
    wr16(cpu->mem, old_sp, s.val);
    break;
  }
  case 0xF: /* POP - read from OLD R15+2, increment R15 by 2 */
  {
    /* Assembly: R4=old_sp; R4+=2 (new_sp); store new_sp as R15; read from mem[new_sp] */
    Operand d       = decode_operand(cpu, inst & 0x3F, 0);
    uint16_t old_sp = get_reg(cpu, 15) & 0xFFFE;
    uint16_t new_sp = (uint16_t)(old_sp + 2);
    set_reg(cpu, 15, new_sp);
    uint16_t v = rd16(cpu->mem, new_sp);
    store_operand(cpu, &d, v);
    break;
  }
  default: break;
  }
}

/* Handle two-operand instructions (opcodes 0x4000-0xFFFF) */
static inline void handle_two_operand(Tms9900Cpu* cpu, uint16_t inst)
{
  uint8_t opcode  = (uint8_t)((inst >> 12) & 0xF);
  uint8_t byte_op = opcode & 1; /* odd opcode = byte variant */
  Operand src = decode_operand(cpu, (uint8_t)(inst & 0x3F), byte_op);
  Operand dst = decode_operand(cpu, (uint8_t)((inst >> 6) & 0x3F), byte_op);

  switch (opcode)
  {
  case 0x4: /* SZC - dst &= ~src (word) */
  case 0x5: /* SZCB (byte) */
  {
    if (byte_op)
    {
      uint8_t res = (uint8_t)dst.val & (uint8_t)~src.val;
      store_operand(cpu, &dst, res);
      cpu->st &= 0x1E;
      set_flags_byte(cpu, res);
    }
    else
    {
      uint16_t res = dst.val & (uint16_t)~src.val;
      store_operand(cpu, &dst, res);
      cpu->st &= 0x1E;
      set_flags_word(cpu, res);
    }
    break;
  }
  case 0x6: /* S - subtract word */
  {
    uint16_t res = sub16(cpu, dst.val, src.val);
    store_operand(cpu, &dst, res);
    break;
  }
  case 0x7: /* SB - subtract byte */
  {
    uint8_t res = sub8(cpu, (uint8_t)dst.val, (uint8_t)src.val);
    store_operand(cpu, &dst, res);
    break;
  }
  case 0x8: /* C - compare word: assembly CMP src,dst → LGT when src > dst */
  {
    cmp16(cpu, src.val, dst.val);
    break;
  }
  case 0x9: /* CB - compare byte: assembly CMP src,dst → LGT when src > dst */
  {
    cmp8(cpu, (uint8_t)src.val, (uint8_t)dst.val);
    break;
  }
  case 0xA: /* A - add word */
  {
    uint16_t res = add16(cpu, dst.val, src.val);
    store_operand(cpu, &dst, res);
    break;
  }
  case 0xB: /* AB - add byte */
  {
    uint8_t res = add8(cpu, (uint8_t)dst.val, (uint8_t)src.val);
    store_operand(cpu, &dst, res);
    break;
  }
  case 0xC: /* MOV - move word */
  {
    store_operand(cpu, &dst, src.val);
    cpu->st &= 0x1E;
    set_flags_word(cpu, src.val);
    break;
  }
  case 0xD: /* MOVB - move byte */
  {
    uint8_t res = (uint8_t)src.val;
    store_operand(cpu, &dst, res);
    cpu->st &= 0x1E;
    set_flags_byte(cpu, res);
    break;
  }
  case 0xE: /* SOC - dst |= src (word) */
  {
    uint16_t res = dst.val | src.val;
    store_operand(cpu, &dst, res);
    cpu->st &= 0x1E;
    set_flags_word(cpu, res);
    break;
  }
  case 0xF: /* SOCB - dst |= src (byte) */
  {
    uint8_t res = (uint8_t)dst.val | (uint8_t)src.val;
    store_operand(cpu, &dst, res);
    cpu->st &= 0x1E;
    set_flags_byte(cpu, res);
    break;
  }
  default: break;
  }
}
void tms9900_init(Tms9900Cpu* cpu, uint8_t* mem, uint8_t* regx38, uint16_t pc, uint16_t wp)
{
  cpu->mem    = mem;
  cpu->regx38 = regx38;
  cpu->pc     = pc;
  cpu->wp     = wp;
  cpu->st     = 0;
#if defined(TMS9900_WATCH_WRITES)
  cpu->onWrite = NULL;
#endif
}

uint16_t run9900_c(Tms9900Cpu* cpu)
{
  return run9900_budget_c(cpu, 0, NULL);
}

uint16_t run9900_budget_c(Tms9900Cpu* cpu, uint32_t budget, bool* outOfBudget)
{
  const int limited = budget != 0;
  if (outOfBudget) *outOfBudget = false;
  while ((*cpu->regx38 & 1u) != 0)
  {
    if (limited && budget-- == 0)
    {
      if (outOfBudget) *outOfBudget = true;
      return cpu->pc;
    }
    uint16_t inst = fetchw(cpu);
    uint8_t op_hi = (uint8_t)(inst >> 8); /* top byte of instruction */

    if (op_hi >= 0x40)
    {
      /* 0x4000-0xFFFF: two-operand instructions */
      handle_two_operand(cpu, inst);
    }
    else if (op_hi >= 0x20)
    {
      /* 0x2000-0x3FFF: COC/CZC/XOR/XOP/MPY/DIV */
      handle_format9(cpu, inst);
    }
    else if (op_hi >= 0x10)
    {
      /* 0x1000-0x1FFF: conditional jumps and JMP */
      if (!handle_branch_group(cpu, inst)) return cpu->pc; /* JMP self-loop acts like IDLE */
    }
    else if (op_hi >= 0x0C)
    {
      /* 0x0C00-0x0FFF: F18A stack ops (RET/CALL/PUSH/POP) + SLC */
      if (op_hi == 0x0E)
        handle_shift_rotate(cpu, inst); /* SLC at 0x0E00 */
      else
        handle_f18a_stack(cpu, inst);
    }
    else if (op_hi >= 0x08)
    {
      /* 0x0800-0x0BFF: SRA/SRL/SLA/SRC */
      handle_shift_rotate(cpu, inst);
    }
    else if (op_hi >= 0x04)
    {
      /* 0x0400-0x07FF: single-operand (BLWP/B/X/CLR/NEG/INV/INC/INCT/DEC/DECT/BL/SWPB/SETO/ABS) */
      handle_jump_single(cpu, inst);
    }
    else
    {
      /* 0x0000-0x03FF: immediate/system (LI/AI/ANDI/ORI/CI/STWP/STST/LWPI/LIMI/IDLE/RTWP) */
      if (!handle_immediate_system(cpu, inst)) return cpu->pc;
    }
  }
  return cpu->pc;
}
