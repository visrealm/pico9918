# Changelog

Every released version of the PICO9918 firmware and configurator. Dates are the release dates on
[Releases](https://github.com/visrealm/pico9918/releases), where the full notes and installation
instructions for each version live. For per-version detail, including which configuration setting
arrived in which version, see
[Firmware Version History](https://github.com/visrealm/pico9918/wiki/Firmware-Version-History).

Current firmware works on every official board: v0.3, v0.4 through v1.3, and PRO v2.0. A single
`.uf2` has carried both the RP2040 and RP2350 images since v1.1.0.

## Unreleased

Work past v1.2.0 lives on branches and is not downloadable. A lite (settings-only) configurator ROM
for the CreatiVision is planned for v1.2.1.

## v1.2.0 - 2026-07-12

First release with SCART RGB support out of the box.

### Added

* SCART RGB output, auto-detected at boot, giving PAL RGBs 576i @ 50 Hz through a 10-pin Sega Saturn
  A/V connector. Correct interlaced timing.
* Configurator output modes: Auto (default), VGA/HDMI and SCART, with the SCART display mode
  switchable between PAL 576i @ 50 Hz and NTSC 480i @ 60 Hz.
* Local build configuration file, so build settings no longer need `-D` flags on every invocation.
  See [Local Build Config File](BUILDING.md#local-build-config-file).

### Changed

* Clock and output changes are now confirmed on the next boot and rolled back automatically if the
  configurator is not reopened, so a setting that kills the picture undoes itself.
* Minor GPU performance improvements.
* The configurator forces a firmware upgrade when it does not detect a supported version.

## v1.1.1 - 2026-04-18

### Added

* Device mode configuration, which matches the GROMCLK and CPUCLK outputs to a specific TMS9918
  family member: TMS9918A (default), TMS992xA, TMS9118 and TMS912x. Web configurator only.
* EGA palette preset.
* NABU and CreatiVision no-cut mod documentation and STLs.

### Fixed

* Visual glitches on some devices in 48- and 60-row modes.
* UF2 block count in the web configurator.
* TI-99 bank count rounding, which affected custom SCART firmware in earlier versions.

## v1.1.0 - 2026-04-03

First release supporting the RP2350-based PICO9918 PRO.

### Added

* PICO9918 PRO (RP2350) hardware support, with detection and device information in the configurator.
* Combined `.uf2` carrying firmware for both the PICO9918 and the PRO, so one ROM updates either.
* Lite configurator builds for the TI-99, MSX and ColecoVision, without firmware update support.
* Schematics and gerbers for the v1.3 and v2.0 boards, and for the VGA v1.2 and HDMI v1.1 dongles.
* Digital A/V dongle wiring guide, FFC cable notes, and MDE1 and solder jumper documentation.

### Changed

* Interrupt timing, improving compatibility with some systems.
* The diagnostics screen timeout is now 15 seconds.

### Fixed

* Sprite collision triggering incorrectly when sprites moved off the right edge of the screen.
* Graphical glitch in multi-page mode with the colour table at address `0xX800`.

## v1.0.3 - 2026-02-15

### Added

* 48- and 60-row modes, enabled by R0 bit 3.
* Opaque sprites, via SAT entry bit 4 when global 16px sprites are enabled.
* Optional palette reset with the register reset command: write `0xC0` to VR `0x32`.
* Firmware and hardware versions on the performance diagnostic overlay.
* Predefined TMS9918A, V9938 and GREY palettes in the configurator.
* PICO9918 support in [JS99'er](https://js99er.visrealm.au).

### Changed

* Automatic diagnostic overlays now disappear once the host enables the display.
* TEXT80 rendering performance improvements.
* CPU to VDP interface reworked with software debounce and corrected delays.

### Fixed

* Position-based attributes on the T1 layer with multiple pages.
* Sprite Y-flip off by one row.
* ECM sprites using base palette 0.
* v0.3 hardware detection.

## v1.0.2 - 2025-10-06

### Added

* SG-1000 and SC-3000 configurator builds.
* macOS build support, and GitHub Actions building every firmware and configurator target.

### Changed

* The Pi Pico SDK boot performance patch is applied automatically.
* Configurator builds moved to CMake, with automatic build tool configuration.

### Fixed

* VRAM read and write 16 KB rollover, which fixes ColecoVision Konami Ping Pong.
* CPU interface read performance, which fixes ColecoVision Buck Rogers Super Game screen corruption.

## v1.0.1 - 2025-05-04

First beta of the unified firmware and software tools, and the release that made the PICO9918
configurable from the machine it is fitted to.

### Added

* Firmware updates over software, from a configurator ROM running on the host.
* Configuration changes over software: scanlines, maximum sprites, core clock speed, diagnostic
  overlays and default palette.
* F18A compatibility mode, included by default.
* Configurator releases for the TI-99/4A, ColecoVision and MSX.
* Hardware autodetection, adapting to v0.3, v0.4 and v1.0+ boards automatically.

## v0.4.4 - 2025-03-30

### Fixed

* Write PIO timing on the CreatiVision, and potentially other machines.

## v0.4.3 - 2024-10-19

### Added

* SCART RGBs builds: NTSC 480i @ 60 Hz and PAL 576i @ 50 Hz.

### Fixed

* v0.3 board configuration not carried through to the build.

### Reverted

* The faster TMS read PIO, after problems on MSX machines.

## v0.4.2 - 2024-10-09

### Added

* More than four sprites per scanline, enabled by default.
* Binary info.

### Changed

* Updated to Pico SDK 2.0.0.
* Removed the explicit TMS to CPU interface PIO delays.

### Fixed

* 80-column mode fixes.

## v0.4.1-f18a-preview1 - 2024-10-09

Pre-release. First public F18A support. Not every combination of display mode and feature was
implemented at this point.

### Added

* Enhanced colour mode (ECM) tiles in Graphics I mode, and ECM sprites in all display modes.
* Second tile layer in 80-column text and Graphics I modes.
* Bitmap layer.
* Position-based attributes for 80-column text and Graphics I modes.
* TMS9900 GPU core.

## v0.4.1 - 2024-08-21

### Fixed

* Status register responsiveness.
* VGA scale multiplier.

## v0.4.0 - 2024-08-04

First release for the integrated v0.4 board, with binaries for both the v0.3 and v0.4 boards.

## v0.3.1 - 2024-07-12

### Changed

* Moved the VDP to CPU interface from GPIO interrupts to PIO. The whole design still rests on this.
* Reduced the binary to about 26 KB, with many performance improvements.

## v0.3 - 2024-06-25

First release. Compatible with the [v0.3 board](pcb/v0.3), which piggy-backs an external Pi Pico.
