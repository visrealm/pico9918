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
    test_gpu_dma            the GPU DMA engine's geometry, from the VHDL
    test_tms9900            the GPU's instruction set, run inside the firmware

The last two are the odd ones: they assert VRAM rather than pixels, and they are the
only things here that run a program on the GPU rather than driving the renderer.
test_gpu_dma is the only thing that reaches the board's DMA trigger, which is an MPU
fault on a device and a software address compare off one; test_tms9900 is the only
thing that executes an instruction inside the running firmware, where core/test/tms9900
reaches the same assembly core only as a UF2 flashed in its place.

Each is runnable alone: `cd core/test && python -m suite.stages.properties.test_d4`.
Each reports through `suite.outcome`, so a failure reads the same whichever ran.
"""
