/**
 * \file
 * \brief host-driven flash access: firmware update and program data storage
 *
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 */

#include "flash.h"

#include "impl/pico9918_priv.h"

#include "hardware/flash.h"

#include "pico.h" // For PICO_RP2040

#include <string.h>


/** \brief a UF2 block as it arrives from the host: 32 byte header, payload, magicEnd */
struct UF2_Block
{
  uint32_t magicStart0;
  uint32_t magicStart1;
  uint32_t flags;
  uint32_t targetAddr;
  uint32_t payloadSize;
  uint32_t blockNo;
  uint32_t numBlocks;
  uint32_t familyID;
  uint8_t data[256]; // the Pico UF2 payload is 256 bytes, not the format's full 476
  uint32_t magicEnd;
};
typedef struct UF2_Block* UF2_Block_Ptr;

#if PICO_RP2040
#define FAMILY_ID 0xe48bff56
#else
#define FAMILY_ID 0xe48bff59
#endif

#define PAYLOAD 256

#define FLASH_STATUS_VALIDATING 1
#define FLASH_STATUS_ERASING    2
#define FLASH_STATUS_WRITING    3

#define FLASH_ERROR_OK       0
#define FLASH_ERROR_HEADER   1
#define FLASH_ERROR_SEQUENCE 2
#define FLASH_ERROR_SIZE     3
#define FLASH_ERROR_VERIFY   4
#define FLASH_ERROR_FULL     6

// Status Reg #2 for flashing status (shared with GPU status)
// bit  7:   running or not
// bit  6-5: retry count
// bits 4-2: error code
// bits 1-0: status

/** \brief set the error code the host reads back in status register 2 */
static void setFlashStatusError(uint8_t error)
{
  TMS_STATUS(tms9918, 2) = (TMS_STATUS(tms9918, 2) & ~0x1c) | ((error & 7) << 2);
}

/** \brief set the operation status the host reads back in status register 2 */
static void setFlashStatusCode(uint8_t status)
{
  TMS_STATUS(tms9918, 2) = (TMS_STATUS(tms9918, 2) & ~0x03) | ((status & 3));
}

static uint32_t lastWriteAddr = 0;

/** \brief validate one UF2 block staged in VRAM and program or read it back */
static void doFlashFirmwareSector(void)
{
  static uint32_t flashing = 0;

  // vram address where uf2 block is stored is set in vreg(0x3f)[5:0] (256 byte boundaries)
  uint8_t flashReg = TMS_REGISTER(tms9918, 0x3f);

  const int vramAddr = (flashReg & 0x3f) << 8;
  const bool write   = flashReg & 0x80;

  setFlashStatusCode(FLASH_STATUS_VALIDATING);

  UF2_Block_Ptr p = (UF2_Block_Ptr)(tms9918->vram.bytes + vramAddr);

  tms9918->flash = 0;

  if ((p->magicStart0 != 0x0A324655) ||                                                                // UF2\n
      (p->magicStart1 != 0x9E5D5157) || (p->magicEnd != 0x0AB16F30) || (p->numBlocks >= 0x00000400) || // 256KB Max
      (p->flags != 0x00002000) ||      // familyID present - no others set
      (p->familyID != FAMILY_ID) ||    // RP2040/RP2350
      (p->targetAddr < 0x10000000) ||  // Flash address
      (p->targetAddr >= 0x10040000) || // +256KB
      ((p->targetAddr & 0xFF) != 0) || // Target must be 256 byte aligned
      (p->payloadSize != PAYLOAD))
  { // Only support standard size
    setFlashStatusError(FLASH_ERROR_HEADER);
    return;
  }

  lastWriteAddr = p->targetAddr & ~(XIP_BASE);

  if (write)
  {
    if (p->blockNo == 0)
    {
      setFlashStatusCode(FLASH_STATUS_ERASING);
      flashing = 1;
      flash_range_erase(0, ((PAYLOAD * p->numBlocks) + 0xFFF) & ~0xFFF);
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

    flash_range_program(a, p->data, PAYLOAD);
  }
  else
  {
    memcpy(p, (uint32_t*)(p->targetAddr), PAYLOAD);
  }

  if ((p->blockNo + 1) == p->numBlocks)
  {
    flashing = 0;
  }
  setFlashStatusError(FLASH_ERROR_OK);
}

#define PROGDATA_BLOCK_COUNT  1024       // 256KB
#define PROGDATA_FLASH_OFFSET (0x100000) // Top 1MB of flash
#define PROGDATA_FLASH_ADDR   (uint32_t*)(XIP_BASE + PROGDATA_FLASH_OFFSET)

// always fully overwritten by memcpy from the actual flash sector before any
// read - doesn't need the crt0 .bss zero-fill
static uint8_t __uninitialized_ram(sectorBuffer)[0x1000]; // capture a sector before writing

/** \brief read or write one GUID-keyed program data block, staged in VRAM */
static void doFlashProgramData(void)
{
  tms9918->flash = 0;

  uint8_t flashReg = TMS_REGISTER(tms9918, 0x3f);

  // grab vram address. register 0x3f's lowest 6 bits are the MSB of the VRAM address
  const int vramAddr = (flashReg & 0x3f) << 8;
  const bool write   = flashReg & 0x80;

  setFlashStatusCode(FLASH_STATUS_VALIDATING);

  // VDP flash block data format
  // bytes 0-3    [4]   (little-endian block id hint). use 0xffffffff if unknown (first read)
  // bytes 4-19   [16]  (128-bit GUID)
  // bytes 20-35  [16]  (User-friendly name)
  // bytes 36-255 [220] (Program data in any format)
  // the block id is only a hint - it is ignored unless the GUID at that block matches
  uint32_t* p = (uint32_t*)(tms9918->vram.bytes + vramAddr);

  // in flash, the blocks are stored with the blockId leading, then
  // the GUID, then the name, etc.

  uint32_t* addr = PROGDATA_FLASH_ADDR;

  // for reading any block - just need a special GUID which includes the block ID
  uint32_t emptyBlockIndex = -1;
  uint32_t blockIndex      = *p;
  bool foundBlock          = false;

  const int blockWords = 256 / sizeof(uint32_t);
  const int guidBytes  = 16;

  // block index encoded in first word already?
  if (blockIndex < PROGDATA_BLOCK_COUNT)
  {
    uint32_t* tempAddr = addr + (blockWords * blockIndex);
    if (*tempAddr == blockIndex)
    {
      if (memcmp(addr + 1, p + 1, guidBytes) == 0)
      {
        foundBlock = true;
        addr       = tempAddr;
      }
    }
  }

  if (!foundBlock)
  {
    for (blockIndex = 0; blockIndex < PROGDATA_BLOCK_COUNT; ++blockIndex, addr += blockWords)
    {
      if (*addr == blockIndex)
      {
        if (memcmp(addr + 1, p + 1, guidBytes) == 0)
        {
          foundBlock = true;
          // found it
          break;
        }
      }
      else if (emptyBlockIndex == -1)
      {
        emptyBlockIndex = blockIndex;
      }
    }
  }

  // we didn't find the block, but we can allocate one
  if (!foundBlock && emptyBlockIndex != -1)
  {
    blockIndex = emptyBlockIndex;
    addr       = PROGDATA_FLASH_ADDR + (emptyBlockIndex * blockWords);
    foundBlock = true;

    if (!write) // reading? just set the block id and return
    {
      p[0] = blockIndex;
      setFlashStatusError(FLASH_ERROR_OK);
      return;
    }
  }

  if (!foundBlock)
  {
    setFlashStatusError(FLASH_ERROR_FULL);
    return;
  }

  // set block index in VRAM
  p[0] = blockIndex;

  if (write)
  {
    setFlashStatusCode(FLASH_STATUS_ERASING);

    uint8_t* sectorPtr = (uint8_t*)(((uintptr_t)addr) & 0xfffff000);
    memcpy(sectorBuffer, sectorPtr, 0x1000);

    uint32_t sectorOffset = ((uintptr_t)sectorPtr) & ~XIP_BASE;
    uint32_t pageOffset   = ((uintptr_t)addr) & 0xfff;

    // write new block into sector
    uint32_t* blockDest = (uint32_t*)(sectorBuffer + pageOffset);
    memcpy(blockDest, p, 0x100);

    flash_range_erase(sectorOffset, 0x1000);

    setFlashStatusCode(FLASH_STATUS_WRITING);

    bool success = false;
    int attempts = 5;
    while (attempts--)
    {
      flash_range_program(sectorOffset, (const void*)sectorBuffer, 0x1000);

      if (memcmp(sectorPtr + pageOffset, (const void*)sectorBuffer + pageOffset, 0x100) == 0)
      {
        success = true;
        break;
      }
    }

    setFlashStatusError(success ? FLASH_ERROR_OK : FLASH_ERROR_VERIFY);
  }
  else // read
  {
    memcpy(p + 1, addr + 1, 0x100 - sizeof(uint32_t));
    setFlashStatusError(FLASH_ERROR_OK);
  }
}

/** \brief dispatch to the firmware or program data path, then clear the trigger */
void __attribute__((noinline)) flashSector(void)
{
  if (TMS_REGISTER(tms9918, 0x3f) & 0x40) // write firmware
  {
    doFlashFirmwareSector();
  }
  else // read or write program data
  {
    doFlashProgramData();
  }

  TMS_STATUS(tms9918, 2) &= ~0x80; // Stopped
  TMS_REGISTER(tms9918, 0x38) = 0;
}
