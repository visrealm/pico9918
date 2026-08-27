# Debugging and testing the PICO9918 {#debugging}

A handful of tools, answering different questions. Most wasted effort on this branch came from
reaching for the wrong one - judging pixel alignment from a photograph, or trying to measure
performance with an instrumented build - so start here rather than with whatever is already
plugged in.

| Question | Tool | Where |
|---|---|---|
| Did the renderer produce the right pixels? | **Live harness** over a debug probe | `test/live/` |
| ...and with no board on the bench? | **`runner.py --desktop`** - the same 111 scenes against the library in-process, about a second | `test/live/desktop/` |
| Does the GPU still run a real program correctly? | **`gpu.py`** - a TMS9900 program in VRAM, timed from trigger to `IDLE`, and its picture frozen | `test/live/gpu-programs/` |
| How long does a scanline take, in a real mode? | **Benchmark ROM** plus the diag overlay | `test/bench/` |
| ...and how long, across the whole scene library, with no person? | **`perf.py`**, the same counters read over SWD | `test/live/` |
| All of the above, one command, one record? | **`runner.py --save`** | `test/live/runs/` |
| What do those records say, and what changed between two? | **`web/report.html`** - open it, drop them on it | `test/live/` |
| What does a scene actually *look* like? | **`web/console.py`** - a grid in a browser, click to put it on the board | `test/live/` |
| What does the suite look like *while it runs*? | **`view.py`** - a window at 60fps, desktop only; or the console's **Run the suite**, for a board | `test/live/` |
| Does the whole chain look right - palette, VGA timing, SCART? | An eye, or a capture device | - |
| Does the host bus behave? | **Bus test-bed** - a second Pico standing in for the host CPU | `test/host/` |
| Is a freshly assembled board wired correctly? | **QC loopback** | `test/qc/` |

**Start with `runner.py`.** It runs every stage in one openocd session in the order that works,
and writes `runs/<board>-<commit>.json` - which is what makes an iteration comparable with the last
one rather than with whatever is still in the terminal.

```
python test/live/runner.py --desktop                       after every edit, no board
python test/live/runner.py --board 2040 --quick             after every edit
python test/live/runner.py --board 2040 --save              before committing
```

`--desktop` answers the pixel question and only the pixel question: it drops the three stages that
measure the device, and it cannot see a line that did not fit. It is the fastest way to find out
that a change broke the picture, and it is never the reason to skip the board.

Then open `test/live/web/report.html` and drop the record on it - or two of them, to see what moved.

---

## 1. Live harness - exact pixels, no human

A Pico debug probe drives the board over SWD. Scene setup writes VDP registers and VRAM straight
into `tms9918Inst`, so there is no cartridge, no host machine and no ROM build; readback is the
exact index buffer the renderer produced; and flashing is openocd's job too. A cycle is edit,
flash, capture, assert.

```
cmake -S . -B build-live -G Ninja -DPICO9918_BUILD_COMBINED=ON -DPICO9918_LIVE_TEST=ON \
      -DPICO9918_VERSION_SUFFIX=live
cmake --build build-live --target combined

python test/live/live9918.py flash build-live/pico9918pro/src/pico9918pro-v1-3-0-live.elf
python test/live/properties/test_d4.py
python test/live/properties/test_text_scroll.py    a property, swept - see test/live/README.md
python test/live/properties/test_text_colour.py
```

```python
from live9918 import Live, default_elf, golden

with Live(default_elf()) as t:
    t.unlock()
    t.reg(0x00, 0x04)                  # text 80
    t.vram(0x0800, bytes(range(80)))   # name table
    rows, pixels = t.capture()         # exactly what the renderer wrote
    assert pixels[7 * 256 + 12] == 0x0f
    result = golden("t80-basic", rows, pixels)   # ok, why, differ, first
```

`golden()` is the workflow steps 3-5 need: freeze what the renderer does today, then require every
refactor to reproduce it byte for byte. It returns how the reference moved rather than only whether
it did - the count of differing pixels and the first one located - because a re-freeze is reviewed
as transitions, and that is where the review starts.

`scenes.py` and `freeze.py` are that workflow in bulk - a scene library covering every mode crossed
with ECM, the layer enables, both scrolls, the page wraps, sprites, priority and the bitmap layer:

```
python test/live/freeze.py             compare every scene against the reference
python test/live/freeze.py --canaries  the scenes that drop a line before any average moves
python test/live/freeze.py --audit     which features are still missing (no board needed)
python test/live/scenes.py             what each scene exercises (no board needed)
```

**A frozen scene that moves is a regression; a provisional one that moves is the point.** Some of
this refactor produces different and *more correct* output - a bitmap layer in text mode, scroll in
every graphics mode - so a scene that exercises a feature the firmware does not have yet records an
absence rather than a behaviour, and says in `scenes.py` what will change it. Re-freeze those
deliberately, one feature at a time, so the change is reviewed rather than absorbed.

**Wiring.** Probe SWCLK/SWDIO/GND to the board, and the board needs power - the debug connector
carries none, though the probe's own test points do. The bench harness here is a ZIF socket with the
bus pulled up and 5V taken off those test points; `test/live/README.md` has it and why each pull-up
matters. openocd comes from the Pico SDK install; the target config follows the silicon
(`rp2040.cfg` or `rp2350.cfg`), not the UF2.

**Firmware support is 288 bytes of `.text`**: `PICO9918_LIVE_TEST=ON` adds one call per scanline
which, while a capture is armed, hands the line to a DMA that copies it into the capture buffer.
That buffer holds 48 rows, which divides every frame height the renderer produces - 192, 240, 384
and 480 - so the reader takes the frame in whole passes. The copy also runs through the DMA sniffer,
which CRC-32s it in hardware, so a reader comparing against a reference it already holds asks for
the CRC of the whole frame and is done in one armed frame without transferring a pixel. Everything
else is memory access from the probe.

> **Never take timing readings from a live-test build.** The copy costs about a microsecond a line
> while a capture is armed, and the buffer inflates every SRAM figure.

### Why not the alternatives

A USB command channel in the firmware would put a USB stack on core 0 and instrument the very code
under test. A host build of the emulator core would need three DMA channels emulated, including
trigger-on-`set_write_addr`, and a host model can diverge from the hardware silently. The probe
runs the real silicon and needs neither.

---

## 2. Benchmark ROM - timing in real modes

`test/bench/` builds one ROM with eleven fixed scenes for TI-99, ColecoVision and MSX. Load it
once, step through with any key, and read FRAME and RENDER off the diag overlay. Scenes are static,
so a reading is repeatable; they are documented, so a number can be attributed to content.

It exists because a timing figure with neither a board nor a scene attached costs a measurement
cycle to reconstruct. **Record board, scene and clock against every number.**

See `test/bench/README.md` for the scene list, what each one is for, and what each should look
like.

---

## 3. Measuring performance

The instrument is the diag overlay (`PICO9918_CONF_DIAG_PERFORMANCE`), which shows:

- **RENDER** - microseconds per scanline inside `pico9918_scan_line`, the library alone.
- **FRAME** - milliseconds of per-scanline work summed over a frame, so it includes `renderer.c`'s
  palette expansion.

`FRAME * 1000 / lines` is the per-line total; subtract RENDER and the remainder is everything
outside the library. That residue is remarkably constant - 5.8 us at 4bpp on RP2350 across eight
scenes of wildly different content - which makes it a good check that a reading is sane, and it has
already caught a transcription error.

**FRAME cannot be compared between scenes with different row counts.** RENDER is per scanline and
compares everywhere.

**The budget is not always 63.6 us.** R0 bit 3 renders every VGA line instead of doubling each one -
this board's own 48- and 60-row modes - so a line there has **31.8 us**, half the usual. The record
stamps each scene's own budget for that reason. On RP2040 today, 80 columns fits at both heights and
40 columns does not at 60 rows or at 48 with sprites; those two carry an over-budget marker so the
drop is reported rather than failing every run.

`test/live/perf.py` reads those same two numbers over SWD, for any scene in the library, with no
overlay to photograph:

```
python test/live/perf.py --save before      # on the control build
python test/live/perf.py --against before   # on the changed one
```

Both builds must be live-test builds, or the comparison is not a control.

**The best headroom test is not a number at all.** A scanline that overruns is not drawn late - the
next one is skipped instead (`vga.c:600` drains the queue and renders only the newest). So "did any
scanline get dropped" is the budget question in its exact per-line form, and `freeze.py` reports it:
`liveTestCapture.seen` records which rows the renderer actually reached. It catches scenes whose
average line sits 10 us inside the budget, which no average can.

**Two things inflate a reading without appearing in it.** The live-test capture costs about a
microsecond a line while it is armed - `perf.py` never arms one, so it stays out of the timings -
and the overlay draws on every scanline in `renderDiag`, which runs *after* the timer stops. So the true cost while measuring is higher than the number reported, and on RP2040 the
overlay alone is worth three extra scenes' worth of dropped lines.

Four hard-won rules:

- **Watch the stack frame, not the instruction count.** On Cortex-M0+, state added to a per-tile
  loop costs more than any batching it enables. Four separate attempts lost this way, one of them by
  restructuring two `if`s: 6 cycles a tile worse while 18 instructions *shorter*, because it had
  turned an `ite` block into branches. Count opcodes by kind, not by number.
- **Isolate what you are measuring.** `pico9918_scan_line` is past the Thumb-1 branch range, so
  116 bytes added to it moved loads into an unrelated cell loop and relaxed 22 branches. The mode
  emitters are `EMITTER_NOINLINE` for that reason, and a measurement always needs a control build
  that differs only in the thing being priced.
- **A resizing change has its own noise floor, and it is not 0.02 us.** Change the library's size and
  modes whose object code was never touched - T40, T80, MCM - move by a few tenths of a microsecond a
  scanline on their own. So measure those too: they are the control that says how much of a delta is
  placement rather than work.

---

### The permanent record

`runner.py --save` writes a record to `test/live/runs/`, which is tracked. Two readable views are
generated from those records by `test/live/perflog.py`:

- **`test/live/PERF-LOG.md`** - the newest run for each board, scene by scene, with each scene's
  headroom against its own budget. Where a build stands.
- **`test/live/perf-history.jsonl`** - one object per run, oldest first, both boards. This is the
  database: it lets a regression be located in time rather than only against whatever ran last.

```
python test/live/perflog.py            regenerate both
python test/live/perflog.py --check    exit 1 if either is out of date
```

Neither file is edited by hand and neither is appended to - both are regenerated whole from the
records, so a run still adds one line to the history while nothing can drift from the record it
claims to summarise.

**Run the suite and regenerate before committing anything that can move a timing:** a change under
`src/`, under `core/src/`, to a linker script, or to the clock presets, SDK or
toolchain. Not every commit needs it, but a resizing change alone moves modes whose object code was
never touched, so "I did not touch that mode" is not a reason to skip it.

**Every run is compared against the newest saved record for its board, and a systematic slowdown
fails it.** That comparison is the record's whole point: a cost of a few tenths of a microsecond on
every scene drops no row, breaks no golden, and leaves every tracked assembly extract
byte-identical, so nothing else in this document can see it. `results.drift` holds the two
thresholds - a mean move and how far the scenes agree on its direction - with the measurements that
set them, including the control pair that says a repeated run moves each scene by 0.04 us and the
mean by nothing. Getting faster is reported rather than failed, and saving the record settles it
either way, because the next run compares against this one.

**`schema` is carried on every row and it is load-bearing.** A schema-2 row has a null `line` and
no `worst` field at all, so it is a different instrument from a schema-3 one and the two cannot be
compared. `results.latest` refuses to return a schema-2 row for that reason.

---

## 4. Designing a hardware test

Every hardware test costs a build, a flash and a look. Before running one:

1. **Write down the expected image pixel by pixel** - which column is which colour, given the
   registers and the pattern bytes. If it cannot be written down, the test is not ready.
2. **Write down what the defect looks like**, the same way. A test that cannot distinguish the two
   is not a test.
3. **Check the failure is not degenerate.** Content that repeats every 8 pixels cannot show a
   7-pixel displacement - 7 is -1 modulo 8, and a total failure reads as a one-pixel nudge.
4. **Put a reference in frame that the subsystem under test cannot move.** Sprites do not pass
   through the tile selection mask, so a sprite bar marks an absolute screen position when
   everything else may have shifted.
5. **Prefer a local check.** "Is the pixel right of the marker black" beats "are these two grids
   aligned" - no counting, no measuring, no comparing two photographs at different zoom.
6. **Name the invariants the scene rests on.** The two-layer complement in the bench ROM's D4
   scenes only holds while both fine scrolls are equal.

Better still, if the question is about pixels: **use the live harness and skip the photograph.**
D4 took four photograph rounds; as an assertion it takes seconds and checks all 192 rows.

---

## 5. Traps that have already cost time

| Trap | Symptom | Fix |
|---|---|---|
| openocd's stderr on an unread pipe | openocd dies mid-capture, connection reset | Log to a file; the harness tails it into the exception |
| Reading 48KB over SWD is slower than a frame | A seqlock reader never sees a stable buffer | Explicit arm/complete handshake - the firmware holds one whole frame still |
| Debug probe connector backwards or not seated | `Error connecting DP: cannot read IDR` | Pin 1 SWCLK, 2 GND, 3 SWDIO, on the **D** cable not the **U** |
| Several CVBasic instances in one directory | A Z80 target full of 9900 mnemonics | Compile each platform in its own directory |
| `CONT1.KEY` idles at **15**, not 0 | Scene advance stuck forever | Test against 15, not truthiness |
| An initialised big struct | 60KB of zeros in the image, copied at boot | Leave it zero-initialised so it lands in `.bss` |
| The board still running the *previous* build | Everything here is addressed by symbol and the symbols come from the ELF, so a stale image does not fail - it reads the wrong addresses. A build that only resized `.bss` kept the capture struct's address, so all 90 goldens passed and the perf strings behind it came back as pixels: `float("")` gave a clean 0.00 us for every scene | `Live.__enter__` compares two blocks of the ELF's image against XIP and refuses to read a board running anything else |
| GCC inlining a rare path into the hot function | +392 bytes and 54 relaxed branches in the scanline function | `noinline` on cold paths in that file |
| An over-budget scanline is skipped, not drawn late | Its row holds the **previous capture's** pixels, so the scene reads as a rendering regression. Six did | `liveTestCapture.seen`; `freeze.py` says OVER BUDGET and refuses to compare |
| The diag overlay is stored config, so it survives a flash | Several scenes silently over budget, and its own cost is outside the timer it feeds | `scenes.apply` turns it off for every scene; `perf.py` turns it back on deliberately |
| Only `PICO9918_CONF_DIAG` was cleared, so the four panel flags came from stored config | `perf.py --panels` was a no-op and every default reading was an all-panels reading. The two boards' numbers were not measuring the same thing | `scenes.apply` clears all five |
| A scene leaves a GPU trigger enabled (`R0x32` bit 0x40 or 0x20) | Order-dependent captures: the scene passes alone and fails in a suite, because the GPU's PC and run state are neither registers nor VRAM and the trigger resumes a program an earlier scene started | `scenes.apply` clears both bits. Nothing below `pico9918_scan_line` reads them, so no reference moves |
| A reference frozen at one line width compared against a capture at another | A board tier that renders 80-column text 512 bytes wide would overwrite the 256-wide reference the other board needs | The firmware reports `LiveTestCapture.width` and a reference is keyed on it; a build only writes its own width's directory |
| A long run redirected to a file looks hung | Python block-buffers stdout to a file, so the log sits minutes behind the run - it reads exactly like a stalled capture | Watch `runs/` for the record, or run without redirecting; `runner.py` prints a header per stage so progress is visible either way |
| A stage that cannot apply to the board under test | `test_text80_8bpp` raised `SystemExit` on a 4bpp build, which would abort the whole suite on RP2040 - the binding board | It reports itself not applicable instead. A gate that always fires on the board you gate on is a gate nobody reads |
| A cost spread evenly over every scene | The v1.3.0 library merge charged 1.15 us a scanline and passed everything: no row dropped, all 16 goldens byte-identical, every tracked assembly extract unchanged, and the leaf emitters' own symbol sizes moved by tens of bytes in both directions. Recovered by three one-line changes, and only because a device run happened to be compared against a build from before the merge | `runner.py` compares every run against the newest saved record for the board and fails on a systematic slowdown - see `results.drift`. Tracking more symbols would not have caught it: the instruction stream changed shape without changing size |

---

## What is missing {#what-is-missing}

- **The host bus.** Everything above bypasses it: the live harness writes memory, the bench ROM
  drives the VDP through a host that is assumed to work. The PIO interface, `/CSW` timing and the
  read-ahead are unverified by any of it. A Pico-based test-bed presenting a TMS9918 bus is the
  right tool, and it cannot see rendered pixels, so it complements the live harness rather than
  replacing it. See `HARDWARE.md` - in the build workspace above this repo, not in it - before
  touching that area.
- **Everything after the index buffer.** `renderer.c`'s palette expansion, VGA timing and SCART are
  only checked by eye today. A capture device would automate that end of the chain.
- **Sprites at 8bpp.** `reference-w512/` holds one sprite scene against the 4bpp set's eleven: ECM
  sprites and magnification are unchecked at the wide line width.
- **The GPU, past the instruction core.** `gpu.py` runs a real program - Tursi's F18A GPU
  Mandelbrot, credited in `test/live/gpu-programs/README.md` - on both cores now: the Thumb one on
  a board, `run9900_c` on the desktop, against one shared reference. So the instruction set, the
  `>6000` register window, the `>FFFE` workspace and the restart handshake are gated. What is still ungated is the rest of the glue: the DMA port at `>8000`, the palette guard's
  fault path, the config-action callbacks and the flash request. Each needs a program written for
  it, and the DMA port and the palette guard need an MPU, so they only exist on a board.
