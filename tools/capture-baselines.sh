#!/usr/bin/env bash
# Capture migration baselines per LIBRARY-MIGRATION-PLAN.md section 5.3:
#   - disassembly of the hot functions (raw + normalized for diffing)
#   - scratch-bank symbol placement from the map file
#   - elf section sizes
# Usage: tools/capture-baselines.sh [build-dir] [output-dir]
# Defaults: build-step0, docs/baselines/<build-dir-name>
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build-step0}"
OUT_DIR="${2:-$REPO_ROOT/docs/baselines/$(basename "$BUILD_DIR")}"

# Refuse to guess when several ELFs are present - a stale one silently poisons the
# gate.
ELF_LIST=$(ls "$BUILD_DIR"/src/*.elf 2>/dev/null || true)
ELF_COUNT=$(printf '%s\n' "$ELF_LIST" | grep -c . || true)
[ "$ELF_COUNT" -ge 1 ] || { echo "no elf found in $BUILD_DIR/src"; exit 1; }
if [ "$ELF_COUNT" -gt 1 ]; then
  echo "ERROR: multiple ELFs in $BUILD_DIR/src - delete stale ones or pass one explicitly:"
  printf '  %s\n' $ELF_LIST
  exit 1
fi
ELF="$ELF_LIST"
# The SDK writes the map to the build root, named after the firmware target - so
# DERIVE it from the ELF already resolved rather than globbing. Globbing a name
# pattern gets this wrong two ways: it can miss a config whose target is named
# differently, losing scratch-placement.txt with no warning at all, and it can pick a
# STALE map alphabetically and report placement from the wrong binary. Deriving from
# $ELF cannot drift from the image being disassembled, and a missing map WARNs rather
# than skipping in silence.
# (the missing-map WARN is emitted after OUT_DIR exists - see below)
MAP="$BUILD_DIR/$(basename "$ELF").map"

# Use the SAME toolchain that built this ELF: disassembling with a different
# binutils than built it poisons the gate. CMakeCache names the real one - globbing
# the toolchain directory picks whichever version sorts first.
TOOLBIN=""
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
  TOOLBIN=$(sed -nE 's|^CMAKE_AR:FILEPATH=(.*)/arm-none-eabi-ar(\.exe)?$|\1|p' \
              "$BUILD_DIR/CMakeCache.txt" | head -1)
fi
if [ -z "$TOOLBIN" ] || [ ! -d "$TOOLBIN" ]; then
  echo "ERROR: cannot determine the toolchain that built $BUILD_DIR."
  echo "  Expected CMAKE_AR in $BUILD_DIR/CMakeCache.txt. Refusing to guess -"
  echo "  disassembling with a different binutils than built the ELF poisons the gate."
  exit 1
fi
# The suffix the same cache line carries, so a tool is looked for the way the
# build named it rather than the way this host spells executables.
EXE=$(sed -nE 's|^CMAKE_AR:FILEPATH=.*/arm-none-eabi-ar(\.exe)$|\1|p' \
        "$BUILD_DIR/CMakeCache.txt" | head -1)
OBJDUMP="$TOOLBIN/arm-none-eabi-objdump$EXE"
SIZE="$TOOLBIN/arm-none-eabi-size$EXE"
[ -x "$OBJDUMP" ] || { echo "arm-none-eabi-objdump not found in $TOOLBIN"; exit 1; }
echo "toolchain: $TOOLBIN"

mkdir -p "$OUT_DIR"
# Every WARN below appends. Start the run with an empty file, or a recapture
# in place doubles yesterday's warnings and reads as new ones.
: > "$OUT_DIR/WARNINGS.txt"

# Now that OUT_DIR exists, a missing map WARNs rather than skipping in silence.
if [ ! -f "$MAP" ]; then
  echo "WARN: no map file at $MAP - scratch placement NOT checked" | tee -a "$OUT_DIR/WARNINGS.txt"
  MAP=""
fi

# The functions whose disassembly is captured and diffed.
#
# THE RULE: renaming or moving a tracked symbol without updating this list leaves
# that code with NO gate at all. Keep the list in step with the source.
#
# Most entries are simply hot - the per-scanline path and what it reaches. The rest
# are here because a specific escape was PROVEN to pass every other gate, and each
# one is the only thing that catches it:
#
#   main                    where every host->library callback is REGISTERED.
#                           Deleting a registration is invisible everywhere else: the
#                           linker then drops the callee entirely, so a feature stops
#                           reaching the VGA side while the register bit still gets
#                           set. Runs once at boot, so tracking it costs nothing.
#   reloadStoredConfig      a callback BODY the library invokes. Tracking `main`
#   applyConfigHostEffects  cannot catch a GUTTED CALLEE - the registration still
#                           compiles identically and the call site still executes -
#                           so every such body needs its own entry. Proven: emptying
#                           reloadStoredConfig left all other tracked functions
#                           byte-identical and passed the goldens, while the
#                           late-config-reload feature silently stopped working.
#   pico9918_config_apply   carries the tier-2 config-applied callback. Deleting the
#                           invocation outright passes the goldens (they register no
#                           callback) and every other tracked symbol.
#   pico9918_frame_update_interrupts
#                           the highest-risk function here, and the goldens cannot
#                           see its semantics at all. A STATUS_COL -> STATUS_5S mask
#                           swap passes goldens and section sizes alike.
#   initLookups.part.0      a compiler artefact name (GCC's partial inlining), stable
#                           across all three configs. It holds the border fill's DMA
#                           SOURCE POINTER: aiming PICO9918_FILL_BORDER at a
#                           palette-index word instead of the pixel colour word makes
#                           every border pixel on every scanline the wrong colour, and
#                           passes everything - the norm extracts, instruction counts
#                           and 16/16 goldens, because the harness never calls
#                           pico9918_frame_scanline. Its gate is the .raw extract,
#                           where fill pointers appear as .word payloads.
#   dmaIrqHandler           the VGA scanline interrupt, where the host re-arms the GPU
#                           palette guard. A deleted call is invisible elsewhere.
#   volatileHack            the GPU loop body, carrying the fault dispatch. Whether a
#                           fault routes to the DMA port or to the palette is an
#                           ORDERING, and no desktop surface runs it at all.
#   pico9918_gpu_rearm_palette_guard
#                           raises the flag BEFORE dropping the region. The other
#                           order leaves the guard down with nothing to notice it, and
#                           passes everything else.
#   the F18A render path    per-scanline rendering code, over 12KB of it, that nothing
#                           else gates. It is also where an always_inline change is
#                           felt: dropping the attribute out-lines renderSprites and
#                           renderEcmTile, adds `bl` calls inside the per-scanline
#                           stagers and moves a kilobyte into .text, while every other
#                           tracked extract AND the goldens stay clean. Only
#                           symbol-sizes.txt and elf-size.txt show it.
#
# BOTH SIDES OF A HOST/LIBRARY SPLIT ARE TRACKED where one exists - tmsScanline with
# pico9918_frame_scanline, tmsPorch with pico9918_frame_porch, tmsEndOfScanline with
# pico9918_frame_end_of_scanline. Neither substitutes for the other: the shim's own
# cost is part of what the split must not regress, and only tracking both makes "did
# the total regress" answerable from this file.
#
# WHAT THIS GATE CANNOT SEE. A read of one global replaced by a read of another of
# the SAME TYPE produces an identical instruction stream - only the literal-pool
# .word payload moves, and the normalizer blinds .word payloads on purpose so that a
# data-layout shift does not read as a code change. Two such swaps in the
# diagnostics rows were measured and are invisible here. They are caught by the
# GOLDENS instead, whose performance panel renders both counters, so a wrong global
# yields a wrong digested NUMBER - which is why the golden harness writes
# pico9918_frame_count explicitly (see overlayPrimeDiag): equal values would
# re-absorb both swaps silently. The general property stands for any same-typed
# global read that no row renders: neither surface sees it.
#
# Verify a change in CALLS against .raw.txt, not .norm.txt, for the same reason.
#
# A SHAPE CHANGE IS NOT A SIZE CHANGE. Tested, not assumed: a merge that measurably
# slowed every scene on the device moved these symbols in BOTH directions, net near
# zero. No entry here and no count this script produces would have caught it -
# `results.drift` is what catches that class.
#
# DELIBERATE OMISSIONS, each a judgement rather than an oversight:
#
#   pico9918_frame_geometry  GCC inlines it into pico9918_frame_end, so it does not
#                            appear in the ELF. An entry that always WARNs trains
#                            readers to ignore WARNINGS.txt, which is the file that
#                            catches a real disappearance. Its gate is the golden
#                            frame surface, which calls it directly.
#   renderDiag               a host function the library never calls, inlined into
#                            tmsScanline in every configuration measured. The
#                            gutted-callee hole does not apply: its caller is tracked
#                            and its body is inside that caller's disassembly.
#   the leaf row workers     rowT1Ecm*, text40Row*, text80Row*. The golden suite pins
#                            their output byte-for-byte across thirteen scenes, so a
#                            change to what they compute cannot escape, and what they
#                            cost is what the device measures. Eighteen entries would
#                            add noise and catch nothing the other two gates miss.
#
# ONE WARN IS EXPECTED, on the PRO config only: renderSprites is inlined into
# pico9918_output_sprites on the RP2350, so its symbol does not exist there. The entry
# stays because it is a real gate on the RP2040. A PRO capture with any OTHER warning,
# or with this one absent, is the thing to look at. Note also that
# pico9918_output_sprites exists as a symbol only while the always_inline mapping
# holds - if a change legitimately removes it, expect a WARN and treat it as signal.
FUNCS="tmsScanline pico9918_frame_scanline
       pico9918_frame_update_interrupts pico9918_palette_regenerate tmsEndOfFrame tmsEndOfScanline
       tmsPorch pico9918_frame_porch pico9918_frame_end_of_scanline pico9918_frame_end
       pico9918_config_apply main reloadStoredConfig applyConfigHostEffects
       pico9918_scan_line pico9918_diag_render pico9918_diag_render_text pico9918_diag_update
       pico9918_splash_render
       tmsReadIrqHandler tmsWriteIrqHandler gpioIrqHandler dmaIrqHandler
       initLookups
       graphics_i_scan_line text_scan_line f18a_tile_layer_scan_line
       renderSprites pico9918_output_sprites compositeTile2OnlyBuffer
       volatileHack pico9918_gpu_rearm_palette_guard"

"$OBJDUMP" -d "$ELF" > "$OUT_DIR/full-disasm.txt"

# Symbol sizes, so a function's extract is bounded by what the ELF says it IS rather
# than by a blank line that may not be there.
#
# BOUND THE EXTRACT BY THE SYMBOL SIZE, NOT BY A BLANK LINE. objdump separates
# symbols with a blank line only when it emits a new symbol HEADER; where a function
# is the last code in its section and is followed directly by unlabelled data, or by
# data whose symbol objdump does not print, an extractor keyed on the blank line runs
# straight on into it. That swept hundreds of words of trailing string data into one
# extract, and because a `.word` line counts as an instruction to `grep -c ':'`, the
# function read at nearly twice its real length. It
# would have been reported as a catastrophic regression. Both halves are fixed: bound by
# size here, and exclude .word from the count below.
"$SIZE" >/dev/null 2>&1 || true
NM="$TOOLBIN/arm-none-eabi-nm$EXE"
[ -x "$NM" ] || { echo "arm-none-eabi-nm not found in $TOOLBIN"; exit 1; }
"$NM" -S "$ELF" > "$OUT_DIR/symbol-sizes.txt"

for FN in $FUNCS; do
  # The list holds SOURCE names. GCC decorates a symbol it has specialised
  # (.constprop.0), split (.part.0) or rewritten the arguments of (.isra.0), and
  # which decoration it picks is an inlining decision that moves with unrelated
  # code. Spelling the suffix in the list makes an entry WARN the first time the
  # compiler changes its mind, and a WARN nobody believes is worse than no entry
  # at all - so resolve the base name to whatever single decorated symbol exists.
  # Two matches is ambiguous and is reported rather than guessed at.
  FN_SYM="$FN"
  FN_START=$(awk -v n="$FN" '$4 == n && NF == 4 { print $1; exit }' "$OUT_DIR/symbol-sizes.txt")
  FN_SIZE=$(awk  -v n="$FN" '$4 == n && NF == 4 { print $2; exit }' "$OUT_DIR/symbol-sizes.txt")
  if [ -z "$FN_START" ]; then
    MATCHES=$(awk -v n="$FN" 'NF == 4 && $4 ~ ("^" n "\\.(constprop|part|isra)\\.[0-9]+$") { print $4 }' \
                "$OUT_DIR/symbol-sizes.txt")
    MATCH_COUNT=$(printf '%s\n' "$MATCHES" | grep -c . || true)
    if [ "$MATCH_COUNT" -gt 1 ]; then
      echo "WARN: symbol $FN is ambiguous - $(printf '%s ' $MATCHES)" | tee -a "$OUT_DIR/WARNINGS.txt"
      continue
    fi
    if [ "$MATCH_COUNT" -eq 1 ]; then
      FN_SYM="$MATCHES"
      FN_START=$(awk -v n="$FN_SYM" '$4 == n && NF == 4 { print $1; exit }' "$OUT_DIR/symbol-sizes.txt")
      FN_SIZE=$(awk  -v n="$FN_SYM" '$4 == n && NF == 4 { print $2; exit }' "$OUT_DIR/symbol-sizes.txt")
    fi
  fi
  if [ -z "$FN_START" ] || [ -z "$FN_SIZE" ]; then
    echo "WARN: symbol $FN not found (inlined or renamed?)" | tee -a "$OUT_DIR/WARNINGS.txt"
    continue
  fi
  # extract exactly [start, start+size): the symbol header line plus every disassembly
  # line whose address falls inside the symbol. Trailing literal pool included (it is
  # inside the size), trailing foreign data excluded.
  awk -v fn="<$FN_SYM>:" -v s="0x$FN_START" -v z="0x$FN_SIZE" '
    index($0, fn)                 { on = 1; print; next }
    on && match($0, /^[0-9a-f]+:/) {
      a = strtonum("0x" substr($0, 1, RLENGTH - 1));
      if (a >= strtonum(s) + strtonum(z)) exit;
      print; next
    }
    on && /^$/                    { exit }
  ' "$OUT_DIR/full-disasm.txt" > "$OUT_DIR/$FN.raw.txt"
  if [ ! -s "$OUT_DIR/$FN.raw.txt" ]; then
    echo "WARN: symbol $FN not found in disassembly" | tee -a "$OUT_DIR/WARNINGS.txt"
    continue
  fi
  # Normalized form for step-to-step diffing. Must be ADDRESS-BLIND: stripping only
  # the encoding column leaves absolute branch targets and literal-pool addresses in
  # the text, so any relocation shows up as hundreds of phantom "changes".
  #   1. drop the "  addr:\t enc enc " prefix
  #   2. blank absolute targets in branches/calls: "bl 100123ac <fn>" -> "bl <fn>"
  #   3. blank pc-relative literal comments: "; 10012340 <x+0x8>" -> "; <lit>"
  #   4. blank bare hex addresses left in operands
  #   5. literal-pool ".word <addr>" payloads are data addresses, not code - blind them
  #      (an unrelated data-layout shift must not read as a code change)
  #   5. literal-pool ".word <addr>" payloads are data addresses, not code
  #   6. any remaining bare hex address (branch targets, "@ (addr <sym+off>)"
  #      comments, symbol-line prefixes) becomes <ADDR>; symbol NAMES are kept, so a
  #      call retargeted to a different function still shows up as a real diff.
  sed -E -e 's/^[[:space:]]*[0-9a-f]+:\s+[0-9a-f]{8}\s+\.word.*$/\t.word\t<ADDR>/' \
         -e 's/^[[:space:]]*[0-9a-f]+:\s+([0-9a-f]{4}\s+)+//' \
         -e 's/\b0x[0-9a-f]{6,}\b/0xADDR/g' \
         -e 's/\b[0-9a-f]{8}\b/<ADDR>/g' \
         -e 's/\+0x[0-9a-f]+>/+OFF>/g' \
         "$OUT_DIR/$FN.raw.txt" > "$OUT_DIR/$FN.norm.txt"
  # INSTRUCTIONS, not lines: a literal-pool ".word" is data the function carries, not
  # work it does, and counting it inflates exactly the number this gate exists to watch
  # (see the extractor note above). Reported alongside the byte size, which is what the
  # ELF actually charges for the symbol and which no counting rule can distort.
  # the source name is what the reader tracks; the decorated one is noted when it
  # differs, so a specialisation appearing or vanishing is visible without being a WARN
  printf "%-30s %5d instructions  (0x%s bytes)%s\n" "$FN" \
    "$(grep ':' "$OUT_DIR/$FN.raw.txt" | grep -vc '\.word' || true)" "$FN_SIZE" \
    "$([ "$FN_SYM" = "$FN" ] || echo "  [$FN_SYM]")"
done | tee "$OUT_DIR/instruction-counts.txt"

# scratch-bank placement (silent SRAM fallthrough is the invisible regression - 5.3.4)
if [ -n "$MAP" ]; then
  grep -E "\.scratch_[xy]" "$MAP" > "$OUT_DIR/scratch-placement.txt" || \
    echo "WARN: no scratch sections found in map" | tee -a "$OUT_DIR/WARNINGS.txt"
fi

"$SIZE" -A "$ELF" > "$OUT_DIR/elf-size.txt"

# full disassembly is derived data - the per-function extracts are what gets diffed/committed
rm -f "$OUT_DIR/full-disasm.txt"

echo "baselines written to $OUT_DIR"
