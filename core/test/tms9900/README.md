# TMS9900 Unit Tests

The TMS9900 the GPU is, instruction by instruction. On a board it runs identical programs through the **ARM assembly core** (`run9900`) and the **portable C core** (`run9900_c` from `tms9900.c`), holding both to the same expected registers and memory.

The assembly core is the one the platform selects, the same way `src/CMakeLists.txt` picks it: `thumb9900_m0.S` on RP2040, `thumb9900_m33.S` otherwise. The two are separate implementations, so a run on one board says nothing about the other - both need flashing. The banner names the core the binary carries.

Off a board there is no assembly core, so both passes run the portable one and every check stands against the value its case states. That makes this file the desktop gate as well: `tools/ci.sh gpucore` builds and runs it, so a change to `tms9900.c` cannot reach a host emulator without these cases passing.

## What it tests

| Group | Tests |
|-------|-------|
| Data Transfer | LI, MOV word/byte, indirect, indexed/absolute, auto-increment |
| Arithmetic | A, S, NEG, ABS, INC/INCT/DEC/DECT, AI, MPY, DIV |
| Logical | SZC (AND-like), SOC (OR), XOR, INV, CLR, SETO, ANDI, ORI, COC, CB |
| Shifts | SRA, SRL, SLA, SRC (all with fixed count and R0-driven count) |
| Compare & Branch | CI, JEQ, JNE, JGT, loop with counter |
| Subroutine | BL *Rn / RT (B *R11) |
| Misc | SWPB, STST, STWP |
| F18A PIX | BL address off a rounded row stride, the 16KB wrap, and a write to the pixel x selects |
| Edge Cases | ADD overflow, SUB borrow, SLA overflow, NEG 0x8000, word alignment |
| BLWP / RTWP | Context save/restore across workspace switch |
| Stress | Fibonacci(10) iterative loop |

## Pass/Fail strategy

Every check names the value it expects, and both passes are held to it: registers, listed memory bytes, and the OV / C / EQ flags read back through STST. A core that disagrees with the other fails on its own line rather than being compared against it, and the failing line prints R0-R7 so the state that produced it is visible without re-running.

## Building

On a board it builds with the firmware, which adds this directory from its own `test/`. The result is `build/<board>/test/tms9900/tms9900_test.uf2`, flashed via BOOTSEL. Each board is its own build: `pico9918` links the m0 core, `pico9918pro` the m33 one.

On a host, from the library root:

```bash
tools/ci.sh gpucore
```

or by hand:

```bash
cmake -B build-gpucore -DPICO9918_TMS9900_TEST=ON
cmake --build build-gpucore
./build-gpucore/test/tms9900/tms9900_test
```

## Output

On a board, over USB serial (115200 baud) or UART. On a host, on stdout, and the exit status is non-zero if anything failed.

```
=========================================
  TMS9900 Unit Tests
  ASM core (m33 ) vs C core
=========================================

=== Data Transfer ===
  LI R0,0x1234
    PASS: LI R0,0x1234 [ASM]
    PASS: LI R0,0x1234 [C]
  ...

=========================================
  Results: 108/108 passed  (0 failed)
=========================================
  ALL TESTS PASSED
=========================================
```

Each case prints its name before it runs, so a hang names the instruction that caused it. On a board the onboard LED then carries the result: **solid on** = all pass, **blinking** = failures.
