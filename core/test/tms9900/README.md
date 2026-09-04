# TMS9900 Dual-Core Unit Tests

Comprehensive micro-test suite that runs identical TMS9900 programs through both the **ARM assembly core** (`run9900`) and the **portable C core** (`run9900_c` from `tms9900.c`), then compares every register and memory result.

The assembly core is the one the platform selects, the same way `src/CMakeLists.txt` picks it: `thumb9900_m0.S` on RP2040, `thumb9900_m33.S` otherwise. The two are separate implementations, so a run on one board says nothing about the other - both need flashing. The banner names the core the binary carries.

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

Each test compares:
1. **Cross-core register match** - all checked registers must agree between ASM and C
2. **Cross-core memory match** - any listed memory locations must agree
3. **Known-value checks** - where the expected result is deterministic, both cores must match the expected value
4. **Flag checks** - OV, C, EQ flags via STST readback

A mismatch between the two cores (not just a wrong value) is always reported as a failure with both cores' values shown, making it easy to pinpoint which core is wrong.

## Building

```bash
cd test/tms9900
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk
make -j4
```

This produces `tms9900_test.uf2`. Flash it to a Pico via BOOTSEL mode.

## Serial output

Connect via USB serial (115200 baud) or UART. Output format:

```
=========================================
  TMS9900 Dual-Core Unit Tests
  ASM core (m33) vs C core
=========================================

=== Data Transfer ===
  PASS: LI R0, 0x1234
  PASS: LI R0 value=0x1234
  ...

=========================================
  Results: 62/62 passed  (0 failed)
=========================================
  ALL TESTS PASSED
=========================================
```

On completion the onboard LED blinks: **solid on** = all pass, **blinking** = failures.
