"""How a VDP is driven and read back.

`vdp.VdpAccess` is the seam the whole suite is written against: write memory,
wait, read the rows the renderer produced. Nothing above this package knows
whether those three operations crossed a debug probe or a pipe.

    vdp         the seam, and the addresses and register constants it needs
    desktop     an in-process library behind the shim, over a pipe
    backend     which of the two a run opens, from its arguments
    image       PNG encoding, nibble unpacking and the palette, for captures

The board implementation is not here. It needs openocd, an ELF and a probe, so
it lives with the firmware that supplies them - `test/live/live9918.py` in the
firmware repository, subclassing the same `VdpAccess`.
"""
