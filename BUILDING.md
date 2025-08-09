# Building PICO9918

This document describes how to build the PICO9918 firmware and configurator ROMs.

## Prerequisites

### Required Tools
- **CMake 3.13+**: Build system generator
- **Raspberry Pi Pico SDK**: Firmware compilation (v2.1.1 recommended for compatibility)
- **Python 3**: Build scripts and asset conversion
- **Git**: For submodules and dependencies

### Development Environment Setup

To set up your development environment for the Raspberry Pi Pico, follow the [Raspberry Pi C/C++ SDK Setup](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html) instructions.

The latest PICO9918 source can be configured and built using the official [Raspberry Pi Pico VSCode plugin](https://github.com/raspberrypi/pico-vscode).

### Python Dependencies
```bash
pip install pillow  # Image processing for splash screen and fonts
```

## Building Firmware

The PICO9918 firmware is the primary component - a TMS9918A VDP emulator for Raspberry Pi Pico.

### Quick Start - Firmware Only
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

### Automatic SDK Patches
The build system automatically applies performance patches to the Pico SDK:
- **Fast Boot**: Optimizes ROSC (Ring Oscillator) for faster boot times
- Patches are applied automatically when using `PICO_SDK_FETCH_FROM_GIT=ON`

## Building Configurator

The configurator creates ROM files for retro computers that can upload firmware to PICO9918.

### Prerequisites - Configurator
In addition to firmware requirements:
- **CVBasic**: Retro BASIC compiler (auto-built if missing)
- **GASM80**: Z80 assembler (auto-built if missing)
- **XDT99**: TI-99/4A tools (auto-built if missing)

### Quick Start - Configurator
```bash
mkdir build && cd build
cmake ..
# Build firmware first (required dependency)
cmake --build . --target firmware
# Build all configurator ROMs
cmake --build . --target configurator_all
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
By default, the build system automatically builds CVBasic, GASM80, and XDT99 from source. This ensures builds work on any platform without pre-installed tools.

The system will:
1. Clone tool repositories from GitHub
2. Build tools from source using CMake
3. Cache built tools for subsequent builds
4. Use locally-built tools for ROM generation

To use existing tools instead (if available):
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

## Unified Build (Complete System)

Build both firmware and all configurator ROMs together:

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

All outputs in `build/dist/`:
- **Firmware**: `pico9918-vga-build-<version>.uf2`
- **Configurator ROMs**: Platform-specific ROM files

### Parallel Builds
```bash
cmake --build . --parallel 8               # Use 8 CPU cores
```

### Ninja Generator (Faster)
```bash
cmake .. -G Ninja
ninja
```

## Cross-Platform Support

### Windows
- **Native**: MSYS2, Visual Studio, or Clang
- **WSL**: Windows Subsystem for Linux

### Linux  
- **Native**: GCC or Clang toolchain
- **Dependencies**: `build-essential cmake python3 python3-pip git`

### macOS
- **Native**: Xcode command line tools  
- **Dependencies**: `brew install cmake python3 git`

### Continuous Integration
The project includes GitHub Actions workflows that automatically build:
- **Firmware**: `.uf2` files for Raspberry Pi Pico
- **Configurator ROMs**: All retro platform ROM files
- **Cross-platform**: Both Windows and Linux builds
- **Artifacts**: Build outputs available for download from successful runs

## Output Structure

```
build/
├── dist/                              # Final artifacts
│   ├── pico9918-vga-build-<version>.uf2  # Pico firmware
│   ├── pico9918_<version>_ti99_8.bin      # TI-99/4A ROM
│   ├── pico9918_<version>_cv.rom          # ColecoVision ROM
│   ├── pico9918_<version>_msx_asc16.rom   # MSX ROM
│   └── ...                               # Other platform ROMs
├── src/                               # Firmware build
│   └── pico9918-vga-build-<version>.uf2  # Original firmware output
├── intermediate/                      # Configurator intermediates
└── external/                          # Auto-built tools
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
