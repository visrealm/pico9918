#!/usr/bin/env python3
"""The permanent performance record, generated from runs/ rather than maintained.

    python perflog.py            regenerate both artifacts
    python perflog.py --check    exit 1 if either is out of date, changing nothing

Two files, because they answer different questions:

  perf-history.jsonl  one object per run, oldest first. The database: every run
                      either board has recorded, so a regression can be located in
                      time instead of only against whatever ran last.
  PERF-LOG.md         the newest run per board, scene by scene, against the line
                      budget. What you read to see where a build stands.

Both are regenerated WHOLESALE from the records, never appended to. A new run
still adds exactly one line to the history, because the records before it have
not changed - so the diff stays one line while nothing can drift from the record
it claims to summarise, which an appended file eventually does.

`schema` is carried on every row and it is load-bearing. A schema-2 row has a null
`line` and no `worst` field at all, so it is a different instrument from a schema-3
one. Do not compare a schema-2 figure against a schema-3 one - the same rule
`results.latest` enforces by refusing to return them.
"""

import argparse
import contextlib
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# properties/ and web/ are folders rather than packages, exactly as runner.py has it
for folder in (HERE, os.path.join(HERE, "properties"), os.path.join(HERE, "web")):
    sys.path.insert(0, folder)

import results
import runner

HISTORY = os.path.join(HERE, "perf-history.jsonl")
LOG = os.path.join(HERE, "PERF-LOG.md")

BOARDS = (("2040", "PICO9918 (RP2040)"), ("pro", "PICO9918 PRO (RP2350)"))

# What each perf group was measured with. perf.py turns the overlay on
# deliberately, so neither of these is the shipping default - the diag-off cost is
# what freeze reports, and the budget below is a per-line figure either way.
SETTINGS = (("one", "diag master only"), ("all", "all four panels"))


def verdict_of(record):
    """runner.verdict is the single definition of pass and fail - a dropped row
    fails with the overlay off and is merely noted with it on - so the log reuses
    it rather than keeping a second copy that can disagree. It prints its notes as
    a side effect, which a bulk regeneration does not want.

    A record old enough to be missing a key it reaches for is reported as such
    rather than skipped: a row that cannot be judged is a fact about the record."""
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            return runner.verdict(record), None
    except (KeyError, TypeError) as e:
        return [], "%s: %s" % (type(e).__name__, e)


def worst_by_scene(record):
    """Per-scene worst-line microseconds for each panel setting. The worst line is
    what a budget is actually about; a mean cannot show it. Absent before schema 3,
    so a setting with nothing to report is left out rather than filled with nulls."""
    out = {}
    for setting, scenes in record.get("perf", {}).items():
        got = {n: e["worst"] for n, e in scenes.items() if e.get("worst") is not None}
        if got:
            out[setting] = got
    return out


def dropped_by_setting(record):
    out = {}
    for setting, scenes in record.get("freeze", {}).items():
        names = sorted(n for n, e in scenes.items() if e.get("dropped"))
        if names:
            out[setting] = names
    return out


def row(tag, record):
    run = record["run"]
    problems, unjudged = verdict_of(record)
    worst = worst_by_scene(record)
    budget = run.get("budget_us")
    clock = run.get("clock_hz")

    return {
        "tag": tag,
        "schema": record.get("schema"),
        "date": run.get("date"),
        "commit": run.get("commit"),
        "branch": run.get("branch"),
        "board": run.get("board"),
        "dirty": run.get("dirty"),
        "version": run.get("version"),
        "verdict": "UNJUDGED" if unjudged else ("PASS" if not problems else "FAIL"),
        "problems": problems,
        "unjudged": unjudged,
        "clock_mhz": round(clock / 1e6) if clock else None,
        "budget_us": budget,
        "stages": run.get("stages"),
        "sdk": run.get("sdk"),
        "toolchain": run.get("toolchain"),
        "options": run.get("options"),
        "sections": run.get("sections"),
        "scenes": len(results.scenes_touched(record)),
        "worst_us": worst,
        "worst_max": {s: list(max(v.items(), key=lambda kv: kv[1])) for s, v in worst.items()},
        "over_budget": {s: sorted(n for n, u in v.items() if budget and u > budget)
                        for s, v in worst.items()},
        "dropped": dropped_by_setting(record),
        # a GPU program's milliseconds belong to the board that ran it, so they are
        # logged beside its line budget - and never in the scene table, which is one
        # instrument measuring one thing per line
        "gpu": {n: {"ms": round(e["us"] / 1000.0, 1) if e.get("us") else None,
                    "state": e.get("state"), "credit": e.get("credit")}
                for n, e in (record.get("gpu") or {}).items()},
    }


def rows():
    """Oldest first, so a new run appends and the diff is one line. Records with no
    date sort first rather than crashing the sort."""
    out = [row(tag, results.read(tag)) for tag in results.available()]
    return sorted(out, key=lambda r: (r["date"] or "", r["tag"]))


def history_text(all_rows):
    return "".join(json.dumps(r, sort_keys=True) + "\n" for r in all_rows)


def scene_table(record, budget):
    """One row per scene, worst-first, so the scenes with no headroom are what you
    read first. Headroom is against the same per-line budget the runner uses."""
    one = record.get("perf", {}).get("one", {})
    every = record.get("perf", {}).get("all", {})
    if not one and not every:
        return ["_This run recorded no timings (the perf stages did not run)._", ""]

    # worst first, then by name: ties broken on set iteration order would reshuffle
    # the table on every regeneration, and a tracked file has to be reproducible
    names = sorted(set(one) | set(every),
                   key=lambda n: (-(one.get(n, {}).get("worst")
                                    or every.get(n, {}).get("worst") or 0), n))
    out = ["| scene | render | line | worst | headroom | worst, panels |",
           "|---|---:|---:|---:|---:|---:|"]
    for n in names:
        a, b = one.get(n, {}), every.get(n, {})
        worst = a.get("worst")
        head = "%+.1f" % (budget - worst) if (worst is not None and budget) else "-"
        out.append("| %s | %s | %s | %s | %s | %s |"
                   % (n, fmt(a.get("render")), fmt(a.get("line")), fmt(worst),
                      head, fmt(b.get("worst"))))
    out.append("")
    return out


def fmt(v):
    return "-" if v is None else "%.1f" % v


def log_text(all_rows):
    out = ["# Live performance log", "",
           "Generated by `perflog.py` from `runs/`. Do not edit: regenerate it.",
           "The full history, both boards, is `perf-history.jsonl`.", ""]

    for board, title in BOARDS:
        mine = [r for r in all_rows if r["board"] == board]
        out += ["## %s" % title, ""]
        if not mine:
            out += ["_No run recorded for this board yet._", ""]
            continue

        latest = mine[-1]
        record = results.read(latest["tag"])
        run = record["run"]
        budget = latest["budget_us"]

        out += ["| | |", "|---|---|",
                "| commit | `%s`%s |" % (latest["commit"], " **DIRTY**" if latest["dirty"] else ""),
                "| date | %s |" % latest["date"],
                "| firmware | %s |" % (latest["version"] or "?"),
                "| verdict | **%s** |" % latest["verdict"],
                "| clock | %s MHz |" % latest["clock_mhz"],
                "| line budget | %s us |" % budget,
                "| scenes | %d |" % latest["scenes"],
                "| stages | %s |" % ", ".join(latest["stages"] or []),
                "| SDK | %s |" % (latest["sdk"] or "?"),
                "| record | `runs/%s.json` (schema %s) |" % (latest["tag"], latest["schema"]),
                ""]

        for setting, label in SETTINGS:
            over = latest["over_budget"].get(setting)
            if over:
                out.append("- over budget, %s: %s" % (label, ", ".join("`%s`" % n for n in over)))
        for setting, names in sorted(latest["dropped"].items()):
            out.append("- dropped a line (%s): %s"
                       % (setting, ", ".join("`%s`" % n for n in names)))
        for name, gpu in sorted(latest["gpu"].items()):
            out.append("- GPU program `%s` by %s: %s%s"
                       % (name, gpu["credit"] or "?",
                          "%.1f ms" % gpu["ms"] if gpu["ms"] else "did not run",
                          "" if gpu["state"] == "ok" else " - **%s**" % gpu["state"]))
        for p in latest["problems"]:
            out.append("- **%s**" % p)
        out.append("")

        out += ["<details><summary>section sizes</summary>", "",
                "| section | bytes |", "|---|---:|"]
        for name, size in sorted((run.get("sections") or {}).items()):
            out.append("| `%s` | %d |" % (name, size))
        out += ["", "</details>", ""]

        out += scene_table(record, budget)

    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if either artifact is out of date, and write nothing")
    args = ap.parse_args()

    all_rows = rows()
    want = {HISTORY: history_text(all_rows), LOG: log_text(all_rows)}

    stale = []
    for path, text in want.items():
        try:
            with open(path, encoding="utf-8") as f:
                if f.read() == text:
                    continue
        except OSError:
            pass
        stale.append(path)

    if args.check:
        for path in stale:
            print("out of date: %s" % os.path.basename(path))
        if not stale:
            print("perf log up to date (%d runs)" % len(all_rows))
        return 1 if stale else 0

    for path in stale:
        with open(path, "w", encoding="utf-8") as f:
            f.write(want[path])
        print("wrote %s" % os.path.basename(path))
    print("%d runs, %d schema-3" % (len(all_rows), sum(1 for r in all_rows if r["schema"] == 3)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
