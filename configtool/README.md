# PICO9918 Configurator

The configurator is a software tool used to modify PICO9918 configuration options, including:

* Clock rate
* Scanline CRT effect
* Scanline sprite limit
* Default palette
* Diagnostics overlays

Additionally, firmware updates can be provided via the Configurator. The full configurator is available for the **TI-99/4A**, **ColecoVision** and **MSX**. With cut-down builds (without firmware updates) available for several other machines.

See the configurator in action:

[![PICO9918 Configurator - ColecoVision](https://img.visualrealmsoftware.com/youtube/thumb/PBArYupT9qM)](https://youtu.be/PBArYupT9qM?t=9)

The configurator was written in a [custom fork of CVBasic](https://github.com/visrealm/CVBasic).

### Web-based Configurator

If you don't have a device supported by the native configurator, the [Web-based PICO9918 Configurator](https://visrealm.github.io/pico9918/config/index.html) can be used to generate a config .uf2 file. Just drag-and-drop the resulting file onto your PICO9918 using the same method as for firmware updates.

## Building

### Prerequisites

#### Option 1: Use existing tools
- CMake 3.13 or later
- Python 3 (accessible as `python3`)
- CVBasic compiler (`cvbasic.exe`)
- GASM80 assembler (`gasm80.exe`) 
- XDT99 XAS99 assembler (for TI-99 builds)

#### Option 2: Auto-build tools (Linux/cross-platform)
- CMake 3.13 or later
- Python 3 (accessible as `python3`)
- Git (for checking out tool repositories)
- C compiler (GCC or Clang for building tools)

### Usage

#### Integrated Build (Recommended)
The configurator is now integrated into the main PICO9918 build system:

```bash
# Build from project root
mkdir build && cd build
cmake ..
make                    # Build firmware
make configurator_all   # Build all configurator ROMs
```

All final artifacts will be in `build/dist/`:
- **Firmware**: `pico9918-vga-build-v1-0-2.uf2`
- **Configurator ROMs**: `pico9918_v1-0-2_*.rom` / `pico9918_v1-0-2_*.bin`

#### Standalone Build (Legacy)
```bash
cd configtool
mkdir build && cd build
cmake .. [-DBUILD_TOOLS_FROM_SOURCE=ON]
make configurator_all
```

#### Build Individual Platforms
```bash
make ti99              # TI-99/4A
make ti99_f18a         # TI-99/4A F18A Testing  
make coleco            # ColecoVision
make msx_asc16         # MSX ASCII16 mapper
make msx_konami        # MSX Konami mapper
make sg1000            # SG1000/SC3000
make nabu              # NABU
make creativision      # CreatiVision
make nabu_mame_package # NABU MAME (.npz)
```

#### Build with Ninja (faster)
```bash
cmake .. -G Ninja [-DBUILD_TOOLS_FROM_SOURCE=ON]
ninja configurator_all
```

## How it Works

1. **Firmware Dependency**: Checks for pre-built firmware in `../build/src/`
2. **UF2 Conversion**: Converts firmware to CVBasic data using `uf2cvb.py`
3. **CVBasic Compilation**: Compiles `.bas` sources for each target platform
4. **Assembly**: Uses GASM80 (most platforms) or XAS99 (TI-99) for final ROM creation
5. **Packaging**: Creates platform-specific ROM files (.bin, .rom, .nabu, etc.)

## Platform Support

| Platform | Mapper/Banking | Output Format | Assembly Tool |
|----------|----------------|---------------|---------------|
| TI-99/4A | 8KB banks | .bin cartridge | XAS99 + linkticart |
| ColecoVision | 16KB banks | .rom | GASM80 |
| MSX | ASCII16/Konami | .rom | GASM80 |
| NABU | No banking | .nabu/.npz | GASM80 |
| CreatiVision | No banking | .bin | GASM80 |

## Tool Detection

### Default Mode (existing tools)
The build system automatically searches for tools in:
1. `configtool/tools/cvbasic/` (bundled tools)
2. `../CVBasic/build/Release/` (local CVBasic build)  
3. System PATH
4. `c:/tools/xdt99/` (Windows) or `/usr/local/bin`, `/opt/xdt99` (Linux)

### Auto-build Mode (`-DBUILD_TOOLS_FROM_SOURCE=ON`)
When enabled, CMake will:
1. Clone CVBasic from https://github.com/visrealm/CVBasic.git
2. Clone gasm80 from https://github.com/visrealm/gasm80.git  
3. Clone XDT99 from https://github.com/endlos99/xdt99.git
4. Build CVBasic and gasm80 from source using their CMake systems
5. Install tools to `build/external/` directory
6. Use the locally-built tools for ROM generation

This mode enables fully cross-platform builds without pre-installed tools.
