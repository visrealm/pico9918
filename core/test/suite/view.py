#!/usr/bin/env python3
"""Watch the suite run, at sixty frames a second.

    python view.py                     every scene, in order - the sweep IS the motion
    python view.py gm1 t80             only scenes matching one of these
    python view.py --hold 0.2          less time on each scene
    python view.py --scroll            turn the scroll register, so a scene moves
    python view.py --free --scroll dump-F18A_Karts_demo    one scene, scrolling, forever
    python view.py --gpu               watch a GPU program draw, then draw it again
    python view.py --gpu --once        just the once

**A scene is a still image.** It is a register file and a VRAM image with nothing
writing to it, so the renderer draws the identical picture every frame - sixty a
second of a scene that does not change looks exactly like one frame, because it is
one picture. The sweep is what moves: a hundred and eleven scenes, each for half a
second. `--scroll` is what moves *within* a scene, and the register goes back before
the capture that decides the verdict.

**`--gpu` is the exception: a GPU program draws.** It is TMS9900 code running on the
shim's own thread while this side renders, which is the shape the device has - the
program on core 0, the renderer on core 1 - so the picture genuinely builds up. The
C core draws Tursi's Mandelbrot in about a tenth of a second, which is a flash and
not a drawing, so it is **filmed and played back**: every frame grabbed as fast as
the shim will answer, then replayed at sixty a second. Every frame is a real render
of real intermediate VRAM; the playback rate is the only thing chosen. A board takes
four seconds and would need none of that, which is the honest difference between the
two rather than a trick in either.

The runner tells you a scene passed. This shows you the picture it passed on, live,
while the comparison that decides it happens in the same loop - so a regression is
something you SEE rather than a pixel count you go and look up afterwards.

**It runs the real freeze stage.** `freeze.run` applies each scene, plays it through
this window, captures it and compares it against the same frozen reference
`runner.py` uses. Nothing here re-implements the test; the viewer is the hook the
stage already calls between applying a scene and capturing it, and the frames it
plays are ahead of that capture, so watching cannot change the verdict.

Desktop only, and the reason is not portability - it is that a board renders 60
frames a second and can hand over about one, so a window fed over SWD would be a
slideshow of a thing it cannot show. Use `web/console.py` for a board.

Everything here is the standard library: tkinter loads a raw PPM straight into a
PhotoImage, which measures 1.7 ms for a 512x384 frame - a tenth of the frame budget
- so the pictures are built in C by the shim and this only has to display them.
"""

import argparse
import os
import sys
import threading
import time
import tkinter

HERE = os.path.dirname(os.path.abspath(__file__))

import suite.stages.freeze as freeze
import suite.stages.gpu as gpu
import suite.scenes as scenes
from suite.access.desktop import Desktop

FPS = 60.0
HOLD_S = 0.5            # how long each scene stays on screen before it is captured

# Frames kept while a GPU program draws. It has to be a cap: the shim renders one in
# about 0.6 ms and this side does nothing else, so an unbounded reel of a
# half-megabyte frames would be hundreds of megabytes of a tenth of a second. When it
# fills, every second frame goes and the grab interval doubles - so the reel always
# spans the WHOLE run, at whatever resolution fits, rather than the first two seconds
# of it.
REEL_MAX = 120

# What each verdict looks like. `ok` is deliberately quiet - a hundred greens in a
# row is a wall of colour nobody reads, and the two that matter have to jump out.
COLOURS = {"ok": "#3a7d7d", "REGRESSION": "#c0392b", "CHANGED": "#b8860b",
           "FREEZE": "#b8860b", "OVER BUDGET": "#b8860b"}


class Viewer:
    def __init__(self, root, names, hold, free, scroll, run_gpu=False, once=False):
        self.names = names
        self.hold = hold
        self.free = free
        self.scroll = scroll
        self.run_gpu = run_gpu
        self.once = once
        self.reel = []              # frames grabbed while a GPU program draws
        self.every = 1              # grabs per frame kept - see REEL_MAX
        self.grabs = 0
        self.frame = None           # newest PPM, handed over by the driver thread
        self.title = ""
        self.verdicts = []
        self.stop = False
        self.paused = False
        self.frames = 0
        self.started = time.perf_counter()
        self.fps = 0.0
        self._held = None           # Tk drops an image the moment nothing refers to it

        root.title("PICO9918 - the library, live")
        root.configure(bg="#111")
        self.canvas = tkinter.Canvas(root, width=1024, height=768, bg="#000",
                                     highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.item = self.canvas.create_image(0, 0, anchor="nw")
        self.status = tkinter.Label(root, text="starting", anchor="w", bg="#111", fg="#ddd",
                                    font=("Consolas", 11), padx=10, pady=6)
        self.status.pack(fill="x")
        root.bind("<Escape>", lambda e: root.destroy())
        root.bind("<space>", self.toggle)

    def toggle(self, _=None):
        self.paused = not self.paused

    # ---- the driver, on its own thread ------------------------------------
    def play(self, name, seconds=None):
        """Render and show frames for a while, paced to 60 a second.

        The pacing is here rather than in the shim because this is the side with a
        clock it can afford to wait on: a frame renders in well under a millisecond,
        so the shim's job is to answer and this one's is to decide when to ask.

        **A scene does not move by itself.** It is a register file and a VRAM image
        with nothing writing to it, so the renderer produces the identical picture
        every frame and sixty of them a second look like one. `--scroll` turns the
        fine-scroll register instead, which is the cheapest honest motion there is -
        and worth watching for its own sake, since scroll tearing is the kind of
        thing an eye catches and a byte comparison cannot.

        The register is put back before this returns. It has to be: the capture that
        decides the verdict happens next, and a scene left mid-scroll would not match
        the reference it was frozen at."""
        self.title = name
        deadline = time.perf_counter() + (self.hold if seconds is None else seconds)
        period = 1.0 / FPS
        nextAt = time.perf_counter()
        scrollFrom = self.board.regs()[0x1B] if self.scroll else None
        step = 0
        try:
            while not self.stop and (seconds == 0 or time.perf_counter() < deadline):
                while self.paused and not self.stop:
                    time.sleep(0.05)
                if scrollFrom is not None:
                    step += 1
                    self.board.reg(0x1B, (scrollFrom + step) & 0xFF)
                self.frame = self.board.view()
                self.frames += 1
                nextAt += period
                rest = nextAt - time.perf_counter()
                if rest > 0:
                    time.sleep(rest)
                else:
                    # behind rather than ahead: drop the debt instead of sprinting to
                    # catch up, which would show a burst of frames at the wrong rate
                    nextAt = time.perf_counter()
        finally:
            if scrollFrom is not None:
                self.board.reg(0x1B, scrollFrom)

    # ---- watching a GPU program draw --------------------------------------
    def grab(self):
        """One frame, while a GPU program is drawing underneath it.

        Called as fast as this loop can go, because the drawing IS the motion and
        there is no waiting to do: the program runs on the shim's own thread, so
        every frame here is a real render of real intermediate VRAM."""
        self.grabs += 1
        if self.grabs % self.every:
            return
        self.reel.append(self.board.view())
        if len(self.reel) >= REEL_MAX:
            del self.reel[::2]
            self.every *= 2

    def replay(self, name):
        """Play the reel at sixty frames a second.

        The C core draws this program in about a tenth of a second, and a tenth of a
        second is a flash rather than a picture being drawn. So it is filmed and
        played back - every frame real, the playback rate the only thing chosen here.
        A board takes four seconds and needs none of this, which is the honest
        difference between the two and not a trick either way."""
        period = 1.0 / FPS
        nextAt = time.perf_counter()
        for at, data in enumerate(self.reel):
            if self.stop:
                return
            while self.paused and not self.stop:
                time.sleep(0.05)
            self.title = "%s   drawing %d/%d" % (name, at + 1, len(self.reel))
            self.frame = data
            self.frames += 1
            nextAt += period
            rest = nextAt - time.perf_counter()
            time.sleep(rest) if rest > 0 else None

    def drive_gpu(self):
        """The GPU programs, on repeat until the window closes.

        It runs the real stage: `gpu.run` applies each program, starts it, waits for
        it - which is where the frames are grabbed - captures the result and compares
        it against the same frozen reference `runner.py` uses. So what is on screen is
        what was tested, and watching cannot change the verdict."""
        while not self.stop:
            self.reel, self.every, self.grabs = [], 1, 0
            gpu.run(self.board, self.names, progress=self.verdict, watching=self.grab)
            if self.once:
                return

    def verdict(self, name, entry, frame=None):
        if self.reel:
            # after the compare, not during: what gets played is the drawing the
            # verdict was reached on
            self.replay(name)
        self.verdicts.append((name, entry))
        # the window is the point, but a run still leaves the same line in the
        # terminal that freeze.py or gpu.py would: watching is not a reason to lose
        # the log, and a GPU line carries the microseconds and the credit
        print((gpu.line if "us" in entry else freeze.line)(name, entry), flush=True)
        if entry["state"] != "ok":
            # linger on anything that is not a pass, and show the picture that failed
            # rather than the next scene's - this is the whole reason to watch
            self.play(name, 1.5)
        elif self.reel:
            # and linger on a finished drawing, so it can be looked at
            self.play(name, self.hold)

    def drive(self):
        try:
            with Desktop() as board:
                self.board = board
                if self.run_gpu:
                    self.drive_gpu()
                    return
                if self.free:
                    scenes.apply(board, self.names[0])
                    self.play(self.names[0], 0)          # 0 seconds means forever
                    return
                freeze.run(board, self.names, progress=self.verdict, between=self.play)
        except BaseException as e:                       # noqa: BLE001
            self.title = "stopped: %s" % e
        self.stop = True

    # ---- the window -------------------------------------------------------
    def tick(self, root):
        data = self.frame
        if data:
            photo = tkinter.PhotoImage(data=data, master=root)
            # Tk scales by whole numbers only, which is exactly right for a picture
            # made of square pixels: 2x a 512-wide line fills a 1024 window with no
            # resampling and no blur
            photo = photo.zoom(2, 2)
            self.canvas.itemconfig(self.item, image=photo)
            self._held = photo

        now = time.perf_counter()
        if now - self.started > 0.5:
            self.fps = self.frames / (now - self.started)
            self.frames, self.started = 0, now

        bad = [n for n, e in self.verdicts if e["state"] == "REGRESSION"]
        moved = [n for n, e in self.verdicts if e["state"] in ("CHANGED", "OVER BUDGET")]
        last = self.verdicts[-1][1]["state"] if self.verdicts else ""
        self.status.configure(
            text="%-34s %4d/%-4d %-11s %2d regressed  %2d to look at   %4.1f fps%s"
                 % (self.title, len(self.verdicts),
                    max(len(self.names), len(self.verdicts)), last,
                    len(bad), len(moved), self.fps, "   PAUSED" if self.paused else ""),
            fg=COLOURS.get(last, "#ddd"))

        if self.stop and not self.frame:
            return
        root.after(8, lambda: self.tick(root))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("filter", nargs="*", help="substrings; empty means every scene")
    ap.add_argument("--hold", type=float, default=HOLD_S, metavar="S",
                    help="seconds each scene stays on screen (default %.1f)" % HOLD_S)
    ap.add_argument("--free", action="store_true",
                    help="hold the first matching scene and let it run, no sweep")
    ap.add_argument("--scroll", action="store_true",
                    help="turn the fine-scroll register every frame, so there is something "
                         "to see: a scene is a still image and does not move by itself")
    ap.add_argument("--gpu", action="store_true",
                    help="run GPU programs instead of scenes, and watch each one draw")
    ap.add_argument("--once", action="store_true",
                    help="with --gpu, stop after one pass instead of repeating")
    args = ap.parse_args()

    names = (gpu.select(args.filter) if args.gpu else freeze.select(args.filter))
    root = tkinter.Tk()
    viewer = Viewer(root, names, args.hold, args.free, args.scroll, args.gpu, args.once)
    driver = threading.Thread(target=viewer.drive, daemon=True)
    driver.start()
    viewer.tick(root)
    try:
        root.mainloop()
    finally:
        viewer.stop = True
        driver.join(timeout=3)

    bad = [n for n, e in viewer.verdicts if e["state"] == "REGRESSION"]
    print("%d scenes, %d regressed%s" % (len(viewer.verdicts), len(bad),
                                         ": " + ", ".join(bad) if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
