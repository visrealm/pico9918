#!/usr/bin/env python3
# concat_uf2.py - Concatenate multiple UF2 files into one
# Usage: python3 concat_uf2.py input1.uf2 input2.uf2 ... output.uf2
import sys

if len(sys.argv) < 3:
    print(f"Usage: {sys.argv[0]} input1.uf2 [input2.uf2 ...] output.uf2")
    sys.exit(1)

inputs = sys.argv[1:-1]
output = sys.argv[-1]

with open(output, 'wb') as out:
    for path in inputs:
        with open(path, 'rb') as f:
            out.write(f.read())

print(f"Combined {len(inputs)} UF2 file(s) into: {output}")
