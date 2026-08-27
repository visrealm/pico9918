# Building PICO9918 {#building}

This document describes how to build the PICO9918 firmware and configurator ROMs.

## Prerequisites

### Required Tools
- **CMake 3.13+**: Build system generator
- **ARM GNU Toolchain**: Cross-compiler for ARM Cortex-M33 (RP2350) and Cortex-M0+ (RP2040)
- **Raspberry Pi Pico SDK**: Firmware compilation (v2.1.1 recommended for compatibility)
- **Python 3**: Build scripts and asset conversion
- **Git**: For the checkout and dependencies

### Platform-Specific Setup

#### Windows
```bash
# Install dependencies
# Download and install ARM GNU Toolchain 15.2.Rel1 from:
# https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
# Extract to C:\arm-toolchain\ and add to PATH

# Python dependencies
pip install pillow

# Install Pico SDK 2.1.1
git clone -b 2.1.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git pico-sdk
cd pico-sdk
git submodule update --init
# Apply performance patches
git apply --ignore-whitespace --ignore-space-change --3way ../picosdk-2.0.0-visrealm-fastboot.patch
cd ..
```

#### Linux (Ubuntu/Debian)
```bash
# Install system dependencies  
sudo apt-get update
sudo apt-get install -y build-essential cmake python3 python3-pip git gcc-arm-none-eabi

# Python dependencies
pip3 install pillow

# Install Pico SDK 2.1.1
git clone -b 2.1.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git pico-sdk
cd pico-sdk
git submodule update --init
# Apply performance patches
git apply --ignore-whitespace --ignore-space-change --3way ../picosdk-2.0.0-visrealm-fastboot.patch
cd ..
```

#### macOS
```bash
# Install dependencies via Homebrew
brew install cmake ninja python3 git

# Install ARM GNU Toolchain (same version as other platforms for consistency)
curl -L "https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads" -o arm-toolchain.tar.xz
sudo tar -xJf arm-toolchain.tar.xz -C /opt
echo 'export PATH="/opt/arm-gnu-toolchain-15.2.rel1/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc

# Python dependencies (may require --break-system-packages on newer macOS)
pip3 install pillow

# Install Pico SDK 2.1.1
git clone -b 2.1.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git pico-sdk
cd pico-sdk
git submodule update --init
# Apply performance patches
git apply --ignore-whitespace --ignore-space-change --3way ../picosdk-2.0.0-visrealm-fastboot.patch
cd ..
```

### Development Environment Setup

To set up your development environment for the Raspberry Pi Pico, follow the [Raspberry Pi C/C++ SDK Setup](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html) instructions.

The latest PICO9918 source can be configured and built using the official [Raspberry Pi Pico VSCode plugin](https://github.com/raspberrypi/pico-vscode).

## Building Firmware

The PICO9918 firmware is the primary component - a TMS9918A VDP emulator for Raspberry Pi Pico.

### Quick Start

**Combined build - firmware for PICO9918 and PICO9918 PRO, plus all configurator ROMs (Recommended)**
```bash
mkdir build && cd build
cmake .. -DPICO9918_BUILD_COMBINED=ON
cmake --build . --target combined
cmake --build . --target build_configurators
```

Outputs in `build/dist/`: combined `.uf2` firmware and all configurator ROMs.

**Firmware only**
```bash
mkdir build && cd build
cmake ..
cmake --build . --target firmware
```

`PICO_BOARD` defaults to `pico9918pro`, so this builds the RP2350 firmware and
writes `build/dist/pico9918pro-v<version>.uf2`. For the RP2040 board, configure
with `-DPICO_BOARD=pico9918`, which writes `build/dist/pico9918-v<version>.uf2`.

Artifacts built off any branch other than `main` carry the branch name, so the
same build on this branch is `pico9918pro-v<version>-<branch>.uf2`. A VGA-only
build (`-DPICO9918_ENABLE_SCART=OFF`) adds `-vgaonly`, and `-DPICO9918_DIAG=ON`
adds `-diag`.

The `firmware` target is what populates `build/dist/`; a bare `cmake --build .`
leaves it empty.

### Firmware Configuration Options

Configure output mode and features with `-D` flags:

```bash
cmake .. -DPICO9918_DIAG=ON
```

#### Available Options
- **`PICO9918_ENABLE_SCART`** (ON/OFF, default ON): Runtime SCART dongle autodetect. VGA is always supported; when a SCART dongle is detected at boot, output switches to RGBs and the PAL/NTSC timing comes from the user configuration. Set OFF to produce a pure VGA-only firmware with no SCART detection code.
- **`PICO9918_NO_SPLASH`** (OFF/ON): Disable splash screen on startup
- **`PICO9918_DIAG`** (OFF/ON): Enable diagnostic mode by default
- **`PICO9918_TEXT80_8BPP`** (board default): 80-column text at 8bpp, which is what ECM, tile palette select and the bitmap layer need in T80. Defaults ON for the RP2350 board, OFF for the RP2040, which lacks the budget for the 512-byte line it costs.
- **`PICO9918_COLD_IN_FLASH`** (ON/OFF, default ON): Keep boot-only code and data resident in flash (XIP) rather than copying it to RAM, buying back SRAM. See `src/xip.h` for what may not live there.
- **`PICO9918_GPU_FRAME_COUNTER`** (OFF/ON): Enable the GPU frame counter, which adds a row to the diagnostic performance panel.
- **`PICO9918_LIVE_TEST`** (OFF/ON): Test builds only - the SWD live capture buffer used by `test/live/`. It costs about a microsecond a line, so timing readings from such a build are not valid. See [DEBUGGING.md](DEBUGGING.md).

#### Configuration Examples
```bash
# Default: VGA + SCART autodetect
cmake ..

# Pure VGA firmware (no SCART support)
cmake .. -DPICO9918_ENABLE_SCART=OFF

# Diagnostic build with no splash
cmake .. -DPICO9918_DIAG=ON -DPICO9918_NO_SPLASH=ON
```

### Local Build Config File

Instead of passing `-D` flags every time, you can keep build settings in an
optional local config file. If `pico9918_config.cmake` exists in the project
root it is included automatically at configure time and can set or override any
build setting; if it is absent the build behaves exactly as normal. The file is
git-ignored, so your local tweaks never get committed.

To get started, copy the committed template
[`pico9918_config.cmake.template`](pico9918_config.cmake.template) to
`pico9918_config.cmake`, then uncomment and edit the settings you want:

```bash
cp pico9918_config.cmake.template pico9918_config.cmake
# edit pico9918_config.cmake (uncomment/change the settings you need)
cmake ..
```

**The template [`pico9918_config.cmake.template`](pico9918_config.cmake.template)
is the source of truth for the full list of overridable settings** - board,
version/artifact naming, the build options above, clock/PLL/flash timing,
GPIO/VGA pin assignments, and custom-hardware behaviour flags. A few examples of
what a config file can contain:

```cmake
# a diagnostic, VGA-only build
set(PICO9918_DIAG ON)
set(PICO9918_ENABLE_SCART OFF)

# target the RP2040 board and a custom artifact suffix
set(PICO_BOARD pico9918)
set(PICO9918_VERSION_SUFFIX "myfork")

# custom hardware: no TMS clock outputs, active-high interrupt, moved pins
set(PICO9918_NO_CLOCKS ON)
set(PICO9918_INT_ACTIVE_HIGH ON)
set(PICO9918_GPIO_INT 20)
set(PICO9918_VGA_RGB_PINS_START 6)
```

Settings still work as normal `-D` flags too; explicit `-D` on the command line
wins over the file.

### Firmware Targets
- **`firmware`**: Build firmware and copy to `build/dist/` (unified CMake system only)
- **`pico9918[pro]-v<version>[-<branch>]`**: Direct firmware target name, which is also the artifact name (available in all builds)

### VSCode Firmware Build
Use the Raspberry Pi Pico VSCode extension:
- **Compile Project**: Builds firmware with current configuration
- **Run Project**: Flashes firmware to connected Pico
- **Flash**: Programs firmware via OpenOCD

#### VSCode Configuration
Set build options in `.vscode/settings.json`:

```json
{
  "cmake.configureArgs": [
    "-DPICO9918_ENABLE_SCART=ON",
    "-DPICO9918_NO_SPLASH=OFF",
    "-DPICO9918_DIAG=OFF"
  ]
}
```

### Firmware Architecture
- **Core 0**: bring-up, then the F18A GPU loop
- **Core 1**: host bus interface, VGA output and per-scanline TMS9918A rendering
- **PIO**: Hardware-timed signal generation
- **DMA**: Memory transfers and sprite processing
- **Flash**: Configuration storage in the top 4KB, with a pending block one sector below

### SDK Performance Patch

> **✅ Automatic Patch Application**
> 
> A performance patch is automatically applied for optimal boot times:
> - **Fast Boot**: Optimizes ROSC (Ring Oscillator) for faster startup  
> - **Automatic**: Applied by CMake when using `PICO_SDK_FETCH_FROM_GIT=ON`
> - **Manual Setup**: Still required when manually installing SDK (see platform instructions above)

#### How It Works
- **FetchContent builds**: CMake automatically applies `picosdk-2.0.0-visrealm-fastboot.patch` after downloading the SDK
- **Manual SDK installs**: You must run the `git apply` command shown in platform setup above
- **Safe Operation**: Patch command includes fallback - build continues even if patch fails

## Building Configurator

The configurator creates ROM files for retro computers that can upload firmware to PICO9918.

### Prerequisites - Configurator

> **✅ No Manual Tool Installation Required!** 
> 
> The build system **automatically downloads and builds** all required tools:
> - **CVBasic** (Retro BASIC compiler)
> - **GASM80** (Z80 assembler)  
> - **XDT99** (TI-99/4A development tools)
>
> Simply run the build commands below - all tools will be built from source automatically.

### Quick Start - Configurator

The recommended approach is the **combined build** above, which builds firmware and all configurator ROMs together and embeds the combined RP2040+RP2350 firmware in each ROM.

To build configurator ROMs against an existing firmware UF2 (standalone):
```bash
mkdir build && cd build
cmake .. -DCONFIGURATOR_ONLY=ON -DPICO9918_FIRMWARE_UF2_PATH=<path-to-firmware.uf2>
cmake --build . --target configurator_all
```

### Configurator Targets
- **`configurator_all`**: `ti99`, `coleco`, `msx_asc16`, `msx_konami`, each with its `_lite` variant, plus `nabu` and `sg1000`
- **`ti99`** / **`ti99_lite`**: TI-99/4A ROM (8KB banks)
- **`coleco`** / **`coleco_lite`**: ColecoVision ROM (16KB banks)
- **`msx_asc16`** / **`msx_asc16_lite`**: MSX ASCII16 mapper ROM
- **`msx_konami`** / **`msx_konami_lite`**: MSX Konami mapper ROM
- **`nabu`**: NABU computer ROM
- **`sg1000`**: SG-1000/SC-3000 ROM
- **`creativision`**: CreatiVision ROM. Currently does not assemble - the image overruns its 16KB ROM, so it is excluded from `configurator_all` rather than failing every build.

The `_lite` variants are built with `NO_UPGRADE`, which drops the firmware-update
path and the embedded firmware image with it.

### Individual Platform Builds
```bash
cmake --build . --target ti99              # TI-99/4A
cmake --build . --target coleco            # ColecoVision  
cmake --build . --target msx_asc16         # MSX
cmake --build . --target nabu              # NABU
cmake --build . --target creativision      # CreatiVision
```

### VSCode Configurator Tasks
- **Build All Configurator ROMs**: Build all configurator targets
- **Build TI-99 Configurator**: Build only TI-99/4A ROM  
- **Build ColecoVision Configurator**: Build only ColecoVision ROM
- **Build MSX Configurator**: Build only MSX ROM

All configurator tasks automatically depend on firmware build.

### Tool Auto-Building

> **🚀 Zero-Configuration Tool Management**
>
> **By default**, the build system automatically handles all configurator tools:

The system will automatically:
1. **Clone** tool repositories from GitHub
2. **Build** tools from source using CMake  
3. **Cache** built tools for subsequent builds
4. **Use** locally-built tools for ROM generation

**No manual tool installation needed!** Works on all platforms out-of-the-box.

#### Advanced: Use Pre-installed Tools (Optional)
If you have CVBasic, GASM80, and XDT99 already installed in PATH:
```bash
cmake .. -DBUILD_TOOLS_FROM_SOURCE=OFF
```

### Platform Support Matrix

| Platform | Banking | Output | Assembler | Notes |
|----------|---------|---------|-----------|-------|
| TI-99/4A | 8KB | `.bin` | XAS99 + linkticart | Cartridge format |
| ColecoVision | 16KB | `.rom` | GASM80 | Standard ROM |
| MSX ASCII16 | 16KB | `.rom` | GASM80 | ASCII16 mapper |
| MSX Konami | 16KB | `.rom` | GASM80 | Konami mapper |
| NABU | None | `.nabu` | GASM80 | NABU computer |
| SG-1000/SC-3000 | None | `.sg` | GASM80 | No banking, so no firmware upgrade |
| CreatiVision | None | `.bin` | GASM80 | Overruns its 16KB ROM; not built by `configurator_all` |

## Combined Build (Recommended)

The combined build produces a single UF2 file that contains firmware for **both** PICO9918 (RP2040) and PICO9918 PRO (RP2350). Configurator ROMs built against this combined UF2 can update either device using the same ROM file.

Enable it with `-DPICO9918_BUILD_COMBINED=ON`:

```bash
mkdir build && cd build
cmake .. -DPICO9918_BUILD_COMBINED=ON
cmake --build . --target combined
cmake --build . --target build_configurators
```

All outputs land in `build/dist/`:
- **Combined firmware**: `pico9918-v<version>.uf2` - works on both RP2040 and RP2350
- **Configurator ROMs**: All platform ROMs, each embedding the combined firmware

### Ninja Generator (Faster)
```bash
cmake .. -DPICO9918_BUILD_COMBINED=ON -G Ninja
ninja combined
ninja build_configurators
```

### Parallel Builds
```bash
cmake --build . --target combined --parallel 8
```

## Firmware-Only Build

To build individual firmware images without the combined file:

```bash
mkdir build && cd build
cmake ..
cmake --build . --target firmware
```

Output: `build/dist/pico9918pro-v<version>.uf2`, the RP2350 firmware, since
`PICO_BOARD` defaults to `pico9918pro`.

For the RP2040 board, configure with `-DPICO_BOARD=pico9918`, or use the combined
build above to produce both.

## Cross-Platform Support

All major platforms are supported with consistent toolchains and build processes. See the **Platform-Specific Setup** section above for detailed installation instructions.

### Platform Summary
- **Windows**: Native build with ARM GNU Toolchain 15.2.Rel1
- **Linux**: Native build with ARM GNU Toolchain 15.2.Rel1
- **macOS**: Native build with ARM GNU Toolchain 15.2.Rel1 (for consistency)
- **WSL**: Use Linux instructions within Windows Subsystem for Linux

### Important Notes
- **Toolchain Consistency**: All platforms use ARM GNU Toolchain 15.2.Rel1 to ensure identical builds
- **macOS Python**: May require `--break-system-packages` flag for pip on newer macOS versions
- **SDK Version**: Use Pico SDK 2.1.1 specifically - newer versions may cause linker issues

### Continuous Integration
The project includes GitHub Actions workflows that automatically build on every push:

#### Individual Platform Workflows
- **Firmware Windows**: `firmware-windows.yml` 
- **Firmware Linux**: `firmware-linux.yml`
- **Firmware macOS**: `firmware-macos.yml`
- **Configurator Windows**: `configurator-windows.yml`
- **Configurator Linux**: `configurator-linux.yml`
- **Configurator macOS**: `configurator-macos.yml`

#### Build Outputs
- **Firmware**: `.uf2` files for Raspberry Pi Pico
- **Configurator ROMs**: All retro platform ROM files
- **Artifacts**: Build outputs available for download from successful runs
- **Badges**: Individual build status badges for each OS and build type

## Output Structure

### Combined Build (`-DPICO9918_BUILD_COMBINED=ON`)
```
build/
├── dist/                                      # Final artifacts
│   ├── pico9918-v<version>.uf2                # Combined RP2040+RP2350 firmware
│   ├── pico9918conf-v<version>_ti99_8.bin     # TI-99/4A ROM (with combined firmware)
│   ├── pico9918conf-v<version>_cv.rom         # ColecoVision ROM
│   ├── pico9918conf-v<version>_msx_asc16.rom  # MSX ROM
│   └── ...                                    # Other platform ROMs
├── pico9918/dist/                             # RP2040-only firmware
├── pico9918pro/dist/                          # RP2350-only firmware
└── configurators/                             # Configurator build tree
```

### Firmware-Only Build (default)
```
build/
├── dist/                                      # Final artifacts
│   └── pico9918pro-v<version>.uf2             # RP2350 firmware (the default board)
└── src/                                       # Build intermediates
```

## Troubleshooting

### Firmware Issues
**Build fails with missing SDK**
```bash
# Install Raspberry Pi Pico SDK
# Set PICO_SDK_PATH environment variable
```

**SDK version compatibility issues**
```bash
# PICO9918 developed with Pico SDK 2.1.1
# SDK 2.2.0 may cause linker errors and memory overflow
# 
# Recommended: manually install Pico SDK 2.1.1 and set PICO_SDK_PATH
git clone -b 2.1.1 https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk && git submodule update --init
export PICO_SDK_PATH=$PWD
cd ../your-build-directory
cmake ..
```

The automatic fetch (`-DPICO_SDK_FETCH_FROM_GIT=ON -DPICO_SDK_FETCH_FROM_GIT_TAG=2.1.1`)
is what CI uses and what the quick start recommends; install the SDK by hand only
if you want one checkout shared across build directories.

**SDK patch issues**
```bash
# If patch fails to apply:
git apply --ignore-whitespace --ignore-space-change --3way ../picosdk-2.0.0-visrealm-fastboot.patch

# If patch was already applied or conflicts:
echo "Patch failed or already applied" # This is normal, firmware will still build

# Patch is optional but improves boot performance
# Firmware works without it, just boots slower
```

**Missing splash/font assets**
```bash
# Install pillow for image conversion
pip install pillow
```

### Configurator Issues  
**Missing tools error**
```bash
# Auto-build is enabled by default, but if you disabled it:
cmake .. -DBUILD_TOOLS_FROM_SOURCE=ON
```

**Firmware dependency error**

The configurator ROMs embed a firmware UF2 from `build/dist/`, which only the
`firmware` target populates:
```bash
cmake --build . --target firmware
```

### General Issues
**Clean build**
```bash
rm -rf build/
mkdir build && cd build
cmake ..
```

**Verbose output**
```bash
cmake --build . --verbose
```
