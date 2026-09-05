# comment_check.py
#
# Comment discipline inside function bodies, as a gate.
#
# Copyright (c) 2026 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918-core
#
# Two rules, both on comment runs sitting at brace depth > 0 in a .c file under src/.
# Headers are out of scope: a header's blocks document the contract, and `extern "C" {`
# makes brace depth meaningless there anyway.
#
# 1. One line. If it needs more than one, it is either a table or a warning, and it
#    says which:
#      - a /** block is documentation, not prose.
#      - a table stays, however long. A table is two or more rows sharing a separator
#        ('|' or ':') at the same column, which is what a byte or bit layout looks
#        like whichever character it was drawn with.
#      - a block opening with a tag from TAGS may run to MAX_TAGGED lines. The tag is
#        the point: a paragraph earns its place by warning about something, not by
#        narrating what the code below does.
#
# 2. A blank line above it. clang-format has no option for this - the only blank lines
#    it inserts are around C++ access specifiers and between definition blocks - so it
#    lives here. Exempt where a blank line would be wrong rather than merely absent:
#    a line that is nothing but a brace, which already reads as a break; inside a
#    backslash-continued macro (a blank line ENDS the macro); after a preprocessor
#    directive or a case label; mid-initializer; and a fallthrough marker, which
#    annotates the statement it is glued to.
#
#    A comment on an `else` is not exempt. A blank line above one would split the chain,
#    so the answer is to brace the arm above: the closing brace is then the separator,
#    and the brace exemption lets it through.
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
BRACE = re.compile(r"^(?:.*\{|\};?)$")
LABEL = re.compile(r"^(?:case\b.*|default\s*):$")
FALLTHROUGH = re.compile(r"^/[*/]+\s*(?:falls?[ _-]?thr(?:u|ough)|no break)\b", re.I)


def isTable(run):
    columns = {}
    for line in run:
        for column in set(m.start() + 1 for m in SEPARATOR.finditer(line)):
            columns[column] = columns.get(column, 0) + 1
    return any(n > 1 for n in columns.values())


def runs(lines):
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

        if start is not None:
            yield start, run
        start, run = None, []

        if not wasblock:
            code = LITERAL.sub("", raw)
            depth += code.count("{") - code.count("}")
            if depth < 0:
                depth = 0


def needsBlankLine(lines, start, run):
    if start == 0:
        return False

    above = lines[start - 1].rstrip()
    stripped = above.strip()

    if not stripped:              # already has one
        return False
    if BRACE.match(stripped):     # a line holding only a brace already reads as a break
        return False
    if above.endswith("\\"):      # a blank line would end the macro
        return False
    if stripped.startswith("#"):  # a preprocessor directive
        return False
    if stripped.endswith(","):    # mid-initializer or mid-argument-list
        return False
    if LABEL.match(stripped):     # a case label
        return False
    if len(run) == 1 and FALLTHROUGH.match(run[0].strip()):
        return False              # annotates the statement above it

    return True


def check(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()

    bad = []
    for start, run in runs(lines):
        first = run[0].strip()

        if needsBlankLine(lines, start, run):
            bad.append((start + 1, len(run), first, "no blank line above it"))

        if len(run) < 2 or first.startswith("/**") or isTable(run):
            continue
        if TAGGED.match(first):
            if len(run) > MAX_TAGGED:
                bad.append((start + 1, len(run), first, "tagged, but over %d lines" % MAX_TAGGED))
            continue
        bad.append((start + 1, len(run), first, "prose"))

    bad.sort()
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
        print("%d finding(s). One line, or a table, or open it with one of: %s" % (found, " ".join(TAGS)))
        print("And a blank line above, unless the line above is only a brace, continues a macro,")
        print("is a directive, is a case label, or leaves an initializer or argument list open.")
        print("Commenting an `else`? Brace the arm above it - the closing brace is the separator.")
        return 1

    print("comments: one line, blank line above")
    return 0


if __name__ == "__main__":
    sys.exit(main())
