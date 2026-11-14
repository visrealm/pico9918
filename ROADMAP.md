# Roadmap

What is planned for the PICO9918 firmware, and roughly in what order. Shipped versions are in
[CHANGELOG.md](CHANGELOG.md); the full notes for each live on
[Releases](https://github.com/visrealm/pico9918/releases).

This is a direction, not a commitment. Dates are deliberately absent: items move between versions
as work lands. Anything not listed here is unscheduled, which is not the same as rejected - open an
issue and it can be considered.

**Versioning.** Patch releases carry bug fixes only, minor releases carry backwards-compatible
features, major releases carry breaking changes. For firmware, "breaking" means a config format
change or a hardware behaviour change that would surprise a user. **The config layout cannot change
on a patch release**: a release that claims a new config byte is a minor at minimum.

## v1.2.1 - host bus timing and compatibility

A maintenance release, focused on the host interface. Several reports of intermittent failures on
specific machines trace back to how the write cycle is entered and when `MODE` is sampled.

* Reject glitches on the `/CSW` falling edge before accepting a write cycle.
* Sample `MODE` at the `/CSW` falling edge rather than after it, and act on the sampled value
  rather than re-reading the pin.
* Drive the read bus before the settling check, and shorten the write entry window.
* Pin the host interface to a fixed PIO rate, independent of the system clock, and move the clock
  state machines to `pio0`.
* Reset palette mode on a status read.
* Correct the position-based tile attribute address.
* Build a configurator for a single target rather than always both, halving ROM size where only one
  board is in use.
* Lite (settings-only) configurator ROM for the CreatiVision.

## v1.3.0 - the tile pipeline rewrite, and pico9918-core

The largest F18A release since F18A support landed. Every display mode now runs through one
shared tile pipeline instead of carrying its own special case, so features that previously
existed in only some modes exist in all of them. The shared path is also *faster* than the
per-mode code it replaced, which is the unusual part: the features came for free.

* Tile layer 2 in every mode, the text modes included.
* Horizontal and vertical scrolling in every mode.
* Enhanced Color Mode in every mode.
* An 8bpp 80-column tier, and unlocked 80-column text rendered a byte a pixel.
* Holes closed along the way: the backdrop and 40-column text take the tile palette select, a
  text cell above an ECM0 layer draws as the ECM tile it is, and text no longer takes a
  vertical page swap the hardware never applies.

Performance, despite all of the above:

* Graphics II, Multicolor and both text modes fold into the shared pipeline behind one address
  generator, specialised at compile time rather than branched at run time.
* The composite step is skipped where there is nothing to arbitrate.
* Cold code and data stay resident in flash rather than RAM, via linker sections and an
  explicit reachability rule, freeing RAM for the renderer.
* Driven by measurement throughout: several changes were reverted for costing more than they
  returned.

The instruments that made it measurable, and that the core extraction then depends on:

* An on-hardware scene runner that replays VDP RAM dumps captured from real F18A software, a
  recorded performance timeline so frame time is a series across releases rather than a
  judgement made once, and a benchmark ROM.

Then the core extraction, gated on the measurement above. Its acceptance criterion is that the
picture and the frame time are unchanged.

* Rendering, GPU, configuration semantics and the splash and diagnostics overlays move out of
  the firmware. The firmware keeps only what is genuinely hardware: the PIO host interface,
  VGA, flash, GPIO, temperature and clocks.
* The library moves in-tree and is published as `pico9918-core`, a generated repository, for
  use by emulators. It is no longer a submodule.
* The configurator's config-byte table is generated from the firmware's, ending a layout that
  was independently declared in four places and had already drifted.
* Fix the config byte that had two owners: CRT scanlines and the render-base selector were the
  same byte, so enabling scanlines also switched the render base.
* Every DMA channel is claimed, so a collision panics at boot instead of corrupting the
  display.
* `clang-format` and Doxygen across the firmware and the library, dead code removed, and the
  stale build and debugging documentation corrected.
* Continuous integration building both targets on Linux, macOS and Windows, with the library's
  standalone build as the boundary gate.

## v1.4.0 - F18A compliance

Close the remaining gaps where PICO9918 does not match real F18A behaviour, so that F18A
software runs unmodified. The display-mode gaps were largely closed by the pipeline work in
v1.3.0; what is left is register semantics, status and the GPU.

* S15 returns the value of a register addressed by a read setup, reconstructed rather than
  returned raw. That is the only register read-back path the hardware offers.
* Locked writes above R7 are masked to three bits rather than ignored, and a non-unlock write
  to R57 re-locks.
* R30 is stored as the five bits the hardware has, and R30 = 0 restores the configured default
  rather than a fixed 31.
* R50 bit 2 (simulated scanlines) is acted on, and R7's power-on default matches.
* The timing counters sample correctly and no longer corrupt the start time.
* Tile layer 2 and tile-layer-1 disable stop being gated away on the 4bpp tier.
* Drop the resident GPU program image, which cannot function without the SPI flash the F18A
  had, and whose reachable handlers write into the status register the host polls.
* Correct the GPU's DMA trigger clear, which zeroes a byte it should leave alone.

Not in scope: the GPU's 7-bit user status at `0xB000` is on hold, and several F18A GPU
facilities (VRAM mirroring, the MPU register-page guard, the nanosecond counter pair) are
recorded as unsupported by choice.

## v2.0.0 - the V9938

The major version is the new VDP. With the core library already in place, this release adds a base
underneath the F18A the same way the TMS9918A sits there today.

* V9938 as a selectable base: 128 KB VRAM, the `GRAPHIC3` through `GRAPHIC7` renderers, interlace,
  R#14 through R#23, and the V9938 status bits.
* Four-port host decode, on PCB v0.4 and later only.
* The V9938 command engine.

Hard constraint, carried from v1.3.0: no per-scanline path may get slower. This is a gate on the
release, not an aspiration.

## Unscheduled

Wanted, but not assigned to a version.

* Extract the VGA driver into a standalone repository shared with `pico-56`, which currently
  carries a diverged fork.
* Expose a real-time clock through PICO9918 registers ([#67](https://github.com/visrealm/pico9918/issues/67)).
* Sega Master System Mode 4 ([#61](https://github.com/visrealm/pico9918/issues/61)).
* Symbols, footprints and 3D renders for incorporating the PICO9918 into new designs
  ([#60](https://github.com/visrealm/pico9918/issues/60)).
* No-cut modification parts list ([#30](https://github.com/visrealm/pico9918/issues/30)).
