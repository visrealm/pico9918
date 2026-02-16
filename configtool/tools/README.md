# PICO9918 Configurator Tools

This directory contains Python tools for working with PICO9918 firmware and configuration.

## Tools

### config_uf2.py
Generates small UF2 files to reset or update just the configuration data of a PICO9918 device. Useful for fixing corrupted configuration or resetting to factory defaults without reflashing the entire firmware.

**Usage:**
```bash
python config_uf2.py -o reset.uf2                  # Default config for RP2040
python config_uf2.py -o reset.uf2 --rp2350         # Default config for RP2350
python config_uf2.py -o config.uf2 --scanlines 1   # Enable scanlines
```

### uf2cvb.py
Converts PICO9918 firmware .UF2 files to banked CVBasic source files. Used by the configurator tool to include firmware data in the CVBasic configurator application.

**Usage:**
```bash
python uf2cvb.py -b 8 -o firmware firmware.uf2
```

### bin2cvb.py
Converts binary files to CVBasic source files of binary data. General-purpose tool for including binary resources in CVBasic programs.

**Usage:**
```bash
python bin2cvb.py -o output file.bin
```

### cvpletter.py
Converts raw CVBasic DATA sections into Pletter-compressed chunks. Used for compressing data in CVBasic programs to save space.

**Usage:**
```bash
python cvpletter.py input.bas output.bas
```

## Notes

- All tools support `--help` for detailed usage information (except cvpletter.py which uses positional arguments)
- These tools are part of the PICO9918 firmware update and configuration workflow
- The configurator tool uses these scripts to build the CVBasic-based configuration utility
