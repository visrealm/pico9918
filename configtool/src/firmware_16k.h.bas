
' ===============================
  GOTO CONFTOOL_START
firmwareFilename:
  DATA BYTE "pico9918-vga-build-v1-0-1.uf2   "


  CONST #FIRMWARE_BLOCKS = 258
  CONST FIRMWARE_BANKS = 5
  CONST FIRMWARE_BLOCKS_PER_BANK = 55
  CONST #FIRMWARE_BLOCK_BYTES = 292
  CONST FIRMWARE_MAJOR_VER = 1
  CONST FIRMWARE_MINOR_VER = 0
  CONST FIRMWARE_PATCH_VER = 1
  CONST #FIRMWARE_YEAR = 2025
  CONST FIRMWARE_MONTH = 5
  CONST FIRMWARE_DAY = 7


CONFTOOL_START:
