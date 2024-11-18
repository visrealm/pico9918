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


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30

def isUf2(buf):
    w = struct.unpack("<II", buf[0:8])
    return w[0] == UF2_MAGIC_START0 and w[1] == UF2_MAGIC_START1

filename = sys.argv[-1]

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

with open("firmware.bas", mode='w') as output:


    try:
        with open(filename, mode='rb') as uf2:
            blocksRead = 0
            bank = 1
            inpbuf = uf2.read(512)            
            while inpbuf:                
                if not isUf2(inpbuf):
                    print("Error!", filename, " is not a .uf2 file")
                    exit()

               if blocksRead % 28 == 0:               
                    output.write("\n' ===============================\n".format(bank))
                    output.write("BANK {0}\n".format(bank))
                    bank += 1
                
                w = struct.unpack("<IIIIIIII", inpbuf[0:32])
                output.write("\n ' Block: {0}\n".format(w[5]))
                output.write(" ' Addr: {0}\n".format(hex(w[3])))
                output.write(" ' Num blocks: {0}\n".format(w[6]))
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

                inpbuf = uf2.read(512)
                blocksRead += 1
                    
    except FileNotFoundError:
        print("The file 'my_file.txt' was not found.")
        exit()
    except IOError:
        print("An error occurred while reading the file 'my_file.txt'.")
        exit()




