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


import sys
import struct
import argparse


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30

def isUf2(buf):
    w = struct.unpack("<II", buf[0:8])
    return w[0] == UF2_MAGIC_START0 and w[1] == UF2_MAGIC_START1


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Convert .UF2 file to banked CVBasic source file of binary data.',
        epilog="GitHub: https://github.com/visrealm/pico9918")
    parser.add_argument('-b', '--banksize', help='bank size (8 or 16)', default=8, type=int, choices=[8,16])
    parser.add_argument('-o', '--outfile', help='output file - defaults to base input file name with .bas extension', default='firmware.bas')
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

    with open(args['outfile'], mode='w') as output:

        output.write("\n' ===============================\n")
        output.write("firmwareFilename:\n")
        output.write("  DATA BYTE \"{0}\"\n".format(filename.ljust(32)))

        BLOCK_SIZE = 8 * 4 + 256    # 9 ints and 256 bytes of data
        BANK_SIZE = 1024 * args['banksize']
        BLOCKS_PER_BANK = int(BANK_SIZE / BLOCK_SIZE)

        print("Bank size: {0} bytes".format(BANK_SIZE))
        print("Block size: {0} bytes".format(BLOCK_SIZE))
        print("Blocks per bank: {0}".format(BLOCKS_PER_BANK))

        try:
            with open(filename, mode='rb') as uf2:
                blocksRead = 0
                bank = 1
                inpbuf = uf2.read(512)            
                while inpbuf:                
                    if not isUf2(inpbuf):
                        print("Error!", filename, " is not a .uf2 file")
                        exit()

                    if blocksRead % BLOCKS_PER_BANK == 0:               
                        output.write("\n' ===============================\n")
                        output.write("BANK {0}\n\n".format(bank))
                        output.write("bank{0}Start:\n".format(bank))
                        output.write("  DATA BYTE {0}\n".format(48 + bank))
                        bank += 1
                    
                    w = struct.unpack("<IIIIIIII", inpbuf[0:32])
                    output.write("\n ' Block: {0}\n".format(w[5]))
                    output.write(" ' Addr: {0}\n".format(hex(w[3])))
                    output.write(" ' Bank: {0}\n".format(bank - 1))
                    for h in range (0, 32, 4):
                        byteStr = []
                        for b in inpbuf[h:h + 4]:
                            byteStr.append("${0}".format(b.to_bytes().hex()))
                        output.write("  DATA BYTE {0}\n".format(", ".join(byteStr)))

                    for r in range(0, 256, 16):
                        byteStr = []
                        for b in inpbuf[32 + r:32 + r + 16]:
                            byteStr.append("${0}".format(b.to_bytes().hex()))
                        output.write("  DATA BYTE {0}\n".format(", ".join(byteStr)))

                    #byteStr = []
                    #for b in inpbuf[508:512]:
                    #    byteStr.append("${0}".format(b.to_bytes().hex()))
                    #output.write("  DATA BYTE {0}\n".format(", ".join(byteStr)))

                    inpbuf = uf2.read(512)
                    blocksRead += 1
                        
        except FileNotFoundError:
            print("The file '{0}' was not found.".format(filename))
            exit()
        except IOError:
            print("An error occurred while reading the file '{0}'.".format(filename))
            exit()
            
    return

# program entry
if __name__ == "__main__":
    sys.exit(main())
