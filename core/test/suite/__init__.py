"""The renderer's test suite: 111 scenes, five properties and a GPU program.

The suite asserts what the library computes and never what it cost. Microseconds
belong to a device, so the firmware repository's runner is what reads them - this
half runs anywhere a C compiler does, which is what makes it a CI gate.

One question, answered five ways:

    access/     how a VDP is driven and read back - the seam that lets one suite
                run against a board over SWD or an in-process shim
    scenes      what to draw: 111 named scenes, plus every VRAM dump in data/dumps
    oracle      the frozen references, and the comparison against them
    stages/     the checks themselves - the seven the renderer owns, and the five
                property suites under stages/properties
    outcome     how a property reports what it found

Two entry points:

    cd core/test && python -m suite.run          the whole suite against a shim
    cd core/test && python -m suite.view         an interactive scene viewer

Every stage is also runnable alone, the same way - `python -m suite.stages.freeze`.

Nothing in here touches `sys.path`. Imports are absolute from `suite`, so a
caller outside the package adds `core/test` to the path once and reaches all of
it, and no module can be shadowed by a same-named one somewhere else.
"""
