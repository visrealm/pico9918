#!/usr/bin/env python3
"""The scene library: one register file and one whole VRAM image per scene.

A scene is pure data. `build(name)` returns `(registers, vram, unlocked)` and
touches no hardware, so scenes can be inspected and diffed without a board.
`apply()` writes all 64 registers with the display off, then all 16KB of VRAM,
then turns the display on - in full, every time, so a scene can inherit nothing
from the one before it and a golden is a property of the firmware alone.

Content is text, drawn with the font the configurator already ships. Every row
says which layer, which page and which row it is, and then runs a ruler that
steps one column per row:

    1A05 56789 BCDEF0 234567 9ABCDE
    2A05 BA987 543210 EDCBA9 76543

so a scroll, a page swap or an off-by-one row is something a person can read off
a screenshot rather than measure - the label changes, or the diagonal kinks. It
is also as sensitive as noise to the failures that matter here, because every
neighbouring cell differs: an address generator that is off by one tile, one row
or one page changes bytes either way.

**A golden is not automatically the correct answer.** Every scene declares
itself either

  * frozen      - the feature combination works today, so any change is a
                  regression and the refactor has to reproduce it byte for byte
  * provisional - the scene exercises something the hardware does and this
                  firmware does not yet, so its capture records an absence. When
                  the feature lands the golden *must* change, and re-freezing it
                  is how that change gets reviewed rather than missed.

The provisional set makes the feature matrix executable. See README.md.
"""

import collections
import re
import os
import sys

from suite.access.vdp import VRAM_REGISTERS

VRAM_SIZE = 0x4000
# config.h's own indices for the diagnostic overlay. The performance panel is the
# one perf.py's numbers come from; CONF_DIAG_PANELS is all five, which is what the
# firmware turns on itself once the splash is done - and what has to be *cleared*,
# because clearing only the master flag left the four panel flags at whatever
# stored config held.
PICO9918_CONF_DIAG = 16
PICO9918_CONF_DIAG_REGISTERS = 17
PICO9918_CONF_DIAG_PERFORMANCE = 18
PICO9918_CONF_DIAG_PALETTE = 19
PICO9918_CONF_DIAG_ADDRESS = 20
CONF_DIAG_PANELS = (PICO9918_CONF_DIAG, PICO9918_CONF_DIAG_REGISTERS, PICO9918_CONF_DIAG_PERFORMANCE,
                    PICO9918_CONF_DIAG_PALETTE, PICO9918_CONF_DIAG_ADDRESS)

# Layout shared by GM1, MCM, T40 and T80. Name tables sit in 4KB blocks because
# the vertical page swap is `addr ^ 0x800` and the horizontal one `addr ^ 0x400`,
# so all four pages of a layer have to be free. Sprite patterns need a 2KB
# boundary and the sprite attribute table a 128-byte one.
PATT = 0x0000        # ECM planes 2 and 3 follow at +ECM_K and +2*ECM_K
ECM_K = 0x0200       # 64 tiles per plane, set by R0x1d bits 3:2 and 7:6
SPR_PATT = 0x0800
SPR_ATTR = 0x0C00
BML = 0x0E00         # 512 bytes, which is all that fits below the name tables
NAME1 = 0x1000       # +0x400 horizontal page, ^0x800 vertical page
NAME2 = 0x2000
COLOR1 = 0x3000
COLOR2 = 0x3800

# A name table whose own base carries both scroll page bits. Position attributes add
# those to the colour table base rather than writing over its, so the attributes for
# this one live 0xC00 further on. PAGED_LOST is where forcing the bits instead of
# adding them puts the read: every other name table here sits on 0x1000 or 0x2000,
# where the two agree and nothing shows.
NAME_PAGED = 0x1C00
PAGED_ATTR = COLOR1 + 0xC00
PAGED_LOST = COLOR1 + 0x400

# R0 bit 3 renders every VGA line instead of doubling each one, so 24 and 30 rows
# of cells become 48 and 60. That is this board's mode, not the F18A's.
R0_DOUBLE_ROWS = 0x08

# GM2 needs 6KB of patterns and 6KB of colours at fixed bases, so it has its own. Its second layer
# has nowhere else to put a colour table - the GM2 base is one bit, so both layers share 0x2000 -
# and only its name table is separate.
NAME_G2 = 0x1800
SPR_ATTR_G2 = 0x1C00
BML_G2 = 0x1E00
COLOR_G2 = 0x2000
SPR_PATT_G2 = 0x3800
NAME2_G2 = 0x3C00

NUM_TILES = 64       # names stay under ECM_K / 8 so all three planes are addressable
BACKDROP = 4         # dark blue: nothing else in the library draws it

# a 64x32 bitmap at (8, 32), which is 512 bytes at 2bpp - the largest that fits
BML_GEOM = dict(r21=8, r22=32, r23=64, r24=32)
# and the full-width case, where the layer covers the line and the tiles are
# skipped entirely by bitmap_layer_scan_line's return
BML_FULL = dict(r21=0, r22=64, r23=0, r24=8)


FONT = None
RULER = "0123456789ABCDEF"
PUNCTUATION = " !\"#$%&'()*+,-./"     # names 0-15, exactly the range attrs_ecm flags


def font():
    """Stimulus: 96 glyphs from ASCII 32, so a tile index is `ord(c) - 32` and
    every name stays under 64 - which is what lets the ECM planes sit 512 bytes
    apart and still address every glyph.

    A copy of the font the firmware's configurator ships, and deliberately its
    own file: what a scene needs is 96 legible 8x8 glyphs, not that particular
    font, so the suite carries its own rather than reaching into a repository it
    may not be inside."""
    global FONT
    if FONT is None:
        here = os.path.dirname(os.path.abspath(__file__))
        path = os.path.join(here, "data", "font.bin")
        with open(path, "rb") as f:
            FONT = f.read()
        if len(FONT) < NUM_TILES * 8:
            raise RuntimeError("%s is %d bytes, expected a 96-glyph font" % (path, len(FONT)))
    return FONT


def glyphs(tiles=NUM_TILES):
    return bytearray(font()[:tiles * 8])


def ecm_planes():
    """Planes 2 and 3 as a shadow one row up and the glyph's core columns, so an
    ECM scene still reads as text - in three colours, with the plane a pixel came
    from visible."""
    g = glyphs()
    p2, p3 = bytearray(len(g)), bytearray(len(g))
    for t in range(NUM_TILES):
        for r in range(8):
            p2[t * 8 + r] = g[t * 8 + r - 1] if r else 0
            p3[t * 8 + r] = g[t * 8 + r] & 0x3C
    return p2, p3


def chars(text, cols):
    """Text to tile indices, padded or clipped to one row."""
    text = (text.upper() + " " * cols)[:cols]
    return bytearray((ord(c) - 32) & 0x3F for c in text)


def name_page(cols, rows, tag, direction=1):
    """Every row says which layer, which page and which row it is, then runs a
    ruler that steps one column per row. A scroll, a page swap or an off-by-one
    row is then something a person can read off the screen rather than measure:
    the label changes, or the diagonal kinks. A seventh of the cells are blank,
    scattered, so the empty-tile skips still fire.

    The last four rows are punctuation, which is the only range the ECM
    attributes give flip, transparency and priority bits to - so those features
    show up in a band of their own instead of garbling the text.
    """
    out = bytearray()
    for row in range(rows):
        if row >= rows - 4:
            out += chars("".join(PUNCTUATION[(x + row) & 0x0F] for x in range(cols)), cols)
            continue
        head = "%s%02X " % (tag, row)
        body = "".join(" " if (x + row) % 7 == 3 else RULER[(x * direction + row) & 0x0F]
                       for x in range(cols - len(head)))
        out += chars(head + body, cols)
    return out


def colors_ecm0(transparent=False):
    """Eight entries, one per group of eight glyph names, so punctuation, digits
    and letters each get their own. Three have a transparent background even on
    layer 1, so the backdrop and the bitmap layer show through part of the text.

    Layer 2 takes `transparent`, which clears every background nibble. That is
    not decoration: an opaque layer 2 covers the screen, and since it outranks
    both layer 1 and a priority bitmap layer, every two-layer scene
    would then be a picture of layer 2 alone and would test nothing else."""
    c = bytearray([0xF4, 0xE0, 0xB1, 0xB0, 0x71, 0x30, 0x91, 0xF1])
    return bytearray(b & 0xF0 for b in c) if transparent else c


def attrs_ecm(transparent=False):
    """One byte per name. The palette select steps every eight glyphs, and the
    flag bits go only to the punctuation names - so the text stays upright and
    readable while the bottom band shows every combination of priority, flipX,
    flipY and transparent. Layer 2 sets the transparency bit throughout, for the
    reason in colors_ecm0."""
    a = bytearray(NUM_TILES)
    for i in range(NUM_TILES):
        a[i] = (((i & 0x0F) << 4) if i < 16 else 0) | ((i >> 3) & 0x0F)
        if transparent:
            a[i] |= 0x10
    return a


def colors_per_cell(cols, rows, first=1):
    """A colour byte per cell for the text modes, whose colour byte is a plain
    fg/bg pair. The foreground steps in diagonal bands and the background stays
    transparent, so the backdrop shows between the glyphs."""
    out = bytearray()
    for row in range(rows):
        for x in range(cols):
            out.append(((first + x // 5 + row // 4) % 15 + 1) << 4)
    return out


def attrs_per_cell(cols, rows, first=1, transparent=False):
    """The same idea for the tile layers, whose per-position byte is an ECM
    attribute rather than a colour pair: palette in the low nibble, and the flag
    bits confined to the bottom four rows so the text above stays upright."""
    out = bytearray()
    for row in range(rows):
        for x in range(cols):
            flags = ((x & 0x0F) << 4) if row >= rows - 4 else 0
            out.append(flags | (0x10 if transparent else 0)
                       | ((first + x // 5 + row // 4) % 15 + 1))
    return out


def text_attrs(cols, rows, first, t2, ecm, per_cell):
    """A text colour table in whichever of its three forms the scene wants: an
    fg/bg pair per cell at ECM0, an ECM attribute per name above it, or an ECM
    attribute per cell when position attributes are on."""
    if not ecm:
        return colors_per_cell(cols, rows, first)
    return attrs_per_cell(cols, rows, first, t2) if per_cell else attrs_ecm(t2)


SPRITE_ART = (
    "       ##       "     # diamond
    "      ####      "
    "     ##  ##     "
    "    ##    ##    "
    "   ##      ##   "
    "  ##        ##  "
    " ##          ## "
    "##            ##"
    "##            ##"
    " ##          ## "
    "  ##        ##  "
    "   ##      ##   "
    "    ##    ##    "
    "     ##  ##     "
    "      ####      "
    "       ##       ",

    "  ############  "     # ring
    " ##          ## "
    "##            ##"
    "##    ####    ##"
    "##   ##  ##   ##"
    "##  ##    ##  ##"
    "##  ##    ##  ##"
    "##  ##    ##  ##"
    "##  ##    ##  ##"
    "##  ##    ##  ##"
    "##  ##    ##  ##"
    "##   ##  ##   ##"
    "##    ####    ##"
    "##            ##"
    " ##          ## "
    "  ############  ",

    "##            ##"     # cross
    " ##          ## "
    "  ##        ##  "
    "   ##      ##   "
    "    ##    ##    "
    "     ##  ##     "
    "      ####      "
    "       ##       "
    "       ##       "
    "      ####      "
    "     ##  ##     "
    "    ##    ##    "
    "   ##      ##   "
    "  ##        ##  "
    " ##          ## "
    "##            ##",

    "################"     # framed bars
    "##            ##"
    "##            ##"
    "##            ##"
    "##   ######   ##"
    "##   ######   ##"
    "##            ##"
    "##            ##"
    "##            ##"
    "##            ##"
    "##   ######   ##"
    "##   ######   ##"
    "##            ##"
    "##            ##"
    "##            ##"
    "################",
)


def sprite_patterns():
    """Four 16x16 shapes, in the quadrant order the VDP reads them: left half
    rows 0-7, left half rows 8-15, then the right half. Recognisable shapes, so a
    sprite in the wrong place or under the wrong layer is obvious."""
    out = bytearray()
    for art in SPRITE_ART:
        rows = [art[i * 16:(i + 1) * 16] for i in range(16)]
        for half in (0, 8):
            for top in (0, 8):
                for r in range(top, top + 8):
                    bits = 0
                    for c in range(8):
                        if rows[r][half + c] != " ":
                            bits |= 0x80 >> c
                    out.append(bits)
    return out


def sprite_attrs():
    """Two clusters of four. Inside a cluster the sprites overlap in y, so four
    share scanlines and the per-line limit, the collision flag and the priority
    order all get exercised; the clusters sit in different parts of the screen so
    the tiles underneath stay visible. Two of each four carry the early-clock
    bit, and only the four shapes above exist, so the names cycle."""
    out = bytearray()
    for i in range(8):
        cluster, n = divmod(i, 4)
        out += bytes([30 + 80 * cluster + 5 * n,          # y
                      24 + 40 * cluster + 26 * n,         # x
                      n * 4,                              # name: 16x16 takes four patterns
                      (0x80 if n & 2 else 0) | (8 + i)])
    out += bytes([0xD0])
    return out


def sprite_grid(count=32):
    """Sprites tiling bands across the screen with no overlap in x, so every one
    of them draws all of its pixels rather than being masked by its neighbour.
    Total emitted work is what a per-frame average measures - a sprite covers its
    own 16 scanlines, or 32 magnified, wherever it sits - so the full 32 is the
    densest ECM sprite instrument the hardware allows: 2048 quads a frame against
    512 for the eight of `sprite_attrs`. Magnified, 32 of them do not fit 63.6 us
    at 252 MHz, so that scene halves the count for the same 2048.

    Colours step per sprite. At ECM3 the attribute's colour nibble supplies
    palette bits 5:3 and the three planes the bottom three, so each sprite lands
    in its own eighth of the palette and a plane read from the wrong place shows
    as a colour, not just a shape. The first sprite starts at y=2 because an
    all-zero attribute is skipped deliberately (pico9918.c:895)."""
    out = bytearray()
    for i in range(count):
        band, col = divmod(i, 8)
        out += bytes([2 + band * 48, col * 32, (i & 3) * 4, i & 0x0F])
    return out + bytes([0xD0])


def sprite_ecm_planes():
    """Planes 2 and 3 for the sprite patterns, which are the same four shapes
    rotated by one and two. Every pixel of the union then has a different
    three-bit value, and the union is larger than any one shape - so more pixels
    survive the transparency mask than a single-plane sprite would give."""
    patt = sprite_patterns()
    shape = len(patt) // 4
    return patt[shape:] + patt[:shape], patt[2 * shape:] + patt[:2 * shape]


def bitmap(width_bytes, rows):
    """A framed box with a diagonal, at 2bpp - four pixels a byte. A rectangle is
    the one shape whose position, size and priority can all be read off a
    photograph. Part of the interior is left transparent, for bml_trans."""
    out = bytearray(width_bytes * rows)
    px = width_bytes * 4
    for y in range(rows):
        for x in range(px):
            edge = x < 2 or x >= px - 2 or y < 2 or y >= rows - 2
            c = 3 if edge else (1 if (x * rows // px) == y else 2)
            if c == 2 and (x + y) & 8:
                c = 0
            out[y * width_bytes + x // 4] |= c << (6 - 2 * (x & 3))
    return out


def mcm_patterns():
    """Multicolor reads the pattern byte as two 4-bit colours, so the pattern
    table is a ramp: name N paints colour N+r on the left and N+r+1 on the right,
    where r is the row within the cell. The name table draws the picture."""
    out = bytearray(256 * 8)
    for n in range(256):
        for r in range(8):
            out[n * 8 + r] = (((n + r) & 0x0F) << 4) | ((n + r + 1) & 0x0F)
    return out


def mcm_names():
    """A diamond gradient inside a frame. Smooth enough that one wrong row or
    column is visible, which a field of noise is not."""
    out = bytearray()
    for y in range(24):
        for x in range(32):
            edge = x == 0 or x == 31 or y == 0 or y == 23
            out.append(0 if edge else (abs(x - 16) + abs(y - 12)) & 0x0F)
    return out


def text_color1(cols, rows):
    """Layer 1's colour table, which sits at COLOR1 until layer 1's name table has
    grown past it: 80 columns by 60 rows is 4800 bytes a table and the shared
    layout leaves 4096. A doubled-row scene has no second layer - there is no room
    for one - so the space comes from where NAME2 would have been."""
    return COLOR1 if cols * rows <= NAME2 - NAME1 else NAME1 + 0x1400


def blank_vram():
    return bytearray(VRAM_SIZE)


def place(vram, addr, data):
    vram[addr:addr + len(data)] = data


# ---- register files -------------------------------------------------------

def tile_regs():
    """Every register the renderer reads, for the shared layout. Anything left
    at zero is left at zero deliberately - the whole file is written, so a scene
    can never inherit a register from the one before it."""
    r = bytearray(64)
    r[0x01] = 0xE2                       # 16K, display on, interrupts, 16x16 sprites
    r[0x02] = NAME1 >> 10
    r[0x03] = COLOR1 >> 6
    r[0x04] = PATT >> 11
    r[0x05] = SPR_ATTR >> 7
    r[0x06] = SPR_PATT >> 11
    r[0x07] = 0xF0 | BACKDROP            # text fg in the high nibble, backdrop in the low
    r[0x0A] = NAME2 >> 10
    r[0x0B] = COLOR2 >> 6
    r[0x1D] = 0x88                       # ECM plane stride 512, tiles and sprites
    r[0x1E] = 31                         # sprites per scanline
    r[0x20] = BML >> 6
    r[0x33] = 32                         # sprites to process
    return r


def gm2_regs():
    r = tile_regs()
    r[0x00] = 0x02                       # graphics II
    r[0x02] = NAME_G2 >> 10
    r[0x03] = 0xFF                       # colours at 0x2000, name mask 0xff, colour follows page
    r[0x04] = 0x03                       # patterns at 0x0000, all three pages
    r[0x05] = SPR_ATTR_G2 >> 7
    r[0x06] = SPR_PATT_G2 >> 11
    r[0x20] = BML_G2 >> 6
    return r


# ---- content sets ---------------------------------------------------------

def tile_vram(per_cell=False):
    """Patterns, both name tables with all four pages labelled, both colour
    tables, sprites and a bitmap layer. Each page carries its own tag - 1A, 1B,
    1C, 1D for layer 1's four pages - so a scroll that lands on the wrong one
    says so in the text.

    The colour table is indexed three different ways and cannot serve all of
    them at once: by `name >> 3` in ECM0, by `name` in ECM1-3, and by cell
    position when R0x32 bit 1 is set. The first two share a table, since ECM0
    only ever reads the first eight bytes; position attributes need their own,
    which is what `per_cell` selects."""
    v = blank_vram()
    plane2, plane3 = ecm_planes()
    place(v, PATT, glyphs())
    place(v, PATT + ECM_K, plane2)
    place(v, PATT + 2 * ECM_K, plane3)

    for page, tag in enumerate(("1A", "1B", "1C", "1D")):
        place(v, NAME1 + page * 0x400, name_page(32, 24, tag))
    for page, tag in enumerate(("2A", "2B", "2C", "2D")):
        place(v, NAME2 + page * 0x400, name_page(32, 24, tag, direction=-1))

    for base, first, t2 in ((COLOR1, 1, False), (COLOR2, 8, True)):
        for page in (0, 0x400):
            place(v, base + page,
                  attrs_per_cell(32, 24, first, t2) if per_cell
                  else colors_ecm0(t2) + attrs_ecm(t2)[8:])

    place(v, SPR_PATT, sprite_patterns())
    place(v, SPR_ATTR, sprite_attrs())
    place(v, BML, bitmap(16, 32))
    return v


def gm2_vram():
    """Each third gets the glyphs treated differently - plain, inverted, boxed -
    and its own colour ramp, so which third a row came from is readable."""
    v = blank_vram()
    g = glyphs(96)
    for third in range(3):
        page = bytearray(2048)
        for t in range(96):
            for r in range(8):
                b = g[t * 8 + r]
                page[t * 8 + r] = b if third == 0 else (b ^ 0xFF if third == 1 else b | 0x81)
        place(v, PATT + third * 0x800, page)

        colors = bytearray(2048)
        for t in range(256):
            for r in range(8):
                colors[t * 8 + r] = ((1 + (r + third * 3 + t // 16) % 15) << 4) | (third * 4)
        place(v, COLOR_G2 + third * 0x800, colors)

    place(v, NAME_G2, name_page(32, 24, "G2"))
    place(v, NAME2_G2, name_page(32, 24, "2G", direction=-1))
    place(v, SPR_PATT_G2, sprite_patterns())
    place(v, SPR_ATTR_G2, sprite_attrs())
    place(v, BML_G2, bitmap(16, 32))
    return v


def mcm_vram():
    """Multicolor gets its own tables: the pattern table is a colour ramp and the
    name table is the picture. Sharing GM1's would paint glyph bits as colour
    pairs, which is neither legible nor a fair test of the address."""
    v = blank_vram()
    place(v, PATT, mcm_patterns())
    place(v, NAME1, mcm_names())
    place(v, NAME1 + 0x400, mcm_names())
    place(v, NAME1 + 0x800, mcm_names())
    place(v, NAME1 + 0xC00, mcm_names())
    place(v, NAME2, name_page(32, 24, "2A", direction=-1))
    place(v, COLOR2, colors_ecm0(True) + attrs_ecm(True)[8:])
    place(v, SPR_PATT, sprite_patterns())
    place(v, SPR_ATTR, sprite_attrs())
    place(v, BML, bitmap(16, 32))
    return v


def text_vram(cols, rows=24, sparse_t2=False, ecm=False, per_cell=False):
    """Text modes address their tables by row * cols, so they need their own
    content rather than the 32-column tables. At 80 columns by 30 rows layer 1's
    tables are 2400 bytes each, which is why the second layer is only laid out
    for the 24-row scenes.

    `ecm` swaps the colour tables from fg/bg pairs to ECM attributes, which is
    the whole difference in the data: above ECM0 a text cell's byte carries
    priority, both flips, transparency and a sub-palette instead of two colours,
    and is indexed by name unless `per_cell` puts it under the cell position.
    The planes go in either way, as they do for the tile layers - ECM0 never
    reads them, so a scene that stays at ECM0 cannot notice."""
    v = blank_vram()
    plane2, plane3 = ecm_planes()
    place(v, PATT, glyphs())
    place(v, PATT + ECM_K, plane2)
    place(v, PATT + 2 * ECM_K, plane3)
    place(v, NAME1, name_page(cols, rows, "1A"))
    place(v, text_color1(cols, rows), text_attrs(cols, rows, 1, False, ecm, per_cell))
    if rows == 24:
        # the vertical page swap's partner. At 80 columns by 30 rows the name table is 2400 bytes
        # and runs into it, so a scrolling page swap is not expressible there at all
        place(v, NAME1 + 0x800, name_page(cols, rows, "1C"))
    if rows == 24:
        n2 = name_page(cols, rows, "2A", direction=-1)
        if sparse_t2:
            for i in range(len(n2)):
                if i % 5:
                    n2[i] = 0                    # mostly blank, so T2 skips cell groups
        place(v, NAME2, n2)
        place(v, COLOR2, text_attrs(cols, rows, 8, True, ecm, per_cell))
    place(v, SPR_PATT, sprite_patterns())
    place(v, SPR_ATTR, sprite_attrs())
    place(v, BML, bitmap(16, 32))
    return v


# ---- the registry ---------------------------------------------------------

SCENES = {}
Scene = collections.namedtuple("Scene", "fn note changes base overbudget")


def scene(name, note, changes=None, base=None, overbudget=()):
    """`changes` names what will legitimately alter this capture. A scene with
    one is provisional: it records an absence, not a behaviour. `base` is the
    scene it should be identical to while the feature is missing, which is what
    `freeze.py --audit` checks - so the absence is asserted rather than assumed,
    and the day it stops holding is the day the feature arrived.

    `overbudget` names the boards this scene is known not to fit on today. The
    drop is still measured and still printed; it is the *verdict* that treats it
    as a standing fact rather than a new failure, the same way the runner already
    treats a drop with the overlay on. A scene that stops dropping is reported
    too - that is the day the marker comes off."""
    def register(fn):
        SCENES[name] = Scene(fn, note, changes, base, tuple(overbudget))
        return fn
    return register


def sprites_off(v):
    """The sprite list ends immediately, so a scene that is not about sprites
    has none."""
    v = bytearray(v)
    place(v, SPR_ATTR, bytes([0xD0]))
    return v


def left_edge_sprites(v):
    """A column of solid sprites flush against x = 0, and nothing else.

    Nothing else here puts a sprite in the first tile column: the four shapes are
    drawings with blank edges and the nearest cluster starts at x = 24. Solid, so
    a tile that should cover one has no transparent pixel to hide behind, and on
    its own, because the clusters cost the scanlines they overlap on and every
    other sprite arrangement is somebody else's scene. The pattern takes the name
    after the four shapes, which leaves their table where it was."""
    v = bytearray(v)
    solid = len(sprite_patterns()) // 8
    place(v, SPR_PATT + solid * 8, bytes([0xFF] * 32))
    attrs = bytearray()
    for i in range(6):
        attrs += bytes([24 + 28 * i, 0, solid, 0x0F])
    place(v, SPR_ATTR, attrs + bytes([0xD0]))
    return v


def apply_regs(r, regs):
    for k, val in regs.items():
        r[int(k[1:], 16)] = val
    return r


# ---- GM1 - every feature here works today, so all of it is frozen ---------

def gm1(ecm=0, tile2=False, sprites=False, **regs):
    r = tile_regs()
    r[0x31] = (ecm << 4) | (0x80 if tile2 else 0)
    apply_regs(r, regs)
    v = tile_vram(per_cell=bool(r[0x32] & 0x02))
    return r, (v if sprites else sprites_off(v)), True


@scene("gm1-locked", "plain TMS9918 Graphics I - renderTileRowLocked, no F18A anything")
def _():
    return tile_regs(), tile_vram(), False


@scene("gm1-ecm0", "one tile layer, the ECM0 emitter")
def _():
    return gm1()


@scene("gm1-ecm1", "one tile layer, one bitplane")
def _():
    return gm1(ecm=1)


@scene("gm1-ecm2", "one tile layer, two bitplanes")
def _():
    return gm1(ecm=2)


@scene("gm1-ecm3", "one tile layer, three bitplanes")
def _():
    return gm1(ecm=3)


@scene("gm1-t2-ecm0", "both tile layers, ECM0 - the selection mask and the composite")
def _():
    return gm1(tile2=True)


@scene("gm1-t2-ecm1", "both tile layers, ECM1")
def _():
    return gm1(ecm=1, tile2=True)


@scene("gm1-t2-ecm2", "both tile layers, ECM2")
def _():
    return gm1(ecm=2, tile2=True)


@scene("gm1-t2-ecm3", "both tile layers, ECM3")
def _():
    return gm1(ecm=3, tile2=True)


@scene("gm1-t1-off", "layer 1 disabled with layer 2 on - D4's second case")
def _():
    return gm1(tile2=True, r32=0x10)


@scene("gm1-layers-off", "both tile layers disabled - only the backdrop may remain")
def _():
    return gm1(r32=0x10)


@scene("gm1-hscroll-3", "layer 1 fine scroll 3")
def _():
    return gm1(r1b=3)


@scene("gm1-hscroll-7", "layer 1 fine scroll 7 - a whole cell bar one pixel")
def _():
    return gm1(r1b=7)


@scene("gm1-hscroll-split", "layers scrolled by different amounts - tmsCopyAlignMask")
def _():
    return gm1(tile2=True, r1b=3, r19=7)


@scene("gm1-coarse", "coarse scroll to cell 20, single name page")
def _():
    return gm1(r1b=20 << 3)


@scene("gm1-coarse-hpage", "coarse scroll past the end with the horizontal page swap on")
def _():
    return gm1(r1b=(20 << 3) | 3, r1d=0x88 | 0x02)


@scene("gm1-vscroll", "layer 1 vertical scroll 100, no page swap")
def _():
    return gm1(r1c=100)


@scene("gm1-vscroll-page", "vertical scroll wrapping into the other name page")
def _():
    return gm1(r1c=100, r1d=0x88 | 0x01)


@scene("gm1-t2-vscroll-page", "both layers scrolled vertically into their other pages")
def _():
    return gm1(tile2=True, r1c=100, r1a=60, r1d=0x88 | 0x11)


@scene("gm1-t2-hscroll", "layer 2 scrolled across and layer 1 not - the only arrangement where "
                        "the second layer's own scroll is what places it, with nothing on the "
                        "first to move with it")
def _():
    return gm1(tile2=True, r19=11)


@scene("gm1-t2-vscroll", "layer 2 scrolled down and layer 1 not")
def _():
    return gm1(tile2=True, r1a=37)


@scene("gm1-scroll-cross", "layer 1 across, layer 2 down - the two layers on different axes, "
                           "which no scene reached and one real dump does")
def _():
    return gm1(tile2=True, r1b=163, r1a=37)


@scene("gm1-scroll-cross-back", "layer 1 down, layer 2 across - the other way round, because the "
                                "two layers take their scroll from different registers and a "
                                "shared address generator")
def _():
    return gm1(tile2=True, r1c=100, r19=11)


@scene("gm1-scroll-both", "both layers scrolled in both axes, all four registers different - the "
                          "case where a scroll taken from the wrong layer or the wrong axis has "
                          "nowhere to hide")
def _():
    return gm1(tile2=True, r1b=163, r1c=100, r19=11, r1a=37)


@scene("t40-scroll-cross", "40-column text with layer 1 across and layer 2 down - the six-pixel "
                           "cell has its own scroll arithmetic and its own emitter")
def _():
    return text(40, r31=0x80, r32=0x02, r1b=163, r1a=37)


@scene("t80-scroll-cross", "80-column text with layer 1 across and layer 2 down")
def _():
    return text(80, r31=0x80, r32=0x02, r1b=163, r1a=37)


@scene("gm1-posattr", "attributes by position rather than by name, ECM1")
def _():
    return gm1(ecm=1, tile2=True, r32=0x02)


@scene("gm1-posattr-pages", "position attributes read from a name table whose base already carries "
                            "the page bits. They are added to the colour table base, so the "
                            "attributes are 0xC00 along from it - and a differently coloured table "
                            "sits where forcing the bits instead of adding them would read. Real "
                            "software puts its name table there; nothing else here does")
def _():
    r, v, unlocked = gm1(ecm=1, r02=NAME_PAGED >> 10, r32=0x02)
    v = bytearray(v)
    place(v, NAME_PAGED, name_page(32, 24, "1A"))
    place(v, PAGED_ATTR, attrs_per_cell(32, 24, 1))
    place(v, PAGED_LOST, attrs_per_cell(32, 24, 8))
    return r, v, unlocked


@scene("gm1-priority", "ECM2 priority tiles over sprites - attribute bit 7")
def _():
    return gm1(ecm=2, tile2=True, sprites=True)


@scene("gm1-priority-scroll", "priority tiles over sprites with both layers finely scrolled by "
                              "different amounts. A scrolled row starts its first tile left of "
                              "the screen, and the sprite mask a priority tile clears is in "
                              "screen pixels - so that tile is the one case the two disagree on")
def _():
    r, v, unlocked = gm1(ecm=2, tile2=True, sprites=True, r1b=4, r19=4)
    return r, left_edge_sprites(v), unlocked


@scene("gm1-sprites", "sprites over both tile layers, ECM0")
def _():
    return gm1(tile2=True, sprites=True)


@scene("gm1-palette", "palette select for layer 1, layer 2 and sprites at once")
def _():
    return gm1(tile2=True, sprites=True, r18=0x3F)


# ---- ECM sprites, which nothing committed reached before -------------------
#
# Sprite ECM is R0x31 bits 1:0, its own field, and until these scenes only the
# gitignored JS99er dumps exercised it - so the emitter had no coverage anyone
# else could run. Each level reads a different number of planes, and the sprite
# plane stride is R0x1d bits 7:6 rather than the tile one at 3:2: 256 bytes here,
# which puts planes 2 and 3 at 0x900 and 0xA00, clear of the attribute table.

SPR_ECM = dict(r1d=0xC8, r33=32, r1e=31)


def sprite_ecm(level, mag=False, count=32, **regs):
    r = tile_regs()
    r[0x31] = level
    if mag: r[0x01] |= 0x01
    apply_regs(r, dict(SPR_ECM, **regs))
    v = tile_vram()
    plane2, plane3 = sprite_ecm_planes()
    place(v, SPR_PATT + 0x100, plane2)
    place(v, SPR_PATT + 0x200, plane3)
    place(v, SPR_ATTR, sprite_grid(count))
    return r, v, True


@scene("gm1-sprites-ecm1", "32 ECM1 sprites, one bit plane - the emitter's cheapest index")
def _():
    return sprite_ecm(1)


@scene("gm1-sprites-ecm2", "32 ECM2 sprites, two bit planes")
def _():
    return sprite_ecm(2)


@scene("gm1-sprites-ecm3", "32 ECM3 sprites, three bit planes - 2048 quads a frame, which is the "
                           "densest the 32-sprite limit allows")
def _():
    return sprite_ecm(3)


@scene("gm1-sprites-ecm3-mag", "16 magnified ECM3 sprites, which is the other ECM emit loop: four "
                               "pixels into an aligned buffer, then doubled out under the collision "
                               "mask. Sixteen because a magnified sprite covers 32 scanlines, so "
                               "this is the same 2048 quads - and 32 of them do not fit at 252 MHz")
def _():
    return sprite_ecm(3, mag=True, count=16)


@scene("gm1-bml-under", "bitmap layer below the tiles")
def _():
    return gm1(tile2=True, sprites=True, r1f=0x80, **BML_GEOM)


@scene("gm1-bml-pri", "priority bitmap layer - over layer 1, under layer 2, under sprites")
def _():
    return gm1(tile2=True, sprites=True, r1f=0xC0, **BML_GEOM)


@scene("gm1-bml-fat", "priority bitmap layer, transparent and fat pixels")
def _():
    return gm1(tile2=True, sprites=True, r1f=0xF0, **BML_GEOM)


@scene("gm1-bml-full", "full-width opaque priority bitmap - the tile pass is skipped")
def _():
    return gm1(tile2=True, sprites=True, r1f=0xC0, **BML_FULL)


# ---- GM2 -----------------------------------------------------------------

def gm2(sprites=False, **regs):
    r = apply_regs(gm2_regs(), regs)
    v = gm2_vram()
    if not sprites:
        v = bytearray(v)
        place(v, SPR_ATTR_G2, bytes([0xD0]))
    return r, v, True


@scene("gm2", "Graphics II, three pattern and colour pages, full name mask")
def _():
    return gm2()


@scene("gm2-locked", "Graphics II locked - same path, no F18A palette")
def _():
    r, v, _ = gm2()
    return r, v, False


@scene("gm2-nosplit", "R4 page bits clear, so all three thirds read page 0")
def _():
    return gm2(r04=0x00)


@scene("gm2-namemask", "R3 low bits masking the name to 0-63, colour page fixed")
def _():
    return gm2(r03=0x87)


@scene("gm2-sprites", "sprites over Graphics II")
def _():
    return gm2(sprites=True)


@scene("gm2-palette", "R0x18 palette select, which GM2 applies to non-zero colours only")
def _():
    return gm2(r18=0x03)


@scene("gm2-bml-pri", "a priority bitmap layer in Graphics II - under the tiles it outranks, over layer 1, under sprites")
def _():
    return gm2(sprites=True, r1f=0xC0, **BML_GEOM)


@scene("gm2-scroll", "Graphics II with both scrolls - the pattern third follows the scrolled row")
def _():
    return gm2(r1b=(20 << 3) | 3, r1c=100)


@scene("gm2-t2", "Graphics II with tile layer 2 - both layers share the colour table, because the GM2 colour base is a single register bit")
def _():
    return gm2(r31=0x80, r0a=NAME2_G2 >> 10, r0b=0xFF)


@scene("gm2-posattr", "position attributes have no effect in an ECM0 graphics mode: they move the attribute address, which only ECM1-3 reads")
def _():
    return gm2(r32=0x02)


@scene("gm2-ecm3", "Graphics II at ECM3 - ECM composes with the mode through the plane 1 address alone, so the thirds become per-third colour sets")
def _():
    return gm2(r31=0x30)


# ---- Multicolor ----------------------------------------------------------

def mcm(sprites=False, locked=False, **regs):
    r = tile_regs()
    r[0x01] = 0xEA                       # multicolor, 16x16 sprites
    apply_regs(r, regs)
    v = mcm_vram()
    return r, (v if sprites else sprites_off(v)), not locked


@scene("mcm", "Multicolor - the pattern byte is the colour pair")
def _():
    return mcm()


@scene("mcm-locked", "Multicolor locked")
def _():
    return mcm(locked=True)


@scene("mcm-sprites", "sprites over Multicolor")
def _():
    return mcm(sprites=True)


@scene("mcm-scroll", "Multicolor with both scrolls - the vertical one moves which tiles are fetched but not which four-line block of each is shown")
def _():
    return mcm(r1b=(20 << 3) | 3, r1c=100)


@scene("mcm-t2", "Multicolor with tile layer 2")
def _():
    return mcm(r31=0x80)


@scene("mcm-bml-pri", "Multicolor with a priority bitmap layer")
def _():
    return mcm(sprites=True, r1f=0xC0, **BML_GEOM)


@scene("mcm-ecm3", "Multicolor at ECM3, which is not multicolour at all: the faked pattern is ECM0-only, so only the plane 1 address survives")
def _():
    return mcm(r31=0x30)


# ---- text modes ----------------------------------------------------------

def text(cols, sprites=False, locked=False, sparse_t2=False, rows=24, **regs):
    r = tile_regs()
    r[0x00] = 0x00 if cols == 40 else 0x04
    r[0x01] = 0xF2                       # text, 16x16 sprites
    r[0x03] = text_color1(cols, rows) >> 6
    apply_regs(r, regs)
    # both row registers come from `rows`, after the overrides, so a scene cannot
    # set the cell count and forget the doubling or the other way round
    if rows in (30, 60):
        r[0x31] |= 0x40
    if rows > 30:
        r[0x00] |= R0_DOUBLE_ROWS
    # 80 columns keeps its ECM0 tables whatever R0x31 says: its provisional scenes assert that
    # selecting ECM changes nothing, and that only means something against identical data
    v = text_vram(cols, rows, sparse_t2, ecm=(cols == 40 and bool(r[0x31] & 0x30)),
                  per_cell=bool(r[0x32] & 0x02))
    return r, (v if sprites else sprites_off(v)), not locked


@scene("t40", "40-column text, six pixels a cell, one colour pair")
def _():
    return text(40)


@scene("t40-locked", "40-column text locked - no sprites at all")
def _():
    return text(40, locked=True)


@scene("t40-sprites", "sprites over 40-column text, which only the F18A allows")
def _():
    return text(40, sprites=True)


@scene("t40-posattr", "40-column text with a colour byte per cell - the 6-pixel 8bpp emitter")
def _():
    return text(40, r32=0x02)


@scene("t40-scroll", "40-column text with both scroll registers set. 163 pixels is cell 27 plus "
                     "one, so the row wraps to its own first cell partway across")
def _():
    return text(40, r1b=(20 << 3) | 3, r1c=100)


@scene("t40-hscroll-split", "both text layers scrolled horizontally by different amounts - "
                            "tmsCopyAlignMask over a six-pixel cell")
def _():
    return text(40, r31=0x80, r32=0x02, r1b=163, r19=5)


@scene("t40-hscroll-past-row", "text scrolled further than a row is wide, which the F18A does not "
                               "wrap. Its column counter is seeded with hscroll/6 and wraps on "
                               "reaching the last column, so a start cell of 40 never meets 39 and "
                               "the row reads on into the next one: the layer comes out a "
                               "character row high. Layer 1 at 240 lands on the boundary exactly, "
                               "layer 2 at 250 a row and four pixels past it")
def _():
    return text(40, r31=0x80, r32=0x02, r1b=240, r19=250)


@scene("t40-t2", "both 40-column text layers, layer 2 blended in as it is emitted - the same "
                 "shape 80 columns uses, and for the same reason")
def _():
    return text(40, r31=0x80, r32=0x02)


@scene("t40-bml-pri", "a priority bitmap layer in 40-column text, arbitrated by the same composite the graphics modes use")
def _():
    return text(40, sprites=True, r1f=0xC0, **BML_GEOM)


@scene("t40-bml-under", "a bitmap layer below 40-column text, with the first three cells of every "
                        "row at colour zero. A cell whose colour byte is zero draws no pixel at all "
                        "at all, so the layer under it must show - and a *leading* run "
                        "of them is the case most easily got wrong, by painting the backdrop instead")
def _():
    r, v, unlocked = text(40, sprites=True, r32=0x02, r1f=0x80, **BML_GEOM)
    for row in range(24):
        place(v, COLOR1 + row * 40, bytes(3))
    return r, v, unlocked


@scene("t40-palette", "both 40-column text layers on their own sub-palettes. Text takes the tile "
                      "palette select like every other mode: an ECM0 pixel is PIX, PRI, then "
                      "`tileps_s & pix_colr_s`, and the backdrop is built the "
                      "same way from layer 1's selector")
def _():
    return text(40, r31=0x80, r32=0x02, r18=0x0B)


@scene("t40-ecm1", "40-column text at ECM1: one bitplane, and the sub-palette from the attribute "
                   "byte instead of a colour pair. The attribute is indexed by name, which is the "
                   "form position attributes are off")
def _():
    return text(40, r31=0x10)


@scene("t40-ecm2", "40-column text at ECM2 - two bitplanes")
def _():
    return text(40, r31=0x20)


@scene("t40-ecm3", "40-column text at ECM3 - three bitplanes, and the bottom band's punctuation "
                   "carries every combination of priority, flipX, flipY and transparent")
def _():
    return text(40, r31=0x30)


@scene("t40-ecm3-posattr", "ECM3 text with position attributes, so the attribute walks with the "
                           "cell and its address takes the row offset that a by-name one must not")
def _():
    return text(40, r31=0x30, r32=0x02)


@scene("t40-t2-ecm3", "both 40-column text layers at ECM3 - the coverage mask six pixels at a "
                      "time, from the plane mask rather than the colour byte")
def _():
    return text(40, r31=0xB0)


@scene("t40-ecm3-sprites", "sprites under ECM3 text, where the attribute's priority bit decides "
                           "per cell which is on top")
def _():
    return text(40, sprites=True, r31=0x30)


@scene("t40-ecm3-scroll", "ECM3 text scrolled both ways: the row runs two cells long and the "
                          "priority clear has to reach the sprite mask in screen pixels, not "
                          "buffer ones")
def _():
    return text(40, sprites=True, r31=0x30, r1b=163, r1c=100)


@scene("t80", "80-column text, two colours from R7")
def _():
    return text(80)


@scene("t80-posattr", "80-column text with a colour byte per cell")
def _():
    return text(80, r32=0x02)


@scene("t80-t2", "both text layers, layer 2 blended inside the layer 2 pass")
def _():
    return text(80, r31=0x80, r32=0x02)


@scene("t80-t2-sparse", "layer 2 mostly empty, so the four-cell transparent skip fires")
def _():
    return text(80, sparse_t2=True, r31=0x80, r32=0x02)


@scene("t80-vscroll", "both text layers scrolled vertically by different amounts")
def _():
    return text(80, r31=0x80, r32=0x02, r1c=100, r1a=60)


@scene("t80-vscroll-page", "80-column text scrolling past its last row with the vertical page size "
                           "bit set, which does nothing: a text row's name address is a row count "
                           "times a column count and the page bit is never part of it "
                           "in either direction")
def _():
    return text(80, r32=0x02, r1c=100, r1d=0x88 | 0x01)


@scene("t80-sprites", "sprites over 80-column text, on their own 256-pixel grid")
def _():
    return text(80, sprites=True, r31=0x80, r32=0x02)


@scene("t80-30rows", "80 columns by 30 rows - 240 scanlines, not 192")
def _():
    return text(80, rows=30, r31=0x40, r32=0x02)


@scene("t40-48rows", "40 columns by 48 rows - R0 bit 3 renders every VGA line instead of "
                     "doubling it, so the same 24 rows of cells become 48 and the renderer has "
                     "half a line's time to make each one")
def _():
    return text(40, rows=48)


@scene("t40-60rows", "40 columns by 60 rows - 30 rows doubled, 480 rendered lines, the tallest "
                     "frame this firmware produces. Its worst row clears a 31.8 us line by under a "
                     "microsecond, so the six-pixel cell being the expensive text emitter still "
                     "shows here more than anywhere: the diagnostic overlay puts it back over")
def _():
    return text(40, rows=60)


@scene("t40-48rows-sprites", "sprites over 48-row text. The sprite grid does not double with the "
                             "rows - the library halves y before it plans a sprite - so each "
                             "sprite covers two rendered lines and its own y is still 0-191. What "
                             "the sprites add is what the diagnostic overlay then pushes over")
def _():
    return text(40, rows=48, sprites=True)


@scene("t80-48rows", "80 columns by 48 rows - 3840 cells, twice the rows at the narrowest cell")
def _():
    return text(80, rows=48, r32=0x02)


@scene("t80-60rows", "80 columns by 60 rows - 4800 cells over 480 rendered lines, which is every "
                     "pixel this board can address and the heaviest frame in the library. Its "
                     "colour table does not fit under NAME2, so it takes that space")
def _():
    return text(80, rows=60, r32=0x02)


@scene("t80-tiles-off", "layer 1 disabled in 80-column text")
def _():
    return text(80, r31=0x80, r32=0x12)


@scene("t80-hscroll", "both 80-column layers scrolled horizontally by different amounts. The "
                      "offset inside the first cell is only ever 0, 2 or 4 pixels, so "
                      "the emitter backs up whole cells and keeps its three-word store")
def _():
    return text(80, r31=0x80, r32=0x02, r1b=(20 << 3) | 3, r19=5)


@scene("t80-hscroll-plain", "80-column text scrolled with the two colours from R7 - three byte "
                            "stores a cell, so the wrap is a second run rather than a test")
def _():
    return text(80, r1b=163)


@scene("t80-hscroll-past-row", "the same overrun at 80 columns, and at the same register value. "
                               "Worth its own scene because it is not the same code: without the "
                               "8bpp tier a TEXT80 line falls past the branch the 40-column one "
                               "takes and is emitted packed two pixels to a byte, so a change to "
                               "either emitter moves one of these two and not the other")
def _():
    return text(80, r31=0x80, r32=0x02, r1b=240, r19=250)


@scene("t80-bml-pri", "80-column text with a priority bitmap layer",
       changes="a bitmap layer in T80 needs the 8bpp tier",
       base="t80-sprites")
def _():
    return text(80, sprites=True, r31=0x80, r32=0x02, r1f=0xC0, **BML_GEOM)


@scene("t80-ecm3", "80-column text with ECM3 selected",
       changes="T80 cannot express ECM at 4bpp at all. RP2350 only, if ever",
       base="t80-posattr")
def _():
    return text(80, r31=0x30, r32=0x02)


# ---- scenes captured from real software -----------------------------------

DUMP_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "dumps")

# what the firmware itself powers up with, for the registers a TMS9918 dump cannot carry. Without
# these a dumped scene loses its sprites entirely, because the sprite pass reads its limits from
# registers the F18A added and a locked host never writes.
DUMP_DEFAULTS = {0x1E: 31, 0x33: 32}


JS99ER_REG = re.compile(r"VR(\d+)\s*:\s*>?([0-9A-Fa-f]{1,2})")


DUMP_REGS = 0x4000        # 64 registers
DUMP_PRAM = 0x4040        # 64 palette entries, two bytes each, F18A order: 0000RRRR GGGGBBBB
DUMP_FULL = 0x40C0


def load_dump(path):
    """A JS99er VDP dump. Two layouts, told apart by length:

        16K + 64 + 128   VRAM, VR0-VR63, and all 64 palette entries - a whole VDP state
        16K + 8          VRAM and VR0-VR7 only, which is what stock JS99er writes

    The short form needs the rest from a text file beside it, in JS99er's own notation:

        VR0:>00 VR1:>E2 VR2:>00 ...        as many per line as it likes, through VR57

    Either way the unlock is inferred from VR57, which only holds 0x1C if the software actually
    unlocked the chip. A sidecar can override anything and add what no dump carries:

        unlock                 # or `lock`, to override what VR57 implies
        49 = 0xB4              # register numbers are the F18A's own, so decimal; 0x for hex
        palette 4 = 0x00F      # 12-bit 0xRGB

    Blank lines and anything after a `#` are ignored.
    """
    with open(path, "rb") as f:
        blob = f.read()
    # The two layouts are exact lengths, not minimums. Accepting anything longer
    # reads whatever follows the first 16K as the register file: three TI-99
    # cartridge images in this directory came through as scenes whose R0 was the
    # `>AA` header byte of a ROM bank, decoded as Graphics II, and were frozen.
    if len(blob) not in (DUMP_FULL, VRAM_SIZE + 8):
        raise RuntimeError(
            "%s is %d bytes, which is neither a full VDP dump (%d) nor a short one (%d)%s"
            % (path, len(blob), DUMP_FULL, VRAM_SIZE + 8,
               " - it looks like a TI-99 cartridge, not a dump" if blob[:1] == b"\xaa" else ""))

    vram = bytearray(blob[:VRAM_SIZE])
    regs = bytearray(64)
    unlocked, palette = None, {}

    if len(blob) == DUMP_FULL:
        regs[:] = blob[DUMP_REGS:DUMP_REGS + 64]
        for i in range(64):
            hi, lo = blob[DUMP_PRAM + i * 2], blob[DUMP_PRAM + i * 2 + 1]
            palette[i] = ((hi & 0x0F) << 8) | lo
    else:
        # the short form cannot carry the registers the F18A added, so the firmware's own
        # power-on values stand in - without them a dumped scene loses its sprites entirely
        for reg, value in DUMP_DEFAULTS.items():
            regs[reg] = value
        for i, value in enumerate(blob[VRAM_SIZE:VRAM_SIZE + 8]):
            regs[i] = value
    sidecar = os.path.splitext(path)[0] + ".txt"
    for candidate in (sidecar, os.path.join(os.path.dirname(path), "registers.txt")):
        if not os.path.exists(candidate):
            continue
        with open(candidate) as f:
            for lineno, raw in enumerate(f, 1):
                line = raw.split("#")[0].strip()
                if not line:
                    continue

                found = JS99ER_REG.findall(line)
                if found:
                    for reg, value in found:
                        regs[int(reg)] = int(value, 16)
                    continue

                if line.lower() in ("lock", "unlock"):
                    unlocked = line.lower() == "unlock"
                    continue

                key, _, value = line.partition("=")
                if not value:
                    raise RuntimeError("%s:%d: expected `VRn:>hh`, `reg = value` or `lock`, got %r"
                                       % (candidate, lineno, line))
                key, value = key.strip().lower(), int(value.strip(), 0)
                if key.startswith("palette"):
                    palette[int(key.split()[1], 0)] = value
                else:
                    regs[int(key.lstrip("vr"), 0)] = value
        break

    if unlocked is None:
        unlocked = regs[0x39] == 0x1C   # what the software wrote to the unlock register
    return regs, vram, unlocked, palette


# A dump that drops a line on a board, which a scene declares for itself and a dump cannot.
# Empty today, and read it narrowly: it means no row goes missing, not that every row
# fits. `dump-ck1-ti` drops nothing, but the eight rows where
# its title puts sixteen 8px ECM3 sprites side by side still run about 4 us past the
# 63.6 the line has. Eight rows of that is 27 us of lag, short of the whole line the
# renderer must fall behind before vga.c skips one - so nothing is dropped and every
# one of those rows is still finished after core 1 began reading it out.
DUMP_OVERBUDGET = {}


def register_dumps():
    """Every dump in data/dumps/ becomes a scene. Drop a file in and it is one - which is what
    makes real software usable as a reference, rather than only scenes we thought to write."""
    if not os.path.isdir(DUMP_DIR):
        return
    for name in sorted(os.listdir(DUMP_DIR)):
        if not name.endswith(".bin"):
            continue
        path = os.path.join(DUMP_DIR, name)
        # skipped here rather than left to fail at build time: this directory is a
        # drop box and a .bin in it is as likely to be a cartridge as a dump
        if os.path.getsize(path) not in (DUMP_FULL, VRAM_SIZE + 8):
            print("skipping %s: %d bytes, not a VDP dump" % (name, os.path.getsize(path)),
                  file=sys.stderr)
            continue
        stem = os.path.splitext(name)[0].strip()
        for suffix in ("_vdpramfull", "_vdpram", "vdpramfull", "vdpram"):
            if stem.lower().endswith(suffix):
                stem = stem[:-len(suffix)]
                break
        stem = stem.strip(" _-").replace(" ", "-")

        def make(path=path):
            regs, vram, unlocked, palette = load_dump(path)
            return regs, vram, unlocked

        SCENES["dump-" + stem] = Scene(make, "captured from real software: %s" % name,
                                       None, None, DUMP_OVERBUDGET.get("dump-" + stem, ()))
        DUMPS["dump-" + stem] = path


DUMPS = {}
register_dumps()

# The scenes that have each dropped a scanline while their own average sat 8 to 25 us
# inside the budget - placement, not work, and no percentage catches them. Run these
# after anything that resizes the library; it is a minute against the full suite's 33
# seconds only because it is the first thing worth knowing.
CANARIES = [n for n in ("gm1-bml-under", "gm1-priority", "gm1-sprites-ecm3-mag",
                        "dump-ck1-ti",
                        "dump-F18A_Karts_demo", "dump-F18A_ZQX-ONE") if n in SCENES]


# ---- driving a board ------------------------------------------------------

def build(name):
    regs, vram, unlocked = SCENES[name].fn()
    if len(vram) != VRAM_SIZE:
        raise ValueError("%s: VRAM image is %d bytes" % (name, len(vram)))
    if len(regs) != 64:
        raise ValueError("%s: register file is %d bytes" % (name, len(regs)))
    return bytes(regs), bytes(vram), unlocked


def quiet(t):
    """Every diagnostic panel off, which is board state a scene cannot describe.

    The overlay is not free: it draws on every scanline, after the timer that
    reports how long the line took. A board whose stored config has it on renders
    several scenes over budget, and an over-budget line is skipped rather than
    drawn late - which reads back as the previous capture's pixels. perf.py turns
    it on deliberately, and leaves it on for whatever runs next.

    Every panel, not just the master flag: leaving the four panel flags at whatever
    stored config held made perf.py's --panels a no-op, and every reading taken on
    a board with them set was an all-panels reading. Anything that applies a scene
    of its own has to call this, or it silently inherits the last stage's overlay.
    """
    for conf in CONF_DIAG_PANELS:
        t.write(t.inst + t.off["config"] + conf, b"\x00")


def apply(t, name):
    """Registers first with the display off, then all of VRAM, then the display
    on. Writing every register and every byte is what stops one scene's leftovers
    from becoming part of the next one's reference."""
    regs, vram, unlocked = build(name)
    t.unlock() if unlocked else t.lock()
    blanked = bytearray(regs)
    blanked[0x01] &= ~0x40
    # and the GPU never runs, which is the one piece of board state a scene cannot
    # describe: its program counter and run state are neither registers nor VRAM, so
    # a scene that leaves a trigger enabled can resume a program some *earlier* scene
    # started and write VRAM underneath the capture. Two dumps do (R0x32 = 0x41 and
    # 0x21), which made them pass alone and fail in a suite. Nothing here renders from
    # the trigger bits, so clearing them costs no pixel and buys order-independence.
    blanked[0x32] &= ~0x60
    t.vram(VRAM_REGISTERS, blanked)
    t.vram(0, vram)

    # the palette is board state like any other, so a dump's must not outlive it
    palette = load_dump(DUMPS[name])[3] if name in DUMPS else {}
    entries = list(t.default_palette())
    for index, rgb in palette.items():
        entries[index] = rgb
    t.palette_all(entries)

    quiet(t)
    t.reg(0x01, regs[0x01])


def note(name):
    return SCENES[name].note


def changes(name):
    return SCENES[name].changes


def over_budget_on(name, board):
    """Whether this scene is already known not to fit on this board."""
    return board in SCENES[name].overbudget


# ---- what a scene exercises -----------------------------------------------
#
# Derived from the register file the scene already returns, never declared beside
# it. A declaration is a second copy of the truth and drifts from the first; this
# cannot, it needs no board, and it works for the dumps - which are someone else's
# content and could not be annotated by hand at all.
#
# Register numbers are the F18A's, so R25 and R27 are the horizontal scrolls and R49
# and R50 the two enhanced control registers.

GMODES = {0b0000: "GM1", 0b0001: "T40", 0b0010: "MCM", 0b0100: "GM2", 0b1001: "T80"}


def sprite_count(regs, vram):
    """How many sprites the list actually holds. >D0 ends it, but only outside
    row30 - there R51 names the stopping sprite instead."""
    sat = (regs[0x05] & 0x7F) << 7
    row30 = bool(regs[0x31] & 0x40)
    stop = regs[0x33] & 0x3F
    limit = (stop & 0x1F) if (row30 and not (stop & 0x20)) else 32
    for i in range(min(limit, 32)):
        y = vram[(sat + i * 4) & (VRAM_SIZE - 1)]
        if y == 0xD0 and not row30:
            return i
    return min(limit, 32)


def budget_us(name, budget):
    """What one rendered line of this scene has to fit in. R0's row doubling
    renders every VGA line instead of each one twice, so the same content gets
    half the time - a 48-row scene averaging 25 us has 7 us spare, not 39.

    The budget is one scanline of a VGA mode, which is a fact about the device
    rather than about the renderer, so the caller supplies it."""
    return budget and budget / (2 if build(name)[0][0] & R0_DOUBLE_ROWS else 1)


def features(name):
    """One flat record of what a scene turns on, for the coverage matrix.

    A locked scene's F18A registers are inert whatever they hold, so `locked` is
    reported alongside rather than folded into the other fields - the library's
    own gate is `isUnlocked`, and hiding that here would make a locked scene
    indistinguishable from an unlocked one that happens to use nothing."""
    regs, vram, unlocked = build(name)
    gmode = (bool(regs[0x00] & 0x04) << 3 | bool(regs[0x00] & 0x02) << 2
             | bool(regs[0x01] & 0x08) << 1 | bool(regs[0x01] & 0x10))
    axes = lambda h, v: ("X" if h else "") + ("Y" if v else "")
    pages = lambda h, v: "%dx%d" % (2 if h else 1, 2 if v else 1)
    return {
        "mode": GMODES.get(gmode, "?%d" % gmode),
        "locked": not unlocked,
        "rows": (30 if regs[0x31] & 0x40 else 24) * (2 if regs[0] & R0_DOUBLE_ROWS else 1),
        "t1": not regs[0x32] & 0x10,
        "t2": bool(regs[0x31] & 0x80),
        "t2pri": bool(regs[0x32] & 0x01),
        "ecm": (regs[0x31] & 0x30) >> 4,
        "posattr": bool(regs[0x32] & 0x02),
        "t1scroll": axes(regs[0x1B], regs[0x1C]),
        "t2scroll": axes(regs[0x19], regs[0x1A]),
        "t1pages": pages(regs[0x1D] & 0x02, regs[0x1D] & 0x01),
        "t2pages": pages(regs[0x1D] & 0x20, regs[0x1D] & 0x10),
        "t1ps": regs[0x18] & 0x03,
        "t2ps": (regs[0x18] & 0x0C) >> 2,
        "bml": bool(regs[0x1F] & 0x80),
        "bmlpri": bool(regs[0x1F] & 0x40),
        "bmltrns": bool(regs[0x1F] & 0x20),
        "bmlfat": bool(regs[0x1F] & 0x10),
        "sprites": sprite_count(regs, vram),
        "spr16": bool(regs[0x01] & 0x02),
        "sprmag": bool(regs[0x01] & 0x01),
        "sprecm": regs[0x31] & 0x03,
        "sprps": (regs[0x18] & 0x30) >> 4,
        "sprmax": regs[0x1E] & 0x1F,
        "gpu": bool(regs[0x32] & 0x60),
    }


def matrix(names=None, budget=None):
    """Features and note for each scene, which is a coverage table: what the
    library reaches, and what nothing in it turns on.

    A run snapshots this for the scenes it touched, so a record stays readable
    against a library that has since changed - the timings are pinned to a commit
    and the description of what produced them has to be too."""
    return {name: {"features": features(name), "note": note(name),
                   "budget_us": budget_us(name, budget),
                   "overbudget": list(SCENES[name].overbudget)}
            for name in (names if names is not None else SCENES)}


if __name__ == "__main__":
    print("%-44s %-4s %-5s %-3s %-3s %-3s %-4s %-4s %-5s %-5s %-4s"
          % ("scene", "mode", "state", "T1", "T2", "BML", "ECM", "SPR", "T1SCR", "T2SCR", "PAGES"))
    for name in SCENES:
        f = features(name)
        print("%-44s %-4s %-5s %-3s %-3s %-3s %-4d %-4d %-5s %-5s %s/%s"
              % (name, f["mode"], "lock" if f["locked"] else "prov" if changes(name) else "ok",
                 "y" if f["t1"] else "-", "y" if f["t2"] else "-", "y" if f["bml"] else "-",
                 f["ecm"], f["sprites"], f["t1scroll"] or "-", f["t2scroll"] or "-",
                 f["t1pages"], f["t2pages"]))
