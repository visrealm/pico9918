#!/usr/bin/env python3
"""The bench: every board on the desk, and everything the harness can do to one.

    python bench.py                 opens http://127.0.0.1:8920/
    python bench.py --no-browser

Each probe is a lane. A lane keeps one openocd session open to its board, so a
scene, a GPU program or a single stage runs in-process against it at once and
hands back pictures rather than text. The heavy jobs - a full suite with its
record, flashing, the bus flows - run the scripts here as child processes, after
the lane has let go of the probe, so the page and the command line stay the same
program.

A lane's queue is the only way to its probe: two jobs can never open the same one,
and the two boards still run side by side.
"""

import argparse
import base64
import functools
import glob
import http.server
import itertools
import json
import os
import queue
import shutil
import signal
import subprocess
import sys
import threading
import time
import traceback
import types
import webbrowser
from urllib.parse import unquote

HERE = os.path.dirname(os.path.abspath(__file__))
WORKTREE = os.path.dirname(os.path.dirname(HERE))
WORKSPACE = os.path.dirname(WORKTREE)
sys.path.insert(0, os.path.join(HERE, "web"))
import hostbus
import probe as probe_module
import report
import results
import suite.scenes as scenes
import suite.stages.freeze as freeze
import suite.stages.gpu as gpu
from console import Board, Server, catalogue, controls, golden_png
from live9918 import (DPIDR, EXE, PICO_SDK, Live, OpenOcd, arm_probe, elf_image, open_board,
                      runs_image)
from runner import FIXTURE, QUICK, RUNNERS, STAGES, TIMED
from suite.access.desktop import LIBRARY_ROOT, SHIM
from suite.stages import verdict

PAGE = os.path.join(HERE, "web", "bench.html")
JOBS = os.path.join(HERE, "jobs")
REPORTS = report.REPORTS_DIR
DEBUGPROBE = (0x2E8A, 0x000C)
TIERS = {"2040": "RP2040", "pro": "RP2350"}
TARGET = {"2040": "rp2040", "pro": "rp2350"}
BODY_LIMIT = 1 << 16
SEQUENCE = itertools.count()


class Cancelled(Exception):
    pass


class Router:
    """sys.stdout, per thread: a job's prints land in its own log, so two lanes
    running at once do not interleave."""

    def __init__(self, base):
        self.base = base
        self.local = threading.local()

    def target(self):
        return getattr(self.local, "out", None) or self.base

    def write(self, text):
        return self.target().write(text)

    def flush(self):
        self.target().flush()

    def __getattr__(self, name):
        return getattr(self.base, name)


class Sink:
    def write(self, text):
        return len(text)

    def flush(self):
        pass


ROUTER = Router(sys.stdout)
sys.stdout = ROUTER


def newest(pattern):
    found = sorted(glob.glob(pattern))
    return found[-1] if found else None


SDK = os.environ.get("PICO_SDK_PATH") or os.path.join(PICO_SDK, "sdk", "2.1.1")
HOST_CC = shutil.which("gcc") or (r"C:\msys64\ucrt64\bin\gcc.exe" if os.name == "nt" else "cc")
INHERITED = ("PICO_SDK_PATH", "PICO_TOOLCHAIN_PATH", "CMAKE_MAKE_PROGRAM")
BOARD_DIR = {"2040": "pico9918", "pro": "pico9918pro"}
ON = ("ON", "1", "TRUE", "YES")


class Firmware:
    """Every configured build folder in every worktree beside this one. A folder is
    rebuilt from its own CMake cache, so whatever configured it - the Builder or a
    hand - stays the one source of its options."""

    def __init__(self):
        self.cache = {}
        self.stamps = {}

    def _cached(self, kind, path, make):
        key = (kind, path, os.path.getmtime(path))
        if key not in self.cache:
            self.cache[key] = make()
        return self.cache[key]

    def image(self, elf):
        return self._cached("image", elf, lambda: elf_image(elf))

    def options(self, build):
        def make():
            out = {}
            with open(os.path.join(build, "CMakeCache.txt"), encoding="utf-8", errors="replace") as f:
                for line in f:
                    key, _, value = line.partition("=")
                    out[key.partition(":")[0]] = value.strip()
            return out
        path = os.path.join(build, "CMakeCache.txt")
        return self._cached("cache", path, make) if os.path.exists(path) else {}

    def stamp(self, worktree):
        """When the worktree's source last changed: its last commit, or a tracked
        file edited since."""
        hit = self.stamps.get(worktree)
        if hit and time.time() - hit[0] < 15:
            return hit[1]

        def git(*args):
            return subprocess.run(["git", "-C", worktree] + list(args), capture_output=True,
                                  text=True).stdout
        times = [int(git("log", "-1", "--format=%ct").strip() or 0)]
        for line in git("status", "--porcelain", "--untracked-files=no").splitlines():
            path = os.path.join(worktree, line[3:].strip().strip('"'))
            if os.path.isfile(path):
                times.append(os.path.getmtime(path))
        self.stamps[worktree] = (time.time(), max(times))
        return max(times)

    def entry(self, worktree, build, board, combined=None):
        opts = self.options(build)
        if combined is None:
            combined = opts.get("PICO9918_BUILD_COMBINED", "").upper() in ON
        src = os.path.join(build, BOARD_DIR[board], "src") if combined else os.path.join(build, "src")
        elf = newest(os.path.join(src, "*.elf"))
        uf2 = newest(os.path.join(src, "*.uf2"))
        built = os.path.getmtime(elf) if elf else None
        return {"id": build + "|" + board, "worktree": os.path.basename(worktree),
                "dir": build, "build": os.path.basename(build), "board": board,
                "configured": bool(opts), "combined": combined,
                "live": opts.get("PICO9918_LIVE_TEST", "").upper() in ON or not opts,
                "target": "build_" + BOARD_DIR[board] if combined else "firmware",
                "elf": elf, "uf2": uf2, "name": os.path.basename(elf) if elf else None,
                "mtime": built, "stale": bool(built and built < self.stamp(worktree))}

    def template(self, worktree):
        """The tools a new build folder should use: whatever configured the newest
        existing one, in this worktree first. The newest CMake installed is not a
        safe default - a major version can refuse a project an older SDK wrote."""
        caches = glob.glob(os.path.join(WORKSPACE, "*", "build*", "CMakeCache.txt"))
        caches.sort(key=lambda c: (os.path.dirname(os.path.dirname(c)) == worktree,
                                   os.path.getmtime(c)), reverse=True)
        for cache in caches:
            opts = self.options(os.path.dirname(cache))
            if opts.get("CMAKE_COMMAND"):
                return opts
        return {"PICO_SDK_PATH": SDK}

    def steps(self, e):
        """The commands, and the folder ninja is in: a combined build configures
        each board as a project of its own, and those find ninja on PATH alone."""
        opts = self.options(e["dir"])
        worktree = os.path.dirname(e["dir"])
        like = opts if opts.get("CMAKE_COMMAND") else self.template(worktree)
        cmake = like.get("CMAKE_COMMAND", "cmake")
        ninja = like.get("CMAKE_MAKE_PROGRAM")
        steps = [[cmake, "--build", e["dir"], "--target", e["target"]]]
        if like is not opts:
            steps.insert(0, [cmake, "-S", worktree, "-B", e["dir"], "-G", "Ninja"]
                         + ["-D%s=%s" % (k, like[k]) for k in INHERITED if like.get(k)]
                         + ["-DPICO9918_BUILD_COMBINED=ON", "-DPICO9918_LIVE_TEST=ON"])
        return steps, os.path.dirname(ninja) if ninja else None

    def builds(self, board=None):
        out = []
        for worktree in glob.glob(os.path.join(WORKSPACE, "*")):
            if not (os.path.isfile(os.path.join(worktree, ".git"))
                    and os.path.isfile(os.path.join(worktree, "CMakeLists.txt"))):
                continue
            live = set()
            for cache in glob.glob(os.path.join(worktree, "build*", "CMakeCache.txt")):
                build = os.path.dirname(cache)
                opts = self.options(build)
                combined = opts.get("PICO9918_BUILD_COMBINED", "").upper() in ON
                for tier in BOARD_DIR:
                    if (board in (None, tier)
                            and (combined or opts.get("PICO_BOARD") == BOARD_DIR[tier])):
                        e = self.entry(worktree, build, tier, combined)
                        out.append(e)
                        if e["live"]:
                            live.add(tier)
            fresh = os.path.join(worktree, "build-live")
            if not os.path.exists(os.path.join(fresh, "CMakeCache.txt")):
                out += [self.entry(worktree, fresh, tier, True) for tier in BOARD_DIR
                        if board in (None, tier) and tier not in live]
        return sorted(out, key=lambda e: (not e["configured"], -(e["mtime"] or 0)))

    def list(self, kind, board):
        return [{"path": e[kind]} for e in self.builds(board) if e[kind]]

    def describe(self, elf):
        build = os.path.dirname(elf)
        while not os.path.isfile(os.path.join(os.path.dirname(build), ".git")):
            if os.path.dirname(build) == build:
                return {"path": elf, "name": os.path.basename(elf)}
            build = os.path.dirname(build)
        tiers = {b: t for t, b in BOARD_DIR.items()}
        board = tiers.get(os.path.basename(os.path.dirname(os.path.dirname(elf))),
                          tiers.get(self.options(build).get("PICO_BOARD"), "2040"))
        e = self.entry(os.path.dirname(build), build, board)
        e["path"] = elf
        return e


FIRMWARE = Firmware()


class Session(Board):
    """The console's board, over a Live the lane opened rather than one argparse did."""

    def __init__(self, live):
        live.border = True
        self.live = live
        self.lock = threading.Lock()
        self.palette = None
        self.uploaded = {}
        self.thumbs = {}

    @property
    def can_capture(self):
        return getattr(self.live, "capture_addr", True) is not None


class Job:
    KEEP = ("id", "lane", "kind", "title", "spec", "status", "created", "started", "ended",
            "code", "argv", "outputs", "result", "progress", "frames")

    def __init__(self, lane, kind, title, spec, body=None, meta=None, hidden=False):
        self.id = time.strftime("%Y%m%d-%H%M%S-") + "%03d" % (next(SEQUENCE) % 1000)
        self.lane, self.kind, self.title, self.spec = lane, kind, title, spec
        self.status = "queued"
        self.created = time.time()
        self.started = self.ended = self.code = None
        self.argv, self.outputs, self.result = [], {}, None
        self.progress, self.frames = "", 0
        self.body, self.hidden = body, hidden
        self.cancelled = False
        self.proc = None
        if meta:
            self.__dict__.update({k: meta[k] for k in self.KEEP if k in meta})
        self.dir = os.path.join(JOBS, self.id)
        self.log = os.path.join(self.dir, "log.txt")

    def summary(self):
        return {k: getattr(self, k) for k in self.KEEP}

    def save(self):
        if not self.hidden:
            with open(os.path.join(self.dir, "job.json"), "w") as f:
                json.dump(self.summary(), f, indent=1)

    def execute(self, lane):
        self.status, self.started = "running", time.time()
        if not self.hidden:
            os.makedirs(self.dir, exist_ok=True)
            self.save()
        log = open(self.log, "wb") if not self.hidden else None
        ROUTER.local.out = JobWriter(self, log) if log else Sink()
        try:
            ok, self.result = self.body(lane, self)
            self.status = "passed" if ok else "failed"
        except Cancelled:
            self.status = "cancelled"
        except (Exception, SystemExit) as e:                            # noqa: BLE001
            self.status = "cancelled" if self.cancelled else "failed"
            if not self.cancelled:
                message = str(e) if isinstance(e, SystemExit) else traceback.format_exc()
                print(message.rstrip())
                self.result = {"error": str(e).splitlines()[0] if str(e) else type(e).__name__}
        finally:
            ROUTER.local.out = None
            if log:
                log.close()
        self.ended = time.time()
        self.save()

    def cancel(self):
        self.cancelled = True
        if self.status == "queued":
            self.status, self.ended = "cancelled", time.time()
            return
        proc = self.proc
        if proc and proc.poll() is None:
            # TRAP: killing python alone leaves its openocd holding the probe
            if os.name == "nt":
                subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)],
                               capture_output=True)
            else:
                os.killpg(proc.pid, signal.SIGKILL)

    def picture(self, lane, name="frame.png"):
        if lane.frame and not self.hidden:
            with open(os.path.join(self.dir, name), "wb") as f:
                f.write(lane.frame["png"])
            self.outputs[name] = name


class JobWriter:
    """A job's stdout. Writing is also where an in-process job notices it was
    cancelled: every stage prints as it goes, and none of them takes a flag."""

    def __init__(self, job, log):
        self.job, self.log = job, log

    def write(self, text):
        if self.job.cancelled:
            raise Cancelled()
        self.log.write(text.encode("utf-8", "replace"))
        self.log.flush()
        line = text.strip()
        if line:
            self.job.progress = line[:120]
        return len(text)

    def flush(self):
        self.log.flush()


class Lane:
    def __init__(self, key, serial=None, port=None, tier=None):
        self.key, self.serial, self.port, self.tier = key, serial, port, tier
        self.present = True
        self.hostbus = self.probe = None
        self.error = None
        self.power = "switch"
        self.session = None
        self.elf = None
        self.firmware = None
        self.info = {}
        self.frame = None
        self.connecting = False
        self.lock = threading.Lock()
        self.queue = queue.Queue()
        self.current = None
        threading.Thread(target=self.work, daemon=True).start()

    @property
    def desktop(self):
        return self.key == "desktop"

    @property
    def board(self):
        return self.key != "build"

    def cdc(self):
        return "%s:%s" % (self.port, self.power) if self.hostbus else None

    def env(self):
        env = dict(os.environ, PYTHONUNBUFFERED="1", PYTHONIOENCODING="utf-8")
        env.pop("LIVE9918_PROBE_CDC", None)
        if self.serial:
            env["LIVE9918_PROBE_RP2040"] = env["LIVE9918_PROBE_RP2350"] = self.serial
        if self.hostbus:
            env["LIVE9918_PROBE_CDC"] = self.cdc()
        return env

    def work(self):
        while True:
            job = self.queue.get()
            if job.cancelled:
                continue
            with self.lock:
                self.current = job
                try:
                    job.execute(self)
                finally:
                    self.current = None
                released = job.kind == "probe" and job.spec.get("name") == "off"
                if (self.board and self.present and self.session is None
                        and job.kind != "connect" and not released):
                    self.connect()
            if self.hostbus:
                self.query()

    # ---- the probe's own channel ---------------------------------------------
    def query(self, held=False):
        """INFO and STATE over the CDC port, when nothing else holds it."""
        if not self.port or (not held and not self.lock.acquire(blocking=False)):
            return
        try:
            with probe_module.Probe(self.port, timeout=1) as p:
                self.hostbus = p.info
                self.probe = p.command("STATE")
        except (Exception, SystemExit):                                # noqa: BLE001
            self.hostbus = self.probe = None
        finally:
            if not held:
                self.lock.release()

    # ---- the board session -----------------------------------------------------
    def close(self):
        if self.session:
            try:
                self.session.__exit__()
            except Exception:                                           # noqa: BLE001
                pass
            self.session = None

    def open(self, elf):
        live = Live(elf, probe=self.serial, cdc=self.cdc(), capture=False)
        session = Session(live)
        session.__enter__()
        return session

    def detect(self):
        if self.hostbus:
            arm_probe(self.port, self.power)
        # TRAP: only the rp2350 config logs the DP identity of both chips
        ocd = OpenOcd(TARGET["pro"], serial=self.serial)
        try:
            ocd.start()
        except RuntimeError:
            pass
        seen = ocd.dpidr()
        ocd.close()
        self.tier = next((t for t, c in TARGET.items() if DPIDR[c] == seen), None)

    def identify(self):
        """Which build the board is running, by reading two blocks of its flash and
        finding the ELF they came from - check_image's test, against every candidate."""
        if self.hostbus:
            arm_probe(self.port, self.power)
        ocd = OpenOcd(TARGET[self.tier], serial=self.serial)
        ocd.start()
        try:
            seen = ocd.dpidr()
            if seen != DPIDR[TARGET[self.tier]]:
                other = next((t for t, c in TARGET.items() if DPIDR[c] == seen), None)
                raise SystemExit("the board on this probe is %s, not %s" % (
                    TIERS[other] if other else "not answering", TIERS[self.tier]))
            read = functools.lru_cache(maxsize=None)(ocd.read)
            for f in FIRMWARE.list("elf", self.tier):
                if runs_image(read, FIRMWARE.image(f["path"])):
                    return f["path"]
            return None
        finally:
            ocd.close()

    def connect(self, elf=None):
        self.close()
        self.connecting = True
        self.error = None
        try:
            if self.desktop:
                self.session = Session(open_board(types.SimpleNamespace(desktop=True)))
                self.session.__enter__()
                self.firmware = {"name": "the library, in a process", "live": True}
            else:
                if not self.tier:
                    self.detect()
                if not self.tier:
                    raise SystemExit("no chip answered on this probe's SWD - is the board powered?")
                elf = elf or self.elf
                session = None
                if elf and os.path.exists(elf):
                    try:
                        session = self.open(elf)
                    except SystemExit:
                        session = None
                if session is None:
                    elf = self.identify()
                    if not elf:
                        self.firmware = {"unknown": True}
                        raise SystemExit("running something that is not any build in this "
                                         "workspace - flash one")
                    session = self.open(elf)
                self.session, self.elf = session, elf
                self.firmware = FIRMWARE.describe(elf)
                live = session.live
                self.info = {"clock_mhz": (live.clock_hz() or 0) // 1000000,
                             "hw": live.hw_version()}
            if self.session.can_capture:
                self.snap()
        except (Exception, SystemExit) as e:                            # noqa: BLE001
            self.close()
            self.error = " ".join(str(e).split()) or type(e).__name__
        finally:
            self.connecting = False

    def ensure(self, capture=True):
        if self.session is None:
            self.connect()
        if self.session is None:
            raise SystemExit(self.error or "no session")
        if capture and not self.session.can_capture:
            raise SystemExit("%s is not a live-test build, so there is no picture to read back"
                             % os.path.basename(self.elf or ""))
        return self.session

    def snap(self, frame=None):
        live = self.session.live
        png, rows = live.frame_png(frame)
        self.frame = {"png": png, "rows": rows, "at": time.time(), "aspect": 4 / 3,
                      "dropped": len(live.dropped)}
        return self.frame

    def summary(self):
        job = self.current
        status = ("absent" if not self.present else "busy" if job and not job.hidden
                  else "connecting" if self.connecting or (job and job.hidden)
                  else "ready" if self.session else "offline")
        frame = self.frame or {}
        return {"key": self.key, "serial": self.serial, "port": self.port, "tier": self.tier,
                "name": "Desktop" if self.desktop else TIERS.get(self.tier, "Probe"),
                "status": status, "error": self.error, "power": self.power,
                "hostbus": {k: self.hostbus.get(k) for k in ("firmware", "protocol", "swclk_hz")}
                if self.hostbus else None,
                "armed": (self.probe or {}).get("armed"), "switched": (self.probe or {}).get("switched"),
                "firmware": (FIRMWARE.describe(self.elf) if self.elf and self.session
                             else self.firmware), "info": self.info,
                "live": bool(self.session and self.session.can_capture),
                "frame": {k: frame[k] for k in ("rows", "at", "aspect", "dropped")} if frame else None,
                "job": job.summary() if job and not job.hidden else None,
                "queued": self.queue.qsize()}


# ---- what a lane can be asked to do --------------------------------------------

def passed_scene(name, entry, board):
    return entry["state"] in ("ok", "FREEZE", "CHANGED") or (
        entry["state"] == "OVER BUDGET" and scenes.over_budget_on(name, board))


def test_scene(name):
    if name not in scenes.SCENES:
        raise ValueError("no scene called %s" % name)

    def body(lane, job):
        s = lane.ensure()
        caught = {}

        def note(_, entry, *frame):
            caught["frame"] = frame[0] if frame else None
        entry = freeze.run(s.live, [name], progress=note)[name]
        print(freeze.line(name, entry))
        lane.snap(caught.get("frame"))
        job.picture(lane)
        golden = golden_png(s, name)
        if golden and entry["state"] != "ok":
            with open(os.path.join(job.dir, "golden.png"), "wb") as f:
                f.write(golden)
            job.outputs["golden.png"] = "golden.png"
        keep = {k: entry.get(k) for k in ("state", "why", "differ", "first", "rows")}
        keep["dropped"] = len(entry.get("dropped") or ())
        return passed_scene(name, entry, lane.tier), keep
    return name, body, {"capture": True}


def test_gpu(name, watch=True):
    if name not in gpu.PROGRAMS:
        raise ValueError("no GPU program called %s" % name)

    def body(lane, job):
        s = lane.ensure()
        t = s.live
        last = [0.0]

        def watching():
            if job.cancelled:
                raise Cancelled()
            if watch and time.time() - last[0] > 0.05:
                lane.snap()
                job.frames += 1
                job.progress = "frame %d" % job.frames
                last[0] = time.time()
            else:
                time.sleep(0.02)
        try:
            entry = gpu.run(t, [name], watching=watching)[name]
        except Cancelled:
            t.gpu_stop()
            raise
        print(gpu.line(name, entry))
        lane.snap()
        job.picture(lane)
        keep = {k: entry.get(k) for k in ("state", "why", "us", "frames", "wall_ms", "differ")}
        return entry["state"] in ("ok", "FREEZE"), keep
    return name, body, {"capture": True}


def test_stage(name, lane):
    if name not in RUNNERS:
        raise ValueError("no stage called %s" % name)
    if lane.desktop and (name in TIMED or name in FIXTURE):
        raise ValueError("%s needs a board" % name)
    if name in FIXTURE and not lane.hostbus:
        raise ValueError("%s needs the host-bus probe" % name)

    def body(lane, job):
        t = lane.ensure().live
        board = "desktop" if lane.desktop else lane.tier
        record = {"run": {"board": board}, "freeze": {}, "perf": {}, "properties": {},
                  "gpu": {}, "hostbus": {}}
        args = types.SimpleNamespace(filter=[], canaries=False, quick=False, probe_cdc=lane.cdc())
        print("== %s" % name)
        RUNNERS[name](t, record, args)
        problems = verdict(record)
        for p in problems:
            print("FAIL " + p)
        if lane.session.can_capture:
            lane.snap()
            job.picture(lane)
        return not problems, {"problems": problems}
    return "Stage " + name, body, {"capture": True}


def test_bus(group):
    if group not in hostbus.GROUPS:
        raise ValueError("no host-bus group called %s" % group)

    def body(lane, job):
        with probe_module.opened(probe_module.spec(lane.cdc())) as p:
            out = hostbus.run(p, [group])
        for f in out.get("failures", ()):
            print("FAIL %s" % (f,))
        for n in out.get("notes", ()):
            print(n)
        return out["ok"], {"failures": len(out.get("failures", ())), "checks": out.get("checks")}
    return "Host bus " + group, body, {"bus": True}


def probe_op(op):
    if op not in ("arm", "off", "cycle"):
        raise ValueError("no probe operation called %s" % op)

    def body(lane, job):
        lane.close()
        with probe_module.Probe(lane.port) as p:
            if op in ("off", "cycle"):
                p.off()
                print("released, power off")
            if op == "cycle":
                time.sleep(0.5)
            if op in ("arm", "cycle"):
                p.arm(lane.power)
                print("armed, %s power" % lane.power)
        lane.query(held=True)
        return True, None
    return {"arm": "Arm the probe", "off": "Release and power off",
            "cycle": "Power cycle"}[op], body, {"bus": True}


def py(*args):
    return [sys.executable, "-u"] + list(args)


def shim_steps():
    tools = FIRMWARE.template(WORKTREE)
    cmake = tools.get("CMAKE_COMMAND", "cmake")
    out = os.path.dirname(SHIM)
    configure = [cmake, "-S", os.path.join(LIBRARY_ROOT, "test", "suite", "shim"), "-B", out,
                 "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + tools.get("CMAKE_MAKE_PROGRAM", "ninja"),
                 "-DCMAKE_C_COMPILER=" + HOST_CC, "-DCMAKE_C_FLAGS=-O2"]
    if os.name == "nt":
        configure.append("-DCMAKE_EXE_LINKER_FLAGS=-static")
    return [configure, [cmake, "--build", out]]


def script(title, steps, after=None, needs=None, path=None):
    """Child processes, one after another, stopping at the first that fails."""
    def body(lane, job):
        lane.close()
        flags = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt"
                 else {"start_new_session": True})
        log = ROUTER.local.out.log
        env = lane.env()
        if path:
            env["PATH"] = path + os.pathsep + env.get("PATH", "")
        for argv in steps:
            job.argv = argv
            log.write(("> %s\n" % subprocess.list2cmdline(argv)).encode())
            job.proc = subprocess.Popen(argv, cwd=HERE, env=env, stdin=subprocess.DEVNULL,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **flags)
            for raw in job.proc.stdout:
                log.write(raw)
                log.flush()
                line = raw.decode("utf-8", "replace").strip()
                if line:
                    job.progress = line[:120]
                for prefix, key in (("record: ", "record"), ("report: ", "report")):
                    if line.startswith(prefix):
                        job.outputs[key] = os.path.normpath(os.path.join(HERE, line[len(prefix):]))
            job.code = job.proc.wait()
            if job.cancelled:
                raise Cancelled()
            if job.code:
                return False, None
        if after:
            after(lane)
        return True, None
    return title, body, needs or {}


def elf_args(lane):
    return ["--board", lane.tier] + (["--elf", lane.elf] if lane.elf else [])


def build(lane, spec, bench):
    """A request from the page, as (title, kind, body). Validated here, so nothing
    the page sends reaches a thread before it has been checked."""
    kind = spec.get("type")
    name = str(spec.get("name") or "")
    opts = spec.get("options") or {}
    if kind == "scene":
        title, body, needs = test_scene(name)
    elif kind == "gpu":
        title, body, needs = test_gpu(name, bool(opts.get("watch", True)))
    elif kind == "stage":
        title, body, needs = test_stage(name, lane)
    elif kind == "bus":
        title, body, needs = test_bus(name)
    elif kind == "probe":
        title, body, needs = probe_op(name)
    elif kind == "suite":
        argv = py("runner.py") + (["--quick"] if opts.get("quick") else [])
        if opts.get("save"):
            argv += ["--save"] + ([str(opts["label"])] if opts.get("label") else [])
        if opts.get("report"):
            argv += ["--report", "--against", "last"]
        argv += ["--desktop"] if lane.desktop else elf_args(lane)
        title, body, needs = script("Suite" + (" (quick)" if opts.get("quick") else ""), [argv])
    elif kind == "device":
        devices = {
            "intpin": ("/INT pin", lambda: py("intpin.py", *elf_args(lane)), {}),
            "flashcheck": ("R63 flash read", lambda: py("flashcheck.py", *elf_args(lane)), {}),
            "pendingflow": ("Display-change flow", lambda: py("pendingflow.py"), {"bus": True}),
            "busident": ("What is in the socket", lambda: py("busflash.py"), {"bus": True}),
        }
        if name not in devices:
            raise ValueError("no device test called %s" % name)
        label, argv, needs = devices[name]
        title, body, needs = script(label, [argv()], needs=needs)
    elif kind == "flash":
        path = str(opts.get("path") or "")
        listed = {f["path"] for k in ("elf", "uf2") for f in FIRMWARE.list(k, lane.tier)}
        if path not in listed:
            raise ValueError("not a build for this board")
        via = opts.get("via")

        def landed(lane, elf=os.path.splitext(path)[0] + ".elf"):
            lane.elf = elf if os.path.exists(elf) else None
        if via == "swd" and path.endswith(".elf"):
            title, body, needs = script("Flash " + os.path.basename(path),
                                        [py("live9918.py", "flash", path)], landed)
        elif via in ("bus", "verify") and path.endswith(".uf2"):
            title, body, needs = script(
                ("Flash over the bus " if via == "bus" else "Verify ") + os.path.basename(path),
                [py("busflash.py", "--uf2", path, "--write" if via == "bus" else "--verify")],
                landed if via == "bus" else None, {"bus": True})
        else:
            raise ValueError("cannot %s %s" % (via, os.path.basename(path)))
    elif kind == "build":
        want = os.path.normcase(os.path.normpath(str(opts.get("dir") or "")))
        e = next((e for e in FIRMWARE.builds(lane.tier)
                  if os.path.normcase(os.path.normpath(e["dir"])) == want), None)
        if e is None:
            raise ValueError("not a build folder for this board")
        steps, ninja = FIRMWARE.steps(e)

        def flash(_):
            elf = FIRMWARE.entry(os.path.dirname(e["dir"]), e["dir"], lane.tier)["elf"]
            if elf:
                bench.submit({"lane": lane.key, "type": "flash",
                              "options": {"path": elf, "via": "swd"}})
        title, body, needs = script(
            "Build %s/%s%s" % (e["worktree"], e["build"], " and flash" if opts.get("flash") else ""),
            steps, flash if opts.get("flash") else None, path=ninja)
    elif kind == "shim" and lane.desktop:
        title, body, needs = script("Build the desktop shim", shim_steps(),
                                    path=os.path.dirname(HOST_CC))
    else:
        raise ValueError("no such kind of job: %s" % kind)
    if lane.desktop and kind not in ("scene", "gpu", "stage", "suite", "shim"):
        raise ValueError("the desktop has no probe")
    if needs.get("bus") and not lane.hostbus:
        raise ValueError("%s needs the host-bus probe" % title)
    if not lane.present:
        raise ValueError("%s is not connected" % lane.key)
    return title, kind, body


class Bench:
    def __init__(self):
        self.lanes = {"desktop": Lane("desktop"), "build": Lane("build")}
        self.jobs = {}
        self.lock = threading.Lock()
        self.records = {}
        self.load()
        self.discover()
        if not os.path.exists(SHIM):
            self.submit({"lane": "desktop", "type": "shim"})

    def load(self):
        for path in glob.glob(os.path.join(JOBS, "*", "job.json")):
            try:
                with open(path) as f:
                    meta = json.load(f)
            except (OSError, ValueError):
                continue
            job = Job(meta.get("lane"), meta.get("kind"), meta.get("title") or meta.get("action"),
                      meta.get("spec"),
                      meta=meta)
            if job.status in ("queued", "running"):
                job.status = "interrupted"
            self.jobs[job.id] = job

    def discover(self):
        tiers = {os.environ.get("LIVE9918_PROBE_" + chip): tier
                 for tier, chip in TIERS.items() if os.environ.get("LIVE9918_PROBE_" + chip)}
        ports = {}
        try:
            from serial.tools import list_ports
            ports = {p.serial_number: p.device for p in list_ports.comports()
                     if (p.vid, p.pid) == DEBUGPROBE and p.serial_number}
        except ImportError:
            pass
        configured = os.environ.get("LIVE9918_PROBE_CDC", "")
        for serial in set(tiers) | set(ports):
            lane = self.lanes.get(serial)
            if lane is None:
                lane = self.lanes[serial] = Lane(serial, serial, tier=tiers.get(serial))
            lane.port = ports.get(serial)
            if lane.port and configured.partition(":")[0] == lane.port:
                lane.power = configured.partition(":")[2] or "switch"
        for lane in self.lanes.values():
            if lane.serial:
                lane.present = lane.serial in ports or lane.serial in tiers
        threads = [threading.Thread(target=lane.query) for lane in self.lanes.values()]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        for lane in self.lanes.values():
            if lane.present and lane.session is None and not lane.connecting and lane.board:
                self.connect(lane)

    def connect(self, lane):
        def body(lane, job):
            lane.connect()
            return True, None
        lane.queue.put(Job(lane.key, "connect", "Connect", {}, body, hidden=True))

    def submit(self, spec):
        lane = self.lanes.get(spec.get("lane"))
        if lane is None:
            raise ValueError("no such board")
        title, kind, body = build(lane, spec, self)
        job = Job(lane.key, kind, title, spec, body)
        with self.lock:
            self.jobs[job.id] = job
        (self.lanes["build"] if kind == "build" else lane).queue.put(job)
        return job

    def state(self):
        with self.lock:
            jobs = sorted(self.jobs.values(), key=lambda j: j.created, reverse=True)[:400]
        return {"worktree": os.path.basename(WORKTREE),
                "lanes": [lane.summary() for lane in self.lanes.values()],
                "jobs": [j.summary() for j in jobs]}

    def record_list(self, board):
        out = []
        for tag in results.available():
            try:
                path = results.resolve(tag)
            except SystemExit:
                continue
            key = (path, os.path.getmtime(path))
            if key not in self.records:
                record = results.read(tag)
                run = record.get("run", {})
                held, ROUTER.local.out = getattr(ROUTER.local, "out", None), Sink()
                try:
                    problems = verdict(record) if "freeze" in record else []
                except Exception:                                        # noqa: BLE001
                    problems = ["unreadable"]
                finally:
                    ROUTER.local.out = held
                self.records[key] = {
                    "tag": tag, "board": run.get("board"), "date": run.get("date"),
                    "commit": run.get("commit"), "branch": run.get("branch"),
                    "dirty": run.get("dirty"), "subject": run.get("subject"),
                    "version": run.get("version"), "clock_mhz": (run.get("clock_hz") or 0) // 1000000,
                    "seconds": run.get("seconds"), "stages": len(run.get("stages") or ()),
                    "all": len(run.get("allStages") or ()), "problems": problems}
            out.append(self.records[key])
        out = [r for r in out if not board or r["board"] == board]
        return sorted(out, key=lambda r: r["date"] or "", reverse=True)

    def report(self, tag, against=None):
        record = results.load(tag)
        was = results.load(against) if against else None
        path = report.report_path(tag + ("-vs-" + against if against else ""))
        held, ROUTER.local.out = getattr(ROUTER.local, "out", None), Sink()
        try:
            report.write(path, record, was, labels=(against, tag))
        finally:
            ROUTER.local.out = held
        return "/reports/" + os.path.basename(path)


def catalog():
    return {"scenes": catalogue(None),
            "gpu": [{"name": n, "credit": p.credit, "note": p.note, "timeout": p.timeout}
                    for n, p in gpu.PROGRAMS.items()],
            "stages": [{"name": s, "quick": s in QUICK, "timed": s in TIMED,
                        "fixture": s in FIXTURE} for s in STAGES],
            "bus": [{"name": n, "doc": (hostbus.GROUPS[n].__doc__ or "").strip().split("\n")[0]}
                    for n in hostbus.ORDER],
            "controls": controls(), "tiers": TIERS}


class Handler(http.server.BaseHTTPRequestHandler):
    bench = None
    origin = None
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        pass

    def send(self, body, kind="application/json", status=200):
        if kind == "application/json":
            body = json.dumps(body).encode()
        try:
            self.send_response(status)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        except ConnectionError:
            self.close_connection = True

    def fail(self, message, status=400):
        self.send({"error": message}, status=status)

    def query(self):
        _, _, q = self.path.partition("?")
        return {k: unquote(v) for k, v in (p.split("=", 1) for p in q.split("&") if "=" in p)}

    def file(self, root, name, kind):
        path = os.path.normpath(os.path.join(root, name))
        if os.path.dirname(path) != os.path.normpath(root) or not os.path.isfile(path):
            return self.fail("no such file", 404)
        with open(path, "rb") as f:
            self.send(f.read(), kind)

    def lane(self, args):
        lane = self.bench.lanes.get(args.get("lane", ""))
        if lane is None:
            raise LookupError("no such board")
        return lane

    def interactive(self, lane, fn, capture=True):
        """A click on the page: done now if the board is free, refused if it is not,
        and never queued behind a suite."""
        if not lane.lock.acquire(blocking=False):
            return self.send({"busy": True}, status=409)
        try:
            out = fn(lane.ensure(capture))
            if isinstance(out, dict) and "png" in out:
                lane.frame = {"png": base64.b64decode(out["png"]),
                              "rows": out["rows"], "aspect": out["aspect"],
                              "dropped": out["dropped"], "at": time.time()}
            return self.send(out)
        except SystemExit as e:
            return self.fail(str(e), 409)
        finally:
            lane.lock.release()

    def guard(self):
        if self.headers.get("Host") not in self.origin:
            self.fail("wrong host", 403)
            return False
        return True

    def do_GET(self):
        path = self.path.partition("?")[0]
        args = self.query()
        if not self.guard():
            return
        try:
            if path in ("/", "/index.html"):
                with open(PAGE, "rb") as f:
                    return self.send(f.read(), "text/html; charset=utf-8")
            if path == "/api/state":
                return self.send(self.bench.state())
            if path == "/api/catalog":
                return self.send(catalog())
            if path == "/api/firmware":
                return self.send(FIRMWARE.builds(args.get("board") or None))
            if path == "/api/records":
                return self.send(self.bench.record_list(args.get("board")))
            if path == "/api/job":
                job = self.bench.jobs.get(args.get("id", ""))
                if job is None:
                    return self.fail("no such job", 404)
                start = int(args.get("from", "0"))
                text = b""
                if os.path.exists(job.log):
                    with open(job.log, "rb") as f:
                        f.seek(start)
                        text = f.read()
                return self.send({"text": text.decode("utf-8", "replace"),
                                  "next": start + len(text), "job": job.summary()})
            if path == "/frame.png":
                frame = self.lane(args).frame
                return self.send(frame["png"], "image/png") if frame else self.fail("no frame", 404)
            if path == "/thumb.png":
                lane, name = self.lane(args), args.get("name", "")
                session = lane.session
                if session is None or session.palette is None or name not in scenes.SCENES:
                    return self.fail("no thumbnail", 404)
                if name not in session.thumbs:
                    session.thumbs[name] = golden_png(session, name)
                png = session.thumbs[name]
                return self.send(png, "image/png") if png else self.fail("no golden", 404)
            if path == "/api/regs":
                return self.interactive(self.lane(args), lambda s: s.state(capture=False), False)
            if path.startswith("/jobs/"):
                _, _, job_id, name = path.split("/", 3)
                return self.file(os.path.join(JOBS, job_id), name, "image/png")
            if path.startswith("/reports/"):
                return self.file(REPORTS, path[len("/reports/"):], "text/html; charset=utf-8")
        except LookupError as e:
            return self.fail(str(e), 404)
        except Exception as e:                                          # noqa: BLE001
            return self.fail("%s: %s" % (type(e).__name__, e), 500)
        self.fail("no such endpoint", 404)

    def do_POST(self):
        path = self.path.partition("?")[0]
        args = self.query()
        # a page on another origin cannot set this header without a preflight this
        # server never answers, so it cannot start a flash from a browser tab
        if self.headers.get("X-Bench") != "1" or not self.guard():
            return self.fail("refused", 403)
        length = int(self.headers.get("Content-Length") or 0)
        if length > BODY_LIMIT:
            return self.fail("too large")
        body = self.rfile.read(length) if length else b""
        shot = args.get("capture") != "0"
        try:
            if path == "/api/run":
                return self.send(self.bench.submit(json.loads(body or b"{}")).summary())
            if path == "/api/cancel":
                job = self.bench.jobs.get(args.get("id", ""))
                if job is None:
                    return self.fail("no such job", 404)
                job.cancel()
                return self.send(job.summary())
            if path == "/api/refresh":
                self.bench.discover()
                return self.send(self.bench.state())
            if path == "/api/connect":
                lane = self.lane(args)
                self.bench.connect(lane)
                return self.send(lane.summary())
            if path == "/api/power":
                lane = self.lane(args)
                if args.get("mode") not in ("switch", "manual"):
                    return self.fail("no such mode")
                lane.power = args["mode"]
                return self.send(lane.summary())
            if path == "/api/report":
                tag = args.get("tag", "")
                against = args.get("against") or None
                if tag not in results.available() or (against and against not in results.available()):
                    return self.fail("no such record", 404)
                return self.send({"url": self.bench.report(tag, against)})
            lane = self.lane(args)
            if path == "/api/capture":
                return self.interactive(lane, lambda s: s.state())
            if path == "/api/show":
                name = args.get("name", "")
                if name not in scenes.SCENES:
                    return self.fail("no such scene", 404)
                return self.interactive(lane, lambda s: s.show(name))
            if path == "/api/poke":
                reg, value = int(args.get("reg", "-1")), int(args.get("value", "0"))
                if not 0 <= reg < 64:
                    return self.fail("register %d is not one of the 64" % reg)
                mask = int(args.get("mask", "255"))
                return self.interactive(lane, lambda s: s.poke(reg, value, mask, shot), shot)
            if path == "/api/unlock":
                return self.interactive(lane, lambda s: s.set_unlocked(args.get("on") != "0"))
            if path == "/api/conf":
                index = int(args.get("index", "-1"))
                return self.interactive(lane, lambda s: s.set_conf(index, args.get("on") != "0", shot), shot)
        except LookupError as e:
            return self.fail(str(e), 404)
        except ValueError as e:
            return self.fail(str(e))
        except Exception as e:                                          # noqa: BLE001
            return self.fail("%s: %s" % (type(e).__name__, e), 500)
        self.fail("no such endpoint", 404)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=8920)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()
    try:
        server = Server(("127.0.0.1", args.port), Handler)
    except OSError as e:
        raise SystemExit("port %d is already in use - is a bench running? (%s)" % (args.port, e))
    Handler.origin = {"127.0.0.1:%d" % args.port, "localhost:%d" % args.port}
    Handler.bench = Bench()
    url = "http://127.0.0.1:%d/" % args.port
    print("%s  ctrl-c to stop" % url)
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
