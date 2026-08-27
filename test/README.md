# Tests

Four directories, answering four different questions. Most wasted effort on this project has come
from reaching for the wrong one - judging pixel alignment from a photograph, or trying to measure
performance with an instrumented build - so pick by the question, not by whatever is already
plugged in.

| Question | Where | What it is |
|---|---|---|
| Did the renderer draw the right pixels, and did every scanline fit? | [`live/`](live/) | A Python harness driving the board over a debug probe |
| What does a scanline cost on a shipping build, driven by a real host? | [`bench/`](bench/) | A cartridge ROM of fixed scenes, read off the diag overlay |
| Does the board talk to a host at all? | [`host/`](host/) | A second Pico pretending to be the host CPU |
| Was this board assembled correctly? | [`qc/`](qc/) | A loopback wiring test, flashed instead of the firmware |

`live/` covers both halves of the first question. It compares pixels against a frozen reference
*and* reads the per-scanline timers, because on this project those are one question: an over-budget
line is skipped rather than drawn late, so a scene that does not fit comes back as pixels that are
wrong. Correctness and headroom are measured in the same pass because they cannot be separated.

The pixel half is not written here, though. The scenes, the references and the seven stages that
assert what the renderer computed belong to the library, at
[`../core/test/suite`](../core/test/suite) - they need no board, so CI runs them under four
compilers on every push. `live/` imports that package and adds the three stages only a device can
answer. [`../core/test/README.md`](../core/test/README.md) routes the library's four gates the same
way this table routes these four.

[`../DEBUGGING.md`](../DEBUGGING.md) is the front door: it says which tool answers which question,
what each one costs, and the traps that have already taken time out of this project.

## What builds with what

`host/`, `qc/` and the library's `tms9900/` are part of the firmware build - `test/CMakeLists.txt`
adds all three, and the root `CMakeLists.txt` adds `test/` in the ordinary firmware configuration.
They produce `pico9918test`, `pico9918qc` and `tms9900_test`: standalone UF2s that **replace** the
firmware on whatever RP2040 they are flashed to. None is built into a shipping image.

`bench/` is a separate CMake project, because it builds Z80 and TMS9900 cartridge ROMs rather than
anything for the RP2040:

```
cmake -S test/bench -B build-bench -G Ninja
cmake --build build-bench
```

`live/` builds nothing. It is Python driving a `PICO9918_LIVE_TEST=ON` firmware build over SWD, and
it is the only one of the four that runs without a person watching a screen.

## Which to reach for

**Start with `live/`.** It is the only tool here that gives an exact answer with no eye involved:
it reads back the index buffer the renderer produced and compares it byte for byte against a frozen
reference, over the whole scene library, in a few minutes. `runner.py` runs the rig and leaves a
record the next run can be compared against.

**`bench/` is for the timing `live/` cannot take.** A live-test build costs about a microsecond a
scanline for the capture copy, so its absolute numbers are inflated - they are a control to compare
against another live-test build, not a figure for a shipping one. `bench/` runs a shipping build on
a real host and shows the numbers on the overlay.

**`host/` and `qc/` are about the board, not the renderer.** Everything in `live/` and most of
`bench/` assumes the host interface works - `live/` writes VDP memory over SWD and bypasses the bus
entirely. These two are where that assumption gets checked. See `HARDWARE.md` - it sits in the
build workspace above this repo, not in it - before changing either. The pin names, the
buffer topology and the bit order are all recorded there, and none of them are guessable from the
firmware source.
