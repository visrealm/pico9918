"""Property suites: one behaviour swept exhaustively, rather than one picture.

A scene freezes what the renderer drew and asks whether it drew it again. A
property asks a question with an answer that can be computed independently -
every scroll offset, every colour pair, every ECM depth - and sweeps the whole
input space, so it fails on a case nobody thought to draw.

    test_d4                 the fourth data byte a host reads back
    test_text_scroll        horizontal scroll across every offset
    test_text_colour        foreground and background across the palette
    test_text_ecm           ECM depths in text modes
    test_text80_8bpp        the 80-column 8bpp tier's packing

Each is runnable alone: `cd core/test && python -m suite.stages.properties.test_d4`.
Each reports through `suite.outcome`, so a failure reads the same whichever ran.
"""
