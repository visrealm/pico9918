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
    uint8_t  data [256];//476]; // Pico .UF2 files only use 256 byte blocks, so I omit the extra 220 unused bytes
    uint32_t magicEnd;
} UF2_Block;
typedef struct UF2_Block * UF2_Block_Ptr;


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

#define FLASH_STATUS_IDLE         0
#define FLASH_STATUS_VALIDATING   1
#define FLASH_STATUS_ERASING      2
#define FLASH_STATUS_WRITING      3
#define FLASH_STATUS_VERIFYING    4

#define FLASH_ERROR_OK            0
#define FLASH_ERROR_HEADER        1
#define FLASH_ERROR_SEQUENCE      2
#define FLASH_ERROR_SIZE          3
#define FLASH_ERROR_VERIFY        4
#define FLASH_ERROR_BUSY          5

#define STATUS_BYTE(R, E, S) 0x80 | ((R & 3) << 5) | ((E & 7) << 2) | (S & 3)

void setFlashStatusRetry(int retry)
{
  TMS_STATUS(tms9918, 2) = (TMS_STATUS(tms9918, 2) & ~0x60) | ((retry & 3) << 5);
}

void setFlashStatusError(uint8_t error)
{
  TMS_STATUS(tms9918, 2) = (TMS_STATUS(tms9918, 2) & ~0x1c) | ((error & 7) << 2);
}

void setFlashStatusCode(uint8_t status)
{
  TMS_STATUS(tms9918, 2) = (TMS_STATUS(tms9918, 2) & ~0x03) | ((status & 3));
}

void doFlashSector()
{
  static uint32_t flashing = 0;
  int i;
  int retry = 0;

  // vram address where uf2 block is stored is set in vreg(0x3f)[5:0] (256 byte boundaries)
  uint8_t flashReg = TMS_REGISTER(tms9918, 0x3f);

  // Status Reg #2 for flasing status (shared with GPU status)
  // bit  7:   running or not
  // bit  6-5: retry count
  // bits 4-2: error code
  // bits 1-0: status
  
  const int vramAddr = (flashReg & 0x3f) << 8;
  const bool write = flashReg & 0x40;
  const bool verify = !write;

  uint8_t statusRetryCount = 0;
  setFlashStatusCode(FLASH_STATUS_VALIDATING);
  
  UF2_Block_Ptr p = (UF2_Block_Ptr)(tms9918->vram.bytes + vramAddr);

  //tms9918->vram.bytes [767] = 0x00; // Wait
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
      (p->payloadSize != PAYLOAD))
  {    // Only support standard size
    setFlashStatusError(FLASH_ERROR_HEADER);
    return;
  }

  const uint32_t writeOffset = 0;//0x100000; //TEMP: Add 1MB
  uint32_t originalTargetAddr = p->targetAddr & ~(XIP_BASE);
  p->targetAddr += writeOffset; 

  if (p->blockNo == 0)
  {
    setFlashStatusCode(FLASH_STATUS_ERASING);
    flashing = 1;
    flash_range_erase (writeOffset, ((PAYLOAD * p->numBlocks) + 0xFFF) & ~0xFFF);
  }
  else if (!flashing)
  {
    setFlashStatusError(FLASH_ERROR_SEQUENCE);
    //tms9918->lockedMask = 0x07;
    return;
  }

  uint32_t a = (p->targetAddr & ~(XIP_BASE));
  uint32_t b = originalTargetAddr >> 12; // Get 4KB block number
  if (b >= 64)
  {
    setFlashStatusError(FLASH_ERROR_SIZE);
    flashing = 0;
    //tms9918->lockedMask = 0x07;
    return;
  }  

  if (write)
  {
  //  strcpy (&(tms9918->vram.bytes [736]), PROGRAMMING);
    //retry = 3;
//retry:
    setFlashStatusCode(FLASH_STATUS_WRITING);
    
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
  }
  setFlashStatusCode(FLASH_STATUS_VERIFYING);

   //else
    //strcpy (&(tms9918->vram.bytes [736]), VALIDATING);
  if (verify)
  {
    if (memcmp ((const void *)(XIP_BASE + a), (const void*)p->data, PAYLOAD) != 0)
    {
  //    if (retry)
    //  {
      //  retry--;
        //setFlashStatusRetry(++statusRetryCount);

        //goto retry;
      //}

      setFlashStatusError(FLASH_ERROR_VERIFY);

      //strcpy (&(tms9918->vram.bytes [736]), FAILEDCOMPARISON);

      //int i = 0;
      //while (i < 728) {
      //  b = *(uint8_t *)(XIP_BASE + a++);
      //  tms9918->vram.bytes [i++] = hexv (b >> 4);
      // tms9918->vram.bytes [i++] = hexv (b);
      //}
      //tms9918->vram.bytes [767] = 0x03; // Failed - comparison

      //flashing = 0;
      //tms9918->lockedMask = 0x07;
      return;
    }
  }
  
  if ((p->blockNo + 1) == p->numBlocks)
  {
    flashing = 0;
    //tms9918->lockedMask = 0x07;
    setFlashStatusError(FLASH_ERROR_OK);
  }
  else
  {
    setFlashStatusError(FLASH_ERROR_OK);
  }
}

void __attribute__ ((noinline)) flashSector () {

  doFlashSector();

  TMS_STATUS(tms9918, 2) &= ~0x80; // Stopped
  TMS_REGISTER(tms9918, 0x38) = 0;
}

