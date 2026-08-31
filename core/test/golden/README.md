# Golden-frame regression harness

Byte-exact regression protection for the library's scanline renderer. Each
scene programs registers and VRAM through the public bus API only (the same
two-stage address-port / data-port writes the PICO9918 firmware performs),
then captures the indexed output of `pico9918_scan_line()` and its returned
status byte for every active line, the post-palette pixel output of each line
(format v3, see below), and a read-back of the 64 live palette RAM entries.
The captured frames are committed under `data/` and re-verified on every build.
This is the renderer's behaviour gate: any change to rendering, register semantics,
the unlock path, sprite flag generation, the TEXT80 pixel packing, the
palette-to-pixel conversion, or the data-port palette write path shows up as a byte
diff.

Alongside the scenes there is one further artifact, `data/overlay.golden`, which
gates the two overlay render paths - the splash logo, the diagnostics panels and
the shared text blitter the host's banner also uses. See "Overlay surface" below.

All scene content comes from a fixed-seed LCG. No `rand()`, no time, no
platform dependencies beyond libc.

## What the goldens protect

The shipping configuration: `PICO9918_MODE=1` (F18A) with
`PICO9918_SINGLE_INSTANCE=1`.

| Scene | Lines | Covers |
|---|---|---|
| graphics-i | 192 | standard Graphics I tiles + sprites; backdrop colour with low nibble >= 8 (R7 = 0xf9) pins the full 4-bit backdrop mask |
| graphics-ii | 192 | standard Graphics II (bitmap) paging |
| text | 192 | standard 40-column text (no sprites) |
| multicolor | 192 | standard Multicolor |
| text80 | 192 | TEXT80 two-tone; packed two-pixels-per-byte output is contract |
| sprites-max | 192 | 5th-sprite status flag and coincidence flag, deterministic |
| f18a-unlocked | 240 | real VR57 unlock, ECM1 tiles/sprites, two tile layers, scroll registers, 30-row mode, raised scanline-sprite limit |
| f18a-ecm3-bml | 192 | ECM3 tiles/sprites, per-position attributes, opaque fat-pixel bitmap layer |
| f18a-text80-attrs | 192 | TEXT80 position-based attributes with a second tile layer |
| f18a-vram-snapshot | 192 | dense LCG VRAM with ECM2, both layers, scrolls, transparent 2bpp bitmap layer - stand-in for GPU output as static VRAM state |
| f18a-bml-priority | 192 | BML priority/write-mask (VR31 bit 0x40) with a width-64 fully-opaque band that suppresses the tile layers, over ECM2 tiles and ECM1 sprites crossing the band edges |
| f18a-bml-wrap | 192 | a bitmap layer wrapped by the display rather than cropped by it - a full-width opaque priority layer at x=40 putting its overrun back at column 0, and a height running past the last scanline |
| f18a-bml-stride | 192 | a bitmap layer 102 pixels wide - not a whole number of bytes, so the row stride rounds up to 26 and this is the only scene where a truncating divide would place the rows differently |
| f18a-ecm0 | 192 | unlocked ECM0 tile path with h-scroll shift==2, non-ECM sprites below tiles including a colour-0 transparent sprite (sprite-mask release), and ReadData/read-ahead data-port traffic feeding the sprite table |
| f18a-gfx2 | 192 | unlocked Graphics II - the f18aOpsTable Graphics-II row (stage4 sprites-then-bitmap trampoline) with 16px sprites and a transparent 2bpp bitmap window |
| raw-reset | 8 | pico9918_reset() called from a deterministic unlocked+configured state, first lines rendered with no mode nudge - the post-reset contract (display off, backdrop 0, default palette) |

The status byte recorded per line is the value *returned* by
`pico9918_scan_line` (sprite 5S/COL flags plus the 5th-sprite number), not
the status register file.

## Palette observability

The library has no public palette read function. Every CPU-side read path
(the data port, `pico9918_vram_value`) masks addresses to the 16K window
(`0x3fff`), so palette RAM at internal address `0x5000` is unreachable, and
`pico9918_default_palette` returns the static default table, not the live
palette. The harness reads the live pram anyway, through the public bus: the
bitmap-layer fetch address `(VR32 << 6) + row * width` is *not* masked to
16K, so a width-64 opaque 2bpp BML with `VR32 = 0xff` fetches bytes
`0x5000..0x507f` (the 128 bytes of the 64-entry palette) on scanlines 65 and
66 and emits every byte as four 2-bit pixels. After each scene's frame is
captured, the harness renders those two probe scanlines with tiles and
sprites disabled and appends the reconstructed 128 bytes to the golden file.
That BML out-of-window fetch behaviour is itself pinned as contract.

## Post-palette surfaces (the pixel output)

The indexed output is palette-independent, so on its own it cannot observe the
palette-to-pixel conversion at all - a dropped `palDirty` flag would be a mutation
escape. That conversion is the library's (`src/pico9918_palette.c` builds
`pico9918_palette_lut[256]`; `PICO9918_EXPAND_INDEXED` expands indexed bytes to
pixels), and v3 captures it.

Per line, the harness records **two** 64-bit FNV-1a digests:

| # | Surface | Produced by |
|---|---|---|
| 0 | library | `pico9918_palette_regenerate()` + `PICO9918_EXPAND_INDEXED` - the library's own path |
| 1 | reference | `refRegenerate()` + `refExpand()` in `golden.c` - an independent reimplementation |

Digests rather than raw pixels: raw would be 1 KB per line (~3.5 MB of
committed binaries) for no extra diagnostic power, since a mismatch is
localised to the exact pixel by re-scanning the in-memory buffers.

### Which pixel format the goldens store

The **shipping Pico BGR16 format**, on both surfaces - not the desktop
RGBA8888 default. The golden build force-includes `goldenPixelPolicy.h` into
every TU (library and harness alike) through the documented host-override
mechanism that `platform/desktop/platform_std.h` provides. Two reasons:

- BGR16 is what the device produces. The expansion has only ever run on Pico;
  capturing the desktop expansion would pin a surface no hardware emits.
- The desktop expansion is currently **wrong**. `pico9918_palette.c` is
  written for a 16-bit pixel: the doubled build packs a pair with
  `PICO9918_PIXEL_FROM_RGB12(data) * 0x10001` and the paired build stages
  through `uint16_t tmpPal[16]`. Under the desktop 32-bit RGBA pixel the
  multiply overflows (`0xffaa00cc` -> `0x007600cc`) and `tmpPal` truncates
  (`0xffaa00cc` -> `0x000000cc`). That defect is real and unfixed; it is
  deliberately not baked into the goldens.

### Why an independent reference

Self-capture proves only that the library is stable, not that it is right. The
reference is written from the prose specification and shares no algebra with
the library: `refPixel()` extracts and reassembles nibbles rather than reusing
the mask/shift pair, and `refRegenerate()` / `refExpand()` reimplement the LUT
build and the expansion. Because both surfaces are BGR16, they are compared
**value-for-value on every pixel of every line**, and a disagreement fails the
run - and blocks `--capture` - even when both match their goldens.

The reference models two behaviours that are easy to get wrong:

- **Partial rebuilds.** The doubled build writes LUT entries 0..63 only, so
  entries 64..255 survive from whichever paired build last ran - across scene
  boundaries. `f18a-text80-attrs` really does render its first line through
  entries `text80` left behind.
- **Class lag.** `pico9918_display_mode()` is a cached value refreshed
  inside `pico9918_scan_line`, and the rebuild happens *before* the line is
  rendered, so a TEXT80 scene's first rebuild still uses the previous mode's
  class.

TEXT80 (the `PICO9918_LUT_PAIRED` class, two different adjacent pixels per byte
rather than a doubled pixel) is covered by the `text80` and
`f18a-text80-attrs` scenes.

### palDirty

`palDirty` is still not readable through any public API, but it is now
**observable**: `renderScene` rebuilds the LUT only when
`pico9918_palette_dirty()` says so, exactly as the firmware's scanline path
does. A palette write that fails to raise the flag leaves a stale LUT and
every following line expands through it, which moves the digests.

## Golden file format (version 3)

Per scene, one binary file `data/<scene>.golden`:

- header: magic `TMSG`, u32 version (3), 32-byte scene name, u32 line count,
  u32 bytes per line (256), u32 status bytes per line (1), u32 palette
  entries (64), u32 digests per line (2); u32s little-endian
- per line: 256 indexed pixel bytes, then 1 status byte
- per line: 2 uint64 little-endian digests - library surface, then reference
- then 64 palette RAM entries, uint16 little-endian each (raw pram memory
  order: low byte `0R`, high byte `GB`)

Version 3 added the digest block and its header field; the indexed and palette
blocks are byte-identical to the v2 captures. Version 2 and earlier files fail
the header check and must be recaptured.

The overlay surface is a **separate file with its own format**, `data/overlay.golden`:

- header: magic `TMSO`, u32 version (1), u32 row count, u32 pixels per row (642),
  u32 digests per row (2); u32s little-endian
- per row: 2 uint64 little-endian digests - library surface, then reference

Keeping it separate is what let the overlay surface land without recapturing the
fourteen scene files or bumping `GOLDEN_VERSION`.

## Workflow

Build (desktop, no Pico SDK), from the library root:

```
cmake -B build-desktop -DPICO9918_GOLDEN=ON -DPICO9918_MODE=1 \
      -DPICO9918_SINGLE_INSTANCE=1 -DCMAKE_C_FLAGS=-O2
cmake --build build-desktop
```

Compare (the CI gate - nonzero exit on any FAIL, prints the first divergence
as scene/line/byte):

```
./build-desktop/test/golden/golden_runner
```

Re-capture (only after an *intentional*, reviewed behaviour change - commit
the regenerated `data/` together with the change that caused it):

```
./build-desktop/test/golden/golden_runner --capture
```

`--data DIR` overrides the golden directory for experiments.

## Notes

- `-O2` is required.
- Golden builds disable `-march=native` (see the root CMakeLists) so codegen
  does not vary per capture/verify machine.
- The GPU is never executed by this harness; F18A GPU effects are represented as
  static register/VRAM state (`f18a-vram-snapshot`). The renderer's suite runs real
  GPU programs - see `test/suite`.
- **The scanline buffer needs slack.** `pico9918_scan_line` is prototyped as
  `pixels[TMS9918_PIXELS_X]`, but a scrolled F18A tile layer renders in whole
  32-bit quads, so the last partial tile of a fine-h-scrolled line is written
  *past* pixel 255 - the shifted-tile path reaches byte 263 (exactly 8), and zero
  once VR25/VR27 are cleared. The firmware already allows for this
  (`tmsScanlineBuffer[TMS9918_PIXELS_X + 8]`, `main.c`); the harness now does
  too. Callers sizing a buffer to exactly 256 bytes get their following object
  corrupted. Worth either fixing or documenting on the public prototype.

## Overlay surface (`data/overlay.golden`, format `TMSO` v1)

The fourteen scenes above render no overlay pixels, so without this the splash, the
diag panels and the pending-display banner would have no behaviour gate at all. The
defect class that matters is a colour-literal conversion rendering *almost* right -
an alpha or green bleed into blue. A digest catches a one-nibble shift; an eyeball
does not.

Same shape as the post-palette surfaces - the library's own render path plus an
independent reference written from the documented behaviour, digested per row with
FNV-1a. It is a **separate artifact** rather than a new per-scene block, so the
fourteen committed scene files and `GOLDEN_VERSION` 3 are untouched; the overlays
are also not per-scene state, being driven by frame counts and push setters rather
than by register/VRAM scenes.

Two honest qualifications on "independent reference", because the phrase can be
read as stronger than it is:

- The pixel-by-pixel cross-check is a **first-failure latch**, not an every-row
  scan: once a divergence is recorded it stops looking.
- For the left-panel columns the reference is **seeded by `memcpy` from the library
  output**, so those columns are pinned only by the committed golden digest, not by
  two independent computations. A defect there would be re-captured as the new
  truth by `--capture`. The genuinely independent spans are the text rendering, the
  splash unpack, the palette swatches and the modelled panel arithmetic.

Row counts also flatter the surface: of the rows emitted, only about 40% contain
any non-background pixel. The blank rows are not padding - they pin gate
boundaries, which is where off-by-ones live - but "N rows" is not "N rows of
content".

**These goldens are compiler-dependent.** The reference deliberately models
`darken()`'s unsequenced-modification UB as darken-in-place, matching what GCC
emits for ARM and for x86 at every optimisation level tested. That is what makes
this surface a gate for the approved `darken()` fix - that fix must leave these
goldens unchanged. The cost is that a toolchain resolving the UB the other way
would fail here for a reason unrelated to any real defect.

The buffer is 642 pixels wide, matching the firmware's real `RGB_PIXELS_X` (640
plus two PIO-autopull guard pixels). That is required, not cosmetic: the banner's
centring is computed against it in `main.c`, and the palette strip's last swatch
reaches pixel 575.

| Group | Rows | Covers |
|---|---|---|
| text | 104 | `pico9918_diag_render_text` - the one text path shared by the diag panels and the host's banner, which the banner calls from the hot border path. Both real banner strings at `main.c`'s own centring and colour; the `labelColor`/`valueColor`/`unitsColor` literals; all four rows of the font sheet; the register panel's `(`/`)` bit glyphs; a chained three-call run (the only case that catches a wrong x advance); x == 0; a non-zero y. Every case renders the glyph band plus one row either side, so the row gate's boundaries are pinned in both directions. |
| splash | 494 | `pico9918_splash_render` driven over frames 0..276 so the enter, hold, exit and reset positions are all pinned by position. Every frame's `y == 0` call is made, because that call is the animation clock. Geometry is the shipping VGA 480p case (vVirtualPixels 240, vPixels 192, vBorder 24). Sampled frames capture the whole bottom border, so the blank rows either side of the band pin the gate. |
| panels | 1200 | `pico9918_diag_render` over the whole frame, five configurations of `PICO9918_CONF_DIAG_REGISTERS` / `PICO9918_CONF_DIAG_ADDRESS` / `PICO9918_CONF_DIAG_PALETTE`, locked and unlocked. |

`PICO9918_CONF_DIAG_PERFORMANCE` **is** covered, and only because the clock is
injectable. `pico9918_diag_update` reads `PICO9918_HOST_TIME_US` when that byte is
set (the GPU% row); against a real wall clock the derived `gpuPctStr` glyphs change
run to run and the goldens would be unrepeatable. `goldenClock.h` replaces it with a
deterministic counter, which is what makes the row - and the `flt2Str` / `uint2Str`
plumbing behind it - testable at all.

Two behaviours the reference models rather than "fixes":

- **The splash row gate is a deliberate uint16 wraparound.** `logoOffset` goes
  negative, so for rows outside the band `y - (vBorder + vPixels + logoOffset)`
  wraps to a large `uint16` and the `< splashHeight` test is false. The reference
  spells the narrowing out.
- **`darken()` is documented pre-existing UB** (`pixels[x++] = (pixels[x] >> 2) &
  0x333`). ARM GCC emits ldrh/strh at the same address, and MinGW GCC 15.2 at both
  -O0 and -O2 likewise darkens in place, so the reference models
  darken-in-place-then-advance - the behaviour that ships. That also makes this
  surface a gate for the approved UB fix: rewriting it as `pixels[x] = ...;
  return x + 1;` is exactly this, so the fix must leave these goldens unchanged.

The prefill is load-bearing. `renderText` paints no background - every non-glyph
pixel goes through `darken()`, which reads the framebuffer pixel already there.
Against a zero buffer a broken `darken()` is invisible, so rows are prefilled from
a fixed affine walk over 16 bits (not the scene LCG, which is reseeded per scene).

## Frame surface (`data/frame.golden`, format `TMSF` v2)

Three groups. Two of them - **mapping** and **geometry** - pin a specification for
code that is still in `src/main.c`. The third - **interrupt** - is a genuine
differential gate over real library code, `pico9918_frame_update_interrupts` in
`src/pico9918_frame.c`, and it calls that function directly. Read the
specification-vs-differential note below before drawing conclusions from a pass.

`TMSF` v2 added the interrupt group. `FRAME_VERSION` is the frame artifact's own
version - `GOLDEN_VERSION` stays **3** and `OVERLAY_VERSION` stays **1**, so neither
the fourteen scene files nor the overlay artifact needed recapturing.

The fourteen scenes call `pico9918_scan_line` **indexed**, with a line number the
harness picks. So they are structurally blind to two things that decide what a
frame actually looks like:

1. **The interlace field mapping**, which chooses *which* VDP line a given display
   line renders. The scenes pin what line N looks like; nothing pins that display
   line N asks for line N.
2. **The frame geometry**, which bounds the display region - `vPixels`, `vBorder`
   and the end-of-frame trigger scanline, plus the virtual-pixel scaling.

A defect in the mapping is a shimmer on real hardware and is invisible to every
other gate here, which is why it gets a surface of its own.

Separate artifact with its own magic and version, so `GOLDEN_VERSION` 3 and the
fourteen committed scene files are untouched and nothing needs recapturing.

### What the mapping and geometry groups are

**All three groups are differential.** Each `frame*` candidate is an adapter over
real library code - `pico9918_frame_map_line_impl` and `pico9918_frame_geometry` -
and each `ref*` is an **independent model** written from the documented behaviour,
sharing no algebra with it. Where the candidate fuses (`y * 2 + (field ^ order)`,
`<< (bool)doubleRows`, `yScale - (bool)doubleRows`), the reference decomposes into
explicit cases. Do not tidy either side to look like the other: the independence is
the whole value of the surface.

The rule that keeps the digests honest: they pin the numbers the library has to
produce, so a candidate rewired to reach that code by a different route MUST
reproduce them byte for byte. The mutation table below is the evidence that the
digests are sensitive to each behaviour the surface claims to pin.

**The interrupt group.** Its candidate is
the shipping library function, called for real.

### Groups

| Group | Rows | Covers |
|---|---|---|
| mapping | 56 | The field mapping at fourteen lines-within-field, each in **all four** field x `interlacedFieldOrder` combinations. Both orders and both fields are pinned at every case because a defect that *swaps the fields* is the exact failure mode this exists to catch, and it is invisible to any single-field case. Covers interlaced-with-double-rows (where the mapping applies), interlaced-without (where it must not), and progressive both ways (where the field bit must be **discarded**, not folded in). Line 119 -> VDP 239 pins the top of a 24-row panel's doubled line space. The last three cases (lines 2000 and 3000) exist solely to pin the **width of the `& 0x0fff` mask**, which no realistic line number exercises. |
| geometry | 10 | The full reachable matrix: progressive VGA across all four double-rows x row-30 combinations, plus interlaced SCART at both timings (PAL 268, NTSC 220) with and without double-rows, **and both interlaced row-30 cases**. Each row carries the returned geometry **and** the post-call params, so a stray write to host-owned fields diverges even when the returned geometry looks right. |
| interrupt | 52 | `pico9918_frame_update_interrupts` **called for real** - thirteen merge cases across all three branches, each in **all four** combinations of (R1 interrupt-enable off/on) x (/INT pin pre-state false/true). See the section below. |

Rows 0-55 are mapping, 56-65 geometry, 66-117 interrupt.

### Interrupt group: the interrupt/status latch merge

`pico9918_frame_update_interrupts` is the plan's **highest-risk function** and until
`TMSF` v2 it had **no correctness gate at all**. The frame surface pinned line
mapping and geometry only; the function's sole guard was the firmware asm diff. An
adversarial review then proved the hole: mutating branch C's `tempStatus &
STATUS_COL` to `tempStatus & STATUS_5S` - which silently drops a collision raised
while F is latched, and lets 5S through where the datasheet blocks it - passed the
goldens **16/16**, the asm gate and the section-size check *simultaneously*.

An asm diff is a **change detector, not a correctness oracle**. It passes any
codegen-identical edit, and it goes blind the moment the function is legitimately
edited, as swapping the V9938 base will. This group is what closes that.

**The behaviour.** Merge the flags raised by the scanline just rendered
(`tempStatus`) into the SR0 latch, publish it, then bring /INT into agreement.
Three mutually exclusive branches, keyed on the latch's current F and 5S:

- **A** - F clear, 5S already latched. The latched **sprite ID** in the low five
  bits must *survive* (it names the fifth sprite of the line that first set 5S), so
  only the three flag bits of `tempStatus` are OR'd in.
- **B** - F clear, no 5S. No ID worth keeping, so `tempStatus` replaces the low five
  bits outright while the incumbent flag bits are preserved.
- **C** - F set. **COL only.** Per the TMS9918A datasheet and the F18A, COL is not
  gated by F, but 5S **is** blocked while F is set, and a new sprite ID must not
  overwrite the latch.

Then the pin: /INT is asserted exactly when R1's interrupt-enable bit **and** SR0's
F bit are both set. **R1 is therefore a second input**, and reconciling an R1 mask
change is much of why the function ends with a pin sync at all.

**The two discriminating cases**, both present:

| Case | Row label | Merge | Why it discriminates |
|---|---|---|---|
| A, 5S latched with ID 5 | `int-a-5s-id5` | `0x45` + `0x9f` -> **`0xc5`** | The latched sprite ID **survives**. Swapping branches A and B yields `0x9f`, telling the host the wrong sprite overflowed. |
| C, F latched | `int-c-block-5s` | `0x85` + `0x40` -> **`0x85`** | 5S is **blocked** while F is set - the latch must not move at all. The `STATUS_COL` -> `STATUS_5S` mutation yields `0xc5`. |

**Three values are digested per row, and the third one is the subtle part:**

- `frameStatusShadow` - the merged SR0 as the frame path's latch holds it. This is
  what the *next* scanline's merge reads.
- `sr0Register` - the register-file copy, which is what a host bus read returns.
  `pico9918_set_status_impl` writes both, so digesting both means a publish that
  updated only one diverges here rather than hiding until the next merge.
  `pico9918_read_status` is deliberately **not** used to read it: reading SR0
  clears F and 5S and releases the pin, destroying the state being digested.
- `intPin` - `tms9918->frameInt`, the **latched pin**.

The pin field **must** be `frameInt`, not `pico9918_interrupt_status()`. That
function *recomputes* `R1_INT_ENABLE && F` from live state, so it returns the right
answer whether or not the pin was ever synced - deleting the
`pico9918_frame_sync_int_impl` call would leave it reading `true` anyway and the
mutation would **escape**. `frameInt` is written only by the sync, so a missing sync
leaves it at its pre-call value. Rows are arranged so the correct post-state differs
from the pre-state in *both* directions, which is what makes the removal visible
(caught at `int-a-5s-id5-r10-pin1`, a row whose correct post-pin is low but whose
pre-state is high).

**Preconditions, and how each is established.** Every row sets the whole of the
state the function reads, so setup is visibly separate from behaviour:

| Input | Established by |
|---|---|
| the SR0 latch | `pico9918_set_status_impl(currentStatus)` - the library's own setter, so the shadow and the register start coherent exactly as on hardware. |
| R1 interrupt enable | a direct `TMS_REGISTER(tms9918, TMS_REG_1)` write. Direct rather than via `pico9918_write_reg_value`, whose path carries unlock-sequence and locked-mask side effects that are not part of this contract. The precondition is that nothing reconciles the pin between the register write and the call under test, so any pin movement in the row is entirely the function's. That holds - but NOT because `pico9918_write_reconcile_int_impl` is uncalled: the firmware's write IRQ handler does call it. It holds because that caller is reached only from a PIO interrupt, which this harness cannot raise - it writes the register directly. |
| the /INT pin pre-state | `frameIntSetup(want)`: install a temporary R1/SR0 pairing that computes to `want`, run `pico9918_frame_sync_int_impl` - the only writer of `frameInt` other than the function under test - then write the row's real precondition over the top. |

**Independence of the reference.** `refMergeStatus` works **per flag bit** and per
field, deciding each output part on its own and assembling the result from named
parts, and selects its branch from the two independent questions the datasheet asks
rather than the candidate's nested `if`. `refIntPin` derives the pin from two
separate named predicates rather than the library's fused `&&` over masked register
reads. It shares no expression with the library. Three reference-side mutations are
in the table below as positive evidence the reference is load-bearing rather than
inert.

**This group writes library state** (R1 and the SR0 latch, every row), unlike the
other two. The frame surface therefore runs **last** in the suite, so nothing else
can see that state - which is what keeps the fourteen scene goldens and the overlay
artifact byte-identical.

### Row-30 under interlace: included, and it pins a firmware defect

Both cases are reachable in a shipping build, and 268 virtual lines **do** hold a
240-line display region: the SCART timing is fixed at boot from
`PICO9918_CONF_SCART_MODE` plus dongle detection, while row-30 is set at **runtime**
by any F18A program writing R49 bit 6 after unlocking. Nothing couples the two.

- **PAL row-30** is ordinary: `vVirtualPixels` 268, `vPixels` 240, `vBorder` **+14**.
- **NTSC row-30** is not: 220 - 240 gives `vBorder` **-10**, and the firmware
  declares `static uint32_t vBorder` (`main.c:81`) while `vPixels` is `int`
  (`:80`), so it converts to **4294967286**. The border test at `main.c:567` then
  sends **all 220 lines** down the border path and renders **none** of them -
  SCART NTSC + row-30 is a blank screen. `vgaSetTriggerScanline(vBorder + vPixels)`
  wraps back to exactly 230, so nothing else looks wrong, which is why it has gone
  unnoticed.

This is a **defect**, and fixing it is a behaviour change needing its own decision -
not this harness's to make. What the surface does is *pin* it, so a fix appears here
as an intentional golden diff.

This is also why `FrameGeometry` uses the shipping widths (`uint32_t vBorder`,
`uint16_t vVirtualPixels`, `uint8_t vPixelScale`) rather than tidy `int`s: an
all-`int` model would compute -10 where the firmware computes 4294967286, i.e. it
would pin a value the firmware never produces.

### The three subtleties, each easy to "tidy" wrongly

- **Under interlace, `vPixels` ignores double-rows.** The doubling is gated on
  `yScale > 1` and interlaced builds run `yScale` 1. Progressive double-rows gives
  384; interlaced double-rows stays **192**.
- **Row-30 progressive gives `vBorder == 0`** - no vertical border at all. Code
  that assumes a non-zero border breaks exactly there.
- **`vPixelScale` / `vVirtualPixels` are rewritten only when `yScale > 1`.** Under
  interlace the host owns them and the library must not write them. Both the values
  *and* the not-writing are pinned.

### Mutation results

Each mutation was applied to the candidate, rebuilt with the delete-obj-and-archive
procedure below (asserting a non-zero "Building C object" count), then restored
byte-identically before the next.

| Mutation | Target | Caught? | First divergence |
|---|---|---|---|
| interlace: `field ^ order` -> `field` | candidate | yes | row 1 `il-dbl-line0-f0-o1` |
| interlace: `y * 2 + ...` -> `y * 2` | candidate | yes | row 1 `il-dbl-line0-f0-o1` |
| interlace: mapping applied unconditionally (drop `R0_DOUBLE_ROWS` gate) | candidate | yes | row 21 `il-nodbl-line0-f0-o1` |
| interlace: mask `0x0fff` -> `0x00ff` | candidate | yes | row 44 `il-dbl-line2000-f0-o0` |
| interlace: mask `0x0fff` -> `0x07ff` | candidate | yes | row 48 `il-dbl-line3000-f0-o0` |
| geometry: `vPixels <<= 1` under interlace too (drop `yScale > 1`) | candidate | yes | row 49 `geom-scart-pal-dbl` |
| geometry: rewrite `vVirtualPixels` unconditionally (drop `yScale > 1`) | candidate | yes | row 49 `geom-scart-pal-dbl` |
| geometry: `baseRows` 30 -> 24 | candidate | yes | row 45 `geom-vga-row30` |
| geometry: border test made signed (hides the NTSC blank screen) | candidate | yes | row 65 `geom-scart-ntsc-row30` |
| interlace: invert `takesEven` parity | reference | yes | row 0 `il-dbl-line0-f0-o0` |
| geometry: double `vPixels` under interlace | reference | yes | row 49 `geom-scart-pal-dbl` |
| geometry: write host-owned `params` under interlace | reference | yes | row 48 `geom-scart-pal` |
| geometry: `vBorder` `uint32_t` -> `int` | candidate | no (**equivalent mutant**, see below) | - |
| **control: comment-only** | - | **no** (correct) | - |

#### Step 4.6: the geometry group is now DIFFERENTIAL, and re-mutated against the library

The table above was produced when `frameGeometry` was an in-harness transcription of
`main.c`, i.e. when the geometry group was a *specification* gate. Step 4.6 moved the
real code into the library as `pico9918_frame_geometry` and rewired the candidate to call
it, discharging the contract stated at the top of the frame surface in `golden.c`. **The
committed digests did not move** - the artifact is still 118 rows and the suite still
16/16 with nothing recaptured, which is what makes the rewire evidence rather than a
claim.

Row numbers shifted by the geometry group's position, so the group was re-mutated with
the mutations now applied to **`src/pico9918_frame.c`, the shipping source**. Every
run deleted the affected objects, the archive and the executable, and recompiled 12 C
objects for the goldens plus 9 for the firmware:

| Mutation | Target | Goldens | Asm gate | First divergence |
|---|---|---|---|---|
| geometry: rewrite `vPixelScale`/`vVirtualPixels` unconditionally (drop the `yScale > 1` guard) | **library** | **CAUGHT** | CAUGHT (`pico9918_frame_end`) | row 61 `geom-scart-pal-dbl` |
| geometry: `baseRows` 30 -> 24 | **library** | **CAUGHT** | CAUGHT (`pico9918_frame_end`) | row 57 `geom-vga-row30` |
| geometry: stray write to host-owned `vPixelScale` outside the guard | **library** | **CAUGHT** | CAUGHT (`pico9918_frame_end`) | row 58 `geom-vga-dbl` |
| **control: comment-only in `pico9918_frame.c`** | - | **no** (correct) | **no** (correct) | - |

The first and third rows are the ones that matter: the geometry group's whole reason for
existing is the ownership rule ("under interlace the host owns `vPixelScale` and
`vVirtualPixels`, and the library must not write them"), and that rule is now enforced
against the shipping function rather than against a copy of it.

Three end-of-frame behaviours that moved in the same step are **NOT** visible here, and
are gated by the asm diff alone - stated so nobody assumes the goldens cover them: the
temperature averaging cadence (`& 0x3f`), the SR13 publish, and the `validWrites` latch.
The surface has no temperature or status-13 group, and adding one needs the injectable
clock. Each was mutation-tested and each was caught in `pico9918_frame_end`.

Interrupt group, mutations applied to the real library source
(`src/pico9918_frame.c`) and to the in-harness reference. Every run recompiled 12
C objects; the control was re-run in this batch and again **not** caught.

| Mutation | Target | Caught? | First divergence |
|---|---|---|---|
| `tempStatus & STATUS_COL` -> `& STATUS_5S` in branch C (**the escape that motivated this group**) | library | yes | row 90 `int-c-block-5s-r10-pin0` |
| swap branches A and B | library | yes | row 66 `int-a-5s-id5-r10-pin0` |
| drop `& 0xe0` in branch A (clobbers the latched sprite ID) | library | yes | row 66 `int-a-5s-id5-r10-pin0` |
| drop `& 0xe0` in branch B (loses the incumbent flags) | library | yes | row 86 `int-b-keep-col-r10-pin0` |
| remove the `pico9918_frame_sync_int_impl` call (pin never updated) | library | yes | row 67 `int-a-5s-id5-r10-pin1` |
| invert `(currentStatus & STATUS_INT) == 0` | library | yes | row 66 `int-a-5s-id5-r10-pin0` |
| reference: ID guard `!haveF && !have5S` -> `!haveF` | reference | yes | row 66 `int-a-5s-id5-r10-pin0` |
| reference: pin drops the R1 predicate | reference | yes | row 66 `int-a-5s-id5-r10-pin0` |
| reference: COL gated by F | reference | yes | row 94 `int-c-pass-col-r10-pin0` |
| **control: comment-only in `pico9918_frame.c`** | - | **no** (correct) | - |

The `nosync` row is worth noting: it diverges at `pin1`, not `pin0`. At
`int-a-5s-id5` with R1 interrupt-enable **off** the correct post-pin is low, so only
a row whose pin *pre-state* is high can see the missing sync. That is exactly why the
group runs every case in both pin pre-states rather than one.

The control is the standing guard against a silently-skipped rebuild. If the
control is ever reported as "caught", the rebuild procedure is broken and every
other result in this table is void.

The two mask rows were **escapes** found by adversarial review: with the original
row set the largest line-within-field was 191, which fits in 8 bits, so nothing
exercised the mask's width - and `>> 12` / `& 0x0fff` is the one piece of algebra
the reference does *not* decompose, making a narrowed mask a correlated error both
surfaces commit together. Lines 2000 and 3000 close it.

### Known limits of this surface, stated rather than papered over

- **`vBorder` `uint32_t` -> `int` is an equivalent mutant, not an escape.** The
  field still holds -10 and the border comparison converts it straight back to
  4294967286 (mixed `int`/`uint32_t` comparisons promote to unsigned), so neither
  the stored bits nor the active-line count change. There is no behavioural
  difference for a digest to detect. The `uint32_t` is kept because it documents
  the firmware's real declaration (`main.c:81`), not because a gate enforces it.
  What *is* enforced is the comparison's signedness, via the `activeLines` field -
  making the test signed is caught at `geom-scart-ntsc-row30`.
- **`/2` -> `>>1` on the border split is not caught, by choice.** They differ only
  for an odd negative numerator, and no reachable configuration produces one:
  `vPixels` is always `baseRows << 3` and every reachable `vVirtualPixels` (240,
  480, 268, 220) is even, so the spare-line count is always even. Adding a
  fabricated odd geometry to force a catch would pin an arithmetic accident - the
  same mistake the row-30 exclusion made in the other direction.
- **A reference rewritten to simply call the candidate is not caught**, and no
  digest can catch it: lost independence is a property of the source, not of the
  values, and the candidate is correct today so a delegating reference produces
  identical digests. This is inherent to the two-surface pattern. The defence is
  review, plus the `interlacedFieldOrder = 3` probe, which makes the two
  implementations observably distinct.
- Digests are taken over structs in **native byte order**, so the artifact would
  not verify on a big-endian host. The file format itself is endian-safe. This
  matches the pre-existing scene and overlay surfaces.

## Mutation-testing note

When temporarily mutating library sources to test the goldens: restore-by-copy preserves
the backup mtime and Ninja will skip recompiling, silently reusing mutated objects. Always
touch restored files (or clean-build) before recapturing or trusting a compare run.

**`touch` alone is not sufficient with MinGW Makefiles.** Restoring a file and
touching it, then mutating and touching again, landed inside one mtime second and
`mingw32-make` reported "Built target" with no compile step - so five different
mutations all produced the same digest and the run looked like a working gate. The
overlay surface's mutation driver deletes the affected `.obj`, the archive and the
executable instead, and asserts that the rebuild log contains a non-zero
"Building C object" count. Verify a null (comment-only) mutation does NOT move the
goldens before trusting that real ones do.
