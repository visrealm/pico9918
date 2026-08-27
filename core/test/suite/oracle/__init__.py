"""The frozen references, and the comparison against them.

A reference is what the renderer drew, kept byte for byte, so that every later
change has to reproduce it or explain itself. Nothing here knows what produced
the pixels - a board and a desktop shim are compared against the same files,
which is the whole reason a reference is worth keeping.

    reference/       the 256-byte line every mode renders
    reference-w512/  the 8bpp 80-column tier, where a line is 512 bytes

A scene a tier does not change keeps one file that both tiers reproduce. A scene
it does change has no file of its own until one is written deliberately, so it
reports a difference rather than freezing whatever the new tier produced.
"""

import os
import zlib

from suite.access.vdp import PIXELS_X

HERE = os.path.dirname(os.path.abspath(__file__))
REFERENCE_DIR = os.path.join(HERE, "reference")


def reference_dir(width):
    """References are keyed on the width of the line that produced them, which is
    what keeps two board tiers from overwriting each other. The 8bpp 80-column
    tier renders 512 bytes a line and every other mode renders 256, so a build
    only ever *writes* its own directory."""
    return REFERENCE_DIR if width == PIXELS_X else "%s-w%d" % (REFERENCE_DIR, width)


def reference(name, width=PIXELS_X):
    """Where a scene's reference is read from: its own width's directory if it has
    one, else the shared one. A scene the tier does not change therefore keeps a
    single reference that both boards reproduce, which is the board-independence
    the oracle rests on; a scene it does change has no file of its own until one
    is written deliberately, so it reports a difference rather than freezing
    whatever the new tier happened to produce."""
    own = os.path.join(reference_dir(width), name + ".bin.z")
    if os.path.exists(own):
        return own
    return os.path.join(REFERENCE_DIR, name + ".bin.z")


def golden(name, rows, pixels, update=False, width=PIXELS_X):
    """Compare a capture against a stored reference, or write one: freeze what the
    renderer does today, then require every refactor to reproduce it byte for byte.

    Returns a dict rather than a verdict, because how a reference moved is worth
    more than that it moved: a re-freeze is reviewed as transitions, and the count
    of differing pixels with the first one located is where that review starts."""
    path = reference(name, width)
    if update or not os.path.exists(path):
        directory = reference_dir(width)
        os.makedirs(directory, exist_ok=True)
        path = os.path.join(directory, name + ".bin.z")
        with open(path, "wb") as f:
            f.write(zlib.compress(pixels, 9))
        return {"ok": True, "wrote": True, "differ": 0, "first": None,
                "why": "wrote %s (%d rows)" % (os.path.basename(path), rows)}
    with open(path, "rb") as f:
        want = zlib.decompress(f.read())
    if want == pixels:
        return {"ok": True, "wrote": False, "differ": 0, "first": None,
                "why": "matches %s" % os.path.basename(name)}
    if len(want) != len(pixels):
        return {"ok": False, "wrote": False, "differ": None, "first": None,
                "why": "size changed: %d -> %d bytes" % (len(want), len(pixels))}
    bad = [i for i in range(len(want)) if want[i] != pixels[i]]
    y, x = divmod(bad[0], width)
    return {"ok": False, "wrote": False, "differ": len(bad),
            "first": {"row": y, "col": x, "want": want[bad[0]], "got": pixels[bad[0]]},
            "why": "%d pixels differ, first at row %d column %d: %d -> %d"
                   % (len(bad), y, x, want[bad[0]], pixels[bad[0]])}
