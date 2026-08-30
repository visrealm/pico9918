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
| `cube.bin` | `>3200` | pico9918-core | A solid cube turning under a fixed light, on the F18A's 2bpp bitmap layer. Two bits a pixel is four colours and a cube shows at most three faces, so opposite faces share one and the bitmap records only WHICH face a pixel belongs to - the shading is three words of palette RAM, rewritten every frame. Two bitmaps, paged by VR32, and the page and the palette both change in the vertical blank - so it waits on the scanline counter at `>7000`, and a host that does not advance the raster while it runs will hang rather than draw the wrong thing. One full turn in 256 frames, then `IDLE`. The angle is carried finer than the sine table is, so how smoothly it turns owes nothing to how big that table is. 1,294 bytes. |
| `mandel.bin` | `>1B02` | **Tursi** | The Mandelbrot set over x -2.0..+0.5, y +1.25..-1.25, 14 iterations in Q13 fixed point, drawn in Graphics II. Sets VR0-VR7 itself, builds its own name table, draws 49,152 pixels and halts on `IDLE`. 22,899,808 instructions. |

**`mandel.bin` is Tursi's program.** It is here as a test fixture, with credit and
with thanks - it is a real F18A GPU program written by someone who was not thinking
about our renderer, which is exactly what makes it worth testing against.

**`cube.bin` covers what Mandelbrot cannot.** Mandelbrot is worth testing against
precisely because it is somebody else's program - but it draws a Graphics II bitmap
and nothing else, so the F18A's own drawing hardware goes untouched by it. The cube
drives the bitmap layer, pages VR32 between two of them, rewrites palette RAM from
inside the GPU every frame, and waits on the scanline counter before it does either -
so it is a fixture for the HOST as much as for the renderer: a host that runs a program
without advancing the raster hangs here rather than quietly drawing the wrong thing.
It also fails legibly: a face in the wrong place is the projection, a face visible that
should not be is the back-face test, and colours that stop moving are the PRAM writes.

`cube.a99` is written to be read, and says why each piece is what it is - the signed
Q14 multiply, the span buffers a convex face needs instead of a depth sort, and the
sizing rule that decides how large the cube can be before it runs off the buffer.

    xas99.py -b -R -18 -a '>3200' cube.a99 -o cube.bin

is how `cube.bin` was assembled, with xdt99. Like `mandel.a99` below it, no build
here assembles it: the binary is committed and its source sits beside it.

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
harness, using that same byte. And it must draw the same picture every time.

That second one is about what a program does with `>7000`, not about whether it reads
it. The cube waits on it every frame and is still deterministic, because it only ever
decides WHEN to page a bitmap, never what to put in one - it draws its 192 frames
whatever the raster does, and the last one is the same picture every run. A program
whose pixels depend on where the raster happened to be draws something different each
time and cannot be frozen.

`python view.py --gpu <name>` watches one draw, which is the quickest way to see
whether a new program does what you meant.
