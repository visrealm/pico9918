# Building pico9918-core

The default build is the useful emulator library: multiple instances, the full PICO9918
renderer and GPU, runtime chip selection, the PRO line width and the debugger API. The
examples, tests and Python module stay out until you ask for them.

## What the build needs

- CMake 3.22 or newer
- a C11 compiler
- Python 3, using only its standard library, to turn the splash and font PNGs into C
  arrays
- Doxygen, only if you want the `docs` target
- Python development headers, only if you want the Python module

The library has no runtime dependencies beyond the platform C runtime. It builds static
by default; a desktop build honours `BUILD_SHARED_LIBS=ON`. Pico firmware is always
static.

## A first build

```sh
cmake -S . -B build
cmake --build build
```

That one build can create TMS9918, TMS9918A, F18A, PICO9918 and PICO9918 PRO instances.
Call `pico9918_set_chip()` after creating one when the emulated machine needs a specific
personality; a new instance otherwise starts at `PICO9918_CHIP_MAX`, which is PRO in the
default wide build.

The 64KB map, enhanced renderer and GPU are always part of pico9918-core. If all you
want is the smaller original chip, that is what `vrEmuTms9918` is for. The
[emulator integration guide](https://github.com/visrealm/pico9918-core/blob/main/EMULATOR-INTEGRATION.md)
explains what each layer needs from the host once it is built.

The PICO9918 firmware supplies its own smaller policy when it adds this directory: both
RP2040 and RP2350 builds turn runtime chip selection and the debug API off. RP2040 also
turns the wide 80-column line off; RP2350 keeps it on.

## Static or shared

The default produces a static library. Use CMake's standard switch for a shared one:

```sh
cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON
cmake --build build-shared
```

The `pico9918::core` target carries the right linkage definition to consumers. A static
Windows consumer inherits `PICO9918_STATIC`; a shared library uses
`PICO9918_COMPILING_DLL` privately while it is compiled, and its consumers get the
`dllimport` declarations. Do not set any of those definitions yourself when linking the
CMake target.

## Put it in another CMake build

Vendored and installed builds use the same target name:

```cmake
# vendored
add_subdirectory(external/pico9918-core)
target_link_libraries(my_emulator PRIVATE pico9918::core)

# or installed
find_package(pico9918_core CONFIG REQUIRED)
target_link_libraries(my_emulator PRIVATE pico9918::core)
```

For the installed form:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/where/you/want
cmake --build build
cmake --install build
```

`test/package/` is a small working consumer. CI installs the library and builds that
project separately, so it catches exports that only happened to work in-tree.

## Library and emulator options

These are the settings an emulator integration is likely to care about. Pass them to
CMake as `-Dname=value` when configuring the library, not as compiler definitions on
the emulator target.

| option | default | what it changes |
|---|---|---|
| `PICO9918_SINGLE_INSTANCE` | `0` | `1` puts one VDP at a fixed address and drops the instance argument from nearly every call. It is for firmware; leave it off in an emulator |
| `PICO9918_TEXT80_8BPP` | `ON` | renders 80-column text at eight bits a pixel, which is what ECM, palette select and the bitmap layer need there. It doubles the widest line to 512 bytes |
| `PICO9918_RUNTIME_CHIP` | `ON` | adds a per-instance TMS9918 / TMS9918A / F18A / PICO9918 / PRO selector. PRO also needs the wide 80-column line |
| `PICO9918_NO_SPLASH` | `OFF` | drops the splash overlay and its image asset |
| `PICO9918_SPLASH_IMAGE` | `res/splash.png` | chooses the PNG generated into the splash asset, relative to `src/overlay/` |
| `PICO9918_DEBUG_API` | `ON` | builds `pico9918_debug.h`: the memory map, span read and write, register store and GPU controls a debugger wants |
| `PICO9918_DIAG_GPU_FRAME_COUNTER` | `OFF` | adds the optional GPU-frames row and its host-pushed counter to the diagnostics overlay |
| `PICO9918_EXAMPLES` | `OFF` | builds the programs in `examples/` against the same public target a consumer uses |
| `PICO9918_PYTHON_BINDING` | `OFF` | builds the CPython extension and makes the static library position-independent |

`PICO9918_SINGLE_INSTANCE=1` changes the calling convention of nearly every public
function. Do not define it yourself when using an installed library. The generated
`pico9918_build_config.h` records the choice the archive was built with and
`pico9918.h` rejects a disagreement.

The line width, runtime-chip and debug choices are recorded there too. Size a line
buffer with `PICO9918_SCANLINE_BUFFER_SIZE` and it will match the archive you actually
linked.

## Advanced build controls

These are mostly useful to the firmware and this repository itself:

| option | default | what it changes |
|---|---|---|
| `PICO9918_GPU_C_CORE` | `OFF` | on a Pico build, uses the portable C TMS9900 core instead of the hand-written Thumb core. Desktop builds already use C, so the switch changes nothing there |
| `PICO9918_PORTABLE_CODEGEN` | `OFF` | suppresses host-specific code generation (`-march=native` or `/arch:AVX2`). `PICO9918_GOLDEN=ON` turns it on automatically |
| `PICO9918_V9938_BASE` | `OFF` | exposes the additive V9938 scaffold. This is not a finished V9938 implementation and is not an emulator personality to offer yet |
| `PICO9918_WERROR` | `OFF` | asks GCC or Clang for `-Wall -Wextra -Wpedantic -Werror`. The MSVC build already uses `/W4 /WX` |

`PICO9918_PICO_BUILD`, `PICO9918_BUILD_*`, `PICO9918_ASSET_PIXEL_SIZE` and
`PICO9918_ASM_SUFFIX` are derived by CMake. `PICO9918_STATIC` and
`PICO9918_COMPILING_DLL` are selected from the target type. They appear in generated
headers or compile commands, but they are not consumer settings and should not be
supplied on the command line.

The source-level platform seam is separate again. A host compiling the library itself
can provide `PICO9918_HOST_OPS_HEADER`, interrupt and critical-section operations, a
clock, and the pixel/fill policy described in `src/impl/platform.h`. Those definitions
must reach the library's translation units; adding them only to the emulator executable
cannot change an archive that has already been compiled. Most desktop integrations do
not need any of them.

## Harness-only options

These select the repository's own test programs. They are not library features:

| option | default | what it builds |
|---|---|---|
| `PICO9918_GOLDEN` | `OFF` | the committed golden-frame regression harness; also enables portable code generation |
| `PICO9918_DEBUG_TEST` | `OFF` | the debugger surface test; also enables `PICO9918_DEBUG_API` |
| `PICO9918_PIXEL_TEST` | `OFF` | the post-palette pixel path and line-geometry test |
| `PICO9918_GPU_TEST` | `OFF` | the library-paced GPU test |
| `PICO9918_TMS9900_TEST` | `OFF` | the portable GPU core's instruction tests |

The ordinary CMake settings - `BUILD_SHARED_LIBS`, `CMAKE_BUILD_TYPE`,
`CMAKE_INSTALL_PREFIX`, generator and toolchain selection - work normally and are not
duplicated above.

## Python module

The in-tree form is:

```sh
cmake -S . -B build-py -DPICO9918_PYTHON_BINDING=ON
cmake --build build-py
```

It produces a module named `pico9918`. The binding is one Python object per VDP, so it
requires `PICO9918_SINGLE_INSTANCE=0`.

The binding can also be built on its own against an installed library:

```sh
cmake -S bindings/python -B build-py -DCMAKE_PREFIX_PATH=/where/pico9918-core/is
cmake --build build-py
```

In that form the installed static library must have been built as position-independent
code. The top-level `PICO9918_PYTHON_BINDING=ON` route handles that for you.

## Documentation and packages

If Doxygen was found while configuring a standalone build:

```sh
cmake --build build --target docs
```

The HTML goes to `doc/code`. Binary packages can be made from the build directory with
`cpack -G TGZ` or `cpack -G ZIP`; they contain whichever library type that build was
configured for and are therefore platform-specific. Shared packages carry `-shared` in
the filename so they can sit beside the default static ones.
