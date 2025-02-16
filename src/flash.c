/*
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 */

#include "impl/vrEmuTms9918Priv.h"

#include "hardware/flash.h"

#include "pico.h" // For PICO_RP2040

#include <string.h>


struct UF2_Block { // 32 byte header, payload & magicEnd
    uint32_t magicStart0;
    uint32_t magicStart1;
    uint32_t flags;
    uint32_t targetAddr;
    uint32_t payloadSize;
    uint32_t blockNo;
    uint32_t numBlocks;
    uint32_t familyID;
    uint8_t  data [476];
    uint32_t magicEnd;
} UF2_Block;
typedef struct UF2_Block * UF2_Block_Ptr;

static uint8_t hexvalues [] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

static inline uint8_t hexv (uint32_t v) {
  return hexvalues [v & 0x0F];
}

/*
#define RP2040_FAMILY_ID            0xe48bff56u
#define RP2350_ARM_S_FAMILY_ID      0xe48bff59u
#define RP2350_RISCV_FAMILY_ID      0xe48bff5au
#define RP2350_ARM_NS_FAMILY_ID     0xe48bff5bu
*/
#if PICO_RP2040
  #define FAMILY_ID 0xe48bff56
#else
  #define FAMILY_ID 0xe48bff59
#endif

#define PAYLOAD 256

//                       'NO PICO9918 DETECTED'
#define SKIPPING         "SKIPPING...         "
#define ERASING          "ERASING...          "
#define VALIDATING       "VALIDATING...       "
#define PROGRAMMING      "PROGRAMMING...      "
#define FAILEDSEQUENCE   "FAILED - SEQUENCE   "
#define FAILEDSIZE       "FAILED - SIZE       "
#define FAILEDCOMPARISON "FAILED - COMPARISON "
#define SUCCESSPOWER     "SUCCESS - POWER CYCLE NEEDED"
#define SUCCESSFLASH     "SUCCESS - FLASH IS THE SAME"

void flashSector () {
  static uint32_t flashing = 0;
  static uint32_t repeat = 0;
  int i;
  int retry = 0;

  // vram address where uf2 block is stored is set in vreg(0x3f)[5:0] (256 byte boundaries)
  uint8_t flashReg = TMS_REGISTER(tms9918, 0x3f);
  
  const int vramAddr = (flashReg & 0x3f) << 8;
  const bool write = flashReg & 0x40;
  const bool verify = !write;

  UF2_Block_Ptr p = (UF2_Block_Ptr)(tms9918->vram.bytes + vramAddr);

  tms9918->vram.bytes [767] = 0x00; // Wait
  tms9918->flash = 0;
  if ((p->magicStart0 != 0x0A324655) || // UF2\n
      (p->magicStart1 != 0x9E5D5157) ||
      (p->magicEnd    != 0x0AB16F30) ||
      (p->numBlocks   >= 0x00000400) || // 256KB Max
      (p->flags       != 0x00002000) || // familyID present - no others set
      (p->familyID    != FAMILY_ID)  || // RP2040/RP2350
      (p->targetAddr  <  0x10000000) || // Flash address
      (p->targetAddr  >= 0x10040000) || // +256KB
      ((p->targetAddr & 0xFF) != 0)  || // Target must be 256 byte aligned
      (p->payloadSize != PAYLOAD)) {    // Only support standard size
    strcpy (&(tms9918->vram.bytes [736]), SKIPPING);
    tms9918->vram.bytes [767] = 0x01; // Ignored - continue
    return;
  }
  if (p->blockNo == 0) {
    flashing = 1;
    if (tms9918->vram.bytes [766] == 0) {
      strcpy (&(tms9918->vram.bytes [736]), ERASING);
      flash_range_erase (0, ((PAYLOAD * p->numBlocks) + 0xFFF) & ~0xFFF);
    }
  } else if (!flashing) {
    strcpy (&(tms9918->vram.bytes [736]), FAILEDSEQUENCE);
    tms9918->vram.bytes [767] = 0x05; // Failed - sequence
    tms9918->lockedMask = 0x07;
    return;
  }
  uint32_t a = (p->targetAddr - (intptr_t)XIP_BASE);
  uint32_t b = a >> 12; // Get 4KB block number
  if (b >= 64) { // Only support loading 256KB of flash (RAM size)
    strcpy (&(tms9918->vram.bytes [736]), FAILEDSIZE);
    tms9918->vram.bytes [767] = 0x04; // Failed - size
    flashing = 0;
    tms9918->lockedMask = 0x07;
    return;
  }
  if (tms9918->vram.bytes [766] == 0) {
    strcpy (&(tms9918->vram.bytes [736]), PROGRAMMING);
    retry = 3;
retry:
    flash_range_program (a, p->data, PAYLOAD);

    // This cache flush may be unstable over 133MHz
    //xip_ctrl_hw->flush = 1;
    //while (!(xip_ctrl_hw->stat & XIP_STAT_FLUSH_READY_BITS))
    //  tight_loop_contents();

    // Alternate 'local' cache flush
    i = 0;
    while (i < PAYLOAD) {
      *(volatile uint32_t *)(XIP_BASE + a + i) = 0;
      i += sizeof(uint32_t);
    }
  } else
    strcpy (&(tms9918->vram.bytes [736]), VALIDATING);

  if (memcmp ((void *)(XIP_BASE + a), p->data, PAYLOAD) != 0) {
    if (retry) {
      retry--;
      repeat++;
      tms9918->vram.bytes [729] = 'R';
      tms9918->vram.bytes [730] = 'E';
      tms9918->vram.bytes [731] = 'P';
      tms9918->vram.bytes [732] = ':';
      tms9918->vram.bytes [733] = hexv (repeat >>  8);
      tms9918->vram.bytes [734] = hexv (repeat >>  4);
      tms9918->vram.bytes [735] = hexv (repeat >>  0);
      goto retry;
    }
    strcpy (&(tms9918->vram.bytes [736]), FAILEDCOMPARISON);
    tms9918->vram.bytes [756] = hexv (a >> 20);
    tms9918->vram.bytes [757] = hexv (a >> 16);
    tms9918->vram.bytes [758] = hexv (a >> 12);
    tms9918->vram.bytes [759] = hexv (a >>  8);
    tms9918->vram.bytes [760] = hexv (a >>  4);
    tms9918->vram.bytes [761] = hexv (a >>  0);
    int i = 0;
    while (i < 728) {
      b = *(uint8_t *)(XIP_BASE + a++);
      tms9918->vram.bytes [i++] = hexv (b >> 4);
      tms9918->vram.bytes [i++] = hexv (b);
    }
    tms9918->vram.bytes [767] = 0x03; // Failed - comparison
    flashing = 0;
    tms9918->lockedMask = 0x07;
    return;
  }
  if (p->blockNo + 1 == p->numBlocks) {
    if (tms9918->vram.bytes [766] == 0)
      strcpy (&(tms9918->vram.bytes [736]), SUCCESSPOWER);
    else
      strcpy (&(tms9918->vram.bytes [736]), SUCCESSFLASH);
    tms9918->vram.bytes [767] = 0x02; // Success - power cycle needed
    flashing = 0;
    tms9918->lockedMask = 0x07;
  } else
    tms9918->vram.bytes [767] = 0x01; // Success - continue
}
