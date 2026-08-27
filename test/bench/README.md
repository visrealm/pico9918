# Render benchmark ROM

One ROM, eleven fixed scenes, advanced with any key or the fire button. It exists so that a render
timing is taken against known content rather than whatever demo is to hand - a figure with neither a
board nor a scene attached cannot be compared to anything.

Every scene is **static once set up** - nothing animates, and nothing is written to VRAM while the
scene is displayed - so a diag reading is repeatable to whatever the noise floor turns out to be.

## Building

The CVBasic toolchain comes from the configurator build, so build that once (any target under
`configurator_all`) and the tools land in `build/pico9918/external`. Then:

```
cmake -S test/bench -B build-bench -G Ninja
cmake --build build-bench
```

ROMs appear in `build-bench/dist`:

| File | Host | Notes |
|---|---|---|
| `pico9918bench_ti99_8.bin` | TI-99/4A | FinalGROM 99. **Needs the 32K expansion** - the CVBasic TI-99 runtime runs from `>a000`. 32KB, no banking |
| `pico9918bench_cv.rom` | ColecoVision | 8KB |
| `pico9918bench_msx.rom` | MSX | 8KB, plain unbanked cart image |

Point `-DPICO9918_TOOLS_DIR=<other build>/external` at a different firmware build tree if this
one has not built the configurator yet.

> Each platform compiles in its own directory. CVBasic crosses its outputs over when several
> instances run concurrently in one working directory, which shows up as a Z80 target full of 9900
> mnemonics. Do not "simplify" that back to a shared `asm/` directory.

> `CONT1.KEY` idles at **15**, not 0 - it reports Coleco-style keypad codes where 15 means
> "nothing pressed" (`cvbasic_9900_prologue.asm:884`), and a full keyboard returns ASCII for
> anything else. Testing it for truthiness leaves the scene advance stuck forever waiting for a
> release that has already happened.

## Scenes

The backdrop colour identifies the scene: scene N runs with backdrop N + 1. Press any key or the
fire button to advance; scene 10 wraps to 0.

| # | Mode | Layers | Sprites | What it is for |
|---|---|---|---|---|
| 0 | T80, 24 rows | T1 | - | Baseline emitter. Cell colour changes every 8 cells |
| 1 | T80, 24 rows | T1 | - | Same, colour changes **every cell** - the worst case for `renderText80Layer`'s `lastColor` memo |
| 2 | T80, 30 rows | T1 | - | Scene 0 with 240 scanlines rather than 192. Confirms a cost is per scanline, not per frame |
| 3 | T80, 24 rows | T1 + T2 | - | Two full emitter passes, T2 opaque on every cell |
| 4 | T80, 24 rows | T1 + T2 | - | T2 present but with **whole 4-cell groups transparent**, which is the only thing the overlay pass can skip |
| 5 | T80, 24 rows | T1 + T2 | 8/line | The composite's mixed path. See below |
| 6 | MCM | T1 | - | Multicolour, the cheapest emitter there is |
| 7 | MCM | T1 | 8/line | Multicolour with the same sprite field |
| 8 | GM1 unlocked | T1 + T2 | - | The two layers tile each cell exactly, layer 2 at fine scroll 7. The reference picture for scenes 9 and 10 |
| 9 | GM1 unlocked | T2 only | - | **Layer 1 disabled** (R0x32 bit 4) - the D4 case |
| 10 | GM1 unlocked | neither | - | **Both layers disabled** - the second D4 case |

All 80 columns of every T80 scene are filled with non-blank cells, so no scene can take an
empty-tile shortcut by accident.

## What each scene should look like

The shapes exist so that "correct" is checkable by eye. Layer 1 draws a **two-pixel bar at the left
of each cell**, layer 2 draws one **at the right**, and every eighth column of layer 1 is a solid
cell as a ruler. Two layers therefore produce two interleaved column grids, and a misalignment is
countable in pixels instead of being a matter of opinion.

| # | Should look like |
|---|---|
| 0 | 80 fine vertical lines, one per cell, with a solid block every 8th column. Colour changes every 8 cells, so the screen reads as coloured groups of 8 |
| 1 | The same grid with a different colour on **every** column |
| 2 | Scene 0, taller - 30 rows instead of 24 |
| 3 | Layer 2 is opaque here, so it **covers layer 1 completely**: the same grid but with the bar on the *right* of each cell, in layer 2's colours. If you can see left-hand bars, layer 2 is not drawing |
| 4 | Alternating groups of four cells: right-hand bars where layer 2 draws, left-hand bars and ruler blocks where it is transparent. The clearest picture of which layer owns which cell |
| 5 | Scene 3 plus a regular field of white sprite blocks over the top two thirds, each covering the left half of a 32-pixel chunk |
| 6 | A mosaic of colour pairs, 32 cells across, four bands per cell row |
| 7 | Scene 6 plus the same sprite field |
| 8 | Clean four-pixel **white** and **black** stripes, **no blue at all**, and **black immediately right of the white sprite bar** |
| 9 | The same **black** stripes on a flat backdrop, still **butting against the right of the white bar**. Nothing else |
| 10 | A flat backdrop and the white bar, nothing else |

### How scenes 8-10 answer the question without measuring anything

The two layers are set up to tile each cell exactly: layer 1 owns the left four pixels and is
opaque, layer 2 owns the right four and is transparent everywhere else. Three fixed colours:

| Colour | Means |
|---|---|
| white | layer 1 drew here |
| black | layer 2 drew here |
| **dark blue** | layer 1's background - **layer 2 should have covered every pixel of it** |

So **any blue on screen is a selection mask that disagrees with the pixels it selects between**.
No counting, no measuring: a correct scene 8 is white and black only.

Both layers run at fine scroll **4, and the two must be equal**: the halves only complement each
other if the layers sit on the same grid. An earlier version used 0 and 7 and produced a one-pixel
blue line beside every white band - the arithmetic's answer, not a defect, but it made the test
unreadable.

**Four, not the register's maximum of seven**, because the picture repeats every 8 pixels and so
does the error. A mask left in the wrong space is displaced by `t1Scroll`, and 7 is -1 modulo 8, so
a total failure of the realign would read as a one-pixel nudge of an endless grid. At 4 it swaps
black for white outright.

### The reference bar

Content that repeats every 8 pixels cannot show a displacement against itself, so scenes 8-10 carry
a **white vertical bar at screen x 8-15**, drawn by twelve unmagnified sprites. Sprites never pass
through the tile selection mask, so the bar is an absolute screen position whatever the mask does.

**The check is local and needs no counting: immediately to the right of the white bar there must be
black**, in scene 8 and in scene 9 alike. Four pixels of black, then four of white (scene 8) or
backdrop (scene 9). If white or backdrop butts against the bar instead, the mask never reached
screen space - D4's first half.

The bar also exercises the T2-only composite's sprite handling, since that path has to fold sprite
coverage out of the mask before it writes.

Scene 9 is the same picture with layer 1 disabled, so the black stripes must stay in the same half
of every cell. If they jump to where the white was, the mask never reached screen space, which is
D4's first half. If anything else appears alongside them, that is the previous scanline's tile
buffer, which is D4's second half. Scene 10 removes both layers, so anything at all on screen is
stale data.

The content is deliberately cost-neutral for the scenes that have recorded numbers. T80 and GM1
emitters do the same work whatever the pattern bytes are, and the multicolour scenes draw from the
one pattern group whose nibbles are both non-zero, so that mode's transparent-colour branch behaves
the same way from one reading to the next.

### The sprite field, and why it is shaped like that

Eight magnified 16x16 sprites per band, on 32-pixel x boundaries, in four bands - 32 sprites, the
maximum the F18A processes. The sprite pattern is **opaque on its left 8 columns and transparent on
its right 8**, so magnified it covers 16 of the 32 pixels it spans.

That is deliberate. `compositeAlignedTileBuffersWithDMA` has three outcomes per 32-pixel chunk:
skip it (`spriteMask == -1`), DMA the whole chunk, or walk it a pixel at a time. A sprite that
covered its chunk completely would take the *skip*, which measures nothing. Half covering every
chunk is what forces the CPU path.

Magnified sprites are 32 scanlines tall, so four bands cover the top 128 scanlines. On a 192-line
scene that is 2/3 of the frame with every chunk mixed and 1/3 with none - which is why scene 5 is
read alongside variant 5's synthetic all-mixed ceiling rather than instead of it.

## VRAM map

Room for 80x30 in every table, so the 24-row and 30-row scenes share one layout.

| Address | Contents | Register |
|---|---|---|
| `$0000` | tile patterns, 2K | VR4 = 0 |
| `$0800` | layer 1 names | VR2 = 2 |
| `$1200` | layer 1 attributes | VR3 = `$48` |
| `$1C00` | layer 2 names | VR10 = 7 |
| `$2600` | layer 2 attributes | VR11 = `$98` |
| `$3000` | sprite patterns | VR6 = 6 |
| `$3800` | sprite attributes | VR5 = `$70` |

The sprite attribute table is deliberately **not** at CVBasic's default address: this program
writes the four attribute bytes itself and never uses `SPRITE` or `MODE`, so whatever the runtime
does at its own address cannot clobber the field.

T80 needs position-based attributes (VR50 bit 1) for layer 2 to exist at all in this firmware -
`text_scan_line` only renders T2 inside that branch - so every T80 scene sets it.

## Designing a scene: derive both pictures first

Every scene here cost a flash, a photograph and a reply to test, and the D4 pair took four rounds
because each round shipped before the arithmetic was done. Before adding one:

1. **Write down the expected image pixel by pixel** - which screen column is which colour, given the
   scroll registers and the pattern bytes. If that cannot be written down, the scene is not ready.
2. **Write down what the defect's image looks like**, the same way. A test that cannot distinguish
   the two is not a test.
3. **Check the failure is not degenerate.** The content here repeats every 8 pixels, so a
   displacement of 7 reads as a 1-pixel nudge. 4 swaps black for white. Pick the magnitude that
   shows.
4. **Put a reference in frame that the subsystem under test cannot move.** Sprites do not pass
   through the tile selection mask, so a sprite bar marks an absolute screen position when
   everything else may have moved. The backdrop is no use - it is the same colour as the border.
5. **Prefer a local check.** "Is the pixel right of the marker black" needs no counting, no
   measuring, and no comparison between two photographs taken at different zoom.
6. **Watch for invariants the scene rests on.** The two-layer complement here only holds while both
   fine scrolls are equal; setting them differently produced a one-pixel blue line that was correct
   arithmetic and a broken test.

## Scenes worth adding next

All of them are on R9's list for steps 2 and 6:

- Priority tiles over sprites, and T1 per-tile priority with R50 bit 0 both ways.
- A priority bitmap layer under an opaque T2 - the D1 case, which no configurator ROM exercises.
- The 33rd tile at fine scroll 7, for the mask overhang.
- ECM 1, 2 and 3 in GM1, and the GM2 + ECM aliasing case of 2.8 with each `tpgsize`.
