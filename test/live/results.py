#!/usr/bin/env python3
"""What a run leaves behind, so a result can outlive the terminal it was printed in.

Every runner here produces a record rather than only a verdict: which scenes
regressed, which rows the renderer never reached, what each scene cost, and what
each property asserted. `report.py` turns one into a page and two into a
comparison.

The metadata is the load-bearing half. A bare `{scene: {render, line}}` cannot
say which build produced it, so a saved control silently becomes a control for
some other commit - which has already cost this branch two rounds of numbers that
were not measuring the same thing. A record stamps the commit, the working tree's
cleanliness, the board, the ELF and the overlay state, and `report.py` refuses to
compare two runs whose overlay state differs.
"""

import datetime
import glob
import json
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from live9918 import EXE, PICO_SDK, tool

# 3: perf's `line` is the mean of `liveTestCapture.lineTimes`, `worst` is present,
#    and `render` carries two decimals. A schema-2 record has none of those three:
#    its `line` comes from a different instrument, it has no `worst`, and its
#    `render` is truncated to whole microseconds - which on the RP2040 is most of the
#    noise floor on its own. A different instrument, so it is refused rather than
#    compared.
#
# The number gates COMPARISON, so it moves when an existing measurement changes
# meaning - not when a new section appears. `gpu` was added without bumping it: an
# older record simply has no such key, nothing compares one run's GPU microseconds
# against another's, and refusing the whole record over an absent section would
# throw away every timing baseline this branch has.
SCHEMA = 3

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
RUNS_DIR = os.path.join(HERE, "runs")

# The sections this project actually argues about, in load order: what stays in
# flash, then what lives in SRAM. RESUME.md quotes `.text` and `.scratch_x`
# deltas against every step, and `.scratch_x` is the one with a hard ceiling -
# core 1's stack owns the bank above it.
#
# The flash and uninitialized ones matter as much on a cold-in-flash build:
# without `.uninitialized_data` here, moving 87 KB out of `.bss` reads as 87 KB
# saved rather than 87 KB relocated, which is the opposite of what happened.
SECTIONS = (".flashtext", ".rodata", ".flashcode", ".flashcode_sdk",
            ".text", ".data", ".bss", ".uninitialized_data",
            ".heap", ".scratch_x", ".scratch_y")

BUDGET_US = 63.6            # one scanline at the lowest clock the firmware offers

# What a change that merely resizes the library moves an untouched mode by, so a
# delta smaller than this is placement rather than work. Section 6a measured 0.45
# us on a smaller binary; with this much code on RP2040 it is nearer 1.5.
NOISE_FLOOR = {"2040": 1.5, "pro": 0.5}

# A systematic shift, and what separates one from placement noise. Both numbers are
# measured over 111 scenes rather than chosen:
#
#   same firmware, two runs   mean +0.00 us, each scene within 0.04, 40% agreement
#   library resized           mean -0.20 us, scenes span -0.77..+0.30, 66% agreement
#   one real one-line fix     mean -0.08 us, 111 of 111 the same way
#   the v1.3.0 library merge  mean +1.15 us, 108 of 111 slower
#
# So the mean alone cannot judge it - a resize moves untouched modes by more than a
# real fix does - and the direction alone cannot either. Together they separate
# cleanly, which matters because the merge above passed every golden and left every
# tracked assembly extract byte-identical: the device is the only surface that can
# see this class of regression at all.
DRIFT_US = 0.25
DRIFT_AGREE = 0.9
DRIFT_MIN_SCENES = 20


def firmware_version(elf):
    """The version CMake stamped into the image, read back out of the image. A
    record has to say what the binary was, not what the tree would build today -
    and picotool reads the same binary-info block a user sees over USB."""
    tools = sorted(glob.glob(os.path.join(PICO_SDK, "picotool", "*", "picotool", "picotool" + EXE)))
    tools = tools or [p for p in [shutil.which("picotool" + EXE)] if p]
    if not tools or not elf or not os.path.exists(elf):
        return None
    out = subprocess.run([tools[-1], "info", elf], capture_output=True, text=True)
    found = re.search(r"^\s*version:\s*(\S+)", out.stdout, re.M)
    return found.group(1) if found else None


def _git(*args, cwd=ROOT):
    try:
        out = subprocess.run(["git", "-C", cwd] + list(args),
                             capture_output=True, text=True, timeout=15)
        return out.stdout.strip() if out.returncode == 0 else ""
    except (OSError, subprocess.SubprocessError):
        return ""


def sections(elf):
    """Per-section sizes out of the ELF. Two records can then explain a timing
    difference with the size difference that caused it, which is the shape most
    of this branch's findings have taken."""
    size = tool("arm-none-eabi-size")
    if not elf or not os.path.exists(elf) or not os.path.exists(size):
        return {}
    try:
        out = subprocess.run([size, "-A", elf], capture_output=True, text=True,
                             timeout=20, check=True).stdout
    except (OSError, subprocess.SubprocessError):
        return {}
    found = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] in SECTIONS:
            try:
                found[parts[0]] = int(parts[1])
            except ValueError:
                pass
    return found


def cmake_cache(elf):
    """The CMakeCache above this ELF, reduced to the entries that change what gets
    built. Comparing two runs is only meaningful once these are on the page: the
    8bpp tier alone changes what half the scenes render, and the SDK and the
    toolchain are each a knob that can be turned without touching a line of ours."""
    if not elf:
        return {}
    directory = os.path.dirname(os.path.abspath(elf))
    for _ in range(6):
        cache = os.path.join(directory, "CMakeCache.txt")
        if os.path.isfile(cache):
            try:
                text = open(cache, errors="replace").read()
            except OSError:
                return {}
            return {m.group(1): m.group(2) for m in
                    re.finditer(r"^(PICO9918_\w+|PICO_SDK_PATH|PICO_BOARD|PICO_PLATFORM)"
                                r":\w+=(.*)$", text, re.M)}
        parent = os.path.dirname(directory)
        if parent == directory:
            break
        directory = parent
    return {}


def sdk_version(cache):
    """The SDK tree's own directory name, which is how the Pico install lays out
    its versions. A path that is not a versioned install is reported whole rather
    than reduced to a meaningless basename."""
    path = (cache.get("PICO_SDK_PATH") or "").rstrip("/\\")
    if not path:
        return None
    tail = os.path.basename(path)
    return tail if any(c.isdigit() for c in tail) else path


def toolchain(elf):
    """The compiler that produced *this ELF*, out of its own `.comment` section.

    Asking the SDK's gcc for its version answers for whichever toolchain the
    harness happens to point at, not the one that did the build - backwards for a
    field whose whole job is provenance, and silently wrong the moment two builds
    are compared across compilers. An ELF linked from objects built by more than
    one compiler carries more than one string, and all of them are reported."""
    readelf = tool("arm-none-eabi-readelf")
    if not elf or not os.path.exists(elf) or not os.path.exists(readelf):
        return None
    try:
        out = subprocess.run([readelf, "-p", ".comment", elf], capture_output=True,
                             text=True, timeout=20, check=True).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    seen = []
    for release, version in re.findall(r"GCC: \((.*?)\)\s+(\d[\d.]*)", out):
        arm = re.search(r"(\d+\.\d+\.Rel\d+)", release)
        entry = "gcc %s%s" % (version, " (Arm %s)" % arm.group(1) if arm else "")
        if entry not in seen:
            seen.append(entry)
    return "; ".join(seen) or None


def metadata(board, elf, clock_hz=None):
    """Everything needed to say what produced a number, and therefore everything
    two records need before a comparison between them means anything.

    `dirty` matters as much as `commit`: a reading taken over uncommitted edits is
    not a reading of that commit, and nothing else in the record can tell. The
    clock matters the same way - the budget assumes the 252 MHz floor and a user
    can clock up, so two sets of numbers could differ by a third and read as a
    regression."""
    cache = cmake_cache(elf)
    return {
        "date": datetime.datetime.now().replace(microsecond=0).isoformat(),
        "board": board,
        "version": firmware_version(elf),
        "commit": _git("rev-parse", "--short", "HEAD"),
        "subject": _git("log", "-1", "--format=%s"),
        "branch": _git("rev-parse", "--abbrev-ref", "HEAD"),
        "dirty": bool(_git("status", "--porcelain", "--untracked-files=no")),
        "elf": os.path.relpath(elf, ROOT).replace("\\", "/") if elf else None,
        "elf_built": (datetime.datetime.fromtimestamp(os.path.getmtime(elf))
                      .replace(microsecond=0).isoformat()) if elf and os.path.exists(elf) else None,
        "clock_hz": clock_hz,
        # the two numbers every reading is judged against, stamped rather than
        # looked up, so a record read years from now is judged by the budget and
        # the floor that were in force when it was taken
        "budget_us": BUDGET_US,
        "floor_us": NOISE_FLOOR.get(board, 0.5),
        "toolchain": toolchain(elf),
        "sections": sections(elf),
        "sdk": sdk_version(cache),
        "board_def": cache.get("PICO_BOARD"),
        "options": {k: v for k, v in cache.items() if k.startswith("PICO9918_")},
    }


def new(board, elf, clock_hz=None):
    """An empty record. Stages fill their own key and leave the others absent, so
    a partial run is readable as a partial run rather than as a run of zeros."""
    return {"schema": SCHEMA, "run": metadata(board, elf, clock_hz),
            "freeze": {}, "perf": {}, "properties": {}, "gpu": {}, "scenes": {}}


def scenes_touched(record):
    """Every scene name the run measured, across whichever stages ran. A scene
    reached by the timings but not the goldens still belongs in the snapshot."""
    names = set()
    for group in ("freeze", "perf"):
        for out in record.get(group, {}).values():
            names |= set(out)
    return sorted(names)


def path_for(record, suffix=None):
    run = record["run"]
    parts = [run["board"], run["commit"] or "nocommit"]
    if run["dirty"]:
        parts.append("dirty")
    if suffix:
        parts.append(suffix)
    return os.path.join(RUNS_DIR, "-".join(parts) + ".json")


def resolve(tag):
    """A tag names a file in runs/, a bare filename or a path - whichever the
    caller had to hand."""
    for candidate in (tag, os.path.join(RUNS_DIR, tag),
                      os.path.join(RUNS_DIR, tag + ".json")):
        if os.path.isfile(candidate):
            return candidate
    raise SystemExit("no run record called %r - runs/ holds %s"
                     % (tag, ", ".join(sorted(available())) or "nothing yet"))


def available():
    if not os.path.isdir(RUNS_DIR):
        return []
    return [f[:-5] for f in os.listdir(RUNS_DIR) if f.endswith(".json")]


def latest(board, exclude=None):
    """The newest record for this board, which is what `--against last` means.
    Records for the other board are skipped rather than reported: their timing
    delta measures the hardware, which is not what an automatic baseline is for.
    Name both records to `--compare` to read that one deliberately."""
    best = None
    for tag in available():
        if tag == exclude:
            continue
        try:
            run = load(tag)["run"]
        except (OSError, ValueError, SystemExit):
            continue
        if run["board"] != board:
            continue
        if best is None or run["date"] > best[1]["date"]:
            best = (tag, run)
    return best[0] if best else None


def drift(record, before, tag=None):
    """How this run's per-scene render time moved against an earlier record.

    `render` is the metric, for the same reason the history uses it: it is the
    renderer's own microseconds, and repeating a run moves it by hundredths. `line`
    and `worst` are whole microseconds off the device, so a systematic shift of a
    few tenths does not appear in them at all - the control pair above shows 95 of
    111 scenes reporting the identical `worst`. `render` also holds across harness
    paths, where they do not: `--only perf` reads `line` about 2 us below the same
    firmware's full-suite figure, and `render` to within 0.01.

    Returns None when the two are not comparable, which is a refusal rather than a
    gap: a different clock or a different set of build options makes every scene
    move, and reporting that as drift would train a reader to ignore the line that
    matters. Nothing here decides anything - `runner.verdict` reads the result out
    of the record, so an archived run is judged by what it was compared against at
    the time rather than against whatever is newest today."""
    if not before:
        return None
    run, was = record["run"], before["run"]
    if run.get("clock_hz") != was.get("clock_hz") or run.get("options") != was.get("options"):
        return None
    now = record.get("perf", {}).get("one", {})
    then = before.get("perf", {}).get("one", {})
    moves = []
    for name in sorted(set(now) & set(then)):
        a, b = then[name].get("render"), now[name].get("render")
        if a is not None and b is not None:
            moves.append(b - a)
    if len(moves) < DRIFT_MIN_SCENES:
        return None
    mean = sum(moves) / len(moves)
    worse = sum(1 for d in moves if d > 0)
    better = sum(1 for d in moves if d < 0)
    agree = max(worse, better) / len(moves)
    return {"against": tag, "scenes": len(moves), "mean_us": round(mean, 3),
            "worse": worse, "better": better, "agree": round(agree, 3),
            "systematic": abs(mean) >= DRIFT_US and agree >= DRIFT_AGREE}


def save(record, path=None, suffix=None):
    path = path or path_for(record, suffix)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(record, f, indent=1, sort_keys=True)
    return path


def read(tag):
    """A record exactly as it was written, whatever instrument wrote it.

    `load` is the gate, and refusing another schema is right for anything that
    compares two numbers. A history cannot work that way: not every sampled record
    is schema 3, and `render` means the same thing in all of them. A caller of this
    one is responsible for reading only the fields its rows share, which `line` and
    `worst` are not."""
    with open(resolve(tag)) as f:
        return json.load(f)


def load(tag):
    record = read(tag)
    if record.get("schema") != SCHEMA:
        raise SystemExit("%s is schema %s, this is schema %d"
                         % (tag, record.get("schema"), SCHEMA))
    return record


def describe(record):
    run = record["run"]
    clock = run.get("clock_hz")
    return "%s %s%s  %s  %s" % (
        run["board"], run["commit"] or "?", " DIRTY" if run["dirty"] else "",
        "%d MHz" % (clock / 1e6) if clock else "clock unknown", run["date"])

