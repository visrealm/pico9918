# PICO9918 internals

A TMS9918A / F18A video display processor on an RP2040 or RP2350, built to drop
into the VDP socket of a classic machine and drive VGA, HDMI or SCART RGB.

This is the source reference. For what the product is and which machines it
fits, see the [project README](https://github.com/visrealm/pico9918); for how to
get a build out of the tree, see \ref building, and for the test and measurement
tools, \ref debugging.

## How the two cores split the work

Core 1 owns everything the host and the monitor can see. `tmsBusInit()` installs
the PIO interrupt handlers for the host's read and write cycles, so a VDP access
from the retrocomputer is serviced there, and `vgaLoop()` then runs the video
loop on the same core, calling `tmsScanline()` once per line at video rate.

Core 0 brings the hardware up - GPIO, hardware revision, clocks, stored config,
renderer, palette and the VGA mode - launches core 1, and then spends the rest
of its life in `gpuLoop()` running the F18A GPU.

The scanline budget is what shapes the code. At 252 MHz a line is 63.6 us, and
everything on the per-line path is written to fit inside it, which is why the
renderer's emitters are cloned per mode rather than branching per pixel.

## Where things live

| Area | Files |
|---|---|
| Entry point and core startup order | `main.c` |
| Host bus, PIO interrupt handlers | `tms_bus.c`, `tms9918.pio`, `gpio.h` |
| VDP emulation, scanline rendering | `core/` |
| Palette indices to VGA pixels | `renderer.c`, `palette.c` |
| VGA and SCART signal generation | `vga/` |
| F18A GPU, a TMS9900 in ARM assembly | `gpu/` |
| System clocks, GROMCLK and CPUCLK | `clocks.c` |
| Settings in flash, firmware update | `config.c`, `flash.c` |
| On-screen diagnostics, splash | `diag.c`, `splash.c` |
| Scanline capture over SWD, test builds only | `livetest.c` |

## Reading order

Start at `main.c` for the startup order, then `pico9918_scan_line()` for how a
line is produced and `tmsScanline()` for how it reaches the wire. `renderTileRow()`
and `renderTextRow()` are where the mode-specific work actually happens; both are
inlined into per-mode clones so the flags fold away at compile time.

Two ground-truth documents outrank anything inferred from the source: `HARDWARE.md`
for the host bus and electrical, and `F18A.md` for F18A behaviour. Both live in the
build workspace above this repository rather than in it.
