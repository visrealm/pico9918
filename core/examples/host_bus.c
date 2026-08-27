/*
 * pico9918-core - drive the VDP the way a guest machine does
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * \example host_bus.c
 *
 * What a system emulator needs. render_frame.c uses the pico9918_util.h helpers to set
 * a screen up; a guest machine has no helpers - it has two ports and four operations,
 * and this is all of them:
 *
 *     MODE=0 write   pico9918_write_data()    a byte into VRAM at the current address
 *     MODE=0 read    pico9918_read_data()     a byte out of it
 *     MODE=1 write   pico9918_write_addr()    half of a two-byte command
 *     MODE=1 read    pico9918_read_status()   SR0, and reading it CLEARS F and 5S
 *
 * Three details decide whether an emulator built on this behaves like the chip:
 *
 *   The second byte of a MODE=1 pair says what the pair was: 0x80 a register write, 0x40
 *   a VRAM write address, 0x00 a VRAM read address. A read address also prefetches, so
 *   the first pico9918_read_data() after one returns the byte you seeked to.
 *
 *   Reading the status port is destructive - it clears F and 5S and drops /INT - and
 *   that read IS the interrupt acknowledgement. pico9918_peek_status() does none of it.
 *
 *   /INT is R1's enable bit AND SR0's F, so masking interrupts in R1 drops the line with
 *   no status read at all.
 *
 * Build it against an installed package:
 *
 *     cmake -S examples -B build-examples
 *     cmake --build build-examples
 *     ./build-examples/host_bus
 */

#include "pico9918.h"

#include <stdio.h>

#define ACTIVE_LINES 192
#define FRAME_LINES  262 /* NTSC: the rest is blanking, and the guest still gets it */

/* The three two-byte commands. Value or low address byte first, then a byte whose top
   bits say which of the three it was. */
#define CMD_REGISTER   0x80
#define CMD_VRAM_READ  0x00
#define CMD_VRAM_WRITE 0x40

static void busWriteRegister(PICO9918_INST_ARG uint8_t reg, uint8_t value)
{
  pico9918_write_addr(PICO9918_INST value);
  pico9918_write_addr(PICO9918_INST CMD_REGISTER | reg);
}

static void busSetAddress(PICO9918_INST_ARG uint8_t cmd, uint16_t addr)
{
  pico9918_write_addr(PICO9918_INST(uint8_t)(addr & 0xff));
  pico9918_write_addr(PICO9918_INST cmd | (uint8_t)(addr >> 8));
}

int main(void)
{
#if PICO9918_SINGLE_INSTANCE
  pico9918_init();
#else
  pico9918_t* tms9918 = pico9918_new();
  if (!tms9918) return 1;
#endif
  pico9918_reset(PICO9918_INST_ONLY);

  /* The eight register writes a Graphics I title screen starts with. A guest runs these
     out of its own ROM; these are the same bytes it would put on the bus. R2, R3 and R4
     are table addresses, each in its own scaled unit. */
  busWriteRegister(PICO9918_INST 0, 0x00);
  busWriteRegister(PICO9918_INST 1, 0xe0); /* 16K, display on, interrupt enable */
  busWriteRegister(PICO9918_INST 2, 0x0e); /* name table    0x3800 */
  busWriteRegister(PICO9918_INST 3, 0x80); /* colour table  0x2000 */
  busWriteRegister(PICO9918_INST 4, 0x01); /* pattern table 0x0800 */
  busWriteRegister(PICO9918_INST 5, 0x76); /* sprite attrs  0x3B00 */
  busWriteRegister(PICO9918_INST 6, 0x03); /* sprite patts  0x1800 */
  busWriteRegister(PICO9918_INST 7, 0x04); /* backdrop: dark blue */

  /* A glyph, its colour, and a screen of it - a byte at a time through the data port,
     which advances the address itself. */
  static const uint8_t glyph[8] = {0x3c, 0x42, 0x81, 0xa5, 0x81, 0x99, 0x42, 0x3c};
  busSetAddress(PICO9918_INST CMD_VRAM_WRITE, 0x0800 + 8); /* pattern 1 */
  for (int i = 0; i < 8; ++i) pico9918_write_data(PICO9918_INST glyph[i]);

  busSetAddress(PICO9918_INST CMD_VRAM_WRITE, 0x2000); /* patterns 0-7 share one byte */
  pico9918_write_data(PICO9918_INST 0xf4);             /* white on dark blue */

  busSetAddress(PICO9918_INST CMD_VRAM_WRITE, 0x3800);
  for (int i = 0; i < 32 * 24; ++i) pico9918_write_data(PICO9918_INST(uint8_t)(i & 1));

  busSetAddress(PICO9918_INST CMD_VRAM_READ, 0x0800 + 8);
  const uint8_t first  = pico9918_read_data(PICO9918_INST_ONLY);
  const uint8_t second = pico9918_read_data(PICO9918_INST_ONLY);
  printf("read back 0x%02X 0x%02X (wrote 0x%02X 0x%02X)\n", first, second, glyph[0], glyph[1]);

  /* The frame loop. A guest's video interrupt fires once per frame and its handler does
     the reading; everything here is what the emulator around the guest does. */
  unsigned lit = 0, acks = 0;
  for (uint16_t y = 0; y < FRAME_LINES; ++y)
  {
    if (y < ACTIVE_LINES)
    {
      pico9918_scan_line(PICO9918_INST y);
      const uint8_t* line = pico9918_line_source(PICO9918_INST_ONLY);
      for (uint32_t x = 0; x < pico9918_line_bytes(PICO9918_INST_ONLY); ++x)
        if ((line[x] & 0x0f) != 0x04) ++lit;
    }
    else if (y == ACTIVE_LINES)
    {
      pico9918_interrupt_set(PICO9918_INST_ONLY);
    }

    /* The guest, polling its interrupt line. Reading the status port is what drops it,
       so this runs exactly once per frame however many lines are left. */
    if (pico9918_interrupt_status(PICO9918_INST_ONLY))
    {
      const uint8_t sr0 = pico9918_read_status(PICO9918_INST_ONLY);
      printf("line %u: /INT, SR0 0x%02X, now %s\n", y, sr0,
             pico9918_interrupt_status(PICO9918_INST_ONLY) ? "still asserted" : "clear");
      ++acks;
    }
  }
  printf("%u lit pixels, %u interrupt%s in one frame\n", lit, acks, acks == 1 ? "" : "s");

  /* F stays set through the mask, so unmasking R1 would bring the line straight back. */
  pico9918_interrupt_set(PICO9918_INST_ONLY);
  busWriteRegister(PICO9918_INST 1, 0xc0); /* same as above, interrupt enable cleared */
  printf("masked in R1: /INT %s, SR0 still 0x%02X\n",
         pico9918_interrupt_status(PICO9918_INST_ONLY) ? "asserted" : "clear",
         pico9918_peek_status(PICO9918_INST_ONLY));

#if !PICO9918_SINGLE_INSTANCE
  pico9918_destroy(PICO9918_INST_ONLY);
#endif
  return 0;
}
