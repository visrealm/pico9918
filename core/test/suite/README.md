# The renderer's suite

111 scenes, five property suites and a 22.9 M-instruction TMS9900 GPU program, each compared
against a committed reference. It asserts what the library computes and never what it cost.

## Running it

```
cmake -S test/suite/shim -B build-suite -DCMAKE_C_FLAGS=-O2
cmake --build build-suite

cd test && python -m suite.run
```

`tools/ci.sh suite` does both of the above for both line-width tiers, which is what CI runs. The
commands here are for a single run against a shim you already have.

Narrow it while you work:

```
python -m suite.run gm1-bml-under            one scene, every stage
python -m suite.run --only freeze            one stage, every scene
python -m suite.stages.properties.test_d4    one property, alone
python -m suite.view                         the scenes in a window, at 60 fps
```

Point `LIVE9918_SHIM` at a shim binary to override where it is looked for.

## Layout

```
run.py          the entry point: stages, filters, the verdict
view.py         the scenes in a window, at sixty frames a second
scenes.py       the scene catalogue - one register file and one whole VRAM image each
outcome.py      how a property reports what it found

access/         how a VDP is driven and read back
oracle/         the frozen references, and the comparison against them
stages/         the checks: the seven the renderer owns, and five property suites
data/           what the scenes are made of - the font, the VRAM dumps, the GPU programs
shim/           the C program the desktop backend talks to, and its build
```

Every directory has a docstring in its `__init__.py` saying what belongs in it. That is the
authority; this file is the map.

## The one rule

**Nothing in the package touches `sys.path`.** Imports are absolute from `suite`, which is why
`python -m` is how everything runs and why a caller from outside - the firmware repository's
`test/live/runner.py` - adds `core/test` once and reaches all of it.

## Two tiers, one suite

References are keyed on the width of the line that produced them. Every mode renders a 256-byte
line; the 8bpp 80-column tier renders 512. So a full run is two builds:

```
cmake -S test/suite/shim -B build-suite-w512 -DLIVE_DESKTOP_TEXT80_8BPP=ON
```

A scene the tier does not change keeps one reference in `oracle/reference/` that both tiers
reproduce. A scene it does change gets its own in `oracle/reference-w512/`, written deliberately -
until then it reports a difference rather than freezing whatever the new tier produced.

## Adding to it

- **A scene**: add it to `scenes.py`, run `--only freeze`, and the missing reference is written on
  the first pass. Review it as a picture before committing it.
- **A VRAM dump**: drop the file in `data/dumps/`. It becomes a scene by being there.
- **A property**: a module in `stages/properties/` reporting through `suite.outcome`, and a line in
  `stages/__init__.py`. Properties are for rules with an independently computable answer - if the
  only way to know the answer is to look at last week's picture, it is a scene.
- **A GPU program**: `data/gpu-programs/README.md` covers the format.
