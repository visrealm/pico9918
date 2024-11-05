/*
 * Project: pico9918
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 * 
 * Purpose: TMS9900 GPU glue code
 * 
 * Credits: JasonACT (AtariAge)
 *
 */

#include "gpu.h"

#include "pico/stdlib.h"
#include "hardware/structs/mpu.h"
#include "hardware/structs/xip_ctrl.h"
#include <hardware/flash.h>
#include "pico.h" // For PICO_RP2040

#include <string.h> // memcpy


/* run9900() implemented in Thumb9900.S */
uint16_t run9900(uint8_t * memory, uint16_t pc, uint16_t wp, uint8_t * regx38);


/* data to pre-load into the GPU RAM */
static uint8_t preload [] = {
 0x02, 0x0F, 0x47, 0xFE, 0x10, 0x0D, 0x40, 0x36, 0x40, 0x5A, 0x40, 0x94, 0x40, 0xB4, 0x40, 0xFA,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0x0C, 0xA0, 0x41, 0x1C, 0x03, 0x40, 0x04, 0xC1, 0xD0, 0x60, 0x3F, 0x00, 0x09, 0x71, 0xC0, 0x21,
 0x40, 0x06, 0x06, 0x90, 0x10, 0xF7, 0xC0, 0x20, 0x3F, 0x02, 0xC0, 0x60, 0x3F, 0x04, 0xC0, 0xA0,
 0x3F, 0x06, 0xD0, 0xE0, 0x3F, 0x01, 0x13, 0x05, 0xD0, 0x10, 0xDC, 0x40, 0x06, 0x02, 0x16, 0xFD,
 0x10, 0x03, 0xDC, 0x70, 0x06, 0x02, 0x16, 0xFD, 0x04, 0x5B, 0x0D, 0x0B, 0x06, 0xA0, 0x40, 0xB4,
 0x0F, 0x0B, 0xC1, 0xC7, 0x13, 0x16, 0x04, 0xC0, 0xD0, 0x20, 0x60, 0x04, 0x0A, 0x30, 0xC0, 0xC0,
 0x04, 0xC1, 0x02, 0x02, 0x04, 0x00, 0xCC, 0x01, 0x06, 0x02, 0x16, 0xFD, 0x04, 0xC0, 0xD0, 0x20,
 0x41, 0x51, 0x06, 0xC0, 0x0A, 0x30, 0xA0, 0x03, 0x0C, 0xA0, 0x41, 0xAE, 0xD8, 0x20, 0x41, 0x51,
 0xB0, 0x00, 0x04, 0x5B, 0xD8, 0x20, 0x41, 0x1A, 0x3F, 0x00, 0x02, 0x00, 0x41, 0xD6, 0xC8, 0x00,
 0x3F, 0x02, 0x02, 0x00, 0x40, 0x06, 0xC8, 0x00, 0x3F, 0x04, 0x02, 0x00, 0x40, 0x10, 0xC8, 0x00,
 0x3F, 0x06, 0x04, 0x5B, 0x04, 0xC7, 0xD0, 0x20, 0x3F, 0x01, 0x13, 0x13, 0xC0, 0x20, 0x41, 0x18,
 0x06, 0x00, 0x0C, 0xA0, 0x41, 0x52, 0x02, 0x04, 0x00, 0x05, 0x02, 0x05, 0x3F, 0x02, 0x02, 0x06,
 0x41, 0x42, 0x8D, 0xB5, 0x16, 0x03, 0x06, 0x04, 0x16, 0xFC, 0x10, 0x09, 0x06, 0x00, 0x16, 0xF1,
 0x10, 0x09, 0xC0, 0x20, 0x3F, 0x02, 0x0C, 0xA0, 0x41, 0x52, 0x80, 0x40, 0x14, 0x03, 0x0C, 0xA0,
 0x41, 0x9A, 0x05, 0x47, 0xD8, 0x07, 0xB0, 0x00, 0x04, 0x5B, 0x0D, 0x0B, 0x06, 0xA0, 0x40, 0xB4,
 0x0F, 0x0B, 0xC1, 0xC7, 0x13, 0x04, 0xC0, 0x20, 0x3F, 0x0C, 0x0C, 0xA0, 0x41, 0xAE, 0x04, 0x5B,
 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x41, 0x10,
 0x02, 0x01, 0x41, 0x15, 0x02, 0x02, 0x0B, 0x00, 0x03, 0xA0, 0x32, 0x02, 0x32, 0x30, 0x32, 0x30,
 0x32, 0x30, 0x36, 0x00, 0x02, 0x02, 0x00, 0x06, 0x36, 0x31, 0x06, 0x02, 0x16, 0xFD, 0x03, 0xC0,
 0x0C, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x88, 0x00, 0x41, 0x18, 0x1A, 0x03, 0xC0, 0x60, 0x41, 0x18, 0x0C, 0x00, 0x0D, 0x00,
 0x0A, 0x40, 0x02, 0x01, 0x0B, 0x00, 0xA0, 0x20, 0x41, 0x16, 0x17, 0x01, 0x05, 0x81, 0xA0, 0x60,
 0x41, 0x14, 0x02, 0x03, 0x41, 0x42, 0x02, 0x02, 0x00, 0x10, 0x03, 0xA0, 0x32, 0x01, 0x06, 0xC1,
 0x32, 0x01, 0x32, 0x00, 0x06, 0xC0, 0x32, 0x00, 0x36, 0x00, 0x36, 0x33, 0x06, 0x02, 0x16, 0xFD,
 0x03, 0xC0, 0x0F, 0x00, 0xC0, 0x60, 0x41, 0x18, 0x0C, 0x00, 0x02, 0x00, 0x3F, 0x00, 0x02, 0x01,
 0x41, 0x42, 0x02, 0x02, 0x00, 0x08, 0xCC, 0x31, 0x06, 0x02, 0x16, 0xFD, 0x0C, 0x00, 0x02, 0x01,
 0x41, 0x4C, 0xD0, 0xA0, 0x41, 0x50, 0x06, 0xC2, 0xD0, 0xA0, 0x41, 0x4F, 0x02, 0x03, 0x0B, 0x00,
 0x03, 0xA0, 0x32, 0x03, 0x32, 0x31, 0x32, 0x31, 0x32, 0x31, 0x36, 0x01, 0x36, 0x30, 0x06, 0x02,
 0x16, 0xFD, 0x03, 0xC0, 0x0C, 0x00, 0x03, 0x40
};

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

static void __attribute__ ((noinline)) flashSector () {
  static uint32_t flashing = 0;
  static uint32_t repeat = 0;
  int i;
  int retry = 0;
  UF2_Block_Ptr p = (UF2_Block_Ptr)&(tms9918->vram.bytes [0]);

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

/*
 * set up the MPU to guard a 32 byte page from a given address
 */
static void guard(void* a) {
  uintptr_t addr = (uintptr_t)a;
#if PICO_RP2040 // Old memory protection unit
  mpu_hw->rbar = (addr & (uint)~0xff) | M0PLUS_MPU_RBAR_VALID_BITS | 0;
  mpu_hw->rasr = 1 | (0x07 << 1) | (0xfe << 8) | 0x10000000; // 0xfe = initial 32 bytes only
#else
  mpu_hw->rnr = 0;
  mpu_hw->rbar = (addr & (uint)~31u) | (2u << M33_MPU_RBAR_AP_LSB) | M33_MPU_RBAR_XN_BITS;
  mpu_hw->rlar = (addr & (uint)~31u) | M33_MPU_RLAR_EN_BITS;
#endif
}

static int didFault = 0;

/*
 * hardfault handler (triggered by MPU for GPU DMA requests)
 */
void isr_hardfault () {
  didFault = 1;
  TMS_REGISTER(tms9918, 0x38) = 0; // Stop the GPU
  mpu_hw->ctrl = 0; // Turn off memory protection - all models
}

/*
 * run a gpu dma job
 */
static void triggerGpuDma()
{
  uint32_t srcVramAddr = __builtin_bswap16(*(uint16_t*)(tms9918->vram.bytes + 0x8000));
  uint32_t dstVramAddr = __builtin_bswap16(*(uint16_t*)(tms9918->vram.bytes + 0x8002));
  uint32_t width = tms9918->vram.bytes[0x8004];
  uint32_t height = tms9918->vram.bytes[0x8005];
  uint32_t stride = tms9918->vram.bytes[0x8006];
  uint32_t params = tms9918->vram.bytes[0x8007];
  *(uint16_t*)(tms9918->vram.bytes + 0x8008) = 0;

  int32_t dstInc = params & 0x02 ? -1 : 1;
  int32_t srcInc = params & 0x01 ? 0 : dstInc;

  uint8_t *srcPtr = tms9918->vram.bytes + srcVramAddr;
  uint8_t *dstPtr = tms9918->vram.bytes + dstVramAddr;
  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x, srcPtr += srcInc, dstPtr += dstInc)
      *dstPtr = *srcPtr;
    srcPtr += (stride - width) * srcInc;
    dstPtr += (stride - width) * dstInc;
  }
}

/*
 * TMS9900 GPU main loop implementation
 */
static void __attribute__ ((noinline)) volatileHack () {
  tms9918->restart = 0;
  if ((tms9918->gpuAddress & 1) == 0) { // Odd addresses will cause the RP2040 to crash
    uint16_t lastAddress = tms9918->gpuAddress;
restart:
    TMS_REGISTER(tms9918, 0x38) = 1;
    TMS_STATUS(tms9918, 2) |= 0x80; // Running

#if PICO_RP2040 // Old memory protection unit
    mpu_hw->ctrl = M0PLUS_MPU_CTRL_PRIVDEFENA_BITS | M0PLUS_MPU_CTRL_ENABLE_BITS; // (=5) Turn on memory protection
#else
    mpu_hw->ctrl = M33_MPU_CTRL_PRIVDEFENA_BITS | M33_MPU_CTRL_ENABLE_BITS; // (=5) Turn on memory protection
#endif
    lastAddress = run9900 (tms9918->vram.bytes, lastAddress, 0xFFFE, &TMS_REGISTER(tms9918, 0x38));
    mpu_hw->ctrl = 0; // Turn off memory protection - all models

    if (TMS_REGISTER(tms9918, 0x38) & 1) { // GPU program decided to stop itself?
      tms9918->gpuAddress = lastAddress;
      tms9918->restart = 0;
    }
    if (tms9918->vram.bytes[0x8008]){
      triggerGpuDma();
    }
    if (didFault) {
      didFault = 0;
      goto restart;
    }
  }
  TMS_STATUS(tms9918, 2) &= ~0x80; // Stopped
  TMS_REGISTER(tms9918, 0x38) = 0;
}

/*
 * initialize the TMS9900 GPU
 */
void gpuInit()
{
  /* copy pre-load code to GPU ram at >4000 ...and >4800 ?*/
  memcpy (tms9918->vram.map.gram1, preload, sizeof (preload));
  memcpy (tms9918->vram.map.gram1 + 0x800, preload, sizeof (preload));
  
  tms9918->gpuAddress = 0x4000;

  guard(&(tms9918->vram.bytes [0x8000]));
}

/*
 * TMS9900 GPU main loop 
 */
void gpuLoop()
{
  while (1)
  {
    if (tms9918->restart)
      volatileHack ();
    if (tms9918->flash)
      flashSector ();
  }
}