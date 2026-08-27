Any custom tools required for the project go here:

# [format.py](format.py)

Applies [`.clang-format`](../.clang-format) to the firmware `src/` and to the
`pico9918-core` library in `core/`, or checks that they are already formatted.

The config is derived from the sources rather than a stock preset: 2-space
indent, Allman braces, `Type* name`, 110 columns, and the column-aligned
`#define` and assignment blocks the codebase already uses. Layouts that are read
by column - the GPU preload dump, the VGA timing tables, the palette, the
tile/text clone tables - are fenced with `clang-format off`/`on` in the source,
because reflowing a grid to fit a column limit loses the thing it is for.

Formatting is whitespace, so it cannot change what the compiler emits; the pass
that introduced it was checked by rebuilding both boards and comparing the
binaries byte for byte.

## Usage

```sh
python3 format.py                  # rewrite both trees in place
python3 format.py --check          # report and exit 1 if anything is unformatted
python3 format.py firmware         # limit to one tree
```

`--check` names each offending file and exits non-zero, so it works as a
pre-commit or CI gate. It needs `clang-format` on `PATH`, or an LLVM install in
`%ProgramFiles%`.

## API documentation

The same sources carry Doxygen comments, configured by
[`Doxyfile`](../Doxyfile) at the repository root:

```sh
doxygen Doxyfile                            # output in build/doxygen/html
cmake --build build --target docs           # same thing, if doxygen was found
```

`EXTRACT_ALL` is deliberately off so that `WARN_IF_UNDOCUMENTED` means
something: a warning is a gap to fill rather than noise to silence. Statics are
extracted, because the renderer's emitters are static and are the part a reader
most needs explained. The `PREDEFINED` block teaches the preprocessor about the
SDK's placement wrappers and the library's instance-argument macros - without it
those swallow the signatures.

Every function, type and file carries a doc block, and that subset is clean.
File-scope macros and variables are covered by a group comment over the block
rather than one comment each, which doxygen still counts as undocumented, so
read the list with those filtered out:

```sh
doxygen Doxyfile 2>&1 | grep -v "(macro definition)\|(variable)"
```

# [size_report.py](size_report.py)

Reports per-symbol code/data sizes from a built ELF, largest first. Useful
for spotting inlining/cloning bloat and for finding cold-in-flash candidates.

By default it groups symbols by **code vs data** (using `nm`'s type letter),
not by section name - the Pico SDK's `copy_to_ram` linker script merges
`__time_critical_func()` code into the `.data` output section alongside real
data, so filtering by section name alone misses it (this bit us once: the
two largest functions in the whole firmware, `pico9918_scan_line` /
`pico9918_output_sprites`, both live in `.data`). Each row still shows which section
the symbol actually lives in.

## Usage

```sh
python3 size_report.py [-h] [-g] [-s SECTIONS] [-n LIMIT] [--toolchain-bin DIR] elf

positional arguments:
  elf                   .elf file, or a build directory to search for one

options:
  -h, --help            show this help message and exit
  -g, --group-sections  group by section instead of code/data, auto-discovering
                        every section that has symbols (largest section first)
  -s SECTIONS, --sections SECTIONS
                        comma-separated section names to group by, e.g.
                        -s .flashcode,.flashcode_sdk (implies --group-sections)
  -n LIMIT, --limit LIMIT
                        max rows per group (default: unlimited)
  --toolchain-bin DIR   directory containing arm-none-eabi-* tools
```

Requires the arm-none-eabi toolchain (`nm`, `objdump`, `size`) on `PATH`, or
pass `--toolchain-bin` pointing at the SDK's toolchain `bin` directory.

The `elf` argument is positional and must come after any `-s`/`--sections`
value on the command line (argparse otherwise can't tell where the section
list ends and the path begins).

## Example usage

```sh
python3 size_report.py build/pico9918pro -n 20
```

Prints the 20 largest code symbols and 20 largest data symbols (wherever
each actually lives), plus a full section-size summary.

```sh
python3 size_report.py build/pico9918pro --group-sections -n 10
```

Same, but grouped by section instead - every section with symbols, largest
first, 10 rows each.

```sh
python3 size_report.py build/pico9918pro -s .flashcode,.flashcode_sdk
```

Lists only symbols in those two specific sections.

Prints the 20 largest symbols in `.flashcode` (flash-resident) and `.text`
(RAM-resident, the `copy_to_ram` cost) from the firmware ELF under
`build/pico9918pro`, plus a full section-size summary.

# [img2carray.py](img2carray.py)

An image converter. Converts images into C arrays for direct use in a PICO9918 program.

The format of the image data will be `const uint16_t[]` for 24 or 32 bit images and will be a combined `const uint16_t[]` for the palette and a `const uint8_t[]` at 4 bits per pixel for 16 color paletized images and 8 bits per pixel for 256 color paletized images.

Alpha values are also supported.

The output format is `0bAAAABBBBGGGGRRRR` or `0xABGR`.

## Dependencies

The script requires the [Pillow Imaging Library](https://pypi.org/project/Pillow/).

Installation:

```sh
python3 -m pip install --upgrade Pillow
```

## Usage

```sh
python3 img2carray.py [-h] [-v] [-p PREFIX] [-o OUT] [-r RAM [RAM ...]] [-i IN [IN ...]]

Convert images into C-style arrays for use with the PICO9918.

options:
  -h, --help            show this help message and exit
  -v, --verbose         verbose output
  -p PREFIX, --prefix PREFIX
                        array variable prefix
  -o OUT, --out OUT     output file - defaults to base input file name with .c extension
  -r RAM [RAM ...], --ram RAM [RAM ...]
                        input file(s) to store in Pi Pico RAM - can use wildcards
  -i IN [IN ...], --in IN [IN ...]
                        input file(s) to store in Pi Pico ROM - can use wildcards
```

### input

A filename or glob (wildcards) to convert. By default, the arrays will be stored in the Pi Pico flash/ROM. TO have an image array assigned to be stored in RAM, pass it in using the -r / --ram command-line prefix.

### output

Optional parameter to specify a single output file.

By default, the output files will be named the same as the input file(s) with a .c/.h extension. This option allows you to combine multiple images into a single C source. A header file of the same name is also generated.

## Example usage

```sh
python img2carray.py -i res/*.png -o images.c
```

Will generate images.c and images.h containing all .png images in the res directory.

## CMake integration

[visrealm_tools.cmake](../visrealm_tools.cmake) contains a `visrealm_generate_image_source()` function which can be used to integrate this tool into your build process.

```sh
visrealm_generate_image_source(<program-name> <output-file> <rom-images> [<ram-images>])
```

Here is an example usage in your project's CMakeLists.txt:

```sh
visrealm_generate_image_source(${PROGRAM} images res/*.png res/myramimage.png)
```

This function will generate the C source file(s) from the input images and also add the .c file to the `target_sources()`. The generated file(s) will be placed in yout project's build directory.

# [bin2carray.py](bin2carray.py)

The binary equivalent of `img2carray.py`: turns arbitrary binary files into C
arrays. `visrealm_generate_bindata_source()` in
[visrealm_tools.cmake](../visrealm_tools.cmake) wraps it, and `test/host` uses it
to embed the ROM its bus test-bed drives.

## Usage

```sh
python bin2carray.py <input-files> -o <output-name>
```

# [concat_uf2.py](concat_uf2.py)

Joins UF2 files end to end. UF2 blocks carry their own target address and family
ID, so concatenating an RP2040 image and an RP2350 image produces the single
combined firmware that boots on either board. The root
[CMakeLists.txt](../CMakeLists.txt) calls it to build `pico9918-v<version>.uf2`.

## Usage

```sh
python concat_uf2.py input1.uf2 [input2.uf2 ...] output.uf2
```
