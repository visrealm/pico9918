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

#define FLASH_STATUS_IDLE         0
#define FLASH_STATUS_VALIDATING   1
#define FLASH_STATUS_ERASING      2
#define FLASH_STATUS_WRITING      3

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

static uint32_t lastWriteAddr = 0;

void doFlashFirmwareSector()
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
  const bool write = flashReg & 0x80;

  uint8_t statusRetryCount = 0;
  setFlashStatusCode(FLASH_STATUS_VALIDATING);
  
  UF2_Block_Ptr p = (UF2_Block_Ptr)(tms9918->vram.bytes + vramAddr);

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
  lastWriteAddr = p->targetAddr & ~(XIP_BASE);

  p->targetAddr += writeOffset;
  if (write)
  {
    if (p->blockNo == 0)
    {
      setFlashStatusCode(FLASH_STATUS_ERASING);
      flashing = 1;
      flash_range_erase (writeOffset, ((PAYLOAD * p->numBlocks) + 0xFFF) & ~0xFFF);
    }
    else if (!flashing)
    {
      setFlashStatusError(FLASH_ERROR_SEQUENCE);
      return;
    }

    uint32_t a = (p->targetAddr & ~(XIP_BASE));
    uint32_t b = lastWriteAddr >> 12; // Get 4KB block number
    if (b >= 64)
    {
      setFlashStatusError(FLASH_ERROR_SIZE);
      flashing = 0;
      return;
    }  

    setFlashStatusCode(FLASH_STATUS_WRITING);
    
    flash_range_program (a, p->data, PAYLOAD);

    // Alternate 'local' cache flush
    i = 0;
    while (i < PAYLOAD) {
      *(volatile uint32_t *)(XIP_BASE + a + i) = 0;
      i += sizeof(uint32_t);
    }
  }
  else
  {
    memcpy(p, (uint32_t*)(p->targetAddr), PAYLOAD);
  }

  if ((p->blockNo + 1) == p->numBlocks)
  {
    flashing = 0;
    setFlashStatusError(FLASH_ERROR_OK);
  }
  else
  {
    setFlashStatusError(FLASH_ERROR_OK);
  }
}

#define PROGDATA_BLOCK_SIZE   256
#define PROGDATA_BLOCK_COUNT  256 //4080          // 1MB - 4KB
#define PROGDATA_ID_SIZE      16            // 128-bit GUID
#define PROGDATA_FLASH_OFFSET (0x100000)    // Top 1MB of flash
#define PROGDATA_FLASH_ADDR   (uint32_t*)(XIP_BASE + PROGDATA_FLASH_OFFSET)

static uint8_t sectorBuffer[0x1000];  // capture a sector before writing

void doFlashProgramData()
{
  tms9918->flash = 0;

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
  const bool write = flashReg & 0x80;

  uint8_t statusRetryCount = 0;
  setFlashStatusCode(FLASH_STATUS_VALIDATING);

  // VRAM block should have 16 byte GUID followed by 16 byte user-friendly name
  // followed by 220 bytes of data
  // the last 4 bytes will get the block Id
  uint32_t *p = (uint32_t *)(tms9918->vram.bytes + vramAddr);

  // in flash, the blocks are stored with the blockId leading, then 
  // the GUID, then the name, etc.

  uint32_t *addr = PROGDATA_FLASH_ADDR;

  // for reading any block - just need a special GUID which includes the block ID
  int emptyBlockIndex = -1;
  int blockIndex = 0;
  bool foundBlock = false;
  /*for (;blockIndex < PROGDATA_BLOCK_COUNT; ++blockIndex)
  {
    if ((*addr == blockIndex) && (*(addr + 1) == *p))
    {
      TMS_REGISTER(tms9918, 7) = 0xf2;  
      if (memcmp(addr + 1, p, 16) == 0)
      {
        foundBlock = true;
        // found it
        break;
      }
    }
    else if (emptyBlockIndex < 0)
    {
      emptyBlockIndex = blockIndex;
    } 
  if (!foundBlock)
  {
    blockIndex = emptyBlockIndex;
    addr = PROGDATA_FLASH_ADDR + (emptyBlockIndex * 256 / sizeof(uint32_t));
  //  TMS_REGISTER(tms9918, 7) = 0xf8;
  }
  else
  {
    TMS_REGISTER(tms9918, 7) = 0xf2;  
  }*/

  foundBlock = true;

  if (write)
  {
    setFlashStatusCode(FLASH_STATUS_ERASING); 
        
    uint8_t *sectorPtr = (uint8_t*)(((int)addr) & 0xfffff000);
    memcpy(sectorBuffer, sectorPtr, 0x1000);
    
    uint32_t sectorOffset = ((uintptr_t)sectorPtr) & ~XIP_BASE;
    uint32_t pageOffset = ((uintptr_t)sectorPtr) & 0xfff;
    
    // write new block into sector
    uint32_t *blockDest = (uint32_t*)(sectorBuffer + pageOffset);
    *blockDest = blockIndex;
    memcpy(blockDest + 1, p, 0x100 - sizeof(uint32_t));

    flash_range_erase(sectorOffset, 0x1000);

    setFlashStatusCode(FLASH_STATUS_WRITING);
        
    bool success = false;
    int attempts = 5;  
    while (attempts--)
    {
      flash_range_program(sectorOffset, (const void*)sectorBuffer, 0x100);

      // flush
      int i = 0;
      while (i < 0x100) {
          *((volatile uint32_t *)(sectorPtr) + i) = 0;
          i += sizeof(uint32_t);
      }

      if (memcmp(sectorPtr, (const void*)sectorBuffer, 0x100) == 0)
      {
        success = true;
        TMS_REGISTER(tms9918, 7) = 0xf7;
        break;
      }
    }
    // write block Id back into VRAM at the end of the block
    p[63] = blockIndex;
  }
  else  // read
  {
    memcpy(p, addr + 1, 0x100 - sizeof(uint32_t));
    p[63] = *addr;
  }

  setFlashStatusError(FLASH_ERROR_OK);
}

void __attribute__ ((noinline)) flashSector () {

  if (TMS_REGISTER(tms9918, 0x3f) & 0x40) // write firmware
  {
    doFlashFirmwareSector();
  }
  else // read or write program data
  {
    doFlashProgramData();
  }

  //TMS_REGISTER(tms9918, 7) = 0xf4;  

  TMS_STATUS(tms9918, 2) &= ~0x80; // Stopped
  TMS_REGISTER(tms9918, 0x38) = 0;
}

