#!/usr/bin/env sh
#
# Project: pico9918-core
#
# The CI jobs' actual commands, one subcommand each.
#
# Copyright (c) 2026 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918-core
#
# The workflow files are thin on purpose: this repository is published from the
# firmware repository, where the same jobs run from a root-level workflow, and
# two copies of the commands would drift. Paths resolve against this script, so
# it behaves the same whether the library is the repository root or a directory
# inside one.
#
# Usage: tools/ci.sh <goldens|suite|warnings|doxygen|package|multi|tms9918|python>
#
#   goldens   the 16 committed frames, byte-exact
#   suite     111 scenes, five properties and a GPU program, both line widths
#   warnings  -Wall -Wextra -Werror
#   doxygen   the docs build, and what it still reports
#   package   install, then find_package it from a separate project and run it
#   multi     the library and the consumer, instance threaded through every signature
#   tms9918   PICO9918_MODE=0, and the frame it renders against the F18A build's
#   python    the Python module against an installed library, and what it renders
#
# Every job but the last three configures F18A and a single instance, which is what
# the firmware ships.
#
# CI_GENERATOR selects a generator (default: CMake's own choice) and CI_CONFIG
# the configuration. Both generator families work: a single-config one ignores
# --config, a multi-config one needs it and puts its artifacts one directory
# deeper, so nothing below assumes an output path.

set -eu

# LIBROOT, not LIB: vcvars exports LIB as the MSVC linker's search path, and
# assigning to a name already in the environment keeps it exported - so a
# variable called LIB here reached cl.exe and the compiler test failed with
# "cannot open file 'kernel32.lib'". Nothing outside an MSVC build would ever
# have shown it.
LIBROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${CI_BUILD_DIR:-$LIBROOT/build-ci}
CMAKE=${CMAKE:-cmake}
CONFIG=${CI_CONFIG:-Release}

# What configure() selects. Only multi() and tms9918() change them.
INSTANCE=1
MODE=1

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

configure() {
  dir=$1
  src=$2
  shift 2
  rm -rf "$dir"
  if [ -n "${CI_GENERATOR:-}" ]; then
    $CMAKE -S "$src" -B "$dir" -G "$CI_GENERATOR" -DCMAKE_BUILD_TYPE="$CONFIG" \
      -DPython3_EXECUTABLE="$PY_PATH" \
      -DPICO9918_MODE=$MODE -DPICO9918_SINGLE_INSTANCE=$INSTANCE "$@"
  else
    $CMAKE -S "$src" -B "$dir" -DCMAKE_BUILD_TYPE="$CONFIG" \
      -DPython3_EXECUTABLE="$PY_PATH" \
      -DPICO9918_MODE=$MODE -DPICO9918_SINGLE_INSTANCE=$INSTANCE "$@"
  fi
}

build() {
  $CMAKE --build "$1" --config "$CONFIG" --parallel
}

# The one artifact of its name, wherever the generator put it.
findExe() {
  found=$(find "$1" -name "$2" -type f 2>/dev/null | head -1)
  [ -n "$found" ] || found=$(find "$1" -name "$2.exe" -type f 2>/dev/null | head -1)
  [ -n "$found" ] || { echo "ci.sh: no $2 under $1" >&2; exit 1; }
  echo "$found"
}

goldens() {
  configure "$OUT" "$LIBROOT" -DPICO9918_GOLDEN=ON -DCMAKE_C_FLAGS=-O2
  build "$OUT"
  "$(findExe "$OUT" golden_runner)"
}

# The goldens are keyed on the width of the line that produced them, so the two
# tiers are two builds - the 256-byte line every mode renders, then the 8bpp
# 80-column one. A single build proves only half the renderer.
tier() {
  dir=$OUT/suite-$1
  shift
  configure "$dir" "$LIBROOT/test/suite/shim" -DCMAKE_C_FLAGS=-O2 "$@"
  build "$dir"
  # -m from test/, so the suite package is found the one way it is meant to be
  LIVE9918_SHIM=$(findExe "$dir" live_shim) sh -c 'cd "$1" && exec "$2" -m suite.run' _ "$LIBROOT/test" "$PY"
}

suite() {
  tier default
  tier w512 -DLIVE_DESKTOP_TEXT80_8BPP=ON
}

warnings() {
  configure "$OUT" "$LIBROOT" -DPICO9918_WERROR=ON -DCMAKE_C_FLAGS=-O2
  build "$OUT"
  echo "no warnings"
}

docs() {
  # A configure, because the Doxyfile is generated: the version in it comes from
  # project(), so that a bump has one place to happen rather than two that can drift.
  configure "$OUT" "$LIBROOT"
  cd "$LIBROOT"
  doxygen "$OUT/Doxyfile" 2> "$OUT/doxygen.log" || { cat "$OUT/doxygen.log"; exit 1; }
  # A count, not a gate. Every warning is an undocumented member: EXTRACT_ALL is
  # off so that the count means something, and a gap is one to fill rather than
  # noise to silence. A doc *error* is different - there should be none.
  #
  # Split, because the two halves are not the same job. A gap in a header a
  # consumer includes is a hole in the published API reference and should be zero.
  # The rest are implementation statics and harness internals - real gaps, but
  # filling them per member would fight this codebase's comment discipline, so
  # they are a backlog rather than a target.
  total=$(grep -c 'warning:' "$OUT/doxygen.log" || true)
  public=$(grep -cE 'of file (pico9918|pico9918_util|pico9918_config|pico9918_frame|gpu|diag|splash)\.h' \
    "$OUT/doxygen.log" || true)
  echo "doxygen: $total undocumented member(s), $public in a header a consumer includes"
  # doc/code is what the Pages job uploads, and it is never committed - a
  # regeneration must not arrive as a thousand-file diff.
  [ -f doc/code/index.html ] || { echo "ci.sh: doxygen wrote no doc/code" >&2; exit 1; }
}

package() {
  stage=$OUT/stage
  configure "$OUT/lib" "$LIBROOT" "-DCMAKE_INSTALL_PREFIX=$stage"
  build "$OUT/lib"
  $CMAKE --install "$OUT/lib" --config "$CONFIG"

  # The part that actually proves the export: a separate project, finding the
  # installed package rather than the build tree.
  configure "$OUT/consumer" "$LIBROOT/test/package" "-DCMAKE_PREFIX_PATH=$stage"
  build "$OUT/consumer"
  "$(findExe "$OUT/consumer" consumer)"

  (cd "$OUT/lib" && cpack -C "$CONFIG")
  ls -l "$OUT/lib"/pico9918-core-*
}

# The instance reaches every emitter as an argument instead of resolving to a
# link-time constant, so this is the only build where a call site that forgot to
# pass it is a compile error rather than silently reading the one global. That is
# what keeps the plumbing honest, and it is what a consumer holding two VDPs
# compiles against.
#
# One runner: a missing argument is the same error under every compiler, and
# everything portability-shaped is already covered by the matrix legs above.
multi() {
  INSTANCE=0
  # Both line widths: the 8bpp 80-column tier compiles call sites the 256-byte
  # line never reaches.
  configure "$OUT/multi" "$LIBROOT" -DPICO9918_WERROR=ON -DCMAKE_C_FLAGS=-O2
  build "$OUT/multi"
  configure "$OUT/multi-w512" "$LIBROOT" -DPICO9918_WERROR=ON \
    -DPICO9918_TEXT80_8BPP=ON -DCMAKE_C_FLAGS=-O2
  build "$OUT/multi-w512"

  # And it has to run, not only link: the consumer creates two instances, gives
  # them different backdrops and checks the two lines differ.
  stage=$OUT/multi-stage
  configure "$OUT/multi-lib" "$LIBROOT" "-DCMAKE_INSTALL_PREFIX=$stage"
  build "$OUT/multi-lib"
  $CMAKE --install "$OUT/multi-lib" --config "$CONFIG"
  configure "$OUT/multi-consumer" "$LIBROOT/test/package" "-DCMAKE_PREFIX_PATH=$stage"
  build "$OUT/multi-consumer"
  "$(findExe "$OUT/multi-consumer" consumer)"
}

# PICO9918_MODE=0 is a TMS9918A: 16KB of VRAM, no GPU, and no unlock - and because
# graphics_i_scan_line forks on the unlock exactly once, that last one folds the whole
# enhanced renderer away rather than needing a condition per feature.
#
# The gate is the example's frame, not a compile. A static archive resolves nothing, so
# it takes an executable to catch the diag overlay calling GPU timers this mode does not
# build - and the locked Graphics I path is shared between the two modes, so the frames
# must match byte for byte. The F18A build in this same job is the reference, which is
# why there is no committed frame to keep in step.
tms9918() {
  for mode in 1 0; do
    MODE=$mode
    configure "$OUT/mode$mode" "$LIBROOT" -DPICO9918_WERROR=ON -DPICO9918_EXAMPLES=ON \
      -DCMAKE_C_FLAGS=-O2
    build "$OUT/mode$mode"
    "$(findExe "$OUT/mode$mode" render_frame)" "$OUT/frame-$mode.ppm"
  done
  cmp "$OUT/frame-1.ppm" "$OUT/frame-0.ppm"
  echo "the TMS9918A build renders the locked frame exactly as the F18A build does"

  # the two axes are independent, so their cross-product gets one build
  MODE=0
  INSTANCE=0
  configure "$OUT/mode0-multi" "$LIBROOT" -DPICO9918_WERROR=ON -DCMAKE_C_FLAGS=-O2
  build "$OUT/mode0-multi"
}

# The Python module, built against an INSTALLED library rather than the build tree -
# the way anyone outside this repository would build it, so a missing export shows up
# here rather than in a bug report. Multi-instance, because one class per VDP is the
# whole point of a binding and the module refuses to compile the other way.
# Not named `python`: PY falls back to the bare `python` above, and a shell function of
# that name is what `command -v "$PY"` finds and what every "$PY" here would then run.
pythonModule() {
  INSTANCE=0
  stage=$OUT/py-stage
  # a static archive that ends up inside a shared module has to be position-independent
  configure "$OUT/py-lib" "$LIBROOT" "-DCMAKE_INSTALL_PREFIX=$stage" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  build "$OUT/py-lib"
  $CMAKE --install "$OUT/py-lib" --config "$CONFIG"

  configure "$OUT/py" "$LIBROOT/bindings/python" "-DCMAKE_PREFIX_PATH=$stage"
  build "$OUT/py"

  module=$(find "$OUT/py" \( -name 'pico9918*.so' -o -name 'pico9918*.pyd' \) -type f | head -1)
  [ -n "$module" ] || { echo "ci.sh: no pico9918 module under $OUT/py" >&2; exit 1; }
  dir=$(dirname "$module")
  if command -v cygpath > /dev/null 2>&1; then dir=$(cygpath -m "$dir"); fi
  # This is the only job that allocates an instance rather than taking the static one, so
  # it is where a field read before it is written shows up - but only against a heap that
  # is not already zero. glibc fills one on request; every other allocator ignores this.
  MALLOC_PERTURB_=170 PYTHONPATH=$dir "$PY" "$LIBROOT/bindings/python/test.py"
}

case ${1:-} in
goldens) goldens ;;
suite) suite ;;
warnings) warnings ;;
doxygen) docs ;;
package) package ;;
multi) multi ;;
tms9918) tms9918 ;;
python) pythonModule ;;
*)
  echo "usage: tools/ci.sh <goldens|suite|warnings|doxygen|package|multi|tms9918|python>" >&2
  exit 2
  ;;
esac
