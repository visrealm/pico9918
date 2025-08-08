# CMake Build System for PICO9918 Configurator

This directory contains a CMake-based build system that replaces the Windows batch file (`build.bat`) with a cross-platform solution.

## Features

- **Cross-platform**: Works on Windows, Linux, and macOS
- **Parallel builds**: Multiple platform targets can build simultaneously  
- **Better dependency tracking**: Only rebuilds what's changed
- **IDE integration**: Works with VSCode CMake extensions
- **Tool detection**: Automatically finds required compilers and tools

## Prerequisites

- CMake 3.13 or later
- Python 3 (accessible as `python3`)
- CVBasic compiler (`cvbasic.exe`)
- GASM80 assembler (`gasm80.exe`) 
- XDT99 XAS99 assembler (for TI-99 builds, Windows only)

## Usage

### Build All Platforms
```bash
cd configtool
mkdir build && cd build
cmake ..
make configurator_all
```

### Build Individual Platforms
```bash
make ti99              # TI-99/4A
make ti99_f18a         # TI-99/4A F18A Testing  
make coleco            # ColecoVision
make msx_asc16         # MSX ASCII16 mapper
make msx_konami        # MSX Konami mapper
make nabu              # NABU
make creativision      # CreatiVision
make nabu_mame_package # NABU MAME (.npz)
```

### Build with Ninja (faster)
```bash
cmake .. -G Ninja
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

The build system automatically searches for tools in:
1. `configtool/tools/cvbasic/` (bundled tools)
2. `../CVBasic/build/Release/` (local CVBasic build)  
3. System PATH
4. `c:/tools/xdt99/` (Windows XAS99 location)

## Comparison with Batch Build

| Aspect | Batch File | CMake |
|--------|------------|-------|
| Platform Support | Windows only | Cross-platform |
| Parallel Builds | Sequential | Parallel |
| Dependency Tracking | Manual | Automatic |
| IDE Integration | None | Full |
| Error Handling | Basic | Comprehensive |
| Maintenance | Complex batch logic | Declarative CMake |

## Backwards Compatibility

The original `build.bat` remains unchanged and functional. This CMake system is an alternative build method, not a replacement.

## Future Enhancements

- [ ] Automatic firmware building if not found
- [ ] CTest integration for ROM validation
- [ ] CPack for distribution packaging
- [ ] Cross-compilation support for ARM/embedded hosts