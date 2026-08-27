# Live test harness

Drives the PICO9918 over a Pico debug probe: sets up a scene by writing VDP registers and VRAM
straight into `tms9918Inst`, reads back the exact indices the renderer produced, and programs the
board - all without a host machine, a cartridge, or a person.

It exists because verifying D4 by photograph took four flash-and-photograph rounds, three of which
were spent on the test rather than the firmware.

## Layout

```
live9918.py     the board: SWD, flashing, capture, PNG
perf.py         every scene against the clock
perflog.py      the perf history, and what moved between two runs
results.py      what a run leaves behind
runner.py       all of it, in one openocd session, in the order that works
ci.sh           both desktop tiers, for CI

history/        the perf record over time, and the page that plots it
web/            the two browser tools; nothing here is needed to run the tests
runs/           the records, small enough to commit
reports/        generated pages - not tracked; the records they are made from are
```

What is *not* here is the renderer's half: the scenes, the references, the seven stages that assert
pixels, and the desktop shim are the library's, under `core/test/suite`. They need no board, so a
board is not what proves them - CI runs them under four compilers on every push, and this harness
imports the same package rather than keeping a second copy in step by hand.

That split is also the division of labour. The library owns what the renderer must compute; this
repository owns the three stages that can only be answered by a device - `diag`, `perf` and
`perf-panels`, which read microseconds and which lines did not fit. `runner.py` runs all ten.

The registry of stages stays explicit in `runner.py` - the order matters, and a suite that derives
its order from a directory listing has hidden it.

## What the firmware has to provide

Almost nothing. Scene setup and flashing are pure SWD memory access, so the only firmware support
is a coherent view of the rendered line, which `PICO9918_LIVE_TEST=ON` adds:

```c
liveTestCaptureRow(y, vPixels, lineBytes, lineSource);   /* one DMA per scanline */
```

That fills `liveTestCapture` with palette indices, plus a `frame` counter that ticks at the top of
each frame and the frame's own height and line width. A reader arms a capture, waits for the
firmware to clear the flag, and reads; nothing halts, and core 1 never waits.

**A frame the reader already has does not come over the wire, and does not even cost a window.**
The capture DMA runs through the RP2040's sniffer, which CRC-32s the bytes in hardware as they are
copied - seeded, reversed and inverted so it is exactly the CRC `zlib` computes, and it costs the
render path nothing but one bit in a config word.

`freeze.py` hands the capture the reference it is about to compare against, and the capture asks the
firmware for `LIVE_TEST_REQUEST_CRC` first: that mode runs the copy on *every* row so the sniffer
sees the whole frame, and one armed frame answers what a window at a time takes four, eight or ten
to. A scene that matches is finished there - **one frame and about eighty bytes**, whatever its
height. Only when the frame CRC differs does the reader fall back to windows, and then each window's
own CRC still spares the ones that match, so a single changed byte reads back one window rather than
the frame.

Golden sweep: **57.5 s at 10 MHz, 49.2 s at 20, 26 s with the window CRC, 14 s with the frame CRC.**
The whole run went 355 s to 283 s to 240 s. The trade is explicit: a matching CRC is
believed rather than verified, so a difference that collides with the reference's CRC-32 would pass.
That is 2^-32 against three quarters of the sweep.

**A property that can say what it expects need not read the frame either.** The scroll sweep builds
the frame each scroll value should produce - the control's rows rotated, backdrop either side - and
hands it to the capture, so the CRC answers the property and the pixels only cross the wire when it
says they differ. The page-size sweep does the same with a plainer expectation: the property *is*
that two frames are identical, so the second is put to the first. 15 of 15 hits at 35 ms each
against 341, and a board that scrolls wrongly still comes back with the byte and the column.

**A sweep moves a register, so it sends a register.** Resending 16KB of VRAM to change one scroll
value costs 62 ms a shot, and a sweep is hundreds of shots. The rig remembers the image it put on the
board and writes only the register file when the tables are byte-identical, without blanking the
display to do it. Four VRAM writes remain, at the points where the content genuinely changes; a
single differing byte sends the whole scene again, so a configuration cannot inherit the last one's
tables. `apply_raw` also clears the GPU trigger bits, as `scenes.apply` does - a trigger left enabled
writes VRAM underneath the scene the rig thinks it put there.

**And the diagnostic counters settle in frames, not half seconds.** The accumulators reset every four
frames and the reading is measurably right by the eighth, so the wait is twelve off the firmware's
own frame counter. Checked against a 40-frame settle over all 98 scenes: median difference 0.000 us,
worst 0.030, against a 1.50 floor.

**The transport is the wall, and it is USB.** A 12 KB read costs a flat ~45 ms of non-wire time at
5, 10, 20 and 30 MHz alike; measured end to end it is 0.96 ms per call plus 3.87 ms per KB, a
ceiling of 258 KB/s at any transfer size. `dump_image` to a reused file saves nothing, openocd's
`read_memory` over the socket is twice as slow, and neither `dap memaccess` nor `apcsw` moves it.
The only lever left was moving fewer bytes, which is what the CRC does.

**The poll loop's sleep is not waste, and polling harder is not free speed.** A reader arms a
capture and waits 10 ms at a time for the firmware to clear the flag. Across 46 scenes that is 4.83
seconds of sleeping - 18% of the wall clock - which looks like an obvious win until you take it out:
spinning with no sleep at all saves **0.24 s of 20.4**. The wait is real, not overshoot. A window
cannot come back faster than the display draws it, so removing the sleep only turns idle waiting
into SWD traffic. Measured at 10 ms, 16-then-1 ms, 1 ms and a bare spin; all four within 0.3 s.

**SWD traffic does not perturb the render.** The same four poll rates over the scenes that drop rows
gave identical counts every time - `t40-48rows-sprites` 2, `t40-60rows` 10, three runs each. Reading
the board hard while core 1 renders does not steal enough AHB bandwidth to cost a scanline, which is
worth knowing before designing around a contention that is not there.

**Do not take timing readings from a live-test build.** The copy costs about a microsecond a line
while a capture is armed, and `.bss` grows by the capture buffer, so **every SRAM figure taken from
a live-test build is that much pessimistic**. Measure the shipping build for both.

**The buffer holds 48 rows, not a whole frame.** The reader takes the window, advances and re-arms,
and because a scene is static data with the GPU triggers cleared the passes reassemble the identical
image. 48 divides every frame height the renderer produces - 192, 240, 384 and 480 - so no pass is
ever a ragged remainder, and it costs 12 KB against a whole frame's 120. That is what makes the
harness possible at all: with R0's row doubling a frame is 480 lines, which would be a quarter of
the RP2040's RAM and half the PRO's, and a 128 KB-VRAM build has less room still. The cost is a
frame per window - about 60 s across the full suite.

## Wiring

| Probe | PICO9918 |
|---|---|
| SWCLK | SWCLK |
| SWDIO | SWDIO |
| GND | GND |

openocd comes from the Pico SDK install and needs no separate download; the target config is
`rp2040.cfg` or `rp2350.cfg`, chosen by which board is on the probe, not by which UF2 was built.

**`--board` picks an ELF, not a board.** One board is on the probe at a time and swapping it takes a
person, so a run aimed at the other tier is always a mistake, and a silent one is the worst kind:
`program` reports success and the RP2040 then answers nothing at all. Every session checks the chip
before it writes, and refuses:

```
pico9918pro-v1-3-0-live.elf is a rp2350 image and the probe did not answer as one (it is rp2040).
One board is wired up at a time, and swapping it takes a person
```

The two target configs fail differently, so the check takes both answers: the matching one logs a DP
identity (`0x0bc12477` for RP2040, `0x00040927` for RP2350) and the other reads a CPUID of zero off a
core that is not there. Neither of those means yes, so a session that gets neither refuses as well -
which is also what a board that has stopped answering looks like.

**If a board does stop answering**, the RP2040's rescue debug port resets its power state machine so
the bootrom halts and takes fresh code:

```
openocd -s <sdk>/openocd/0.12.0+dev/scripts -f interface/cmsis-dap.cfg \
        -c "adapter speed 1000" -c "set RESCUE 1" -f target/rp2040.cfg -c "exit"
python live9918.py flash ../../build-live/pico9918/src/pico9918-v1-3-0-live.elf
```

### The bench harness

The board under test sits in a **ZIF socket** on a carrier board rather than in a console. There is
no host, so the carrier defines the bus idle state itself: `CD0-7`, `/CSR`, `/CSW`, `MODE` and `RST`
each carry a **4.7K pull-up to +5V**, and **+5V and GND come off the debug probe's own test points**.

That arrangement is what makes the harness work, and each part earns its place:

- **`/CSR` and `/CSW` idle high, so no bus cycle ever begins.** Both are active low (TMS9918A pins
  15 and 14). The firmware's PIO waits on their edges, so with nothing driving them it sees no host
  traffic at all - which is exactly the condition this harness needs, because it writes VDP
  registers and VRAM straight into `tms9918Inst` over SWD. A host asserting a chip select mid-scene
  would be interference, not coverage.
- **`RST` idles high, which is *not* reset** - it is active low, TMS9918A pin 34. Holding it high
  also holds `/BOE` low, so the data buffers stay enabled; `/BOE` is static and RST-gated, not a
  per-cycle control. See `HARDWARE.md`, in the build workspace above this repo.
- **`MODE` is pulled up to give a floating input a level, not to select a port.** It is sampled
  *inside* a cycle - `tms9918.pio` reads it once `/CSR` is stable low - so with no cycle its state
  never reaches the firmware. What the pull-up buys is a 5V input that is defined rather than
  drifting.
- **Power from the probe means one USB cable feeds both sides**, so there is no second supply and
  no sequencing between probe and board to get wrong.

`MODE1` is not on the carrier: it is pulled high on the PICO9918 board itself. It is the V9938's
second mode line (`gpio.c`, "Mode 1 (V9938)") and has no TMS9918A pin of its own.

> **This rig deliberately cannot test the host interface.** Nothing drives the bus, so the PIO
> interface, `/CSW` timing and the read-ahead are untested by everything here - see "What it does
> not cover" below, and `../host/` for the tool that does exercise them.

## Use

`runner.py` is the whole rig in one command, in one openocd session, in the order that works:

```
python runner.py --board 2040                 every stage, one record
python runner.py --board 2040 --quick         canaries and properties, minutes faster
python runner.py --board 2040 --only freeze perf
python runner.py --board 2040 --save --report writes runs/ and reports/
python runner.py --board 2040 --save --report --against 2040-aba934b
python runner.py --board 2040 --clock 1        reboot at 302 MHz and measure there
```

### Without a board: `--desktop`

```
cmake -S test/live/desktop -B build-live-desktop -G Ninja -DCMAKE_C_FLAGS=-O2
cmake --build build-live-desktop

python runner.py --desktop                    seven stages, 111 scenes, about a second
python freeze.py --desktop                    the goldens alone
python gpu.py --desktop                       the GPU programs, on the C core
python web/console.py --desktop               the visualiser, same page
```

Same scenes, same frozen references, same properties - against the library in a process instead of
a board. **All 111 scenes reproduce their device-frozen goldens byte for byte**, and that is
structural rather than lucky: the shim captures through `PICO9918_LINE_CAPTURE`, the hook the
firmware's own harness uses, at the same call site inside `pico9918_frame_scanline` and from the same
pointer. It renders through the frame path the firmware renders through; it does not reimplement
it.

**What it cannot do, and does not pretend to.** `diag`, `perf` and `perf-panels` are dropped with a
note - a microsecond off a PC is not a smaller version of a microsecond off the device - and
`dropped` is always empty, so the line-fits acceptance test stays on hardware. There is no timing
hook compiled into the shim's build at all, so the numbers cannot appear by accident. A desktop run
records its board as `desktop`, which is load-bearing: `results.latest` picks a drift baseline by
board, and a run with no timings must never become the baseline for one that has them.

A desktop pass says the renderer computes the right picture. Only the board says it computes it in
time.

**Two tiers, because the goldens are keyed on the line width.** The default build renders the
256-byte line every mode uses; `-DLIVE_DESKTOP_TEXT80_8BPP=ON` is the PRO's 8bpp 80-column tier and
reproduces `golden-w512`. Point `LIVE9918_SHIM` at the second build directory to run it.

### Watching it: `view.py`

```
python view.py                     every scene, half a second each
python view.py --scroll            turn the scroll register, so a scene moves
python view.py --free --scroll dump-F18A_Karts_demo    one scene, scrolling, forever
```

A window, sixty frames a second, standard library only. It runs **the real freeze stage**:
`freeze.run` applies each scene, plays it through the window from the hook it already calls between
applying and capturing, then captures and compares it. The frames you watch are ahead of the
capture, so watching cannot change a verdict - and `--scroll` puts the register back before it.

**A scene is a still image.** Registers plus a VRAM image, with nothing writing to it - so the
renderer draws the identical picture every frame and sixty a second of one scene looks exactly like
one frame, because it is one picture. The sweep is the motion; `--scroll` is the motion within a
scene.

Measured on this machine: the shim answers a frame in **0.64 ms** including the pipe and the palette
expansion, and Tk loads a 512x384 PPM in **1.71 ms** - so 60fps runs at about a seventh of the
budget. The expansion is the shim's because it is free in C and would cost a Python viewer its whole
frame turning 200,000 indices through a table sixty times a second; what crosses the pipe is a
finished PPM, which Tk accepts as raw bytes with no base64.

Desktop only, and not for portability's sake: a board renders sixty frames a second and can hand
over about one, so a window fed over SWD would be a slideshow. `web/console.py` has a **Run the
suite** button that lights the scene grid up verdict by verdict, which is the watchable version for
a board.

**`--clock` is the one knob that reboots the board.** The system clock is the denominator of every
timing here, and `clocks.c` reads it once out of flash before the renderer starts, so the running
copy of the configuration cannot carry it. The presets are the firmware's own: `0` = 252 MHz, the
floor the budget assumes, `1` = 302 MHz and `2` = 352 MHz. `--clock-tested` sets `PICO9918_CONF_CLOCK_TESTED`
the same way. Both are stored, so they persist until something sets them back - a run says
`rebooted at 302 MHz` when it changed one, and every record carries the clock it measured at.

The higher presets are worth more than faster numbers: **`t40-60rows` and `t40-48rows-sprites` have
never fitted a 63.6 us line at 252 MHz**, so the RP2040 had no reference for either. At 302 MHz they
render whole (23.6 and 21.6 us a line) and the references in `golden/` came from there. Pixels do
not depend on the clock - only whether the line had time to draw - so those references are the right
image for both presets, and a 252 MHz run still reports the scenes over budget and skips the
comparison.

The rig writes the configuration sector itself, with both cores halted, rather than asking the
firmware to save its own. **That route is not safe with a debugger attached**: `PICO9918_CONF_SAVE_FORCED`
makes the GPU core erase and program flash while core 0 renders out of it, and the board then stops
after one frame and answers zeros for every flash address until the sector is rewritten from
outside. Nothing here takes it, but the firmware does on its own account whenever the stored
configuration is blank or older than the build - so a board can arrive in that state on the first
boot after a version bump.

`--clock` is also the way out of it, because a blank or unreadable sector falls back to the copy in
RAM: `readConfig` builds a valid block there before handing it to the save that never finishes, so
storing that both repairs the board and completes what the firmware was attempting. Erasing the
sector on its own does not - the next boot only tries the same save again.

Everything in one session is what makes the rig cheap enough to run every iteration. `d4` still runs
first, but nothing depends on that: stage order cannot fix order-dependence, because the state a
stage inherits comes from the previous *run*, so each property sets its own up (see Properties).

**The run leaves a record, not just a verdict.** `runs/<board>-<commit>.json` holds every scene's
state, the rows it dropped, what it cost and what each property asserted - stamped with the commit,
the branch, whether the tree was dirty, the board, the firmware version, the **clock read off the
board**, the toolchain version, every `PICO9918_*` option the ELF was configured with and its
section sizes. That is what a saved control was missing: a bare `{scene: {render, line}}` cannot say
what produced it, so it goes stale silently. The record is small enough to commit, so a comparison
can reach back through the branch rather than only to whatever is on this disk.

**New fields are additive, and old records stay readable.** A record written before a field existed
simply does not carry it, and the page leaves that row out rather than printing a dash against every
old run. Anything added to the record has to work that way: read it if it is there, say nothing if
it is not, and never make an older record unreadable.

> **Deleting a committed record dirties the tree, and the next run says so.** Records are tracked,
> so `rm runs/*.json` is a pending deletion and every run taken before it is committed stamps
> `dirty` - which is correct, and misleading if what you wanted was a clean baseline. Commit the
> removal first. Ordinary runs need no cleanup at all: each writes `<board>-<commit>.json`, a new
> untracked file, which does not count as dirt.

**A dropped row fails the run with the overlay off; with every panel on it does not.** Overlay-off is
the renderer's own cost, which is the budget that matters. Overlay-on is the overlay's cost, which
`freeze.py --diag` exists to *measure* - several RP2040 scenes drop there today, so failing on it
would report FAILED on every run. Regressions fail wherever they appear.

**`web/console.py` is the one that shows you rather than tells you.** A browser front-end for the
board: a grid of every scene, click one to put it on the screen, drop a VDP dump on the page to see
it live.

```
python web/console.py --board 2040          opens http://127.0.0.1:8919/
```

It holds one openocd session for its lifetime, so clicking a scene is instant and **nothing else
here can talk to the board while it runs**. Thumbnails are rendered from the frozen goldens rather
than captured, so the grid paints before the first scene is applied and needs no probe to do it -
and because the palette it colours them with is the one `scenes.apply` installs, a thumbnail and the
live capture of the same scene are byte-identical PNGs. A scene with no golden is one that overruns,
so its capture was never comparable; the card says so instead of showing a picture.

**And the registers are writable.** Scroll sliders, layer and bitmap switches, ECM and palette
selects, sprite size and magnification, 30 rows, double rows, 80 columns - plus a raw `R__ = __` for
anything without a control. Every write is a read-modify-write on the board, so a control that owns
four bits of a register cannot take the other four with it, and every reply carries the register
file that produced the picture: a bit the firmware ignores - anything F18A while locked - snaps back
rather than lying about what it did. "What happens if layer 2 scrolls the other way" is a slider
instead of a scene, an edit and a flash.

The control table lives in `web/console.py` and is the same register bits `scenes.features` decodes,
written once so the panel cannot offer a knob the coverage matrix has no column for.

**The diagnostic panels are controls too**, though they are stored configuration rather than
registers - so the switch writes the byte and marks the configuration dirty, and the firmware's own
`applyConfig` derives the master flag and rebuilds the overlay at end of frame, exactly as the
configurator does. They never appear in a capture, because `liveTestCaptureRow` runs before the
overlay draws; they are for the monitor. What does show up is the cost: turning all four on takes
`gm1-priority` from no dropped rows to three.

Those two sit beside the picture rather than in the grid below it, with the raw register write, since
neither changes what the board draws - one changes what it reports about itself and the other is an
escape hatch. The grid below is the scene.

**`Live preview` turns the capture off without turning the board off.** Every control writes over
SWD and then reads a frame back, and the frame is the expensive half - 0.36 s against 0.01 s for the
write alone. With it off a slider tracks what the monitor is doing rather than what the transfer is
doing, and `Re-capture` says when to look at this picture again.

**A slider writes while it moves.** A drag raises far more events than the board can take, so what is
held is the slider rather than a queue of values: one write is in flight at a time and each reads the
slider when it is sent, so every write carries wherever the drag has got to and the one that settles
it always lands. Dragged 41 steps at 125 a second, the board ends on the value the slider ends on.
The panel does not re-sync mid-drag either - an answer to an earlier write would pull the thumb back
to somewhere the hand has already left.

**What you see is the shape the board draws, not the shape of the bytes.** A capture is 256 or 512
bytes a line and 192 to 480 rows, but the picture is always 512 VGA columns - 256 indices doubled
across, or 512 rendered one to one in 80-column text - by 384 or 480 lines, since every row is two
VGA lines unless R0's row doubling makes it one. Laying the page out by the byte geometry drew a
48-row scene as a tall strip and a 30-row one as a letterbox, so the server sends the true aspect
with each capture and each catalogue entry, and both the live panel and the thumbnails carry it.

In the grid every thumbnail gets the same frame, shaped to the tallest picture there is: 512 columns
by 480 lines. A 30 or 60 row scene fills it and a 24 or 48 row one is centred in it with black above
and below, which keeps the captions on one line across the grid instead of stepping up and down with
the mode.

**The page is one screen and the scene list is the only thing that scrolls.** The board is on the
left with the tweak panel under it, and the browser is a column of its own on the right. A hundred
scenes below the picture meant scrolling it off the screen to pick the next one and back up to look
at the result, which is the one thing this page exists to let you do.

That column is a fixed width in pixels rather than a share of the page. As a fraction it took the
same cut of every extra pixel of window, so the wider the screen the *smaller* the picture got -
which is backwards. Now every pixel of extra width goes to the picture, and the picture is sized
against the box it sits in rather than a seeded height, in whichever axis runs out first. On a
screen too short to hold the picture and the controls at once the left column scrolls; the browser
never does.

An uploaded dump is applied and shown but not kept. Writing it into `dumps/` makes it a scene with a
golden, which is a decision to take deliberately rather than by drag and drop.

**`web/report.html` is the report.** Open it and drop a record on it, or
two to compare. Nothing is generated, downloaded or served: it is one file with no CDN, no fonts and
no network request of any kind, and the records are read in the browser.

It has three views. **Budget** is each scene against the time its own mode gives a line - 63.6 us,
or half that where R0 doubles the rows - with the scenes that dropped a row marked, because that and
not the average is the acceptance test. **Coverage** is
what each scene exercises. **Compare** is the delta against another run: a diverging bar per scene
with a loss growing left in red and a win growing right in green, render on its upper half and line
on its lower, scaled to the largest move in the pair and ticked at the board's noise floor. Deltas
are always **B minus A**, and the page says so above the table.

Two records dropped together open on the difference, oldest first, with a Swap A/B button when that
guess is wrong. Records arrive through the file picker or a drop rather than `fetch`, which is
blocked on `file://`.

`web/report.py` writes a copy with a pair already embedded, for handing to someone:

```
python web/report.py runs/2040-bce997d.json
python web/report.py runs/2040-bce997d.json --against last
python web/report.py --compare 2040-aba934b 2040-bce997d      any two records
python web/report.py --list
```

It substitutes the records into `web/report.html` and nothing else, so a generated report and a
hand-loaded one are the same program looking at the same data. Every row, delta, gap and verdict is
derived in the browser from the records themselves rather than summarised into the page, which is
what keeps there from being a Python implementation of the merge and a JavaScript one drifting
apart. Generated copies land in `reports/`, which is not tracked - the records they are made from
are.

**Two records may be arbitrary.** Different boards, clocks, compilers, CMake options, commits - so
the record carries all of them and the page leads with the ones that differ. That is not decoration:
comparing a PRO run against an RP2040 one turns up `TEXT80_8BPP ON -> OFF` and `.scratch_x -256 B`
before a single scene is read, which is most of the explanation for whatever the scenes then say.
Where the difference makes a delta meaningless - a different board, or a different clock - every
timing delta is greyed out and the page says why, because only pixels are board-independent. The
golden and property columns still mean exactly what they say across boards; that they hold on both
is the assertion the oracle rests on.

The individual tools all still run standalone and all take `--board` / `--elf`:

```
python live9918.py flash build-live/pico9918pro/src/pico9918pro-v1-3-0-live.elf
python live9918.py shot frame.png --elf <same elf>
python live9918.py regs --elf <same elf>
python scenes.py                              the feature matrix, no board needed
```

or from a script, which is the point:

```python
with Live(elf) as t:
    t.unlock()
    t.reg(0x00, 0x04)                    # text 80
    t.reg(0x01, 0xF0)
    t.vram(0x0800, bytes(range(80)))     # name table
    rows, pixels = t.capture()           # 240 x 256 palette indices
    assert pixels[7 * 256 + 12] == 0x0f
```

`capture()` returns the indices the renderer wrote, before the palette expansion - so a comparison
is byte-for-byte against an expectation, with no optics, no colour drift and no photograph. Use
`png()` when you want to *look* at the frame instead.

## The scene library and the frozen reference

`scenes.py` holds the hand-written scenes - every mode crossed with ECM, the layer enables, both
scrolls, the page wraps, sprites, priority and the bitmap layer - and every VDP dump in `dumps/`
becomes one more. `python scenes.py` lists them, so no count is written down here to go stale. A
scene is pure data: `build(name)` returns a 64-byte register file and a whole 16KB VRAM image and
touches no hardware, so scenes can be listed, diffed and reviewed without a board. `apply()` writes
*all* of both, every time, so nothing leaks from one scene into the next reference.

Content is **text**, drawn with the font the configurator already ships. Every row says which layer,
which page and which row it is, then runs a ruler that steps one column per row:

```
1A05 56789 BCDEF0 234567 9ABCDE     layer 1, page A, row 05
2A05 BA987 543210 EDCBA9 76543      layer 2 runs the other way, so the two cross
```

A scroll, a page swap or an off-by-one row is then something a person can read off a screenshot -
`gm1-vscroll-page` visibly switches from `1A0C` to `1C00` partway down the screen - rather than
something to be measured. It is no less sensitive than noise to the failures that matter, because
every neighbouring cell differs; and unlike noise, a wrong answer can be diagnosed by looking at it.

**What a scene exercises is derived, never declared.** `scenes.features(name)` reads the mode, both
layer enables, ECM, both scrolls, both page sizes, the palette selects, every bitmap-layer flag and
the sprite configuration straight out of the register file the scene already returns, using the
F18A's own bit positions from `f18a_cpu.vhd`'s register write decode. A declaration beside each
scene would be a second copy of the truth and would drift from the first; this cannot, it needs no
board, and it works for the dumps - which are someone else's content and could not be annotated by
hand at all. `python scenes.py` prints the matrix and `web/report.py`'s Coverage view renders it.

That makes the gaps executable too. It already reports that no scene scrolls layer 2 in both axes,
none uses a 2x2 page layout on either layer, and the tile and sprite palette selects only ever take
their first and last values - all real holes, none of which anything else here would have noticed.
A gap is either a scene worth writing or a feature the firmware lacks, and the matrix deliberately
does not try to tell those apart.

Two scene properties are load-bearing and easy to lose:

- **Layer 2 is transparent between its glyphs.** An opaque layer 2 covers the screen, and since it
  outranks both layer 1 and a priority bitmap layer, every two-layer scene would be a
  picture of layer 2 alone and would test nothing else. That mistake was made and caught here.
- **Sprites come in two clusters of four**, overlapping in y, so the per-line limit, collisions and
  the priority order all get exercised - and their names cycle, because only four shapes exist.

```
python freeze.py                 compare every scene against test/live/golden/
python freeze.py --update        write the references
python freeze.py gm2 mcm         only scenes whose name contains one of these
python freeze.py --canaries      the scenes that drop a line before any average moves
python freeze.py --png shots     also write a PNG per scene
python freeze.py --audit         which features are still missing (needs no board)
python freeze.py --diag          with every overlay panel on: what the overlay costs, per line
```

**A golden is not automatically the right answer.** Each scene declares itself:

- **frozen** - the feature combination works today, so any change is a regression and the refactor
  has to reproduce it byte for byte. Nearly all of them are.
- **provisional** (2) - the scene exercises something the F18A does and this firmware does not yet,
  so its capture records an *absence*. When the feature lands the golden must change; re-freezing it
  with `--update` is how that change gets reviewed rather than missed.
- **over budget on a named board** - the scene renders correctly and does not fit. The drop is
  measured and printed as always; the *verdict* treats it as a standing fact rather than a new
  failure, the way a drop with the overlay on is already treated. The day it stops dropping the run
  says so, and the marker comes off.

Each provisional scene also names the scene it should be identical to while the feature is missing,
which is what `--audit` checks - so the feature matrix is executable rather than a claim in prose.
Today it reports two absences, both on a 4bpp build: the bitmap layer and ECM in 80-column text, which
need the 8bpp tier. Once a feature arrives its scene stops being expressible as an absence and gets
goldens of its own instead.

**48 and 60 rows are this board's own modes.** R0 bit 3 renders every VGA line instead of doubling
each one, so 24 and 30 rows of cells become 48 and 60 - and the same content gets **half a line's
time**, 31.8 us instead of 63.6. The record stamps each scene's own budget so a 28 us line in a
doubled-row scene reads as the 3 us of headroom it has rather than the 35 it would have at 24 rows.
80 columns fits at both heights on RP2040; 40 columns does not at 60 rows, nor at 48 with sprites,
because the six-pixel cell is the expensive text emitter. Those two carry the over-budget marker.

Two properties worth keeping true: captures are repeatable, so a second pass over an unchanged
firmware matches every byte; and the indices are the renderer's, not the board's, so the same
references must hold on RP2040 and RP2350 (`--board 2040`) until something deliberately tiers them.
Both hold today: every frozen scene is byte-identical on both boards, including the ones frozen on
one board and checked on the other. Timings are not board-independent and are not meant to be - the
same worst-case scene is
61.03 us on RP2040 and 38.67 on the PRO, so the budget is always the RP2040 number.

**A scene that does not fit is not a scene that renders differently.** An over-budget scanline is
skipped rather than drawn late (`vga.c:600`), so its row here holds whatever the *previous* capture
left, which reads exactly like a rendering regression - six scenes did, before the harness could
tell the difference. `liveTestCapture.seen` records which rows the renderer reached, and `freeze.py`
reports those scenes as OVER BUDGET and refuses to compare them. That makes it the sharpest headroom
test there is: it is per line, where an average is not.

The diagnostic overlay matters here, which is why `apply()` turns it off. It draws on every scanline
and it is stored config, so it survives a flash. `perf.py` turns it back on, because its numbers
come from there - and since the sample now closes *after* the overlay draws, `line` includes it and
`render` does not.

**The overlay never reaches a capture**: `liveTestCaptureRow` runs before `renderDiag`, so
`freeze.py --diag` compares the same goldens and only the dropped-row report moves. That makes it
the instrument for what the overlay costs, and a per-line one - which an average over a frame cannot
be, because the panels cover part of the screen and their cost lands on some lines and not others.
On RP2040 with every panel on, **12 scenes drop lines where none drop with it off**. To price the
overlay rather than test it, use a simple *unlocked* scene: the register panel is 228 rows unlocked
and 48 locked, and on a scene with headroom nothing drops, so the average is honest. `perf.py
--panels` does the same for the frame average, opt-in so the saved history stays comparable.

## Properties, which beat goldens for anything new

A golden records whatever the firmware did. For a feature that has just arrived that is the wrong
instrument: it freezes the implementation's own opinion, and the reasoning that produced the
expectation is the reasoning being tested. So each new feature also gets a property taken from the
hardware reference and asserted over a sweep, and the goldens are what stop it regressing afterwards.

```
python properties/test_d4.py                    layer arbitration, all 192 rows against an exact image
                                     (--elf, not a positional path)
python properties/test_text_scroll.py           a scrolled row is the unscrolled one rotated - and the
                                     vertical page size bit does nothing
python properties/test_text_colour.py           every pixel is its sub-palette-0 colour in the sub-palette
                                     its layer chose, and a cell with no colour writes nothing
python properties/test_text_ecm.py              a text cell above ECM0 is an ordinary ECM tile six pixels
                                     wide, and its X flip mirrors six bits rather than eight
```

**A property blanks all 64 registers and all of VRAM before it writes its own**, the way
`scenes.apply` does. Writing only what it names leaves it at the mercy of whatever the last run left
on the board - a bitmap layer enabled by an earlier scene covers the screen and fails rows the
property never set up. Running it first does not help: the state it inherits comes from the *previous
run*, not the previous stage.

Three habits make them worth more than the goldens they sit beside:

- **Sweep, do not sample.** 15 scroll values across 9 configurations found two bugs a golden would
  have frozen instead.
- **Measure what you cannot derive.** Which layer owns a pixel is not deducible from one capture, so
  both files take an extra capture with a different backdrop and read ownership off it: layer 2 owns
  a pixel wherever changing the backdrop leaves it alone. Guessing instead is how an earlier draft
  came to ignore a layer 2 pixel that happened to *be* the backdrop colour.
- **Run it against the old firmware first.** A property that passes before the change proves
  nothing. Both text properties were run on the pre-change build and both failed there. Where the
  old firmware had no implementation at all to run against - T40 at ECM1-3 - the equivalent is to
  build the bug the property exists to catch: `test_text_ecm.py`'s flip half was confirmed by
  mirroring eight bits instead of six, which it caught at the second cell of the first row.

- **Prefer a property that compares two paths over one that restates the code.** The strongest
  assertion in `test_text_ecm.py` is that a text cell draws what a Graphics I tile draws from the
  same name and attribute - one scene of each geometry, differing only in the name table's stride.
  It tests plane addressing, the Y flip, transparency against the backdrop, the sub-palette and the
  priority bit at once, against a path frozen against hardware long before T40 had one.

## Scenes from real software

`test/live/dumps/` takes VDP dumps out of JS99er, and every `.bin` in it becomes a scene called
`dump-<name>`. Drop a file in and it is one - no code, no registration.

A dump comes in one of two layouts, told apart by length:

| Bytes | Contents |
|---|---|
| `0x0000-0x3fff` | VDP RAM |
| `0x4000-0x403f` | VR0-VR63, every register |
| `0x4040-0x40bf` | 64 palette entries, two bytes each, `0000RRRR GGGGBBBB` |

That is a whole VDP state, and it needs nothing else. Stock JS99er writes the shorter form instead -
16KB of VRAM with only VR0-VR7 appended - and for that a text file beside the dump carries the rest
in JS99er's own notation:

```
VR0:>00 VR1:>E2 VR2:>00 ...        as many per line as it likes, through VR57
```

Either way the unlock is inferred from VR57, which only holds `0x1C` if the software actually
unlocked the chip. A sidecar - `<name>.txt`, or a shared `registers.txt` - can override anything and
supply what a short dump cannot carry:

```
unlock                 # or `lock`, to override what VR57 implies
49 = 0xB4              # register numbers are the F18A's own, so decimal; 0x for hex
palette 4 = 0x00F      # 12-bit 0xRGB
```

**The palette is board state, so it is written for every scene**, not only for dumps: a dump's
palette must not outlive it. Scenes without one get the firmware's own boot palette, read out of the
running image rather than copied into this file where it could drift.

**This is what R1 asks for.** A scene the library invented can only prove the renderer is
self-consistent; a dump of real software proves it against something that was drawn by someone who
did not know how our renderer works. The first one in - GM1 unlocked, layer 2, ECM3, horizontal
scroll at cell 18 fine 4, two-page horizontal on both layers - exercises more of the pipeline at
once than any hand-written scene here.

**A dump is a snapshot, and that is all it claims to be.** Several of these scenes drive the F18A
GPU during the frame - switching sprite attribute tables mid-screen, changing registers at a
scanline, cycling the palette - and a static capture of VRAM and registers cannot reproduce any of
it. So a dump will not always look like the game does live: retroplex renders two sprites here
because the other table is swapped in by GPU code we never run.

That costs the reference nothing. The golden says what this exact VDP state renders, and the
comparison is our renderer against itself, like for like. What a dump must never be treated as is a
picture of what the software looks like.

**Dumps and their goldens are committed**, 16 KB each. Keeping them out meant the coverage they
carry only existed on one disk, and `dump-ck1-ti` is the argument for tracking them: it found a
position-attribute addressing bug that none of the written scenes reached, and a reader without the
file could neither see it nor tell whether a change had put it back.

A dump that does not fit a board cannot say so for itself the way a scene can, so `DUMP_OVERBUDGET`
in `scenes.py` names the ones that do not. It is empty today, and `dump-ck1-ti` - its one entry
until the sprite pass got cheaper - is the reason to read that narrowly. See **A dropped row is not
the only way to miss the budget** below.

## Programs for the GPU

`gpu.py` is the same idea one level down. A scene is a register file and a VRAM image; a **GPU
program** is 548 bytes of TMS9900 code dropped into a blank VDP at a start address, and it writes
both of those itself. The stage hands it the blank VDP, starts it, waits for it to halt, and then
compares the picture it left behind against a frozen reference in `golden/`.

**The programs are other people's work.** `gpu-mandel` is **Tursi's** F18A GPU Mandelbrot, used
here with credit and with thanks - and that provenance is the reason it is worth running, for the
same reason a dump of real software beats a scene the library invented. `gpu-programs/README.md`
credits each program, `gpu.py` prints the credit on every run, and it travels in the record.

```
python test/live/gpu.py                    every program, on the PRO
python test/live/gpu.py --desktop          ...on the portable C core instead
python test/live/gpu.py --update           freeze the picture a program draws
python test/live/runner.py --board pro     the `gpu` stage, last, after everything else
```

**One number is the test; the other three are the report.** The picture is compared byte for byte -
that is the assertion, and it is board-independent, so one reference serves both boards and the
desktop. Alongside it a run records:

| | where it comes from | what it is good for |
|---|---|---|
| **microseconds** | `gpuTimeUs`, the library's own accumulator, measured around the execution call inside the firmware | how fast this processor runs a real program |
| **frames** | the display's own counter over the same interval | the only figure here that compares with a capture card |
| **wall ms** | measured out in the harness | the gap to the accumulator IS the harness overhead, rather than a thing to argue about |

Nothing compares the microseconds against anything, because the same program on a board and on a
workstation is two processors, not two readings of one instrument. `gpu-mandel` runs in **3899 ms**
on the PRO at 252 MHz and **96 ms** on the C core on a desktop, for the same 22,899,808 instructions
and the same picture.

**It is clock-linear, and that has been measured rather than assumed** - which matters, because the
first thing anyone comparing two numbers for this program will have got wrong is the clock:

| clock | trigger to IDLE | frames | cycles per instruction |
|---|---|---:|---|
| 252 MHz | 3899.3 ms | 236 | 42.91 |
| 302 MHz | 3228.2 ms | 196 | 42.57 |
| 352 MHz | 2762.0 ms | 168 | 42.46 |

Repeatable to 0.05% at a given clock, and 1% better than linear at the top - the renderer's fixed
per-line work is a smaller slice of each line there, so it steals less SRAM bandwidth from the core
running the program. Which is also why **the same program measured against a capture card comes out
shorter**: a card measures first pixel to last, and this program spends its first few per cent
setting registers and building tables before it draws anything.

**Running it in both places is a differential test of the two cores.** A board executes the
hand-written Thumb core in `gpu/platform/thumb9900_{m0,m33}.S`; `--desktop` executes `run9900_c` in
`gpu/tms9900.c`, 1257 lines whose only previous gate was a code review against that assembly. They
now have to agree on a picture, pixel for pixel, after twenty-three million instructions.

**And the reference itself was checked against a third implementation.** `gpu-mandel`'s golden was
frozen from the C core and then compared against the picture an unrelated Python TMS9900 interpreter
draws from the same 548 bytes - identical, all 49,152 indices. So the reference is not merely "what
we did the day we froze it": it is what the program means.

**The program runs beside the renderer, in both places.** On a board that is free: core 0 is
already sitting in `pico9918_gpu_loop`, so starting a program is one store to `restart` and it draws
while core 1 renders, exactly as a host-loaded one would. The desktop shim gives it a thread of its
own for the same reason - not for speed, but because that is the shape of the machine, and because a
program run inline would render nothing until it finished. The two threads share VRAM with no lock,
which is the fidelity rather than an oversight: on the device they share it across two cores with no
lock either. Both places call `pico9918_gpu_step`, the loop's body split out for exactly this, so it
is the same code either way.

So the harness is the same three calls on both - `gpu_start`, `gpu_poll`, `gpu_stop` - and the loop
that uses them lives once, in `gpu.py`, where the timeout belongs because the timeout is the
program's.

A program has to **stop**, on `IDLE` or by clearing bit 0 of VDP register `>38`. One that does not
is stopped by the harness, in both places and by the same switch the program itself would use:
`run9900` tests that byte every instruction. And it has to be **deterministic** - nothing here seeds
a clock, so a program that reads the scanline at `>7000` draws whatever the renderer happened to be
doing.

**And you can watch it draw.** `python view.py --gpu` runs the programs in the 60fps window. See
`view.py` for why a tenth of a second of C core gets filmed and played back while a board's four
seconds would not.

`gpu-programs/README.md` covers the format and how to add one.

## A dropped row is not the only way to miss the budget

Everything this rig reports about the budget - `freeze.py`'s OVER BUDGET, the capture's `seen`
bits, `DUMP_OVERBUDGET` - rests on a row going missing. A row only goes missing once the renderer
is a **whole line** behind: core 1 posts one request per line, and `vga.c` drops a row only when it
finds a newer request already waiting. So a row that overruns by less than a line period is still
rendered, just late - into the buffer core 1 has already started reading out. That tears the left
of the row instead of losing it, and no counter here moves.

`dump-ck1-ti` is the worked example. Its eight sprite rows run 67-68 us against 63.6. Eight rows
of ~3.4 us is 27 us of accumulated lag, well short of a line, so `freeze.py` reports `ok`, the
golden matches byte for byte, and the picture on a monitor is still visibly wrong. Before the
sprite pass got cheaper the same rows were ~8 us over, 8 of them crossed a whole line, and exactly
one row - 125, just past the band - was dropped and reported.

The per-row microseconds are the only instrument that sees this. Live-test builds record them in
`liveTestCapture.lineTimes`, one saturating byte per display row, written for every row whether or
not a capture is armed - and `liveTestCapture.skipped` / `skippedRows` count the rows that really
did go missing, which `seen` cannot, because `seen` is cleared per armed capture and only covers
the window it copied. **`lineTimes` is not cleared between scenes**, so rows past a shorter scene's
last row still hold the previous scene's numbers; read only the rows the current mode renders.

## Traps

- **Unlocking is sticky.** It survives until the board resets, so a locked scene has to call
  `lock()` rather than assume.
- **PRAM holds `0xFRGB` big-endian.** `pico9918.c` byte-swaps `defaultPalette` on the way in.
  Decoding it little-endian turns dark blue into yellow, which cost one round of "the capture does
  not match the screen" - the indices were right all along.
- **A T80 capture byte is two pixels.** `png()` expands it to a 512-wide image; a raw byte compare
  does not care, but anything that indexes the palette per byte does.

## What it does not cover

The indices are the renderer's output, not the display's. Everything after them - the palette
expansion in `renderer.c`, VGA timing, SCART - needs a capture device or an eye. And nothing here
touches the host bus: the PIO interface, `/CSW` timing and the read-ahead are the one part of the
system this harness deliberately bypasses, which is what a Pico-based bus test-bed would be for.
