# GPU programs

Stimulus for `test/suite/stages/gpu.py`: TMS9900 programs the F18A's on-board GPU
runs out of VRAM. Each one is loaded into a blank VDP at its start address and
triggered, and what it draws is compared against a frozen reference in
`../../oracle/reference/`.

A program covers what no register write can. It executes on the same core the
firmware runs a host's GPU code on, reaches the VDP register file through the GPU's
`>6000` window, and uses the F18A's hidden workspace at `>FFFE` - so a program that
draws the right picture is a statement about the instruction core, the register
window and the renderer together.

| file | start | credit | what it is |
|---|---|---|---|
| `mandel.bin` | `>1B02` | **Tursi** | The Mandelbrot set over x -2.0..+0.5, y +1.25..-1.25, 14 iterations in Q13 fixed point, drawn in Graphics II. Sets VR0-VR7 itself, builds its own name table, draws 49,152 pixels and halts on `IDLE`. 22,899,808 instructions. |

**`mandel.bin` is Tursi's program.** It is here as a test fixture, with credit and
with thanks - it is a real F18A GPU program written by someone who was not thinking
about our renderer, which is exactly what makes it worth testing against.

`mandel.a99` is an annotated reverse-engineered source of the 634-byte TI-99/4A
memory image it came out of - a host loader at `>A000` and this 548-byte GPU payload
at `>A056`, which is the part that runs on the GPU and the only part kept here. It is
included as provenance and as documentation: it names every register the program
writes and every F18A behaviour it relies on, each checked against the v1.9 VHDL. It
is not assembled by any build here.

## Adding one

Drop the binary in this directory and add an entry to `PROGRAMS` in
`../../stages/gpu.py` naming its file, its start address (even - the GPU refuses an
odd one), who wrote it, and how long to give it. Then, from `test/`,
`python -m suite.stages.gpu --update <name>` to freeze the picture it draws - and look
at that picture before committing it.

Two things make a program suitable. It must **stop** - on `IDLE`, or by clearing
bit 0 of VDP register `>38`; one that runs past its timeout is stopped by the
harness, using that same byte. And it must draw the same picture every time:
nothing here seeds a clock or a frame counter, so a program that reads `>7000` (the
current scanline) draws whatever the renderer happened to be doing.

`python view.py --gpu <name>` watches one draw, which is the quickest way to see
whether a new program does what you meant.
