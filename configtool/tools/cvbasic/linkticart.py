#!/usr/bin/env python3

import os.path
import sys

# take the first name, and return (base,zeros)
# where zeros is how many leading zeros are in the index
def parseFilename(fn):
    zerocnt = 1
    namebase = fn[:-5]
    while (namebase[-1] == '0'):
        zerocnt += 1
        namebase = namebase[:-1]
    return (namebase, zerocnt)

# converts a binary from xas99 (xas99.py -b -R file.a99) with banks to a single non-inverted cart image
# note: minimal error checking - GIGO.
# pass the name of the first file (ie: file_b0.bin)
if (len(sys.argv) < 3):
    print('Pass the first output file (ie: file_b0.bin), and the output file, and optionally a name for the cart')
    print('ie: linkticart.py file_b0.bin file_8.bin "AWESOME GAME"')
    exit(1)

f = open(sys.argv[1], 'rb')
data = f.read()
f.close()

# first 80 bytes are the cartridge header
hdr = data[0:80]

if (len(sys.argv) > 3):
    name = sys.argv[3].upper()
    while (len(name)<20):
        name += ' '
    p = hdr.find(b'CVBASIC GAME        *')
    if p == -1:
        print('WARNING: Could not find cart name to set it')
    else:
        hdr = hdr[0:p] + bytearray(name, 'utf-8') + hdr[p+20:]

# after 16k starts the RAM data
ram = data[16384:]

# make sure we have 3 pages to pull from (especially if not banked, it won't be padded)
while len(ram) < 8192*3:
    ram += b'\xff'*8192

fo = open(sys.argv[2], 'wb')

# write the loader pages
fo.write(hdr)
fo.write(ram[0:8112])
fo.write(hdr)
fo.write(ram[8112:16224])
fo.write(hdr)
fo.write(ram[16224:24336])
# any excess is discarded

# now check if there are any pages to concatenate
# track pages written so we can square up the final size
sz = 3

if (sys.argv[1][-5:] != '0.bin'):
    print('Banking not detected - finishing cart...')
else:
    print('Banked cart detected...')

    (namebase,zerocnt) = parseFilename(sys.argv[1])

    file = namebase + str(sz).zfill(zerocnt) + '.bin'
    
    while os.path.isfile(file):
        f = open(file, 'rb')
        data = f.read()
        if len(data) < 8192:
            while len(data)<8192:
                data += b'\xff'
        f.close()
        fo.write(data)
        sz+=1
        file = namebase + str(sz).zfill(zerocnt) + '.bin'

# calculate number of files needed for power of two
desired=0
if sz>64:
    desired=128
elif sz>32:
    desired=64
elif sz>16:
    desired=32
elif sz>8:
    desired=16
elif sz>4:
    desired=8
else:
    desired=4

while sz<desired:
    sz+=1
    fo.write(hdr)
    fo.write(b'\xff'*8112)

fo.close()
print('Wrote final cart size:',sz*8,'KB')










