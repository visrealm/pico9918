#!/usr/bin/env python3
"""Emit the sampled history as JSON for history.html to draw.

    python chartdata.py > history-data.json

One object per board, holding the samples in branch order, each scene's readings
across them, and the per-mode median. The page draws; this decides what the numbers
mean, so there is one place where a scene is called optimised rather than newly
capable.
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import results
from timeline import samples
from gains import classify, UNUSABLE

MODES = ("gm1", "gm2", "mcm", "t40", "t80", "dump")


def commit_date(commit):
    """When the firmware was written, not when it was measured. A record's own date is
    the day the sample was taken, which for a history is every point at once."""
    out = subprocess.run(["git", "-C", os.path.join(HERE, "..", "..", ".."),
                          "log", "-1", "--format=%ad", "--date=short", commit],
                         capture_output=True, text=True)
    return out.stdout.strip() if out.returncode == 0 else ""


def mode_of(scene):
    head = scene.split("-")[0]
    return head if head in MODES else "dump"


def median(values):
    ordered = sorted(values)
    n = len(ordered)
    if not n:
        return None
    return ordered[n // 2] if n % 2 else (ordered[n // 2 - 1] + ordered[n // 2]) / 2


def board_data(board):
    found = [s for s in samples(board) if s[0] not in UNUSABLE]
    if not found:
        return None

    points, perf = [], []
    for idx, commit, tag in found:
        record = results.read(tag)
        run = record["run"]
        sha = commit if idx != 999 else run.get("commit", "")
        points.append({"idx": None if idx == 999 else idx,
                       "commit": sha or "tip",
                       "label": "tip" if idx == 999 else "#%d" % idx,
                       "date": commit_date(sha) or (run.get("date") or "")[:10],
                       "measured": (run.get("date") or "")[:10],
                       "subject": run.get("subject", "")})
        perf.append(record["perf"].get("one", {}))

    scenes = []
    for name in sorted(perf[-1]):
        values = [p.get(name, {}).get("render") for p in perf]
        arrived, first, last = classify(list(zip([p["idx"] or 999 for p in points], values)))
        if first is None:
            continue
        scenes.append({"name": name, "mode": mode_of(name), "values": values,
                       "arrived": arrived if arrived != 999 else None,
                       "delta": round(last - first, 2)})

    # the per-mode line is a median over the scenes that already worked: mixing in a
    # scene that gained its feature would draw the capability as a slowdown
    optimised = [s for s in scenes if s["arrived"] is None]
    series = {}
    for mode in MODES:
        rows = [s for s in optimised if s["mode"] == mode]
        if not rows:
            continue
        series[mode] = {
            "count": len(rows),
            "median": [median([r["values"][i] for r in rows
                               if r["values"][i] is not None]) for i in range(len(points))],
        }

    deltas = sorted(s["delta"] for s in optimised)
    gained = [s for s in scenes if s["arrived"] is not None]
    return {
        "board": board,
        "points": points,
        "series": series,
        "scenes": scenes,
        "summary": {
            "optimised": len(optimised),
            "gained": len(gained),
            "faster": sum(1 for d in deltas if d < 0),
            "slower": sum(1 for d in deltas if d > 0),
            "median": round(median(deltas), 2),
            "best": round(deltas[0], 2),
        },
    }


DATA_MARKER = "const RAW = null;"


def main():
    out = {b: d for b in ("2040", "pro") if (d := board_data(b))}
    if "--html" not in sys.argv:
        json.dump(out, sys.stdout, indent=1)
        return

    # the same substitution report.py does: one page, the records dropped into it, so
    # a generated page and a hand-loaded one are the same program on the same data
    page = os.path.join(HERE, "history.html")
    with open(page, encoding="utf-8") as f:
        html = f.read()
    if DATA_MARKER not in html:
        sys.exit("%s no longer contains %r" % (page, DATA_MARKER))
    filled = html.replace(DATA_MARKER, "const RAW = %s;" % json.dumps(out))
    dest = os.path.join(HERE, "history-report.html")
    with open(dest, "w", encoding="utf-8") as f:
        f.write(filled)
    print(dest)


if __name__ == "__main__":
    main()
