#!/usr/bin/env python3
"""Preload a pair of run records into report.html.

    python report.py runs/2040-bce997d.json
    python report.py runs/2040-bce997d.json --against last
    python report.py --compare 2040-aba934b 2040-bce997d
    python report.py --list

The page is `report.html` beside this file and it is the whole report: open it and
drop one record on it, or two. This script only substitutes a pair into a copy of
it, so a generated report and a hand-loaded one are the same program looking at
the same data - there is no second implementation to disagree.
"""

import argparse
import io
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import results

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, "report.html")
# beside the records they are made from, not beside the page that renders them
REPORTS_DIR = os.path.join(os.path.dirname(HERE), "reports")

# what report.html leaves for the records to replace
DATA_MARKER = "const RAW = null;"
TITLE_MARKER = "<title>Scanline Report</title>"


def default_path(record, suffix=None):
    """Beside the record and named after it - including its suffix, or two runs of
    the same commit write one file and the second silently replaces the first."""
    return report_path(os.path.basename(results.path_for(record, suffix))[:-5])


def report_path(base):
    os.makedirs(REPORTS_DIR, exist_ok=True)
    return os.path.join(REPORTS_DIR, base + ".html")


def write(path, record, against=None, view=None, labels=None):
    """Embed the records themselves, not a summary of them. Everything the page
    shows is derived in the browser from exactly these two objects, so a page
    handed a different pair renders identically to one generated for them."""
    if against and against["run"]["board"] != record["run"]["board"]:
        print("  note: comparing %s against %s - every timing delta is what the hardware costs"
              % (record["run"]["board"], against["run"]["board"]))
    labels = labels or (None, None)
    data = {"a": against, "b": record, "view": view or ("compare" if against else "budget"),
            "labels": {"a": labels[0], "b": labels[1]}}
    if against:
        title = "Scanline Diff %s to %s" % (against["run"]["commit"] or "?",
                                            record["run"]["commit"] or "?")
    else:
        title = "Scanline Report %s %s" % (record["run"]["board"], record["run"]["commit"] or "")

    page = io.open(TEMPLATE, encoding="utf-8").read()
    for marker in (DATA_MARKER, TITLE_MARKER):
        if page.count(marker) != 1:
            raise RuntimeError("report.html no longer has exactly one `%s` to substitute" % marker)
    page = page.replace(DATA_MARKER, "const RAW = %s;" % json.dumps(data))
    page = page.replace(TITLE_MARKER, "<title>%s</title>" % title.strip())

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    io.open(path, "w", encoding="utf-8", newline="\n").write(page)
    return path



def main():
    ap = argparse.ArgumentParser(
        description="Render a run record, or the difference between two of them. Two records may "
                    "come from different boards, clocks, compilers or build options - the page "
                    "reports every field that differs, and where the timing is not like-for-like it "
                    "presents the deltas as what the hardware costs rather than as a change to act "
                    "on.")
    ap.add_argument("record", nargs="?", help="a run record, or a tag in runs/")
    ap.add_argument("--against", metavar="TAG",
                    help="the baseline; `last` picks the newest record for the same board")
    ap.add_argument("--compare", nargs=2, metavar=("BEFORE", "AFTER"),
                    help="two arbitrary records, opened on the difference")
    ap.add_argument("--out", metavar="PATH")
    ap.add_argument("--list", action="store_true", help="what is in runs/")
    args = ap.parse_args()

    if args.list or not (args.record or args.compare):
        for tag in sorted(results.available()):
            print("%-34s %s" % (tag, results.describe(results.load(tag))))
        return 0

    # the output is named after the *records*, not their metadata: two runs of one
    # commit share every metadata field, so deriving the name from those would have
    # the second silently replace the first
    def stem(tag):
        return os.path.basename(results.resolve(tag))[:-5]

    labels = (None, None)
    if args.compare:
        against, record = (results.load(t) for t in args.compare)
        labels = (stem(args.compare[0]), stem(args.compare[1]))
        out = args.out or report_path("diff-%s-to-%s" % labels)
    else:
        record = results.load(args.record)
        against = None
        if args.against == "last":
            tag = results.latest(record["run"]["board"], exclude=stem(args.record))
            against = results.load(tag) if tag else None
            if not against:
                print("nothing saved for this board to compare against")
        elif args.against:
            against = results.load(args.against)
        if against:
            labels = (stem(tag if args.against == "last" else args.against), stem(args.record))
        out = args.out or report_path(stem(args.record))

    write(out, record, against, labels=labels)
    try:
        shown = os.path.relpath(out)
    except ValueError:  # another drive has no path relative to this one
        shown = os.path.abspath(out)
    print("wrote %s" % shown)
    return 0


if __name__ == "__main__":
    sys.exit(main())
