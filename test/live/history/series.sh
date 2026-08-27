#!/usr/bin/env bash
# Measure the stored historical builds on whichever board is on the probe, saving
# one runner record per sample. Perf stages only: an old build fails today's
# goldens by design (the references were re-frozen several times on this branch),
# and the timings are what a history is wanted for.
#
# The record's commit field comes from the worktree, not from the ELF, so the
# firmware's real commit rides in the --save suffix and in the store's manifest.

set -u

BOARD="${1:?usage: series.sh <pro|2040>}"

# Locations are derived from where this script sits, so the worktree can live
# anywhere. Override PERF_HISTORY_ELFS if the ELF store is not beside the repo.
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LIVE=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$LIVE/../.." && pwd)
STORE=${PERF_HISTORY_ELFS:-$(dirname "$ROOT")/perf-history-elfs}

if [ ! -d "$STORE" ]; then
    echo "no ELF store at $STORE - see README.md to build one, or set PERF_HISTORY_ELFS"
    exit 1
fi

case "$BOARD" in
    pro)  PREFIX=pico9918pro ;;
    2040) PREFIX=pico9918 ;;
    *)    echo "board must be pro or 2040"; exit 1 ;;
esac

cd "$LIVE" || exit 1

for dir in "$STORE"/*-*; do
    [ -d "$dir" ] || continue
    name=$(basename "$dir")
    idx=${name%%-*}
    commit=${name#*-}
    elf=$(echo "$dir/$PREFIX-v"*-live.elf)
    if [ ! -f "$elf" ]; then
        echo "=== $name: no $BOARD build (see the store's README)"
        continue
    fi

    echo "=== $name on $BOARD"
    if ! timeout 300 python live9918.py flash "$elf" > /dev/null 2>&1; then
        echo "  FLASH FAILED"
        continue
    fi
    timeout 900 python runner.py --board "$BOARD" --elf "$elf" \
        --only perf perf-panels --save "hist$idx-$commit" --report 2>&1 \
        | grep -E "record:|report:|PASS|FAILED|Traceback|Error" | head -6
done

echo "=== series done for $BOARD"
