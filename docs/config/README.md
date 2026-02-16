# PICO9918 Configuration Generator

This is a web-based tool for generating custom configuration UF2 files for the PICO9918.

## Live Tool

🌐 **[Open Configuration Tool](index.html)**

Or access it online at: `https://visrealm.github.io/pico9918/config/`

## What This Tool Does

The PICO9918 stores its configuration in the top 4KB of flash memory. When you update the firmware, this configuration area is preserved. However, if the configuration becomes corrupted or incompatible, this tool allows you to generate a small UF2 file (512 bytes) to reset or update just the configuration without reflashing the entire firmware.

## Features

- **Display Settings**: Configure scanlines, sprite limits, and clock presets
- **Palette Editor**: Customize all 16 TMS9918A colors with preset palettes (TMS9918A, V9938, Greyscale, Sepia)
- **Diagnostic Modes**: Enable debugging overlays for development
- **Instant Download**: Generate and download UF2 files directly in your browser
- **Offline Support**: Works without an internet connection once loaded
- **Auto Hardware Detection**: Firmware automatically detects and validates hardware configuration

## How to Use

1. Open `index.html` in your web browser
2. Configure display settings as desired
3. (Optional) Customize the palette or enable diagnostic modes
4. Click "Generate & Download UF2"
5. Save the generated `.uf2` file

## Installing the Configuration

1. Hold the BOOTSEL button on your PICO9918 while powering it on
2. The device will appear as a USB drive named "RPI-RP2"
3. Drag and drop the generated `.uf2` file onto the drive
4. The device will automatically reboot with the new configuration

## Configuration Options Explained

### Display Settings

- **CRT Scanlines**: Adds horizontal scanlines for a CRT monitor effect
- **Scanline Sprite Limit**: Limits sprites per scanline (0 = no limit, mimics original TMS9918A behavior)
- **Clock Preset**: Selects different timing presets (0 = default)

### Palette Editor

Customize the 16 TMS9918A colors with preset palettes or create custom colors:
- **TMS9918A (Default)**: Original colorful palette
- **V9938**: MSX2 palette
- **Greyscale**: Monochrome grey tones
- **Sepia**: Vintage warm brown tones

Note that:
- Color 0 (transparent) is always black
- Colors use 12-bit RGB (4 bits per channel)
- Alpha channel is always 0xF (opaque) for colors 1-15

### Diagnostic Modes

Enable on-screen debugging information:
- **Registers**: Shows VDP register values
- **Performance**: Shows frame timing and performance metrics
- **Palette**: Shows current palette information
- **Address**: Shows memory address information

⚠️ **Warning**: Diagnostic modes are for debugging and may affect performance.

## Deploying to GitHub Pages

To make this tool available online:

1. Ensure the `docs/` folder is in your repository
2. Go to Settings → Pages in your GitHub repository
3. Set Source to "Deploy from a branch"
4. Select branch `main` (or `master`) and folder `/docs`
5. Click Save
6. The tool will be available at `https://<username>.github.io/<repository>/`

For the pico9918 repository: `https://visrealm.github.io/pico9918/`

## Technical Details

- **File Size**: Generated UF2 files are exactly 512 bytes (one UF2 block)
- **Target Address**: `0x101FF000` (top 4KB of 2MB flash)
- **Format**: Standard UF2 format with RP2040/RP2350 family ID
- **Compatibility**: Works in all modern browsers (Chrome, Firefox, Safari, Edge)
- **Dependencies**: None - fully self-contained single HTML file

## Comparison with Python Tool

This web tool generates identical UF2 files to the Python command-line tool (`configtool/tools/config_uf2.py`). Both tools:
- Use the same configuration structure (256 bytes)
- Target the same flash address
- Apply the same validation requirements
- Generate byte-identical output for the same settings

The web tool offers a more user-friendly interface, while the Python tool is better for automation and scripting.

## Troubleshooting

### Configuration is rejected by firmware

The firmware will automatically detect your hardware (Pico model and display driver). If configuration is still rejected, make sure:
- All values are within valid ranges
- Your firmware version supports the configuration format

### Download doesn't work

- Try a different browser
- Check browser security settings
- Ensure JavaScript is enabled
- Try opening the file directly instead of via a web server

### Configuration doesn't apply

- Verify the UF2 file was successfully loaded (device should reboot)
- Check that your firmware version supports configuration updates
- Try resetting to default configuration first

## Source Code

This is a self-contained HTML file with embedded CSS and JavaScript. To view or modify the source, simply open `index.html` in a text editor.

The implementation follows the same logic as the Python script but uses JavaScript for browser compatibility.

## License

This tool is part of the PICO9918 project and is licensed under the MIT License.

Copyright (c) 2024 Troy Schrapel

## Links

- [PICO9918 GitHub Repository](https://github.com/visrealm/pico9918)
- [Firmware Releases](https://github.com/visrealm/pico9918/releases)
- [AtariAge Forum Discussion](https://atariage.com/forums/topic/323020-pico9918-drop-in-replacement-for-tms9918a)
- [Python Configuration Tool](../../configtool/tools/config_uf2.py)
