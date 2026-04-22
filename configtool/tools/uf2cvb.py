#
# Project: pico9918
#
# PICO9918 Configurator .UF2 to CVBasic converter
#
# Copyright (c) 2024 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918
#

import math
import os
import re
import sys
import struct
import argparse
from datetime import datetime


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30

UF2_FAMILY_RP2040 = 0xe48bff56
UF2_FAMILY_RP2350 = 0xe48bff59

def isUf2(buf):
    w = struct.unpack("<II", buf[0:8])
    return w[0] == UF2_MAGIC_START0 and w[1] == UF2_MAGIC_START1


def write_block(output, bank, w, inpbuf, global_idx):
    """Write a single UF2 block as CVBasic DATA statements"""
    output.write("\n ' Block: {0}\n".format(w[5]))
    output.write(" ' Addr: {0}\n".format(hex(w[3])))
    output.write(" ' Bank: {0}\n".format(bank))
    output.write("uf2Block{0}:\n".format(global_idx))
    for h in range(0, 32, 4):
        byteStr = ["${:02x}".format(b) for b in inpbuf[h:h + 4]]
        output.write("  DATA BYTE {0}\n".format(", ".join(byteStr)))
    for r in range(0, 256, 16):
        byteStr = ["${:02x}".format(b) for b in inpbuf[32 + r:32 + r + 16]]
        output.write("  DATA BYTE {0}\n".format(", ".join(byteStr)))
    byteStr = ["${0:02X}".format(b) for b in inpbuf[508:512]]
    output.write("  DATA BYTE {0}\n".format(", ".join(byteStr)))


def write_blocks_section(output, blocks, start_bank, blocks_per_bank, use_banking, global_idx_start):
    """Write a section of blocks to output organised by bank. Returns next available bank."""
    bank = start_bank
    for section_idx, (w, inpbuf) in enumerate(blocks):
        if section_idx % blocks_per_bank == 0:
            output.write("\n' ===============================\n")
            if use_banking:
                output.write("BANK {0}\n\n".format(bank))
                output.write("bank{0}Start:\n".format(bank))
                output.write("  DATA BYTE {0}\n".format("${:02x}".format(bank)))
            output.write("bank{0}Data:\n".format(bank))
            bank += 1
        write_block(output, bank - 1, w, inpbuf, global_idx_start + section_idx)
    return bank


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Convert .UF2 file to banked CVBasic source file of binary data.',
        epilog="GitHub: https://github.com/visrealm/pico9918")
    parser.add_argument('-b', '--banksize', help='bank size (0, 8 or 16)', default=8, type=int, choices=[0,8,16])
    parser.add_argument('-o', '--outfile', help='output file - defaults to base input file name with .bas extension', default='firmware')
    parser.add_argument('uf2file')
    args = vars(parser.parse_args())

    filename = args['uf2file']

    #    uint32_t magicStart0;
    #    uint32_t magicStart1;
    #    uint32_t flags;
    #    uint32_t targetAddr;
    #    uint32_t payloadSize;
    #    uint32_t blockNo;
    #    uint32_t numBlocks;
    #    uint32_t familyID;
    #    uint8_t  data [476];
    #    uint32_t magicEnd;

    BLOCK_SIZE = 9 * 4 + 256    # 9 ints and 256 bytes of data
    BANK_OVERHEAD = 48          # bank overhead. we can't use it all :(
    if (args['banksize']):
        BANK_SIZE = (1024 * args['banksize']) - BANK_OVERHEAD
    else:
        BANK_SIZE = 1024 * 128
    BLOCKS_PER_BANK = int((BANK_SIZE - 1) / BLOCK_SIZE)

    majorVer = 0
    minorVer = 0
    patchVer = 0

    match = re.search(r".*-v(\d*)-(\d*)-(\d*)", filename)
    if (match):
        majorVer = match.group(1)
        minorVer = match.group(2)
        patchVer = match.group(3)

    # Read all blocks and group by family ID
    all_blocks = []
    try:
        with open(filename, mode='rb') as uf2:
            inpbuf = uf2.read(512)
            while inpbuf:
                if not isUf2(inpbuf):
                    print("Error!", filename, " is not a .uf2 file")
                    exit()
                w = struct.unpack("<IIIIIIII", inpbuf[0:32])
                all_blocks.append((w, inpbuf))
                inpbuf = uf2.read(512)
    except FileNotFoundError:
        print("The file '{0}' was not found.".format(filename))
        exit()
    except IOError:
        print("An error occurred while reading the file '{0}'.".format(filename))
        exit()

    # Group blocks by family ID, preserving order within each group
    families = {}
    for w, buf in all_blocks:
        fid = w[7]
        if fid not in families:
            families[fid] = []
        families[fid].append((w, buf))

    rp2040_blocks = families.get(UF2_FAMILY_RP2040, [])
    rp2350_blocks = families.get(UF2_FAMILY_RP2350, [])
    is_combined = len(rp2040_blocks) > 0 and len(rp2350_blocks) > 0

    if is_combined:
        blocks_2040 = len(rp2040_blocks)
        blocks_2350 = len(rp2350_blocks)
        banks_2040 = math.ceil(blocks_2040 / BLOCKS_PER_BANK)
        banks_2350 = math.ceil(blocks_2350 / BLOCKS_PER_BANK)
        rp2350_start_bank = banks_2040 + 2  # banks are 1-indexed starting at 2
        max_blocks = max(blocks_2040, blocks_2350)
        total_banks = banks_2040 + banks_2350
    else:
        single_blocks = list(families.values())[0] if families else all_blocks
        total_blocks = len(single_blocks)
        total_banks = math.ceil(total_blocks / BLOCKS_PER_BANK)

    use_banking = args['banksize'] != 0

    print("Output file     : {0}".format(args['outfile'] + ".bas"))
    print("Firmware version: {0}.{1}.{2}".format(majorVer, minorVer, patchVer))
    print("Bank size       : {0} bytes".format(BANK_SIZE))
    print("Block size      : {0} bytes".format(BLOCK_SIZE))
    print("Blocks per bank : {0}".format(BLOCKS_PER_BANK))
    if is_combined:
        print("Combined UF2    : RP2040={0} blocks, RP2350={1} blocks".format(blocks_2040, blocks_2350))
    else:
        print("Single UF2      : {0} blocks".format(total_blocks))

    fwDate = datetime.fromtimestamp(os.path.getmtime(filename))

    with open(args['outfile'] + ".h.bas", mode='w') as header:
        with open(args['outfile'] + ".bas", mode='w') as output:

            header.write("\n' ===============================\n")
            header.write("  GOTO CONFTOOL_START\n")
            header.write("firmwareFilename:\n")
            header.write("  DATA BYTE \"{0}\"\n".format(os.path.basename(filename).ljust(32)[:32]))
            header.write("\n\n")
            header.write("  CONST FIRMWARE_BLOCKS_PER_BANK = {0}\n".format(BLOCKS_PER_BANK))
            header.write("  CONST #FIRMWARE_BLOCK_BYTES = {0}\n".format((4 * 9) + 256))
            header.write("  CONST FIRMWARE_MAJOR_VER = {0}\n".format(majorVer))
            header.write("  CONST FIRMWARE_MINOR_VER = {0}\n".format(minorVer))
            header.write("  CONST FIRMWARE_PATCH_VER = {0}\n".format(patchVer))
            header.write("  CONST #FIRMWARE_YEAR = {0}\n".format(fwDate.year))
            header.write("  CONST FIRMWARE_MONTH = {0}\n".format(fwDate.month))
            header.write("  CONST FIRMWARE_DAY = {0}\n".format(fwDate.day))

            if is_combined:
                header.write("  CONST FIRMWARE_COMBINED = 1\n")
                header.write("  CONST #FIRMWARE_BLOCKS = {0}\n".format(max_blocks))
                header.write("  CONST #FIRMWARE_BLOCKS_2040 = {0}\n".format(blocks_2040))
                header.write("  CONST #FIRMWARE_BLOCKS_2350 = {0}\n".format(blocks_2350))
                header.write("  CONST FIRMWARE_BANKS_2040 = {0}\n".format(banks_2040))
                header.write("  CONST FIRMWARE_BANKS_2350 = {0}\n".format(banks_2350))
                header.write("  CONST FIRMWARE_2350_START_BANK = {0}\n".format(rp2350_start_bank))
            else:
                header.write("  CONST FIRMWARE_COMBINED = 0\n")
                header.write("  CONST #FIRMWARE_BLOCKS = {0}\n".format(total_blocks))
                header.write("  CONST FIRMWARE_BANKS = {0}\n".format(total_banks))

            header.write("\n\nCONFTOOL_START:\n")

            if is_combined:
                write_blocks_section(output, rp2040_blocks, 2, BLOCKS_PER_BANK, use_banking, 0)
                write_blocks_section(output, rp2350_blocks, rp2350_start_bank, BLOCKS_PER_BANK, use_banking, blocks_2040)
            else:
                write_blocks_section(output, single_blocks, 2, BLOCKS_PER_BANK, use_banking, 0)

    print("Total banks     : {0}".format(total_banks))
    print("Total blocks    : {0}".format(len(all_blocks)))

    return


# program entry
if __name__ == "__main__":
    sys.exit(main())
