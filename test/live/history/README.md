# render-perf historical builds

Firmware built from sampled points along the `render-perf` branch, so today's SWD
harness can measure what the branch's work actually bought. One directory per
sample, named `<index-on-branch>-<commit>`, holding the RP2040 and PRO ELFs, plus
a `manifest.tsv` mapping index to commit, date, submodule pointer and subject.

Neither the ELF store nor the manifest is in this repository: both are large and
machine-local. **Building them is no longer scripted** - the sweep that produced
them walked a submodule path this layout does not have, so it was retired rather
than left to rot. The measurements it produced are committed and remain valid; see
`perf-history.jsonl` and the records in `test/live/runs/`.

`series.sh <pro|2040>` is the half that still runs: it flashes each stored sample
and saves a record per point into `test/live/runs/`. It derives its paths from the
repository's own location, expecting the ELF store beside it, and accepts
overrides:

| variable | what it points at | default |
|---|---|---|
| `PERF_HISTORY_ELFS` | the ELF store | `../perf-history-elfs` |

## The recipe they were built with

Kept because rebuilding the store is a manual job. The same recipe for every sample,
so what moves between points is the firmware and nothing else:

    cmake -S . -B <dir> -G Ninja -DPICO_BOARD=<pico9918|pico9918pro> \
          -DPICO9918_LIVE_TEST=ON -DPICO9918_VERSION_SUFFIX=live

A fresh configure per sample, deliberately: a reused cache carries a newer
commit's option defaults backwards. Only the firmware ELF is built, not the
default target, which otherwise compiles CVBasic and XDT99 from source for an ELF
nobody wanted.

**The toolchain is constant.** `CMakeLists.txt` pins `toolchainVersion 15_2_Rel1`
and that never changes anywhere on the branch, so every sample is gcc 15.2.1 -
the same compiler as every reading taken today. There is no compiler confound to
subtract.

A note for anyone rebuilding the store by hand: the branch's submodule commits are
on the private remote only, so a fresh worktree cannot fetch them from `origin`. Add
a sibling clone as a remote and fetch from there.

## Which numbers mean anything

This section is a **dataset changelog**: it records where the archived samples change
meaning, which is what a reader needs to interpret them. It is not a history of the
firmware.

**`render` is the metric.** It is `pico9918_scan_line`'s own microseconds, and
`renderTimePerScanlineStr` exists in every build here, unchanged in meaning. This
is the one to graph.

**`line` exists everywhere but breaks in the middle.** At branch index 87
(`797e1a2`) the sample moved to close *after* the diagnostic overlay draws, so
`line` went from excluding the overlay's cost to including it. Samples 32, 44, 60
and 79 are the old meaning; 119 onwards are the new one. Comparable within each
era, not across the boundary.

**A scene whose feature postdates the sample is not a fair comparison.** The
harness writes the same registers and VRAM whatever the firmware's age, so an old
build renders the stimulus with code that cannot do the job, and comes out cheap
because it is doing less. Measured on the PRO at sample 44 against today:

| scene | 44 | today | what it means |
|---|---|---|---|
| `t80-ecm3` | 7.90 | 28.09 | 44 cannot do ECM in T80 at all |
| `t80-bml-pri` | 18.40 | 47.93 | no bitmap layer in T80 either |
| `t80-hscroll` | 16.76 | 42.08 | a real 25 us **increase**, honestly earned |
| `t40-48rows` | 8.80 | 8.59 | comparable: same work, both builds |

So there is no single "total gain" figure. T80 got substantially more expensive
because it went from a packed 4bpp line to a 512-byte 8bpp line that can express
ECM, the palette select, the bitmap layer and the shared composite. The 256-pixel
modes are where a like-for-like speedup shows.

## Why the series measures perf stages only

`series.sh` passes `--only perf perf-panels`. An old build fails today's goldens by
design - the references were re-frozen several times on this branch (D9's
transparency rule, the tile palette select, the 512-wide tier) - so `freeze` and the
property stages would report a wall of expected failures and cost minutes each. Run
them deliberately if when-did-pixels-change is the question.

## Traps

- **A record's `commit` field is the worktree's, not the ELF's.** `results.py` asks
  git, and git answers about `render-perf`. The firmware's real commit is in the
  `--save` suffix (`hist<index>-<commit>`) and in `manifest.tsv`. Do not key a graph
  on the record's `commit` for these.
- **Sample 213 has no PRO build.** It does not link: `SCRATCH_X overflowed by 84
  bytes`, `.stack1_dummy` will not fit. That is real history rather than a recipe
  problem - `f6c061f` (index 225) later cut both stacks to 1024 and bounded each
  bank, which is what fixed it. Expect the same for neighbouring commits between the
  sprite work and 225; sample 197 is the late PRO point that does link.
- **The 8bpp tier only exists from index 111.** Before that a PRO build has no
  8bpp 80-column text, because the option did not exist to default on.
- **Requires the harness tolerance from `a312746`.** Before it, `struct_offsets`
  raised whenever any of its 12 capture fields was missing, and the last three
  arrive at index 209 - so an unmodified harness could only open the final 21
  commits of 230. Sample 32 resolves 9 of 17 fields; all five VDP struct offsets
  are identical to today's, which is why scene setup works at all.
