/**
 * \file
 * \brief pico9918-core - the TMS9900 cores' unit tests: ARM assembly against portable C
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Runs tests sequentially against the ARM assembly core (run9900) then
 * the portable C core (run9900_c), printing PASS/FAIL for each.
 * Each test prints its name BEFORE running so a hang is immediately
 * identifiable from the output.
 *
 * Memory layout:
 *   0x0000 - workspace (WP=0x0000, registers R0..R15 at offsets 0..30)
 *   0x0100 - test program
 *   0x0200 - scratch / indirect target area
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef PICO_BUILD
#include "pico/stdlib.h"
#endif
#include "tms9900.h"

/* A host has no assembly core to compare against, so both passes run the portable
   one and every check stands against the value the case states. Which is the point
   of running here at all: on a board these cases are a cross-check, and off it they
   are the only per-instruction test the portable core gets. */
#ifdef PICO_BUILD
extern uint16_t run9900(uint8_t* memory, uint16_t pc, uint16_t wp, uint8_t* regx38);
#endif


/* -------------------------------------------------------------------------
 * Memory layout constants
 * ---------------------------------------------------------------------- */
#define MEM_SIZE  (0x10000u + 36)
#define WP        0xFFFEu
#define PROG      0x0100u
#define SCRATCH   0x0200u
#define REG(r)    ((uint32_t)(WP + (r) * 2u))

static _Alignas(4) uint8_t mem[MEM_SIZE];

static int total  = 0;
static int passed = 0;
static int failed = 0;

/* -------------------------------------------------------------------------
 * Memory helpers (big-endian word access)
 * ---------------------------------------------------------------------- */
static inline void w16(uint32_t addr, uint16_t v)
{
  mem[addr]     = (uint8_t)(v >> 8);
  mem[addr + 1] = (uint8_t)(v & 0xFF);
}

static inline uint16_t r16(uint32_t addr)
{
  return (uint16_t)((mem[addr] << 8) | mem[addr + 1]);
}

/* -------------------------------------------------------------------------
 * Test runner
 *
 * Call run_asm() or run_c() after setting up mem[], then use CHECK_* macros.
 * ---------------------------------------------------------------------- */

/* Reset mem and set registers from array (pass NULL for all-zero) */
static void setup(const uint16_t* regs)
{
  memset(mem, 0, sizeof(mem));
  if (regs) {
    for (int i = 0; i < 16; i++)
      w16(REG(i), regs[i]);
  }
}

/* Place program at PROG */
static void load_prog(const uint8_t* prog, uint16_t len)
{
  memcpy(mem + PROG, prog, len);
}


static void run_c(void);

static void run_asm(void)
{
#ifdef PICO_BUILD
  uint8_t r38 = 1;
  run9900(mem, PROG, WP, &r38);
#else
  run_c();
#endif
}

static void run_c(void)
{
  uint8_t r38 = 1;
  Tms9900Cpu cpu;
  tms9900_init(&cpu, mem, &r38, PROG, WP);
  run9900_c(&cpu);
}

/* Print a test result */
static void check(const char* label, int ok, const char* detail)
{
  total++;
  if (ok) {
    passed++;
    printf("    PASS: %s\n", label);
  } else {
    failed++;
    printf("    FAIL: %s - %s\n", label, detail);
    printf("          regs:");
    for (int i = 0; i < 8; i++) printf(" R%d=%04X", i, r16(REG(i)));
    printf("\n");
  }
}

/* Helpers so we don't need snprintf everywhere */
static char _det[128];

#define CHECK_REG(lbl, reg, expected) do { \
  uint16_t _v = r16(REG(reg)); \
  if (_v == (expected)) { check(lbl, 1, ""); } \
  else { snprintf(_det,sizeof(_det),"R%d=%04X expected %04X",reg,_v,(uint16_t)(expected)); check(lbl,0,_det); } \
} while(0)

#define CHECK_MEM16(lbl, addr, expected) do { \
  uint16_t _v = r16(addr); \
  if (_v == (expected)) { check(lbl, 1, ""); } \
  else { snprintf(_det,sizeof(_det),"[%04X]=%04X expected %04X",(uint16_t)(addr),_v,(uint16_t)(expected)); check(lbl,0,_det); } \
} while(0)

#define CHECK_MEM8(lbl, addr, expected) do { \
  uint8_t _v = mem[addr]; \
  if (_v == (expected)) { check(lbl, 1, ""); } \
  else { snprintf(_det,sizeof(_det),"[%04X]=%02X expected %02X",(uint16_t)(addr),_v,(uint8_t)(expected)); check(lbl,0,_det); } \
} while(0)

/* -------------------------------------------------------------------------
 * Instruction encoders
 * ---------------------------------------------------------------------- */
#define MAX_PROG 128u

static inline void emit(uint8_t* b, uint16_t* o, uint16_t w)
{
  b[(*o)++] = (uint8_t)(w >> 8);
  b[(*o)++] = (uint8_t)(w & 0xFF);
}

#define IDLE 0x0340u

static inline void li     (uint8_t* b, uint16_t* o, uint8_t rd, uint16_t imm) { emit(b,o,(uint16_t)(0x0200u|rd)); emit(b,o,imm); }
static inline void ai     (uint8_t* b, uint16_t* o, uint8_t rd, uint16_t imm) { emit(b,o,(uint16_t)(0x0220u|rd)); emit(b,o,imm); }
static inline void andi_op(uint8_t* b, uint16_t* o, uint8_t rd, uint16_t imm) { emit(b,o,(uint16_t)(0x0240u|rd)); emit(b,o,imm); }
static inline void ori_op (uint8_t* b, uint16_t* o, uint8_t rd, uint16_t imm) { emit(b,o,(uint16_t)(0x0260u|rd)); emit(b,o,imm); }
static inline void ci     (uint8_t* b, uint16_t* o, uint8_t rd, uint16_t imm) { emit(b,o,(uint16_t)(0x0280u|rd)); emit(b,o,imm); }
static inline void stwp   (uint8_t* b, uint16_t* o, uint8_t rd)               { emit(b,o,(uint16_t)(0x02A0u|rd)); }
static inline void stst   (uint8_t* b, uint16_t* o, uint8_t rd)               { emit(b,o,(uint16_t)(0x02C0u|rd)); }
static inline void rtwp   (uint8_t* b, uint16_t* o)                           { emit(b,o,0x0380u); }

static inline void blwp   (uint8_t* b, uint16_t* o, uint16_t addr) { emit(b,o,(uint16_t)(0x0400u|(2u<<4)|0)); emit(b,o,addr); }
static inline void b_ind  (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x0440u|(1u<<4)|rd)); }
static inline void clr    (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x04C0u|rd)); }
static inline void neg    (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x0500u|rd)); }
static inline void inv    (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x0540u|rd)); }
static inline void inc    (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x0580u|rd)); }
static inline void inct   (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x05C0u|rd)); }
static inline void dec    (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x0600u|rd)); }
static inline void dect   (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x0640u|rd)); }
static inline void bl_abs (uint8_t* b, uint16_t* o, uint16_t addr) { emit(b,o,(uint16_t)(0x0680u|(2u<<4)|0)); emit(b,o,addr); }
static inline void swpb   (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x06C0u|rd)); }
static inline void seto   (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x0700u|rd)); }
static inline void abs_r  (uint8_t* b, uint16_t* o, uint8_t rd)    { emit(b,o,(uint16_t)(0x0740u|rd)); }
static inline void rt     (uint8_t* b, uint16_t* o)                { emit(b,o,(uint16_t)(0x0440u|(1u<<4)|11u)); }

/* Shift format: 0000 1ooo CCCC WWWW  (C=count in bits 7-4, W=register in bits 3-0) */
static inline void sra    (uint8_t* b, uint16_t* o, uint8_t rd, uint8_t c) { emit(b,o,(uint16_t)(0x0800u|((uint16_t)(c&0xF)<<4)|(rd&0xF))); }
static inline void srl    (uint8_t* b, uint16_t* o, uint8_t rd, uint8_t c) { emit(b,o,(uint16_t)(0x0900u|((uint16_t)(c&0xF)<<4)|(rd&0xF))); }
static inline void sla    (uint8_t* b, uint16_t* o, uint8_t rd, uint8_t c) { emit(b,o,(uint16_t)(0x0A00u|((uint16_t)(c&0xF)<<4)|(rd&0xF))); }
static inline void src_op (uint8_t* b, uint16_t* o, uint8_t rd, uint8_t c) { emit(b,o,(uint16_t)(0x0B00u|((uint16_t)(c&0xF)<<4)|(rd&0xF))); }

static inline void jmp    (uint8_t* b, uint16_t* o, int8_t off) { emit(b,o,(uint16_t)(0x1000u|(uint8_t)off)); }
static inline void jlt    (uint8_t* b, uint16_t* o, int8_t off) { emit(b,o,(uint16_t)(0x1A00u|(uint8_t)off)); }
static inline void jle    (uint8_t* b, uint16_t* o, int8_t off) { emit(b,o,(uint16_t)(0x1200u|(uint8_t)off)); }
static inline void jeq    (uint8_t* b, uint16_t* o, int8_t off) { emit(b,o,(uint16_t)(0x1300u|(uint8_t)off)); }
static inline void jne    (uint8_t* b, uint16_t* o, int8_t off) { emit(b,o,(uint16_t)(0x1600u|(uint8_t)off)); }
static inline void jgt    (uint8_t* b, uint16_t* o, int8_t off) { emit(b,o,(uint16_t)(0x1500u|(uint8_t)off)); }
static inline void joc    (uint8_t* b, uint16_t* o, int8_t off) { emit(b,o,(uint16_t)(0x1C00u|(uint8_t)off)); }
static inline void jnc    (uint8_t* b, uint16_t* o, int8_t off) { emit(b,o,(uint16_t)(0x1D00u|(uint8_t)off)); }

static inline void coc    (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0x2000u|((uint16_t)rd<<6)|rs)); }
static inline void czc    (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0x2400u|((uint16_t)rd<<6)|rs)); }
static inline void xor_rr (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0x2800u|((uint16_t)rd<<6)|rs)); }
static inline void pix    (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0x2C00u|((uint16_t)rd<<6)|rs)); }
static inline void mpy    (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0x3800u|((uint16_t)rd<<6)|rs)); }
static inline void div_op (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0x3C00u|((uint16_t)rd<<6)|rs)); }

static inline void szc_rr (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0x4000u|((uint16_t)rd<<6)|rs)); }
static inline void sub_rr (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0x6000u|((uint16_t)rd<<6)|rs)); }
static inline void cb_rr  (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0x9000u|((uint16_t)rd<<6)|rs)); }
static inline void add_rr (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0xA000u|((uint16_t)rd<<6)|rs)); }
static inline void mov_rr (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0xC000u|((uint16_t)rd<<6)|rs)); }
static inline void mov_ir (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0xC000u|((uint16_t)rd<<6)|(1u<<4)|rs)); }
static inline void mov_ri (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0xC000u|((uint16_t)(0x10u|rd)<<6)|rs)); }
static inline void mov_ar (uint8_t* b, uint16_t* o, uint16_t addr, uint8_t rd) { emit(b,o,(uint16_t)(0xC000u|((uint16_t)rd<<6)|(2u<<4)|0)); emit(b,o,addr); }
static inline void movb_rr(uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0xD000u|((uint16_t)rd<<6)|rs)); }
static inline void soc_rr (uint8_t* b, uint16_t* o, uint8_t rs, uint8_t rd) { emit(b,o,(uint16_t)(0xE000u|((uint16_t)rd<<6)|rs)); }

/* -------------------------------------------------------------------------
 * Test groups - each test prints its name, runs ASM then C, reports PASS/FAIL
 * ---------------------------------------------------------------------- */

static void test_data_transfer(void)
{
  printf("\n=== Data Transfer ===\n");
  uint8_t p[MAX_PROG]; uint16_t n;

  /* LI R0, 0x1234 */
  printf("  LI R0,0x1234\n");
  n=0; li(p,&n,0,0x1234); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("LI R0,0x1234 [ASM]", 0, 0x1234);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("LI R0,0x1234 [C]",   0, 0x1234);

  /* MOV R0, R1 */
  printf("  MOV R0,R1\n");
  n=0; li(p,&n,0,0xABCD); mov_rr(p,&n,0,1); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("MOV R0->R1", 1, 0xABCD);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("MOV R0->R1", 1, 0xABCD);

  /* MOV indirect: *R0 -> R1 (R0 points to SCRATCH, SCRATCH holds 0xBEEF) */
  printf("  MOV *R0,R1\n");
  n=0; mov_ir(p,&n,0,1); emit(p,&n,IDLE);
  { uint16_t regs[16]={0}; regs[0]=SCRATCH;
    setup(regs); w16(SCRATCH,0xBEEF); load_prog(p,n);
    run_asm(); CHECK_REG("MOV *R0->R1", 1, 0xBEEF);
    setup(regs); w16(SCRATCH,0xBEEF); load_prog(p,n);
    run_c();   CHECK_REG("MOV *R0->R1", 1, 0xBEEF); }

  /* MOV R0, *R1 (store to address in R1) */
  printf("  MOV R0,*R1\n");
  n=0; mov_ri(p,&n,0,1); emit(p,&n,IDLE);
  { uint16_t regs[16]={0}; regs[0]=0x1234; regs[1]=SCRATCH;
    setup(regs); load_prog(p,n); run_asm(); CHECK_MEM16("MOV R0->*R1", SCRATCH, 0x1234);
    setup(regs); load_prog(p,n); run_c();   CHECK_MEM16("MOV R0->*R1", SCRATCH, 0x1234); }

  /* MOV @addr, R1 (absolute load) */
  printf("  MOV @addr,R1\n");
  n=0; mov_ar(p,&n,SCRATCH,1); emit(p,&n,IDLE);
  setup(NULL); w16(SCRATCH,0xCAFE); load_prog(p,n);
  run_asm(); CHECK_REG("MOV @abs->R1", 1, 0xCAFE);
  setup(NULL); w16(SCRATCH,0xCAFE); load_prog(p,n);
  run_c();   CHECK_REG("MOV @abs->R1", 1, 0xCAFE);

  /* MOV *R0+, R2 (auto-increment) */
  printf("  MOV *R0+,R2\n");
  n=0; emit(p,&n,(uint16_t)(0xC000u|((uint16_t)2<<6)|(3u<<4)|0)); emit(p,&n,IDLE); /* src=mode3/R0 */
  { uint16_t regs[16]={0}; regs[0]=SCRATCH;
    setup(regs); w16(SCRATCH,0x5A5A); load_prog(p,n);
    run_asm(); CHECK_REG("autoinc R0", 0, SCRATCH+2); CHECK_REG("autoinc R2", 2, 0x5A5A);
    setup(regs); w16(SCRATCH,0x5A5A); load_prog(p,n);
    run_c();   CHECK_REG("autoinc R0", 0, SCRATCH+2); CHECK_REG("autoinc R2", 2, 0x5A5A); }

  /* MOVB Rs, Rd - high byte of source copied to high byte of dest */
  printf("  MOVB R0,R1\n");
  n=0; li(p,&n,0,0x5500); movb_rr(p,&n,0,1); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("MOVB R0->R1", 1, 0x5500);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("MOVB R0->R1", 1, 0x5500);
}

static void test_arithmetic(void)
{
  printf("\n=== Arithmetic ===\n");
  uint8_t p[MAX_PROG]; uint16_t n;

  /* A: basic add */
  printf("  A R0,R1 (3+4=7)\n");
  n=0; li(p,&n,0,3); li(p,&n,1,4); add_rr(p,&n,0,1); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("A 3+4=7", 1, 7);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("A 3+4=7", 1, 7);

  /* A: carry (0xFFFF + 1 = 0, carry set) */
  printf("  A carry (0xFFFF+1)\n");
  n=0; li(p,&n,0,0xFFFF); li(p,&n,1,1); add_rr(p,&n,0,1); stst(p,&n,2); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("ADD carry result", 1, 0);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("ADD carry result", 1, 0);
  /* carry bit = 0x10 (C flag) in stst word */
  setup(NULL); load_prog(p,n); run_asm(); { uint16_t st=r16(REG(2));
    snprintf(_det,sizeof(_det),"ST=%04X C-bit missing",st);
    check("ADD carry flag [ASM]", (st&0x1000)!=0, _det); }
  setup(NULL); load_prog(p,n); run_c();   { uint16_t st=r16(REG(2));
    snprintf(_det,sizeof(_det),"ST=%04X C-bit missing",st);
    check("ADD carry flag [C]",   (st&0x1000)!=0, _det); }

  /* S: subtract */
  printf("  S R0,R1 (5-3=2)\n");
  n=0; li(p,&n,0,3); li(p,&n,1,5); sub_rr(p,&n,0,1); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("S 5-3=2", 1, 2);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("S 5-3=2", 1, 2);

  /* NEG */
  printf("  NEG R0 (1->0xFFFF)\n");
  n=0; li(p,&n,0,1); neg(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("NEG 1", 0, 0xFFFF);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("NEG 1", 0, 0xFFFF);

  /* ABS positive */
  printf("  ABS R0 (0x0005)\n");
  n=0; li(p,&n,0,5); abs_r(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("ABS pos", 0, 5);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("ABS pos", 0, 5);

  /* ABS negative */
  printf("  ABS R0 (0xFFFB->5)\n");
  n=0; li(p,&n,0,0xFFFB); abs_r(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("ABS neg", 0, 5);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("ABS neg", 0, 5);

  /* INC */
  printf("  INC R0 (4->5)\n");
  n=0; li(p,&n,0,4); inc(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("INC", 0, 5);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("INC", 0, 5);

  /* INCT */
  printf("  INCT R0 (4->6)\n");
  n=0; li(p,&n,0,4); inct(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("INCT", 0, 6);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("INCT", 0, 6);

  /* DEC */
  printf("  DEC R0 (5->4)\n");
  n=0; li(p,&n,0,5); dec(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("DEC", 0, 4);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("DEC", 0, 4);

  /* DECT */
  printf("  DECT R0 (6->4)\n");
  n=0; li(p,&n,0,6); dect(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("DECT", 0, 4);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("DECT", 0, 4);

  /* AI */
  printf("  AI R0,0x10 (5+16=21)\n");
  n=0; li(p,&n,0,5); ai(p,&n,0,0x10); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("AI", 0, 21);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("AI", 0, 21);

  /* MPY */
  printf("  MPY R1,R0 (3*4=12)\n");
  n=0; li(p,&n,0,3); li(p,&n,1,4); mpy(p,&n,1,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("MPY hi", 0, 0); CHECK_REG("MPY lo", 1, 12);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("MPY hi", 0, 0); CHECK_REG("MPY lo", 1, 12);

  /* DIV: 12/4=3 rem 0 */
  printf("  DIV R2,R0 (12/4=3)\n");
  n=0; li(p,&n,0,0); li(p,&n,1,12); li(p,&n,2,4); div_op(p,&n,2,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("DIV quot", 0, 3); CHECK_REG("DIV rem", 1, 0);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("DIV quot", 0, 3); CHECK_REG("DIV rem", 1, 0);

  /* NEG 0x8000 -> overflow */
  printf("  NEG 0x8000 (overflow)\n");
  n=0; li(p,&n,0,0x8000); neg(p,&n,0); stst(p,&n,1); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("NEG 0x8000 result", 0, 0x8000);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("NEG 0x8000 result", 0, 0x8000);
  setup(NULL); load_prog(p,n); run_asm(); { uint16_t st=r16(REG(1));
    snprintf(_det,sizeof(_det),"ST=%04X OV-bit missing",st);
    check("NEG 0x8000 OV [ASM]", (st&0x0800)!=0, _det); }
  setup(NULL); load_prog(p,n); run_c();   { uint16_t st=r16(REG(1));
    snprintf(_det,sizeof(_det),"ST=%04X OV-bit missing",st);
    check("NEG 0x8000 OV [C]",   (st&0x0800)!=0, _det); }
}

static void test_logical(void)
{
  printf("\n=== Logical ===\n");
  uint8_t p[MAX_PROG]; uint16_t n;

  /* SZC (AND NOT) */
  printf("  SZC R0,R1\n");
  n=0; li(p,&n,0,0x00FF); li(p,&n,1,0xFFFF); szc_rr(p,&n,0,1); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("SZC", 1, 0xFF00);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("SZC", 1, 0xFF00);

  /* SOC (OR) */
  printf("  SOC R0,R1\n");
  n=0; li(p,&n,0,0x0F0F); li(p,&n,1,0xF0F0); soc_rr(p,&n,0,1); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("SOC", 1, 0xFFFF);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("SOC", 1, 0xFFFF);

  /* XOR */
  printf("  XOR R0,R1\n");
  n=0; li(p,&n,0,0xAAAA); li(p,&n,1,0xFFFF); xor_rr(p,&n,0,1); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("XOR", 1, 0x5555);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("XOR", 1, 0x5555);

  /* INV */
  printf("  INV R0\n");
  n=0; li(p,&n,0,0xAAAA); inv(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("INV", 0, 0x5555);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("INV", 0, 0x5555);

  /* CLR */
  printf("  CLR R0\n");
  n=0; li(p,&n,0,0x1234); clr(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("CLR", 0, 0);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("CLR", 0, 0);

  /* SETO */
  printf("  SETO R0\n");
  n=0; seto(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("SETO", 0, 0xFFFF);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("SETO", 0, 0xFFFF);

  /* ANDI */
  printf("  ANDI R0,0x0F0F\n");
  n=0; li(p,&n,0,0xFFFF); andi_op(p,&n,0,0x0F0F); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("ANDI", 0, 0x0F0F);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("ANDI", 0, 0x0F0F);

  /* ORI */
  printf("  ORI R0,0xFF00\n");
  n=0; li(p,&n,0,0x00FF); ori_op(p,&n,0,0xFF00); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("ORI", 0, 0xFFFF);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("ORI", 0, 0xFFFF);

  /* COC - EQ set if (src & dst)==src */
  printf("  COC R0,R1 (match)\n");
  n=0; li(p,&n,0,0x0F0F); li(p,&n,1,0xFFFF); coc(p,&n,0,1); stst(p,&n,2); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); { uint16_t st=r16(REG(2));
    snprintf(_det,sizeof(_det),"ST=%04X EQ-bit missing",st);
    check("COC EQ [ASM]", (st&0x2000)!=0, _det); }
  setup(NULL); load_prog(p,n); run_c();   { uint16_t st=r16(REG(2));
    snprintf(_det,sizeof(_det),"ST=%04X EQ-bit missing",st);
    check("COC EQ [C]",   (st&0x2000)!=0, _det); }

  /* CB (compare byte) - sets EQ when high bytes match */
  printf("  CB R0,R1 (equal)\n");
  n=0; li(p,&n,0,0xAA00); li(p,&n,1,0xAAFF); cb_rr(p,&n,0,1); stst(p,&n,2); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); { uint16_t st=r16(REG(2));
    snprintf(_det,sizeof(_det),"ST=%04X EQ-bit missing",st);
    check("CB EQ [ASM]", (st&0x2000)!=0, _det); }
  setup(NULL); load_prog(p,n); run_c();   { uint16_t st=r16(REG(2));
    snprintf(_det,sizeof(_det),"ST=%04X EQ-bit missing",st);
    check("CB EQ [C]",   (st&0x2000)!=0, _det); }
}

static void test_shifts(void)
{
  printf("\n=== Shifts ===\n");
  uint8_t p[MAX_PROG]; uint16_t n;

  /* SRA 4 */
  printf("  SRA R0,4\n");
  n=0; li(p,&n,0,0x8000); sra(p,&n,0,4); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("SRA 4", 0, 0xF800);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("SRA 4", 0, 0xF800);

  /* SRL 4 */
  printf("  SRL R0,4\n");
  n=0; li(p,&n,0,0x8000); srl(p,&n,0,4); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("SRL 4", 0, 0x0800);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("SRL 4", 0, 0x0800);

  /* SLA 4 */
  printf("  SLA R0,4\n");
  n=0; li(p,&n,0,0x0001); sla(p,&n,0,4); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("SLA 4", 0, 0x0010);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("SLA 4", 0, 0x0010);

  /* SRC 4 */
  printf("  SRC R0,4\n");
  n=0; li(p,&n,0,0x1234); src_op(p,&n,0,4); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("SRC 4", 0, 0x4123);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("SRC 4", 0, 0x4123);

  /* SRA by R0 (count=0 means use R0 low nibble) */
  printf("  SRA R1,R0 (count from R0)\n");
  n=0; li(p,&n,0,4); li(p,&n,1,0x8000); sra(p,&n,1,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("SRA R0-count", 1, 0xF800);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("SRA R0-count", 1, 0xF800);

  /* SLA overflow flag */
  printf("  SLA overflow (0x4000<<1)\n");
  n=0; li(p,&n,0,0x4000); sla(p,&n,0,1); stst(p,&n,1); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); { uint16_t st=r16(REG(1));
    snprintf(_det,sizeof(_det),"ST=%04X OV-bit missing",st);
    check("SLA OV [ASM]", (st&0x0800)!=0, _det); }
  setup(NULL); load_prog(p,n); run_c();   { uint16_t st=r16(REG(1));
    snprintf(_det,sizeof(_det),"ST=%04X OV-bit missing",st);
    check("SLA OV [C]",   (st&0x0800)!=0, _det); }
}

static void test_branches(void)
{
  printf("\n=== Branches ===\n");
  uint8_t p[MAX_PROG]; uint16_t n;

  /* CI + JEQ taken: skip LI R0,0xDEAD, R0 stays 0 */
  printf("  CI/JEQ taken\n");
  n=0;
  li(p,&n,0,5); ci(p,&n,0,5);      /* R0=5; CI R0,5 -> EQ */
  jeq(p,&n,2);                       /* JEQ +2 words -> skip next LI */
  li(p,&n,0,0xDEAD);                /* should be skipped */
  emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("JEQ taken", 0, 5);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("JEQ taken", 0, 5);

  /* CI + JEQ not taken: R0 gets overwritten */
  printf("  CI/JEQ not taken\n");
  n=0;
  li(p,&n,0,5); ci(p,&n,0,6);      /* R0=5; CI R0,6 -> not EQ */
  jeq(p,&n,2);                       /* JEQ not taken */
  li(p,&n,0,0x1111);                /* R0 = 0x1111 */
  emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("JEQ not taken", 0, 0x1111);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("JEQ not taken", 0, 0x1111);

  /* JNE loop (count down from 3 to 0) */
  printf("  JNE loop (3 iters)\n");
  n=0;
  li(p,&n,0,3);                     /* R0=3 */
  /* loop: DEC R0 (2 bytes), CI R0,0 (4 bytes), JNE -4 (2 bytes) = 8 bytes */
  dec(p,&n,0);                       /* 2 bytes @ +4 */
  ci(p,&n,0,0);                      /* 4 bytes @ +6 */
  jne(p,&n,(int8_t)(-4));            /* PC after=+12, target=+12+(-4*2)=+4 (DEC) */
  emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("JNE loop R0", 0, 0);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("JNE loop R0", 0, 0);

  /* BL / RT */
  printf("  BL/RT\n");
  /* Program: LI R0,0; BL @sub; IDLE
   *   sub: LI R0,0xBEEF; RT */
  uint16_t sub_off = 0;
  n=0;
  li(p,&n,0,0);
  uint16_t bl_off = n; bl_abs(p,&n,0); /* BL @0 - patch below */
  emit(p,&n,IDLE);
  sub_off = n;
  li(p,&n,0,0xBEEF);
  rt(p,&n);
  /* patch BL target: absolute address of sub = PROG + sub_off */
  uint16_t sub_addr = PROG + sub_off;
  p[bl_off+2] = (uint8_t)(sub_addr>>8);
  p[bl_off+3] = (uint8_t)(sub_addr&0xFF);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("BL/RT R0", 0, 0xBEEF);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("BL/RT R0", 0, 0xBEEF);
}

static void test_misc(void)
{
  printf("\n=== Misc ===\n");
  uint8_t p[MAX_PROG]; uint16_t n;

  /* SWPB */
  printf("  SWPB R0\n");
  n=0; li(p,&n,0,0x1234); swpb(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("SWPB", 0, 0x3412);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("SWPB", 0, 0x3412);

  /* STST */
  printf("  STST R0 (after EQ)\n");
  n=0; li(p,&n,0,5); ci(p,&n,0,5); stst(p,&n,1); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); { uint16_t st=r16(REG(1));
    snprintf(_det,sizeof(_det),"ST=%04X EQ-bit missing",st);
    check("STST EQ [ASM]", (st&0x2000)!=0, _det); }
  setup(NULL); load_prog(p,n); run_c();   { uint16_t st=r16(REG(1));
    snprintf(_det,sizeof(_det),"ST=%04X EQ-bit missing",st);
    check("STST EQ [C]",   (st&0x2000)!=0, _det); }

  /* STWP */
  printf("  STWP R0\n");
  n=0; stwp(p,&n,0); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("STWP", 0, WP);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("STWP", 0, WP);
}

/* PIX in BL mode, on the three things a byte-per-four-pixels layer decides:
   the row stride, which pixel of the byte, and where the address wraps. A
   width that is not a multiple of four is what separates the stride from the
   pixel count, so every case here uses one. */
static void test_pix(void)
{
  printf("\n=== F18A PIX (BL) ===\n");
  uint8_t p[MAX_PROG]; uint16_t n;

  /* Address only: VR35=10 is a stride of 3 bytes, so (x=5,y=3) is byte 10. */
  printf("  PIX address, VR35=10\n");
  n=0; li(p,&n,1,0x0503); li(p,&n,2,0x4000); pix(p,&n,1,2); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); mem[0x6020]=0; mem[0x6023]=10;
  run_asm(); CHECK_REG("PIX addr", 2, 0x000A);
  setup(NULL); load_prog(p,n); mem[0x6020]=0; mem[0x6023]=10;
  run_c();   CHECK_REG("PIX addr", 2, 0x000A);

  /* VR32=0xFF puts the layer at 0x3FC0, so row 100 leaves 16KB and wraps. */
  printf("  PIX address wraps at 16KB\n");
  n=0; li(p,&n,1,0x0064); li(p,&n,2,0x4000); pix(p,&n,1,2); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); mem[0x6020]=0xFF; mem[0x6023]=4;
  run_asm(); CHECK_REG("PIX wrap", 2, 0x0024);
  setup(NULL); load_prog(p,n); mem[0x6020]=0xFF; mem[0x6023]=4;
  run_c();   CHECK_REG("PIX wrap", 2, 0x0024);

  /* Colour 3 at x=5 is the second pixel of the byte, whatever the stride. */
  printf("  PIX write, colour 3 at x=5\n");
  n=0; li(p,&n,1,0x0503); li(p,&n,2,0x0003); pix(p,&n,1,2); emit(p,&n,IDLE);
  setup(NULL); load_prog(p,n); mem[0x6020]=0; mem[0x6023]=10;
  run_asm(); CHECK_MEM8("PIX write", 0x000A, 0x30);
  setup(NULL); load_prog(p,n); mem[0x6020]=0; mem[0x6023]=10;
  run_c();   CHECK_MEM8("PIX write", 0x000A, 0x30);
}

static void test_blwp(void)
{
  printf("\n=== BLWP/RTWP ===\n");
  uint8_t p[MAX_PROG*4]; uint16_t n;

  /*
   * Layout within p[] placed at PROG:
   *   [0x00] BLWP @vec         (4 bytes)
   *   [0x04] LI R0,0x1111      (4 bytes)  <- return lands here
   *   [0x08] IDLE
   *   [0x10] vec_lo: new_wp    (2 bytes)
   *   [0x12] vec_hi: sub_entry (2 bytes)
   *   [0x20] new workspace (32 bytes)
   *   [0x40] sub: LI R3,0xD0AE; RTWP
   */
  uint16_t vec_off     = 0x10;
  uint16_t new_wp_off  = 0x20; /* new workspace within p[] */
  uint16_t sub_off     = 0x40;

  uint16_t vec_addr    = PROG + vec_off;
  uint16_t new_wp_addr = PROG + new_wp_off;
  uint16_t sub_addr    = PROG + sub_off;

  memset(p, 0, sizeof(p));
  n = 0;
  blwp(p, &n, vec_addr);    /* BLWP @vec */
  li(p, &n, 0, 0x1111);     /* after return: R0=0x1111 */
  emit(p, &n, IDLE);

  /* vector: new_wp, then sub_entry */
  p[vec_off]   = (uint8_t)(new_wp_addr >> 8);
  p[vec_off+1] = (uint8_t)(new_wp_addr & 0xFF);
  p[vec_off+2] = (uint8_t)(sub_addr >> 8);
  p[vec_off+3] = (uint8_t)(sub_addr & 0xFF);

  /* subroutine at sub_off */
  n = sub_off;
  li(p, &n, 3, 0xD0AE);  /* in new workspace: R3 = 0xD0AE */
  rtwp(p, &n);

  uint16_t prog_len = n;
  printf("  BLWP/RTWP\n");

  setup(NULL); load_prog(p, prog_len);
  run_asm();
  CHECK_REG("BLWP/RTWP R0 after return [ASM]", 0, 0x1111);

  setup(NULL); load_prog(p, prog_len);
  run_c();
  CHECK_REG("BLWP/RTWP R0 after return [C]", 0, 0x1111);
}

static void test_stress(void)
{
  printf("\n=== Stress: Fibonacci(10)=55 ===\n");
  uint8_t p[MAX_PROG]; uint16_t n;

  /*
   * R0=10 (counter), R1=0 (a), R2=1 (b)
   * loop: R3=R1; R3+=R2; R1=R2; R2=R3; DEC R0; CI R0,0; JNE loop
   * Each iteration: 4 MOV/DEC * 2 bytes + ADD 2 bytes + CI 4 bytes + JNE 2 bytes = 16 bytes
   * JNE offset = -8 words
   */
  printf("  Fibonacci(10)\n");
  n=0;
  li(p,&n,0,10); li(p,&n,1,0); li(p,&n,2,1);
  /* loop start (12 bytes in) */
  mov_rr(p,&n,1,3);    /* R3=R1    2 bytes */
  add_rr(p,&n,2,3);    /* R3+=R2   2 bytes */
  mov_rr(p,&n,2,1);    /* R1=R2    2 bytes */
  mov_rr(p,&n,3,2);    /* R2=R3    2 bytes */
  dec(p,&n,0);          /* R0--     2 bytes */
  ci(p,&n,0,0);         /* CI R0,0  4 bytes */
  jne(p,&n,(int8_t)(-8)); /* back 8 words=16 bytes to loop start */
  emit(p,&n,IDLE);

  setup(NULL); load_prog(p,n); run_asm(); CHECK_REG("Fib(10) [ASM]", 1, 55);
  setup(NULL); load_prog(p,n); run_c();   CHECK_REG("Fib(10) [C]",   1, 55);
}

/* =========================================================================
 * main
 * ====================================================================== */
int main(void)
{
#ifdef PICO_BUILD
  stdio_init_all();

#if defined(LIB_PICO_STDIO_USB)
  for (int i = 0; i < 100 && !stdio_usb_connected(); i++)
    sleep_ms(100);
#else
  sleep_ms(2000);
#endif
#endif

  printf("\n");
  printf("=========================================\n");
  printf("  TMS9900 Unit Tests                     \n");
#ifdef PICO_BUILD
  printf("  ASM core (%-4s) vs C core              \n", TMS9900_ASM_CORE);
#else
  printf("  portable C core, both passes            \n");
#endif
  printf("=========================================\n");

  test_data_transfer();
  test_arithmetic();
  test_logical();
  test_shifts();
  test_branches();
  test_misc();
  test_pix();
  test_blwp();
  test_stress();

  printf("\n=========================================\n");
  printf("  Results: %d/%d passed  (%d failed)\n", passed, total, failed);
  printf("=========================================\n");
  if (failed == 0)
    printf("  ALL TESTS PASSED\n");
  else
    printf("  FAILURES DETECTED\n");
  printf("=========================================\n");

#ifdef PICO_BUILD
  /* A board has nowhere to return an exit status to, so the LED carries it. */
  const uint LED = 25;
  gpio_init(LED);
  gpio_set_dir(LED, GPIO_OUT);
  while (1) {
    gpio_put(LED, failed == 0 ? 1 : 0);
    sleep_ms(500);
    gpio_put(LED, 0);
    sleep_ms(500);
  }
#endif
  return failed == 0 ? 0 : 1;
}
