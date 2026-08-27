#!/usr/bin/env python3
"""A browser front-end for the board: pick a scene, tweak it live, drop a dump on it.

    python console.py --board 2040
    python console.py --board 2040 --port 8919 --no-browser

Everything else here reads the board to *assert* something. This reads it to show
you, which is the one thing a golden cannot do: a scene that differs by 18606
pixels from row 256 is a number until you look at the two pictures. And the
registers are writable, so "what happens if layer 2 scrolls the other way" is a
slider rather than a scene, an edit and a flash.

One openocd session stays open for the life of the server, because opening one
costs a second and a half and the whole point is that clicking a scene is
instant. Board access is serialised behind a lock - there is one board.

Thumbnails come from the frozen goldens, not from the board: they are the same
bytes the board produced, they need no probe, and the grid therefore paints
before the first scene is applied. A scene with no golden - one that overruns, so
its capture was never comparable - says so instead.
"""

import argparse
import base64
import http.server
import json
import os
import socketserver
import sys
import threading
import time
import webbrowser
import zlib

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)),
                                "core", "test", "suite"))
import suite.stages.freeze as freeze
import results
import suite.scenes as scenes
from live9918 import (PIXELS_X, board_args, open_board, png_bytes, reference,
                      rgb_palette, unpack_nibbles)
from suite.scenes import (PICO9918_CONF_DIAG_ADDRESS, PICO9918_CONF_DIAG_PALETTE, PICO9918_CONF_DIAG_PERFORMANCE,
                    PICO9918_CONF_DIAG_REGISTERS)

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "console.html")
UPLOAD_LIMIT = 1 << 20          # a dump is 16576 bytes; anything near this is a mistake

# What the page offers as a control, and which register bits each one is. The
# same bits `scenes.features` decodes, written once here so the panel cannot
# drift from the matrix - a control the coverage table has no column for is a
# control nobody will think to check.
#
# `mask` is the field; the shift comes from the mask. `invert` is for a bit that
# reads as a disable, so the switch still reads as the thing being on.
CONTROLS = [
    ("Layers", [
        (0x32, 0x10, "bit", "Tile layer 1", {"invert": True}),
        (0x31, 0x80, "bit", "Tile layer 2", {}),
        (0x32, 0x01, "bit", "Layer 2 priority", {}),
        (0x32, 0x02, "bit", "Position attributes", {}),
    ]),
    ("Scroll", [
        (0x1B, 0xFF, "range", "Layer 1 X", {}),
        (0x1C, 0xFF, "range", "Layer 1 Y", {}),
        (0x19, 0xFF, "range", "Layer 2 X", {}),
        (0x1A, 0xFF, "range", "Layer 2 Y", {}),
    ]),
    ("Pages", [
        (0x1D, 0x02, "bit", "Layer 1 two across", {}),
        (0x1D, 0x01, "bit", "Layer 1 two down", {}),
        (0x1D, 0x20, "bit", "Layer 2 two across", {}),
        (0x1D, 0x10, "bit", "Layer 2 two down", {}),
    ]),
    ("Bitmap layer", [
        (0x1F, 0x80, "bit", "Enabled", {}),
        (0x1F, 0x40, "bit", "Priority", {}),
        (0x1F, 0x20, "bit", "Transparent", {}),
        (0x1F, 0x10, "bit", "Fat pixels", {}),
    ]),
    ("Sprites", [
        (0x01, 0x02, "bit", "16x16", {}),
        (0x01, 0x01, "bit", "Magnified", {}),
        (0x31, 0x03, "field", "Sprite ECM", {}),
        (0x1E, 0x1F, "range", "Per scanline", {}),
    ]),
    ("Mode", [
        (0x31, 0x30, "field", "Tile ECM", {}),
        (0x31, 0x40, "bit", "30 rows", {}),
        (0x00, 0x08, "bit", "Double rows", {"note": "48 and 60 rows, at half the line budget"}),
        (0x00, 0x04, "bit", "80 columns", {}),
        (0x01, 0x40, "bit", "Display on", {}),
    ]),
    ("Palette select", [
        (0x18, 0x03, "field", "Layer 1", {}),
        (0x18, 0x0C, "field", "Layer 2", {}),
        (0x18, 0x30, "field", "Sprites", {}),
    ]),
    # `conf` swaps the meaning of the first column: a stored configuration byte
    # rather than a VDP register, which is what a diagnostic panel is. They never
    # reach a capture - liveTestCaptureRow runs before the overlay draws - so this
    # is for the monitor, and the cost still lands on the line: turning them on can
    # take a scene over budget, which the dropped-row count then reports.
    ("Diagnostics", [
        (PICO9918_CONF_DIAG_REGISTERS, 0x01, "bit", "Registers", {"conf": True}),
        (PICO9918_CONF_DIAG_PERFORMANCE, 0x01, "bit", "Performance", {"conf": True}),
        (PICO9918_CONF_DIAG_PALETTE, 0x01, "bit", "Palette", {"conf": True}),
        (PICO9918_CONF_DIAG_ADDRESS, 0x01, "bit", "Address", {"conf": True}),
    ]),
]

# The four panel flags, per src/config.h. PICO9918_CONF_DIAG itself is not here: the
# firmware recomputes it from these whenever the configuration is marked dirty,
# which is the same path the configurator takes.
DIAG_CONF = (PICO9918_CONF_DIAG_REGISTERS, PICO9918_CONF_DIAG_PERFORMANCE, PICO9918_CONF_DIAG_PALETTE, PICO9918_CONF_DIAG_ADDRESS)

# Groups that belong beside the picture rather than in the grid of scene controls
# below it: these two change what the board *reports*, not what it draws.
SIDE_GROUPS = {"Diagnostics"}


def controls():
    """The table above as the page wants it: the shift derived from the mask, so
    the table stays a list of bits rather than a list of bits and their positions."""
    out = []
    for group, items in CONTROLS:
        rendered = []
        for reg, mask, kind, label, extra in items:
            shift = (mask & -mask).bit_length() - 1
            rendered.append(dict(reg=reg, mask=mask, shift=shift, kind=kind, label=label,
                                 max=mask >> shift, **extra))
        out.append({"group": group, "items": rendered, "side": group in SIDE_GROUPS})
    return out


def aspect(rows, doubled):
    """What shape the picture is on the screen, which is not the shape of its bytes.

    The renderer's line is always 512 VGA columns however many bytes carry it -
    256 indices doubled across, or 512 rendered one to one in 80-column text - and
    every row is two VGA lines unless R0's row doubling makes it one. So a capture
    is 512 by 384 or 512 by 480 on the glass, and the byte geometry that reaches
    the browser (256x384 here, 512x240 there) is the wrong thing to lay out by:
    it renders a 48-row scene as a tall strip and a 30-row one as a letterbox."""
    return 512.0 / (rows * (1 if doubled else 2))


class Board:
    """The board, and the one lock that owns it."""

    def __init__(self, args):
        self.live = open_board(args)
        self.lock = threading.Lock()
        self.palette = None
        self.uploaded = {}      # name -> (regs, vram, unlocked, palette), this session only

    def __enter__(self):
        self.live.__enter__()
        with self.lock:
            self.palette = list(self.live.default_palette())
        return self

    def __exit__(self, *_):
        self.live.__exit__()

    def state(self, capture=True):
        """What the page needs to draw itself: the picture and the registers that
        produced it, read together so a control can never show a value the picture
        does not have."""
        with self.lock:
            out = {}
            regs = self.live.regs()
            if capture:
                png, rows = self.live.frame_png()
                out["png"] = base64.b64encode(png).decode()
                out["rows"] = rows
                out["dropped"] = len(self.live.dropped)
                out["width"] = self.live.width
                out["aspect"] = aspect(rows, regs[0] & scenes.R0_DOUBLE_ROWS)
            out["regs"] = list(regs)
            out["conf"] = {i: self.live.conf(i) for i in DIAG_CONF}
            out["unlocked"] = self.live.read(self.live.inst + self.live.off["isUnlocked"], 1)[0] != 0
            return out

    def show(self, name):
        with self.lock:
            if name in self.uploaded:
                self._apply_dump(*self.uploaded[name])
            else:
                scenes.apply(self.live, name)
        return self.state()

    def poke(self, reg, value, mask=0xFF, capture=True):
        """Read, merge, write - so a control that owns four bits of a register
        cannot take the other four with it, and the page never has to hold a copy
        of the register file that could go stale."""
        with self.lock:
            merged = (self.live.regs()[reg] & ~mask) | (value & mask)
            self.live.reg(reg, merged)
        return self.state(capture)

    def set_unlocked(self, on):
        with self.lock:
            self.live.unlock() if on else self.live.lock()
        return self.state()

    def set_conf(self, index, value, capture=True):
        """A diagnostic panel, which is stored configuration rather than a register.

        Marking the configuration dirty is what makes it take: the renderer picks
        that up at end of frame and runs the firmware's own `applyConfig`, which
        derives the master PICO9918_CONF_DIAG flag from these four and rebuilds the overlay.
        Writing the byte alone would leave the two disagreeing."""
        with self.lock:
            self.live.conf(index, value)
            self.live.write(self.live.inst + self.live.off["configDirty"], b"\x01")
        return self.state(capture)

    def _apply_dump(self, regs, vram, unlocked, palette):
        """`scenes.apply` reads its data out of the registry; an upload has none,
        so this is the same sequence over data held in memory."""
        self.live.unlock() if unlocked else self.live.lock()
        blanked = bytearray(regs)
        blanked[0x01] &= ~0x40
        blanked[0x32] &= ~0x60
        self.live.vram(scenes.VRAM_REGISTERS, blanked)
        self.live.vram(0, vram)
        entries = list(self.palette)
        for index, rgb in palette.items():
            entries[index] = rgb
        self.live.palette_all(entries)
        for conf in scenes.CONF_DIAG_PANELS:
            self.live.write(self.live.inst + self.live.off["config"] + conf, b"\x00")
        self.live.reg(0x01, regs[0x01])


class Cancelled(Exception):
    """A sweep the page asked to stop, raised out of the progress hook."""


class Sweep:
    """The suite, run where it can be watched.

    Not a second copy of it: this calls `freeze.run`, the same function the runner's
    freeze stage calls, and follows its progress hook. So what fills the grid IS the
    comparison that decides pass or fail, and the picture beside each verdict is the
    very bytes that were compared - `frame_png` is handed the capture rather than
    taking a fresh one from a board that has already moved to the next scene.

    Progress is polled rather than streamed. A sweep holds the board for its whole
    length, so the alternative is a page that cannot ask anything until it finishes;
    the results list has its own lock and the poll never touches the board.
    """

    def __init__(self, board):
        self.board = board
        self.lock = threading.Lock()
        self.thread = None
        self.results = []
        self.total = 0
        self.done = True
        self.cancel = False
        self.error = None
        self.started = 0.0
        self.ended = 0.0

    def state(self, since=0):
        with self.lock:
            # until it finishes, how long it has been going; after, how long it took.
            # Not "now minus started" throughout: a page that polls a minute later
            # would report the sweep as having taken a minute
            end = self.ended or time.time()
            return {"total": self.total, "count": len(self.results),
                    "running": not self.done, "error": self.error,
                    "seconds": round(end - self.started, 2) if self.started else 0,
                    "since": since, "results": self.results[since:]}

    def start(self, names):
        if not self.done:
            return self.state()
        with self.lock:
            self.results, self.total = [], len(names)
            self.done, self.cancel, self.error = False, False, None
            self.started, self.ended = time.time(), 0.0
        self.thread = threading.Thread(target=self._run, args=(names,), daemon=True)
        self.thread.start()
        return self.state()

    def stop(self):
        self.cancel = True
        return self.state()

    def _note(self, name, entry, frame):
        """One scene's verdict, with the picture it was reached from."""
        png, _ = self.board.live.frame_png(frame)
        now = time.time()
        with self.lock:
            self.results.append({
                "name": name, "state": entry["state"], "why": entry["why"],
                "differ": entry["differ"], "first": entry["first"],
                "rows": entry["rows"], "width": entry["width"],
                "dropped": len(entry["dropped"]),
                "ms": round((now - self.last) * 1000),
                "png": base64.b64encode(png).decode(),
            })
            self.last = now
        # freeze.run has no cancel flag to test, and does not need one: a hook that
        # raises stops the loop where it stands, and _run catches this one
        if self.cancel:
            raise Cancelled()

    def _run(self, names):
        self.last = self.started
        try:
            # the whole sweep under the board lock: a control poked between two
            # scenes would be measured by the next one
            with self.board.lock:
                freeze.run(self.board.live, names, progress=self._note)
        except Cancelled:
            pass
        except Exception as e:                                   # noqa: BLE001
            with self.lock:
                self.error = str(e)
        finally:
            with self.lock:
                self.done = True


def golden_png(board, name):
    """A scene's frozen reference as a picture. The palette is the firmware's own
    plus whatever a dump overrides, which is exactly what `scenes.apply` installs -
    so the thumbnail and the live capture are the same picture."""
    regs, _, _ = scenes.build(name)
    width = PIXELS_X
    path = reference(name, width)
    if not os.path.exists(path):
        return None
    pixels = zlib.decompress(open(path, "rb").read())
    rows = len(pixels) // width
    entries = list(board.palette)
    if name in scenes.DUMPS:
        for index, rgb in scenes.load_dump(scenes.DUMPS[name])[3].items():
            entries[index] = rgb
    if regs[0] & 0x04:
        pixels, width = unpack_nibbles(pixels, rows, width)
    return png_bytes(width, rows, pixels[:rows * width], rgb_palette(entries))


def catalogue(board):
    """Every scene the grid can offer, with what it exercises and whether it is
    frozen, provisional or known not to fit."""
    out = []
    for name in sorted(scenes.SCENES):
        f = scenes.features(name)
        head = name.split("-")[0]
        out.append({
            "name": name,
            "group": head if head in ("gm1", "gm2", "mcm", "t40", "t80") else "dump",
            "mode": f["mode"], "rows": f["rows"], "ecm": f["ecm"], "sprites": f["sprites"],
            "note": scenes.note(name),
            "budget": scenes.budget_us(name, results.BUDGET_US),
            "aspect": aspect(f["rows"] * 8, f["rows"] > 30),
            "changes": scenes.changes(name),
            "overbudget": list(scenes.SCENES[name].overbudget),
            "golden": os.path.exists(reference(name, PIXELS_X)),
        })
    return out


class Handler(http.server.BaseHTTPRequestHandler):
    board = None
    sweep = None
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        pass                    # the interesting output is the board's, not the socket's

    def send(self, body, kind="application/json", status=200):
        if kind == "application/json" and not isinstance(body, bytes):
            body = json.dumps(body).encode()
        try:
            self.send_response(status)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        except ConnectionError:
            # nobody is listening any more, and `fail` writes through here too - so
            # an error response to a socket that just went away must not raise again
            self.close_connection = True

    def fail(self, message, status=400):
        self.send({"error": message}, status=status)

    def query(self):
        _, _, q = self.path.partition("?")
        return dict(p.split("=", 1) for p in q.split("&") if "=" in p)

    def do_GET(self):
        path = self.path.partition("?")[0]
        args = self.query()
        try:
            if path in ("/", "/index.html"):
                with open(PAGE, "rb") as f:
                    return self.send(f.read(), "text/html; charset=utf-8")
            if path == "/api/scenes":
                # `where` names what is being looked at, and the page puts it in the
                # heading: a desktop render and a board capture are the same picture
                # by design, so a screenshot has to say which one it was
                return self.send({"board": self.board.live.target,
                                  "elf": self.board.live.label,
                                  "where": ("the library" if self.board.live.target == "desktop"
                                            else "the board"),
                                  "controls": controls(),
                                  "scenes": catalogue(self.board)})
            if path == "/api/thumb":
                name = args.get("name", "")
                if name not in scenes.SCENES:
                    return self.fail("no such scene", 404)
                png = golden_png(self.board, name)
                if png is None:
                    return self.fail("no golden for this scene", 404)
                return self.send(png, "image/png")
            if path == "/api/state":
                return self.send(self.board.state(capture=args.get("capture") != "0"))
            if path == "/api/sweep":
                # `from` is how many results the page already has, so each picture
                # crosses once however often it polls
                return self.send(self.sweep.state(int(args.get("from", "0"))))
        except Exception as e:                                  # noqa: BLE001
            return self.fail("%s: %s" % (type(e).__name__, e), 500)
        self.fail("no such endpoint", 404)

    def do_POST(self):
        path = self.path.partition("?")[0]
        args = self.query()
        length = int(self.headers.get("Content-Length") or 0)
        if length > UPLOAD_LIMIT:
            return self.fail("that is %d bytes; a VDP dump is %d" % (length, scenes.DUMP_FULL))
        body = self.rfile.read(length) if length else b""
        # a write costs a frame over SWD and its answer lands after the monitor has
        # already moved, so the page can ask for the write on its own
        shot = args.get("capture") != "0"
        try:
            if path == "/api/show":
                name = args.get("name", "")
                if name not in scenes.SCENES and name not in self.board.uploaded:
                    return self.fail("no such scene", 404)
                return self.send(self.board.show(name))
            if path == "/api/sweep":
                if args.get("stop") == "1":
                    return self.send(self.sweep.stop())
                # the page sends what it is showing, so a filtered grid sweeps just
                # that - validated here, because a name the registry does not have
                # would fail inside the thread where nobody is looking
                names = [n for n in body.decode().split("\n") if n]
                unknown = [n for n in names if n not in scenes.SCENES]
                if unknown:
                    return self.fail("no such scene: %s" % ", ".join(unknown[:3]), 404)
                return self.send(self.sweep.start(names or sorted(scenes.SCENES)))
            if path == "/api/poke":
                reg, value = int(args.get("reg", "-1")), int(args.get("value", "0"))
                mask = int(args.get("mask", "255"))
                if not 0 <= reg < 64:
                    return self.fail("register %d is not one of the 64" % reg)
                return self.send(self.board.poke(reg, value, mask, shot))
            if path == "/api/unlock":
                return self.send(self.board.set_unlocked(args.get("on") != "0", shot))
            if path == "/api/conf":
                index = int(args.get("index", "-1"))
                if index not in DIAG_CONF:
                    return self.fail("not a diagnostic panel", 404)
                return self.send(self.board.set_conf(index, args.get("on") != "0", shot))
            if path == "/api/upload":
                return self.upload(args.get("name", "upload"), body)
        except Exception as e:                                  # noqa: BLE001
            return self.fail("%s: %s" % (type(e).__name__, e), 500)
        self.fail("no such endpoint", 404)

    def upload(self, name, body):
        """A dump dropped on the page. It is applied and shown, and it lives for
        this session only - writing it into dumps/ would make it a scene with a
        golden, which is a decision to take deliberately and not by drag and drop."""
        if len(body) not in (scenes.DUMP_FULL, scenes.VRAM_SIZE + 8):
            return self.fail(
                "%d bytes, which is neither a full VDP dump (%d) nor a short one (%d)%s"
                % (len(body), scenes.DUMP_FULL, scenes.VRAM_SIZE + 8,
                   " - that looks like a TI-99 cartridge" if body[:1] == b"\xaa" else ""))
        scratch = os.path.join(HERE, "dumps", ".upload.bin")
        os.makedirs(os.path.dirname(scratch), exist_ok=True)
        with open(scratch, "wb") as f:
            f.write(body)
        try:
            loaded = scenes.load_dump(scratch)
        finally:
            os.remove(scratch)
        key = "upload:" + os.path.basename(name)
        self.board.uploaded[key] = loaded
        out = self.board.show(key)
        out["name"] = key
        self.send(out)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    """`allow_reuse_address` is deliberately off. On Windows it does not mean what
    it means elsewhere: it lets a second process bind a port another is already
    listening on, and requests then go to whichever the kernel picks. A console
    left running from before a rename served a stale page from a path that no
    longer existed, which reads as the new one being broken. Failing to bind is
    the honest answer, and it has to be said rather than left out: HTTPServer sets
    it in the base class, so a subclass that simply omits it still inherits it."""

    daemon_threads = True
    allow_reuse_address = False

    def handle_error(self, request, client_address):
        """A browser closing a connection is not a fault here.

        The grid is a hundred thumbnails over a handful of keep-alive sockets, and
        a browser retires those whenever it rearranges its pool - which a click
        does. The thread parked on that socket then raises ConnectionAbortedError,
        and the default handler prints a traceback for it, so the console reads as
        broken while it is working. Anything that is not the peer leaving still
        gets printed."""
        if not isinstance(sys.exc_info()[1], (ConnectionError, TimeoutError)):
            super().handle_error(request, client_address)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    board_args(ap)
    ap.add_argument("--port", type=int, default=8919)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()
    args.flash = False

    # before the board, because opening the probe takes a second and a half and a
    # port that is already taken should say so straight away
    try:
        server = Server(("127.0.0.1", args.port), Handler)
    except OSError as e:
        raise SystemExit("port %d is already in use - another console is running (%s)"
                         % (args.port, e))

    with Board(args) as board:
        Handler.board = board
        Handler.sweep = Sweep(board)
        url = "http://127.0.0.1:%d/" % args.port
        print("%s  %s on %s" % (url, board.live.label, board.live.target))
        print("ctrl-c to stop")
        if not args.no_browser:
            webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
