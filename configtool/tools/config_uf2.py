#!/usr/bin/env python3
#
# Project: pico9918
#
# PICO9918 Configuration UF2 Generator
#
# Copyright (c) 2024 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918
#

import struct
import argparse
import sys

# UF2 constants
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
UF2_FLAG_FAMILYID = 0x00002000

# RP2040/RP2350 family IDs
FAMILY_ID_RP2040 = 0xe48bff56
FAMILY_ID_RP2350 = 0xe48bff59

# Flash configuration
CONFIG_FLASH_OFFSET = 0x1FF000  # Top 4KB of 2MB flash
XIP_BASE = 0x10000000
CONFIG_FLASH_ADDR = XIP_BASE + CONFIG_FLASH_OFFSET
CONFIG_BYTES = 256

# Configuration byte offsets (from config.h)
# Bytes 0-7 are not settable - generated at runtime by firmware
CONF_PICO_MODEL       = 0
CONF_HW_VERSION       = 1
CONF_SW_VERSION       = 2
CONF_SW_PATCH_VERSION = 3
CONF_CLOCK_TESTED     = 4
CONF_DISP_DRIVER      = 5
CONF_FLASH_STATUS     = 6

# Settable via registers
CONF_CRT_SCANLINES    = 8
CONF_SCANLINE_SPRITES = 9
CONF_CLOCK_PRESET_ID  = 10

CONF_DIAG             = 16
CONF_DIAG_REGISTERS   = 17
CONF_DIAG_PERFORMANCE = 18
CONF_DIAG_PALETTE     = 19
CONF_DIAG_ADDRESS     = 20

CONF_PALETTE_IDX_0    = 128

# Pico models
PICO_MODEL_RP2040 = 1
PICO_MODEL_RP2350 = 2

# Hardware versions
HWVer_0_3 = 0x03
HWVer_1_x = 0x10

# Display drivers
DISP_DRIVER_VGA  = 0
DISP_DRIVER_NTSC = 1
DISP_DRIVER_PAL  = 2

# Default TMS9918A palette (0xARGB format)
DEFAULT_PALETTE = [
    0x0000, 0xF000, 0xF2C3, 0xF5D6, 0xF54F, 0xF76F, 0xFD54, 0xF4EF,
    0xFF54, 0xFF76, 0xFDC3, 0xFED6, 0xF2B2, 0xFC5C, 0xFCCC, 0xFFFF
]


def create_default_config(pico_model=PICO_MODEL_RP2040,
                         disp_driver=DISP_DRIVER_VGA,
                         scanlines=0,
                         scanline_sprites=0,
                         clock_preset=0,
                         custom_palette=None):
    """
    Create a default configuration array (256 bytes)

    Note: PICO_MODEL and DISP_DRIVER must match the target hardware for the
    config to pass validation. HW_VERSION and SW_VERSION will be updated by firmware.
    """
    config = bytearray(CONFIG_BYTES)

    # Set fields needed for validation to pass
    config[CONF_PICO_MODEL] = pico_model
    config[CONF_DISP_DRIVER] = disp_driver

    # Leave HW_VERSION, SW_VERSION, etc. as 0 - firmware will update these

    # Set user-configurable options
    config[CONF_CRT_SCANLINES] = scanlines & 0x01
    config[CONF_SCANLINE_SPRITES] = scanline_sprites & 0x03
    config[CONF_CLOCK_PRESET_ID] = clock_preset & 0x03

    # Set diagnostics to off
    config[CONF_DIAG] = 0
    config[CONF_DIAG_REGISTERS] = 0
    config[CONF_DIAG_PERFORMANCE] = 0
    config[CONF_DIAG_PALETTE] = 0
    config[CONF_DIAG_ADDRESS] = 0

    # Set palette
    palette = custom_palette if custom_palette else DEFAULT_PALETTE

    # Palette entry 0 is always 0x0000 (black)
    config[CONF_PALETTE_IDX_0] = 0x00
    config[CONF_PALETTE_IDX_0 + 1] = 0x00

    # Palette entries 1-15 use default TMS9918A colors with 0xF alpha
    for i in range(1, 16):
        rgb = palette[i]
        # Store as big-endian (high byte first)
        config[CONF_PALETTE_IDX_0 + (i * 2)] = (rgb >> 8) & 0xFF
        config[CONF_PALETTE_IDX_0 + (i * 2) + 1] = rgb & 0xFF

    return config


def create_uf2_block(data, target_addr, block_no, num_blocks, family_id):
    """
    Create a single UF2 block (512 bytes)
    """
    block = bytearray(512)

    # Header (32 bytes)
    struct.pack_into("<I", block, 0, UF2_MAGIC_START0)    # magic start 0
    struct.pack_into("<I", block, 4, UF2_MAGIC_START1)    # magic start 1
    struct.pack_into("<I", block, 8, UF2_FLAG_FAMILYID)   # flags
    struct.pack_into("<I", block, 12, target_addr)        # target address
    struct.pack_into("<I", block, 16, len(data))          # payload size
    struct.pack_into("<I", block, 20, block_no)           # block number
    struct.pack_into("<I", block, 24, num_blocks)         # total blocks
    struct.pack_into("<I", block, 28, family_id)          # family ID

    # Data (476 bytes available, but we only use what we need)
    block[32:32+len(data)] = data

    # Magic end (4 bytes)
    struct.pack_into("<I", block, 508, UF2_MAGIC_END)

    return block


def generate_config_uf2(output_file, config_data, family_id):
    """
    Generate a UF2 file containing configuration data
    """
    # Create a single UF2 block with the configuration data
    block = create_uf2_block(config_data, CONFIG_FLASH_ADDR, 0, 1, family_id)

    # Write to file
    with open(output_file, 'wb') as f:
        f.write(block)

    print(f"Created {output_file}")
    print(f"  Family ID: 0x{family_id:08X} ({'RP2040' if family_id == FAMILY_ID_RP2040 else 'RP2350'})")
    print(f"  Target address: 0x{CONFIG_FLASH_ADDR:08X}")
    print(f"  Config size: {len(config_data)} bytes")


def main():
    parser = argparse.ArgumentParser(
        description='Generate UF2 file to reset/update PICO9918 configuration.\n\n'
                    'Note: Hardware-specific settings (Pico model, HW version, display driver)\n'
                    'are automatically detected by the firmware and cannot be set via this tool.',
        epilog="Examples:\n"
               "  %(prog)s -o reset.uf2                         # Default VGA config for RP2040\n"
               "  %(prog)s -o reset.uf2 --rp2350 --pal          # Default PAL config for RP2350\n"
               "  %(prog)s -o config.uf2 --scanlines 1          # VGA config with scanlines\n"
               "  %(prog)s -o config.uf2 --ntsc --scanlines 1   # NTSC config with scanlines\n",
        formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument('-o', '--output',
                       help='output UF2 file name',
                       required=True)

    parser.add_argument('--rp2040',
                       action='store_true',
                       help='target RP2040 (default)')

    parser.add_argument('--rp2350',
                       action='store_true',
                       help='target RP2350')

    parser.add_argument('--vga',
                       action='store_true',
                       help='VGA display driver (default)')

    parser.add_argument('--ntsc',
                       action='store_true',
                       help='NTSC display driver')

    parser.add_argument('--pal',
                       action='store_true',
                       help='PAL display driver')

    parser.add_argument('--scanlines',
                       type=int,
                       choices=[0, 1],
                       default=0,
                       help='enable CRT scanlines (0=off, 1=on, default=0)')

    parser.add_argument('--scanline-sprites',
                       type=int,
                       choices=[0, 1, 2, 3],
                       default=0,
                       help='scanline sprite limit (0-3, default=0)')

    parser.add_argument('--clock-preset',
                       type=int,
                       choices=[0, 1, 2],
                       default=0,
                       help='clock preset ID (0-2, default=0)')

    args = parser.parse_args()

    # Determine family ID and Pico model for UF2
    if args.rp2350:
        family_id = FAMILY_ID_RP2350
        pico_model = PICO_MODEL_RP2350
    else:
        family_id = FAMILY_ID_RP2040
        pico_model = PICO_MODEL_RP2040

    # Determine display driver
    if args.pal:
        disp_driver = DISP_DRIVER_PAL
    elif args.ntsc:
        disp_driver = DISP_DRIVER_NTSC
    else:
        disp_driver = DISP_DRIVER_VGA

    # Create configuration data
    config = create_default_config(
        pico_model=pico_model,
        disp_driver=disp_driver,
        scanlines=args.scanlines,
        scanline_sprites=args.scanline_sprites,
        clock_preset=args.clock_preset
    )

    # Generate UF2 file
    generate_config_uf2(args.output, config, family_id)

    print("\nConfiguration settings:")
    print(f"  Pico model: {'RP2350' if pico_model == PICO_MODEL_RP2350 else 'RP2040'}")
    disp_names = {DISP_DRIVER_VGA: 'VGA', DISP_DRIVER_NTSC: 'NTSC', DISP_DRIVER_PAL: 'PAL'}
    print(f"  Display driver: {disp_names[disp_driver]}")
    print(f"  CRT scanlines: {'ON' if args.scanlines else 'OFF'}")
    print(f"  Scanline sprites: {args.scanline_sprites}")
    print(f"  Clock preset: {args.clock_preset}")
    print(f"  Palette: Default TMS9918A")

    return 0


if __name__ == "__main__":
    sys.exit(main())
