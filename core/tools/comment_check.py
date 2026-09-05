# comment_check.py
#
# Refuse multi-line prose comments inside function bodies.
#
# Copyright (c) 2026 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918-core
#
# The rule this enforces: inside a function body, a comment gets one line. If it
# needs more than one, it is either a table or a warning, and it says which.
#
#   - .c files under src/ only. A header's blocks document the contract, and
#     `extern "C" {` makes brace depth meaningless there anyway.
#   - a /** block is documentation, not prose.
#   - a table stays, however long. A table is two or more rows sharing a separator
#     ('|' or ':') at the same column, which is what a byte or bit layout looks like
#     whichever character it was drawn with.
#   - a block opening with a tag from TAGS may run to MAX_TAGGED lines. The tag is
#     the point: a paragraph earns its place by warning about something, not by
#     narrating what the code below does.
#
# Usage: tools/comment_check.py [root]   (default: the library root above this script)

import os
import re
import sys

TAGS = ("WARNING:", "LOAD-BEARING:", "TRAP:")
MAX_TAGGED = 4

TAGGED = re.compile(r"/[*/]+\s*(?:%s)" % "|".join(re.escape(t) for t in TAGS))
LITERAL = re.compile(r"'(?:\\.|[^'\\])*'|\"(?:\\.|[^\"\\])*\"")
SEPARATOR = re.compile(r" [|:]")


def isTable(run):
    columns = {}
    for line in run:
        for column in set(m.start() + 1 for m in SEPARATOR.finditer(line)):
            columns[column] = columns.get(column, 0) + 1
    return any(n > 1 for n in columns.values())


def blocks(lines):
    """Yield (index, run) for every comment run sitting at brace depth > 0."""
    depth = 0
    inblock = False
    start = None
    run = []

    for i, raw in enumerate(lines):
        s = raw.strip()
        wasblock = inblock
        comment = True

        if inblock:
            if "*/" in s:
                inblock = False
        elif s.startswith("/*"):
            if "*/" not in s:
                inblock = True
        elif not s.startswith("//"):
            comment = False

        if comment:
            if depth > 0:
                if start is None:
                    start, run = i, []
                run.append(raw)
            continue

        if start is not None and len(run) > 1:
            yield start, run
        start, run = None, []

        if not wasblock:
            code = LITERAL.sub("", raw)
            depth += code.count("{") - code.count("}")
            if depth < 0:
                depth = 0


def check(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()

    bad = []
    for start, run in blocks(lines):
        first = run[0].strip()
        if first.startswith("/**"):
            continue
        if isTable(run):
            continue
        if TAGGED.match(first):
            if len(run) <= MAX_TAGGED:
                continue
            bad.append((start + 1, len(run), first, "tagged, but over %d lines" % MAX_TAGGED))
            continue
        bad.append((start + 1, len(run), first, "prose"))
    return bad


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    src = os.path.join(root, "src")

    found = 0
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames[:] = [d for d in sorted(dirnames) if not d.startswith(".") and d != "build"]
        for name in sorted(filenames):
            if not name.endswith(".c"):
                continue
            path = os.path.join(dirpath, name)
            for line, count, text, why in check(path):
                rel = os.path.relpath(path, root).replace(os.sep, "/")
                print("%s:%d: %d-line comment in a function body (%s)" % (rel, line, count, why))
                print("    %s" % text[:100])
                found += 1

    if found:
        print()
        print("%d block(s). One line, or a table, or open it with one of: %s" % (found, " ".join(TAGS)))
        return 1

    print("comments: no multi-line prose in a function body")
    return 0


if __name__ == "__main__":
    sys.exit(main())
