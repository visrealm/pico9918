Any custom tools required for the project go here:

# [img2carray.py](img2carray.py)

An image converter. Converts images into C arrays for direct use in a PICO-56 program.

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

Convert images into C-style arrays for use with the PICO-56.

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

[The root CMakeLists.txt](../CMakeLists.txt) contains a `visrealm_generate_image_source()` function which can be used to integrate this tool into your build process.

```sh
visrealm_generate_image_source(<program-name> <output-file> <rom-images> [<ram-images>])
```

Here is an example usage in your project's CMakeLists.txt:

```sh
visrealm_generate_image_source(${PROGRAM} images res/*.png res/myramimage.png)
```

This function will generate the C source file(s) from the input images and also add the .c file to the `target_sources()`. The generated file(s) will be placed in yout project's build directory.