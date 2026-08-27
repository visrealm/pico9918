# format.py
#
# Apply .clang-format to the firmware and the pico9918-core library, or check
# that they are already formatted. --check exits non-zero and names the files,
# so it works as a pre-commit or CI gate.
#
# Layouts meant to be read by column - the GPU preload dump, the VGA timing
# tables, the palette, the tile/text clone tables - are fenced in the source
# with clang-format off/on and are not reflowed.
#
# Copyright (c) 2026 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918
#

import argparse
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBRARY = "core"

TREES = [
    ("firmware", ["src/**/*.c", "src/**/*.h"]),
    ("pico9918-core", [LIBRARY + "/src/**/*.c",
                      LIBRARY + "/src/**/*.h",
                      LIBRARY + "/bindings/python/*.c"]),
]

# board headers come from the Pico SDK's template and are left as they arrived
EXCLUDE = ("/boards/",)


def find_clang_format():
    exe = shutil.which("clang-format")
    if exe:
        return exe
    for base in (os.environ.get("ProgramFiles", ""), os.environ.get("ProgramW6432", "")):
        candidate = os.path.join(base, "LLVM", "bin", "clang-format.exe")
        if base and os.path.isfile(candidate):
            return candidate
    sys.exit("clang-format not found - install LLVM or put it on PATH")


def sources(patterns):
    out = []
    for pattern in patterns:
        for path in glob.glob(os.path.join(ROOT, pattern), recursive=True):
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            if not any(x in "/" + rel for x in EXCLUDE):
                out.append(rel)
    return sorted(set(out))


def main():
    ap = argparse.ArgumentParser(description="format the C sources, or check they are formatted")
    ap.add_argument("--check", action="store_true", help="report and exit 1 instead of rewriting")
    ap.add_argument("tree", nargs="?", choices=[name for name, _ in TREES],
                    help="limit to one tree (default: both)")
    args = ap.parse_args()

    clang_format = find_clang_format()
    unformatted = []
    counted = 0

    for name, patterns in TREES:
        if args.tree and args.tree != name:
            continue
        files = sources(patterns)
        counted += len(files)
        for rel in files:
            cmd = [clang_format, "--dry-run", "-Werror" if args.check else "-i", rel]
            if not args.check:
                cmd = [clang_format, "-i", rel]
            if subprocess.run(cmd, cwd=ROOT, capture_output=True).returncode:
                unformatted.append(rel)
        print("%-14s %3d files" % (name, len(files)))

    if args.check:
        for rel in unformatted:
            print("  needs formatting: %s" % rel)
        print("%d of %d files need formatting" % (len(unformatted), counted))
        return 1 if unformatted else 0

    if unformatted:
        for rel in unformatted:
            print("  FAILED: %s" % rel)
        return 1
    print("formatted %d files" % counted)
    return 0


if __name__ == "__main__":
    sys.exit(main())
