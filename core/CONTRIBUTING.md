# Contributing

This repository is **generated**. The library is developed at `core/` inside
[visrealm/pico9918](https://github.com/visrealm/pico9918) and split out from
there by `tools/split.sh`, so a pull request opened here has nowhere to land: the
next split overwrites it.

That is not bureaucracy. Every scanline this code renders has a time budget on an
RP2040 running at video rate, and the only place a change can be measured against
a real device - and against the goldens, the 111-scene suite and the ARM
disassembly baselines - is the firmware repository. A change that is correct and
20 instructions slower is a regression that only shows up there.

## Where to open things

- **Issues and pull requests:** the [firmware
  repository](https://github.com/visrealm/pico9918/issues). Issues are turned off
  here so there is one tracker rather than two halves of one.
- **A question about using the library** - the API, building it, embedding it in
  an emulator - is welcome there too. It is the same maintainer either way.

## The portability exception

One class of contribution is genuinely easier to make here, and is wanted:
**making the library build and run somewhere it currently does not.** A new
platform header under `src/platform/`, a compiler whose warnings it fails, a
32-bit or big-endian target, a build system that cannot find it.

If that is what you have, open an issue on the firmware repository describing the
target, or send a patch - a diff, a branch, a link to a fork. It will be applied
on the firmware side and reach here through the next split, with attribution. You
do not need the Pico SDK or a device for any of it: `tools/ci.sh` runs the whole
desktop gate, and `test/package/` builds the library the way an outside consumer
does.

## What a change has to clear

Whichever side it is applied on:

    tools/ci.sh goldens     the 16 committed frames, byte-exact
    tools/ci.sh suite       111 scenes, five properties and a GPU program, both line widths
    tools/ci.sh warnings    -Wall -Wextra -Werror
    tools/ci.sh multi       the instance threaded through every signature
    tools/ci.sh tms9918     PICO9918_MODE=0, its frame against the F18A build's
    tools/ci.sh package     install it, then find_package it from a separate project

A change that touches the renderer additionally has to leave the firmware's ARM
images where they were, or explain the difference: `tools/capture-baselines.sh` in
the firmware repository disassembles the hot functions and counts their
instructions, and the numbers are committed.

## Style

`.clang-format` is in the repository and is the answer to formatting questions.
Two things it cannot express, both of which matter here:

- **The hot path takes no new branches.** One branch to route to a separate
  implementation is fine; a condition per feature scattered through the
  per-scanline loop is not.
- **Comments state what is not evident from the code, and nothing about how the
  code used to be.** A comment describing a change reads as noise the moment the
  change is old.
