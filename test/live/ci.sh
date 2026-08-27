#!/usr/bin/env sh
#
# Project: pico9918
#
# The desktop live suite, both line-width tiers, for CI.
#
# Copyright (c) 2026 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918
#
# 111 scenes, four property suites and a 22.9 M-instruction GPU program, each
# compared against a committed frame - under whichever compiler the runner
# provides. It is the library's strongest desktop gate and it costs seconds.
#
# The suite itself is the library's, at core/test/suite - core/tools/ci.sh runs
# it there. This is the firmware repository's copy of the same two tiers, run
# through the runner that also drives a board, so the stage list a board run
# uses is the stage list CI proves.
#
# Usage: test/live/ci.sh
#
# CI_GENERATOR selects a generator (default: CMake's own choice), PYTHON the
# interpreter.

set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
CMAKE=${CMAKE:-cmake}

PY=${PYTHON:-}
if [ -z "$PY" ]; then
  if command -v python3 > /dev/null 2>&1; then PY=python3; else PY=python; fi
fi

# Pin the interpreter CMake uses instead of letting find_package(Python3) take
# whichever it meets first. On a Windows runner the MSYS2 shell has the hosted
# tool cache's python ahead of mingw64's on PATH, and pillow was installed for
# mingw64's - so the overlay assets refused to configure against an interpreter
# nobody chose. cygpath because CMake wants a native path, and it only exists
# where that distinction does.
PY_PATH=$(command -v "$PY")
if command -v cygpath > /dev/null 2>&1; then
  PY_PATH=$(cygpath -m "$PY_PATH")
fi

tier() {
  name=$1
  shift
  dir=$ROOT/build-live-$name
  echo "== $name tier"
  rm -rf "$dir"
  if [ -n "${CI_GENERATOR:-}" ]; then
    $CMAKE -S "$ROOT/core/test/suite/shim" -B "$dir" -G "$CI_GENERATOR" \
      -DPython3_EXECUTABLE="$PY_PATH" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS=-O2 "$@"
  else
    $CMAKE -S "$ROOT/core/test/suite/shim" -B "$dir" \
      -DPython3_EXECUTABLE="$PY_PATH" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS=-O2 "$@"
  fi
  $CMAKE --build "$dir" --config Release --parallel

  shim=$(find "$dir" -name live_shim -type f 2> /dev/null | head -1)
  [ -n "$shim" ] || shim=$(find "$dir" -name live_shim.exe -type f 2> /dev/null | head -1)
  [ -n "$shim" ] || { echo "ci.sh: no live_shim under $dir" >&2; exit 1; }

  cd "$HERE"
  LIVE9918_SHIM=$shim $PY runner.py --desktop
}

# The 256-byte line every mode renders, then the PRO's 8bpp 80-column tier.
tier desktop
tier desktop-w512 -DLIVE_DESKTOP_TEXT80_8BPP=ON
