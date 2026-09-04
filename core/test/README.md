# Tests

Six directories, answering six different questions. Pick by the question, not by whichever one
you ran last.

| Question | Where | What it is | Needs |
|---|---|---|---|
| Does the renderer still draw exactly what it drew before? | [`golden/`](golden/) | A C program that dumps every line of fixed scenes byte for byte | A C compiler |
| Does it draw the right thing across every mode, scene and rule? | [`suite/`](suite/) | 111 scenes, five property suites and two TMS9900 GPU programs | A C compiler and Python |
| Does the post-palette pixel path lay out the way the header says? | [`pixel/`](pixel/) | Both palette LUT layouts and the scanline geometry, at both line widths | A C compiler |
| Has an armed GPU program run by the time the arming write returns? | [`gpu/`](gpu/) | The rate the library paces the GPU from, and that write | A C compiler |
| Is the installed package actually usable? | [`package/`](package/) | A separate project that finds the library with `find_package` and calls it | A C compiler |
| Does the TMS9900 GPU core execute correctly? | [`tms9900/`](tms9900/) | Every instruction group, held to the same expected values | A C compiler; a Pico to also run the assembly cores |

Only the assembly cores need hardware, and nothing here needs a PICO9918. That is the point of the
library being separable: all of this runs on a laptop in seconds, and CI runs the renderer suite on
Linux, macOS and Windows under GCC, Clang, MinGW and MSVC on every push.

## The two that both compare pixels, and why both exist

`golden/` and `suite/` sound like the same gate and are not.

`golden/` is a **closed set, maximum detail**. Sixteen artifacts, a handful of scenes, and for each
line it captures the indexed pixels, the status byte the host would have read, the post-palette
surface, and a read-back of all 64 palette registers. Everything goes through the public bus API -
the same two-stage address-port writes the firmware performs - so it is the gate on register
semantics and the unlock path as much as on pixels. When it fails, it tells you which byte.

`suite/` is **broad coverage, one question per scene**. 111 scenes including VRAM dumps of real
software, swept across two line-width tiers, plus five property suites that compute the answer
independently and sweep the whole input space rather than freezing one picture. When it fails, it
tells you which scene and how many pixels moved.

A change that breaks the renderer usually breaks both. A change that breaks only `golden/` is
usually about the bus or the palette; one that breaks only `suite/` is usually a mode you were not
thinking about.

## What is not here

Anything that measures time, and anything that needs a PICO9918. A scanline's cost is a property of
a device - its clock, its flash, its DMA - so the timing stages live with the firmware that runs on
one, in the firmware repository's `test/live/`. That harness imports [`suite/`](suite/) rather than
keeping a copy, and adds the three stages only a board can answer.
