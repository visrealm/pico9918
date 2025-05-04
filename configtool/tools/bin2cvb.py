#
# Project: pico9918
#
# PICO9918 Configurator .binary to CVBasic data converter
#
# Copyright (c) 2025 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918
#


import sys
import argparse
import glob

def main() -> int:
    parser = argparse.ArgumentParser(
        description='Convert binary files to CVBasic source files of binary data.',
        epilog="GitHub: https://github.com/visrealm/pico9918")
    parser.add_argument('-o', '--outfile', help='output file - defaults to base input file name with .bas extension')
    parser.add_argument('binfile', nargs='+', help='binary file or pattern to match multiple files')
    args = vars(parser.parse_args())

    bin_files = []
    for pattern in args['binfile']:
        bin_files.extend(glob.glob(pattern))

    if not bin_files:
        print("No matching files found.")
        sys.exit(1)

    for filename in bin_files:
        output_filename = args['outfile'] if args['outfile'] else filename + ".bas"

        with open(output_filename, mode='w') as output:
            try:
                with open(filename, mode='rb') as binFile:
                    inpbuf = binFile.read(8)
                    while inpbuf:
                        byteStr = ["${:02x}".format(b) for b in inpbuf]
                        output.write("  DATA BYTE {0}\n".format(", ".join(byteStr)))
                        inpbuf = binFile.read(8)

            except FileNotFoundError:
                print(f"The file '{filename}' was not found.")
                continue
            except IOError:
                print(f"An error occurred while reading the file '{filename}'.")
                continue

    return

# program entry
if __name__ == "__main__":
    sys.exit(main())