# Building PICO9918

This document describes how to build the PICO9918 firmware and configurator ROMs.

## Prerequisites

### Required Tools
- **CMake 3.13+**: Build system generator
- **ARM GNU Toolchain**: Cross-compiler for ARM Cortex-M0+ (RP2040)
- **Raspberry Pi Pico SDK**: Firmware compilation (v2.1.1 recommended for compatibility)
- **Python 3**: Build scripts and asset conversion
- **Git**: For submodules and dependencies

### Platform-Specific Setup

#### Windows
```bash
# Install dependencies
# Download and install ARM GNU Toolchain 13.2.1-1.1 from:
# https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v13.2.1-1.1/xpack-arm-none-eabi-gcc-13.2.1-1.1-win32-x64.zip
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
curl -L "https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v13.2.1-1.1/xpack-arm-none-eabi-gcc-13.2.1-1.1-darwin-arm64.tar.gz" -o arm-toolchain.tar.gz
sudo tar -xzf arm-toolchain.tar.gz -C /opt
echo 'export PATH="/opt/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin:$PATH"' >> ~/.zshrc
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

**Combined build — firmware for PICO9918 and PICO9918 PRO, plus all configurator ROMs (Recommended)**
```bash
mkdir build && cd build
cmake .. -DPICO9918_BUILD_COMBINED=ON
cmake --build . --target combined
cmake --build . --target build_configurators
```

Outputs in `build/dist/`: combined `.uf2` firmware and all configurator ROMs.

**Firmware only (RP2040)**
```bash
mkdir build && cd build
cmake ..
cmake --build . --target firmware
```

Output: `build/dist/pico9918-vga-build-<version>.uf2`

### Firmware Configuration Options

Configure output mode and features with `-D` flags:

```bash
cmake .. -DPICO9918_SCART_RGBS=ON -DPICO9918_DIAG=ON
```

#### Available Options
- **`PICO9918_SCART_RGBS`** (OFF/ON): Enable SCART RGBs output instead of VGA
- **`PICO9918_SCART_PAL`** (OFF/ON): Use PAL 576i timing instead of NTSC 480i  
- **`PICO9918_NO_SPLASH`** (OFF/ON): Disable splash screen on startup
- **`PICO9918_DIAG`** (OFF/ON): Enable diagnostic mode by default

#### Configuration Examples
```bash
# VGA output (default)
cmake ..

# SCART RGBs NTSC output
cmake .. -DPICO9918_SCART_RGBS=ON

# SCART RGBs PAL output  
cmake .. -DPICO9918_SCART_RGBS=ON -DPICO9918_SCART_PAL=ON

# Diagnostic build with no splash
cmake .. -DPICO9918_DIAG=ON -DPICO9918_NO_SPLASH=ON
```

### Firmware Targets
- **`firmware`**: Build firmware and copy to `build/dist/` (unified CMake system only)
- **`pico9918-vga-build-<version>`**: Direct firmware target name (available in all builds)

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
    "-DPICO9918_SCART_RGBS=OFF",
    "-DPICO9918_SCART_PAL=OFF",
    "-DPICO9918_NO_SPLASH=OFF", 
    "-DPICO9918_DIAG=OFF"
  ]
}
```

### Firmware Architecture
- **Core 0**: TMS9918A emulation, host interface  
- **Core 1**: VGA output generation
- **PIO**: Hardware-timed signal generation
- **DMA**: Memory transfers and sprite processing
- **Flash**: Configuration storage in upper 1MB

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

To build configurator ROMs against an existing firmware (standalone):
```bash
mkdir build && cd build
cmake .. -DPICO9918_BUILD_COMBINED=ON
cmake --build . --target combined
cmake --build . --target build_configurators
```

### Configurator Targets
- **`configurator_all`**: Build all configurator ROMs
- **`ti99`**: TI-99/4A ROM (8KB banks)
- **`coleco`**: ColecoVision ROM (16KB banks)
- **`msx_asc16`**: MSX ASCII16 mapper ROM
- **`msx_konami`**: MSX Konami mapper ROM
- **`nabu`**: NABU computer ROM
- **`creativision`**: CreatiVision ROM
- **`nabu_mame`**: NABU MAME ROM
- **`nabu_mame_package`**: NABU MAME NPZ package

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
| NABU MAME | None | `.npz` | GASM80 | MAME emulator |
| CreatiVision | None | `.bin` | GASM80 | CreatiVision console |

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
- **Combined firmware**: `pico9918-vga-<version>.uf2` — works on both RP2040 and RP2350
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

Output: `build/dist/pico9918-vga-build-<version>.uf2` (RP2040 only)

For RP2350 (PICO9918 PRO) firmware only, set the board in CMake or use the combined build above.

## Cross-Platform Support

All major platforms are supported with consistent toolchains and build processes. See the **Platform-Specific Setup** section above for detailed installation instructions.

### Platform Summary
- **Windows**: Native build with ARM GNU Toolchain 13.2.1-1.1
- **Linux**: Native build with `gcc-arm-none-eabi` package  
- **macOS**: Native build with ARM GNU Toolchain 13.2.1-1.1 (for consistency)
- **WSL**: Use Linux instructions within Windows Subsystem for Linux

### Important Notes
- **Toolchain Consistency**: All platforms use ARM GNU Toolchain 13.2.1-1.1 to ensure identical builds
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
│   ├── pico9918-vga-<version>.uf2             # Combined RP2040+RP2350 firmware
│   ├── pico9918_<version>_ti99_8.bin          # TI-99/4A ROM (with combined firmware)
│   ├── pico9918_<version>_cv.rom              # ColecoVision ROM
│   ├── pico9918_<version>_msx_asc16.rom       # MSX ROM
│   └── ...                                    # Other platform ROMs
├── pico9918/dist/                             # RP2040-only firmware
├── pico9918pro/dist/                          # RP2350-only firmware
└── configurators/                             # Configurator build tree
```

### Firmware-Only Build (default)
```
build/
├── dist/                                      # Final artifacts
│   └── pico9918-vga-build-<version>.uf2       # RP2040 firmware
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

# Note: PICO_SDK_FETCH_FROM_GIT_TAG has known issues with tag resolution
```

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
```bash
# Build firmware first
cmake --build .
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
