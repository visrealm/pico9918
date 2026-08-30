# pico9918-core

<a href="https://github.com/visrealm/pico9918-core/actions/workflows/render.yml"><img src="https://github.com/visrealm/pico9918-core/actions/workflows/render.yml/badge.svg"/></a>
<a href="https://github.com/visrealm/pico9918-core/actions/workflows/portability.yml"><img src="https://github.com/visrealm/pico9918-core/actions/workflows/portability.yml/badge.svg"/></a>
<a href="https://github.com/visrealm/pico9918-core/actions/workflows/bindings.yml"><img src="https://github.com/visrealm/pico9918-core/actions/workflows/bindings.yml/badge.svg"/></a>
<a href="https://github.com/visrealm/pico9918-core/actions/workflows/docs.yml"><img src="https://github.com/visrealm/pico9918-core/actions/workflows/docs.yml/badge.svg"/></a>
<a href="https://github.com/visrealm/pico9918-core/actions/workflows/package.yml"><img src="https://github.com/visrealm/pico9918-core/actions/workflows/package.yml/badge.svg"/></a>

TMS9918A / TMS9928A / TMS9929A video display processor emulation with F18A
enhancements, in C11 with no runtime dependencies.

It renders **one scanline at a time** into a buffer you own, allocates nothing per
line, and never calls back into your program. That is what lets the same code drive
the [PICO9918](https://github.com/visrealm/pico9918) at video rate on an RP2040 and
sit inside a desktop emulator unchanged.

Building the overlay image assets (the splash and the diagnostics font) needs
**Python 3 with [pillow](https://pypi.org/project/pillow/)** at build time -
`tools/img2carray.py` turns the PNGs in `src/overlay/res/` into C arrays. The
emulator core itself links nothing extra.

## What it renders

The four documented modes of the
[TMS9918A datasheet](http://www1.cs.columbia.edu/~sedwards/papers/TMS9918.pdf),
plus the [F18A](https://github.com/dnotq/f18a) superset:

| | |
|---|---|
| **Base modes** | Graphics I, Graphics II, Multicolor, Text (40 column) |
| **Base features** | sprites with magnification, fifth-sprite reporting, sprite collision, VSYNC interrupt |
| **F18A modes** | 80-column text, 24 / 30 / 48 / 60 row screens |
| **F18A tiles** | a second tile layer, per-tile palette select, position-based attributes, tile/sprite priority |
| **F18A colour** | ECM 1, 2 and 3 (one, two and three bitplanes) on both tiles and sprites, a 64-entry palette |
| **F18A scrolling** | horizontal and vertical, per-page, with split and cross-page cases |
| **F18A layers** | a bitmap layer, above or below the tiles |
| **F18A GPU** | a TMS9900 core running programs out of VRAM |

Locked, it is a TMS9918A: the enhancements are unreachable until a guest runs the
F18A unlock sequence.

## Quick start

A complete, buildable version of this is [`examples/render_frame.c`](examples/render_frame.c) -
it sets a mode up, fills the tables, renders 192 lines and writes a PPM:

```
cmake -S . -B build -DPICO9918_MODE=1 -DPICO9918_EXAMPLES=ON
cmake --build build --target render_frame
./build/examples/render_frame frame.ppm
```

```c
#include "pico9918.h"
#include "pico9918_util.h"

static uint32_t framebuffer[192 * TMS9918_PIXELS_X];

int main(void)
{
  // one VDP, or as many as you like - the instance reaches every call
  pico9918_t* tms9918 = pico9918_new();

  pico9918_reset(tms9918);

  // Graphics I with the default table addresses and a cleared VRAM. A system
  // emulator would not use the helpers at all: pico9918_write_addr(),
  // pico9918_write_data(), pico9918_read_status() and pico9918_read_data() are
  // the whole host bus, and a guest program writes everything else for itself.
  pico9918_initialise_gfx_i(tms9918);

  // one 8x8 glyph into pattern 1 - after initialise_gfx_i, which clears VRAM last
  static const uint8_t tile[8] = {0x3C, 0x42, 0x81, 0xA5, 0x81, 0x99, 0x42, 0x3C};
  pico9918_set_address_write(tms9918, TMS_DEFAULT_VRAM_PATT_ADDRESS + 8);
  pico9918_write_bytes(tms9918, tile, sizeof tile);

  // Graphics I gives one colour byte per eight patterns, so this colours a whole group
  pico9918_set_address_write(tms9918, TMS_DEFAULT_VRAM_COLOR_ADDRESS);
  pico9918_write_byte_rpt(tms9918, pico9918_fg_bg_color(TMS_WHITE, TMS_DK_BLUE), 32);

  // a row of them
  pico9918_set_address_write(tms9918, TMS_DEFAULT_VRAM_NAME_ADDRESS);
  pico9918_write_byte_rpt(tms9918, 1, 32);

  pico9918_write_register_value(tms9918, TMS_REG_1, TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE);

  // Nothing is drawn until you ask, and nothing allocates per line. scan_line
  // renders into an internal buffer and returns the status byte a host would have
  // read; line_source and line_bytes are how you reach what it drew.
  for (uint16_t y = 0; y < 192; ++y)
  {
    pico9918_scan_line(tms9918, y);

    const uint8_t* line   = pico9918_line_source(tms9918);
    const uint32_t pixels = pico9918_line_bytes(tms9918);
    for (uint32_t x = 0; x < pixels; ++x)
    {
      // a palette index, not a colour. 0x0rgb comes back, four bits a channel.
      framebuffer[y * pixels + x] = pico9918_default_palette(line[x] & 0x0F);
    }
  }

  pico9918_destroy(tms9918);
  return 0;
}
```

Built with `PICO9918_SINGLE_INSTANCE=1` - what the firmware ships - there is one VDP at a
fixed address instead: `pico9918_init()` replaces `pico9918_new()` and every call above
drops its first argument. The `PICO9918_INST` macros in `pico9918.h` spell a call that
compiles either way, which is what the examples and the tests use.

## Examples

`PICO9918_EXAMPLES=ON` adds these to the library's own build. Each links
`pico9918::core` and includes only the public headers, so each also builds against an
installed package: `cmake -S examples -B build-examples -DCMAKE_PREFIX_PATH=<staging>`.

| | |
|---|---|
| [`render_frame.c`](examples/render_frame.c) | Graphics I from nothing: registers, three tables, 192 lines, a PPM |
| [`host_bus.c`](examples/host_bus.c) | the same screen driven the way a guest machine drives it - two ports, four operations, and a frame interrupt acknowledged by reading the status port |
| [`f18a_modes.c`](examples/f18a_modes.c) | one name table drawn twice, locked and unlocked: ECM2, attributes by screen position, the second tile layer and both scrolls |
| [`gpu_program.c`](examples/gpu_program.c) | a program loaded into VRAM and run on the F18A's TMS9900, on a thread of its own beside a raster paced to 60Hz |

The last two need `PICO9918_MODE=1` and are skipped without it.

[`gpu_program.py`](examples/gpu_program.py) is the same job the other way round: one
thread, alternating bounded slices of program with lines of raster. Both shapes are
real, and which one a host wants is the decision those two files are about - a GPU
program can wait on the display, so a host that never advances the raster while one
runs waits forever. They run the same two programs, and are worth reading together.

## From Python

`bindings/python/` is the same VDP as a module: one class, the host's ports, and the frame
read back as palette indices or as RGB.

```
cmake -S . -B build -DPICO9918_MODE=1 -DPICO9918_PYTHON_BINDING=ON
cmake --build build
PYTHONPATH=build/bindings/python python bindings/python/test.py --png frame.png
```

```python
import pico9918

vdp = pico9918.Vdp()
vdp.write_regs([0x00, 0xE0, 0x0E, 0x80, 0x01, 0x76, 0x03, 0x04])
vdp.write_vram(0x3800, bytes(32 * 24))
frame = vdp.indices(192)          # one palette index a byte
```

It needs the default `PICO9918_SINGLE_INSTANCE=0` - a class per VDP is the point of it.

Built `PICO9918_MODE=1`, it carries the F18A's GPU as well. Unlock the chip, put a
TMS9900 program in VRAM and write its entry address to VR54 and VR55 - the low byte
last, because writing that one is what starts it:

```python
vdp.gpu_init()
vdp.unlock()
vdp.write_vram(0x1B02, program)
vdp.write_reg(0x36, 0x1B)
vdp.write_reg(0x37, 0x02)   # arms it at 0x1B02 and away it goes
vdp.gpu_step()              # runs to IDLE, then returns
```

`gpu_step()` runs the program to completion, which is fine until the program waits on
something. A GPU program can read the scanline being scanned out at `>7000`, and one
that pages a bitmap in the vertical blank polls it until the raster is somewhere safe -
so `gpu_step()` never returns, because the caller that would move the raster is the one
blocked inside it. `gpu_step_n()` is bounded and comes back with the PC kept, which is
what lets the one thread do both:

```python
while vdp.gpu_step_n(20000):
    vdp.raster()            # a display line, and the register that program is reading
```

The firmware instead gives the GPU a core of its own. A Python caller has no way to hand
it one - the GIL is held across every call into the module, deliberately - so the
interleave above is the shape to reach for here.

## Building

```
cmake -S . -B build -DPICO9918_MODE=1
cmake --build build
```

**`-DPICO9918_MODE=1` is what turns the F18A on.** The default build is a plain
TMS9918A - everything in the F18A rows above needs it.

| option | default | |
|---|---|---|
| `PICO9918_MODE` | `0` | `0` is a plain TMS9918A: 16KB of VRAM, no GPU, no unlock, and the enhanced renderer folds away entirely. `1` is the F18A |
| `PICO9918_SINGLE_INSTANCE` | `0` | `1` puts one VDP at a fixed address and drops the instance argument from every call. What the firmware ships |
| `PICO9918_TEXT80_8BPP` | `OFF` | 80-column text at eight bits a pixel, which is what ECM, palette select and the bitmap layer need there. Doubles the line to 512 bytes |
| `PICO9918_NO_SPLASH` | `OFF` | drop the splash overlay and its image asset |
| `PICO9918_EXAMPLES` | `OFF` | build `examples/` |
| `PICO9918_WERROR` | `OFF` | `-Wall -Wextra -Werror` |

`PICO9918_SINGLE_INSTANCE` and `PICO9918_TEXT80_8BPP` change the public headers, so
they reach consumers through the exported target and the generated
`pico9918_build_config.h`. Size a line buffer with `PICO9918_SCANLINE_BUFFER_SIZE` and
it will be right for the library you actually linked.

## Installing

```
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/where/you/want
cmake --build build
cmake --install build
```

then, from another project:

```cmake
find_package(pico9918_core CONFIG REQUIRED)
target_link_libraries(app PRIVATE pico9918::core)
```

`test/package/` is that, as a working project - it is what the `package` CI job builds
and runs to prove the export.

## Testing

There is no `ctest` target. `tools/ci.sh` is the whole desktop gate, one subcommand a job,
and it is the same script CI runs:

```
tools/ci.sh goldens     the 16 committed frames, byte-exact
tools/ci.sh suite       111 scenes, five properties and two GPU programs, both widths
tools/ci.sh warnings    -Wall -Wextra -Werror
tools/ci.sh multi       the instance threaded through every signature
tools/ci.sh tms9918     PICO9918_MODE=0, its frame against the F18A build's
tools/ci.sh package     install it, then find_package it from a separate project
tools/ci.sh python      the Python module against an installed library
tools/ci.sh doxygen     the API documentation
```

None of it builds for the RP2040 or RP2350 - the Pico path needs the SDK - so a green
badge here means the library is correct and portable, not that the firmware builds.

## Where it is used

The [PICO9918](https://github.com/visrealm/pico9918) is a drop-in replacement for the
TMS9918A in a TI-99/4A, ColecoVision, MSX, NABU, CreatiVision or any other machine that
used one. This library is its renderer.

[HBC-56](https://github.com/visrealm/hbc-56) uses `vrEmuTms9918`, the library this one
grew out of, to render to an SDL texture.

## Contributing

This repository is generated: the library is developed at `core/` in
[visrealm/pico9918](https://github.com/visrealm/pico9918), where a change can be measured
against a device, and split out from there. Issues and pull requests belong on that
repository - see [CONTRIBUTING.md](CONTRIBUTING.md), which also covers the one class of
change that is easier to make here.

## License

This code is licensed under the [MIT](https://opensource.org/licenses/MIT "MIT") license
