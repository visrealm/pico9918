# TMS9900 C core: verification against the assembly

`tms9900.c` is the portable C interpreter; `thumb9900_m0.S` is the ARM assembly core
and the reference. They must agree instruction for instruction, and this document is
the line-by-line comparison.

The tables below are what has been checked. The list first is the more useful half
for anyone editing the C core: every one of these is a place where the obvious C
reading of the instruction is WRONG, and where the assembly is the authority.

---

## Twenty-three ways a C interpreter gets this wrong

Each entry is the rule the C core must satisfy, not a suggestion.

1. Parity is **ODD**: P=4 for an odd bit-count.
2. Mode-2 with reg=0 is absolute `@address` - skip the add entirely.
3. STST stores `st << 8`: ST lives in the HIGH byte of a TMS word.
4. BLWP must not store ST as `wr16(st << 8)` - that zeroes the low byte of the R15 slot.
5. CLR and SETO do **not** update flags.
6. A JMP self-loop (disp == -2) exits like IDLE.
7. Stack convention: the address is OLD R15 (pre-decrement) for PUSH and CALL, OLD+2 for POP and RET.
8. SUB carry is **borrow-NOT**: carry=1 means no borrow.
9. NEG sets carry only when the result is 0.
10. SRA/SRL/SRC/SLC initialise flags with mask 0x0E, which keeps OV.
11. Byte register access (mode=0 byte ops) reads and writes the HIGH byte of the word.
12. Effective addresses are word-aligned for word ops.
13. CMP (the C instruction) takes its operands as `cmp16(src, dst)`.
14. ABS computes `set_flags_word` on the ORIGINAL value, not the negated result.
15. ABS flags, NEG of 0x8000, and overflow detection each have their own case.
16. BLWP mode-0 double-dereferences: read from the workspace address, not `mem[reg_value]`.
17. BLWP does **not** clear ST. The assembly leaves it alone.
18. B/BL/CALL mode-0 branch to the WORKSPACE ADDRESS, not the register value.
19. X mode-0 double-dereferences: the register value IS the instruction, not a pointer to it.
20. **Two-operand src/dst are the opposite way round to the obvious reading:** source is bits 5:0, destination is bits 11:6.
21. SRC's rotation formula is not the naive one - see the shift/rotate table below.
22. BLWP context save ORDERING: read new_wp, save old WP/PC/ST into the new
    workspace, and only then read new_pc from `[source+2]`. Reading new_pc first is
    wrong: if the new workspace's R13-R15 (`new_wp+26..31`) overlaps the BLWP vector,
    the saved context must overwrite the new PC BEFORE it is read.
23. `cpu->st` is uint16_t in C and byte-sized in the assembly - every flag operation
    must produce a value <= 0xFF.

---

## What has been verified

### Initialization & Main Loop

| Area | Assembly | C | Match |
|------|----------|---|-------|
| Entry point register setup | R8=mem, R9=regx38, R3=PC, R11=mem+WP, R1=0 | `tms9900_init` sets mem, regx38, pc, wp, st=0 | ✓ |
| Main loop control | `LDRB [R9]; LSRS #1; BCC IDLE` | `while (*regx38 & 1)` | ✓ |
| Instruction fetch | `LDRH [R8,R3]; ADDS R3,#2; UXTH R3` | `fetchw()` reads BE word, PC+=2 | ✓ |
| PC return on stop | Stores R3 to RETCODE, returns via R0 | `return cpu->pc` | ✓ |
| C wrapper | N/A | Creates struct, calls `run9900_c` | ✓ |

### Instruction Dispatch

| Area | Assembly | C | Match |
|------|----------|---|-------|
| Dispatch mechanism | 2048-entry JMPTBL indexed by `inst_BE >> 5` | `op_hi = inst >> 8` with cascading if/else | ✓ |
| 0x00-0x03 range | Immediate/system (LI, AI, etc.) | `handle_immediate_system` | ✓ |
| 0x04-0x07 range | Single-operand via sdecode2a | `handle_jump_single` | ✓ |
| 0x08-0x0B range | Shifts (SRA, SRL, SLA, SRC) | `handle_shift_rotate` | ✓ |
| 0x0C-0x0F range | F18A stack + SLC | `handle_f18a_stack` / `handle_shift_rotate` | ✓ |
| 0x10-0x1F range | Conditional jumps | `handle_branch_group` | ✓ |
| 0x20-0x3F range | COC/CZC/XOR/XOP/MPY/DIV via sdecode2b | `handle_format9` | ✓ |
| 0x40-0xFF range | Two-operand (word via sdecode2d, byte via sdecode1d) | `handle_two_operand` | ✓ |
| X instruction re-dispatch | `B startX` (same as main loop) | Same if/else chain as main loop | ✓ |

### Operand Decoding

#### `decode_operand` vs `sdecode2a` (single-operand, word)

| Mode | Assembly | C | Match |
|------|----------|---|-------|
| 0 (register) | `ADD R5,R11` → R5 = WP+reg*2 | `o.addr = wp+reg*2; o.val = get_reg(reg)` | ✓ |
| 1 (indirect) | Read reg value, word-align, add R8 | `o.addr = get_reg(reg) & 0xFFFE` | ✓ |
| 2 (indexed) | Fetch imm, add reg (skip if reg==0), word-align | `ea = (reg==0) ? offset : get_reg(reg)+offset; ea &= 0xFFFE` | ✓ |
| 3 (auto-inc) | Read reg, increment by 2, word-align old value | `raw = get_reg(reg); set_reg(reg, raw+2); o.addr = raw & 0xFFFE` | ✓ |

#### `decode_operand` vs `sdecode2d` (two-operand, word)

| Area | Assembly | C | Match |
|------|----------|---|-------|
| Source field extraction | `R6[3:0]` (BE bits 3:0 = S register) | `inst & 0x3F` (bits 5:0 = Ts+S) | ✓ |
| Dest field extraction | `R6[9:6]` via shift/mask | `(inst >> 6) & 0x3F` (bits 11:6 = Td+D) | ✓ |
| Source decoded first | sdecode2d does source, then dest | C calls `decode_operand(src)` then `decode_operand(dst)` | ✓ |
| Source modes 0-3 | Same as sdecode2a, reads value after | Same as above | ✓ |
| Dest modes 0-3 | Word-aligned for all modes | Word-aligned via `& 0xFFFE` | ✓ |

#### `decode_operand` vs `sdecode1d` (two-operand, byte)

| Area | Assembly | C | Match |
|------|----------|---|-------|
| Mode 0 source | `LDRB [WP+reg*2]` (high byte) | `rd8(mem, wp+reg*2)` | ✓ |
| Mode 1 source | Read reg value, NO word-align, `LDRB` | `o.addr = get_reg(reg)` (no align) | ✓ |
| Mode 2 source | Fetch imm, add reg (skip if 0), NO align | Same, no align for byte | ✓ |
| Mode 3 source | Increment by 1 (byte), NO align | `inc = 1`, no align | ✓ |
| Mode 0 dest | `ADD R6,R11` (WP+D*2) | Same | ✓ |
| Mode 1 dest | Read reg, NO align | Same | ✓ |
| Mode 2 dest | Fetch, add reg, NO align | Same | ✓ |
| Mode 3 dest | Increment by 1, NO align | Same | ✓ |

#### `decode_operand` vs `sdecode2b` (format 9: COC/CZC/XOR/MPY/DIV)

| Area | Assembly | C | Match |
|------|----------|---|-------|
| Source | Full addressing modes (same as sdecode2a) | `decode_operand(inst & 0x3F, 0)` | ✓ |
| Dest | Always register: D from bits 9:6 | `dreg = (inst >> 6) & 0xF` | ✓ |
| Source value read | `LDRH [R5]; REV16` → R5 = native value | `src.val` from decode_operand | ✓ |
| Dest value read | `LDRH [R6]; REV16` → R4 = native value | `get_reg(dreg)` | ✓ |

### `store_operand` vs Assembly Stores

| Mode | Assembly | C | Match |
|------|----------|---|-------|
| 0 word | `STRH` at WP+reg*2 (LE) | `set_reg(reg, v)` (BE) | ✓ |
| 0 byte | `STRB` at WP+reg*2 (high byte) | `wr8(mem, wp+reg*2, v)` | ✓ |
| Non-0 word | `STRH` at effective addr | `wr16(mem, addr, v)` | ✓ |
| Non-0 byte | `STRB` at effective addr | `wr8(mem, addr, v)` | ✓ |

### Flag Computation

#### `set_flags_word` vs `OP_COMP_W`

| Value | Assembly | C | Match |
|-------|----------|---|-------|
| 0 | `ADDS #0x20` (EQ) | `st \|= EQ` | ✓ |
| Positive (1-0x7FFF) | `ADDS #0xC0` (LGT\|AGT) | `st \|= LGT\|AGT` | ✓ |
| Negative (0x8000-0xFFFF) | `ADDS #0x80` (LGT only) | `st \|= LGT` | ✓ |
| Pre-mask | Caller does `ANDS #0x1E` | Function does `st &= 0x1E` | ✓ |

#### `set_flags_byte` vs `OP_COMP_B`

| Value | Assembly (PARITY0 table) | C | Match |
|-------|--------------------------|---|-------|
| 0 | 0x20 (EQ) | parity_tbl[0]=0, then EQ → 0x20 | ✓ |
| 1 | 0xC4 (LGT\|AGT\|P) | parity_tbl[1]=4, then LGT\|AGT → 0xC4 | ✓ |
| 128 | 0x84 (LGT\|P) | parity_tbl[128]=4, then LGT → 0x84 | ✓ |
| 255 | 0x80 (LGT) | parity_tbl[255]=0, then LGT → 0x80 | ✓ |
| Pre-mask | Caller does `ANDS #0x1A` | Function does `st & ~(LGT\|AGT\|EQ\|P)` = `st & 0x1A` | ✓ |

#### Parity Table

- Assembly PARITY table (line 714) matches C `parity_tbl` exactly (256 bytes, P=4 for ODD bit-count)
- Assembly PARITY0 table (line 731) combines LGT/AGT/EQ/P - verified to produce same results as C's separate `set_flags_byte` logic

### Immediate/System Instructions (0x0000-0x03FF)

| Instruction | Sub-opcode | Verified Areas | Match |
|-------------|------------|----------------|-------|
| LI (0x0200) | 0x10 | Fetch imm, set_reg, flags (st&=0x1E + set_flags_word) | ✓ |
| AI (0x0220) | 0x11 | Fetch imm, add16(reg, imm), set_reg | ✓ |
| ANDI (0x0240) | 0x12 | Fetch imm, AND, set_reg, flags | ✓ |
| ORI (0x0260) | 0x13 | Fetch imm, OR, set_reg, flags | ✓ |
| CI (0x0280) | 0x14 | Fetch imm, cmp16(reg_val, imm) - operand order verified | ✓ |
| STWP (0x02A0) | 0x15 | Stores WP offset (R11-R8 in asm) | ✓ |
| STST (0x02C0) | 0x16 | `STRH R1` stores ST in high byte; C uses `st << 8` | ✓ |
| LWPI (0x02E0) | 0x17 | Fetch imm, word-align, set WP | ✓ |
| LIMI (0x0300) | 0x18 | Skip immediate word (PC += 2) | ✓ |
| IDLE (0x0340) | 0x1A | Return PC, exit loop | ✓ |
| RTWP (0x0380) | 0x1C | Restore ST from byte at WP+30, PC from WP+28, WP from WP+26 | ✓ |

### Single-Operand Instructions (0x0400-0x07FF)

| Instruction | Verified Areas | Match |
|-------------|----------------|-------|
| BLWP (0x0400) | Mode-0 reads from workspace addr; context save order fixed this session | ✓ |
| B (0x0440) | Mode-0 branches to workspace address; other modes use effective addr | ✓ |
| X (0x0480) | Mode-0 uses register value AS instruction; others read from memory | ✓ |
| CLR (0x04C0) | No flag update (matches assembly) | ✓ |
| NEG (0x0500) | st&=0x06; 0x8000→OV+flags; 0→carry; else negate+store+flags | ✓ |
| INV (0x0540) | Bitwise NOT, st&=0x1E, set_flags_word | ✓ |
| INC (0x0580) | add16(val, 1), store | ✓ |
| INCT (0x05C0) | add16(val, 2), store | ✓ |
| DEC (0x0600) | sub16(val, 1), store | ✓ |
| DECT (0x0640) | sub16(val, 2), store | ✓ |
| BL (0x0680) | Save PC to R11, then branch (same mode-0 handling as B) | ✓ |
| SWPB (0x06C0) | Byte swap, no flags | ✓ |
| SETO (0x0700) | Set to 0xFFFF, no flags | ✓ |
| ABS (0x0740) | st&=0x06; 0x8000→OV; negative→negate+store; flags on ORIGINAL value | ✓ |

### Shift/Rotate Instructions (0x0800-0x0BFF, 0x0E00)

| Instruction | Verified Areas | Match |
|-------------|----------------|-------|
| SRA (0x0800) | st&=0x0E; count=0→read R0 low nibble; arithmetic right shift; carry from last bit out | ✓ |
| SRL (0x0900) | Same init; logical right shift | ✓ |
| SLA (0x0A00) | st&=0x06; overflow via sign-change detection; carry from bit 16 of result; count=16 special path | ✓ |
| SRC (0x0B00) | st&=0x0E; rotate right via `(v>>count)\|(v<<(16-count))`; carry from bit (count-1) | ✓ |
| SLC (0x0E00) | st&=0x0E; rotate left via `(v<<count)\|(v>>(16-count))`; carry detection | ✓ |
| Count extraction | Assembly: `R0>>12` (LE bits). C: `(inst>>4)&0xF` (BE bits). Same count. | ✓ |
| Register extraction | Assembly: `LSLS/LSRS` bit manipulation. C: `inst&0xF`. Same register. | ✓ |
| Count=0 from R0 | Assembly: `LDRB [WP+1] & 0x0F`. C: `get_reg(0) & 0xF`. Both read low nibble of R0. | ✓ |

### Conditional Jumps (0x1000-0x1FFF)

| Jump | Condition | Assembly | C | Match |
|------|-----------|----------|---|-------|
| JMP (0x10) | Unconditional | `SXTH; ASRS #7` | `(int8_t)(inst&0xFF) * 2` | ✓ |
| JMP self | disp==-2 → IDLE | `ADDS R4,R0,#2; BNE` | `disp==-2 → return 0` | ✓ |
| JLT (0x11) | AGT=0 AND EQ=0 | `TST #0x60; BNE skip` | `(st & (AGT\|EQ)) == 0` | ✓ |
| JLE (0x12) | LGT=0 OR EQ=1 | EQ→carry→jump; LGT→carry→skip | `!LGT \|\| EQ` | ✓ |
| JEQ (0x13) | EQ=1 | `LSRS #6; BCC skip` | `st & EQ` | ✓ |
| JHE (0x14) | LGT=1 OR EQ=1 | `TST #0xA0; BEQ skip` | `st & (LGT\|EQ)` | ✓ |
| JGT (0x15) | AGT=1 | `LSRS #7; BCC skip` | `st & AGT` | ✓ |
| JNE (0x16) | EQ=0 | `LSRS #6; BCS skip` | `!(st & EQ)` | ✓ |
| JNC (0x17) | C=0 | `LSRS #5; BCS skip` | `!(st & C)` | ✓ |
| JOC (0x18) | C=1 | `LSRS #5; BCC skip` | `st & C` | ✓ |
| JNO (0x19) | OV=0 | `LSRS #4; BCS skip` | `!(st & OV)` | ✓ |
| JL (0x1A) | LGT=0 AND EQ=0 | `TST #0xA0; BNE skip` | `!(st & (LGT\|EQ))` | ✓ |
| JH (0x1B) | LGT=1 AND EQ=0 | EQ→skip; LGT=0→skip | `LGT && !EQ` | ✓ |
| JOP (0x1C) | P=1 | `LSRS #3; BCC skip` | `st & P` | ✓ |
| TB (0x1F) | Clears EQ | `ANDS #0xDF` | `st &= ~EQ` | ✓ |

### Displacement Calculation

- Assembly: `SXTH R0,R0; ASRS R0,#7` on LE instruction → extracts signed byte displacement × 2
- C: `(int16_t)((int8_t)(inst & 0xFF)) * 2` on BE instruction → same result
- Verified with: +5→+10, -5→-10, -1→-2 (self-jump), +1→+2

### Format 9 Instructions (0x2000-0x3FFF)

| Instruction | Verified Areas | Match |
|-------------|----------------|-------|
| COC (0x2000) | EQ if `(dst & src) == src`; else clear EQ | ✓ |
| CZC (0x2400) | EQ if `(dst & src) == 0`; else clear EQ | ✓ |
| XOR (0x2800) | `dst ^ src`, store, st&=0x1E, set_flags_word | ✓ |
| XOP/PIX (0x2C00) | BM pattern-name offset; BL address off a rounded-up row stride, wrapped in 16KB, subpixel from x, plus the conditional write and read-back flags | ✓ |
| MPY (0x3800) | 16×16→32 multiply; high word to dreg, low to dreg+1 | ✓ |
| DIV (0x3C00) | OV if src ≤ high_word (or src=0); else quotient+remainder | ✓ |
| LDCR/STCR (0x3000) | CRU not emulated; assembly skips extra word for indexed mode; C decode_operand naturally consumes it (but may spuriously auto-increment in mode 3) | ~✓ |

### Two-Operand Word Instructions (0x4000-0xFFFF, even opcodes)

| Instruction | Verified Areas | Match |
|-------------|----------------|-------|
| SZC (0x4000) | `dst & ~src`, store, st&=0x1E, set_flags_word | ✓ |
| S (0x6000) | sub16(dst, src), store | ✓ |
| C (0x8000) | cmp16(src, dst) - operand order verified against `CMP R5,R2` | ✓ |
| A (0xA000) | add16(dst, src), store | ✓ |
| MOV (0xC000) | Store src to dst, st&=0x1E, set_flags_word(src) | ✓ |
| SOC (0xE000) | `dst \| src`, store, st&=0x1E, set_flags_word | ✓ |

### Two-Operand Byte Instructions (0x5000-0xFFFF, odd opcodes)

| Instruction | Verified Areas | Match |
|-------------|----------------|-------|
| SZCB (0x5000) | `dst & ~src` (byte), store byte, st&=0x1A (eff.), set_flags_byte | ✓ |
| SB (0x7000) | sub8(dst, src), store byte | ✓ |
| CB (0x9000) | cmp8(src, dst) - parity on SOURCE byte, then LGT/AGT/EQ | ✓ |
| AB (0xB000) | add8(dst, src), store byte | ✓ |
| MOVB (0xD000) | Store src byte to dst, set_flags_byte(src) | ✓ |
| SOCB (0xF000) | `dst \| src` (byte), store, set_flags_byte | ✓ |

### ALU Flag Details

#### `add16` / `add8`

| Flag | Assembly | C | Match |
|------|----------|---|-------|
| Init mask | 0x06 (keep P, bit1) | `st &= 0x06` | ✓ |
| Carry (word) | `LSRS R4,R7,#16; BEQ nc` (bit 16 of 32-bit result) | `res & 0x10000` | ✓ |
| Carry (byte) | `LSRS R4,R0,#8; BEQ nc` (bit 8) | `res & 0x100` | ✓ |
| Overflow | Same-sign inputs, different-sign result | `((a^b)&MSB)==0 && ((a^r)&MSB)` | ✓ |

#### `sub16` / `sub8`

| Flag | Assembly | C | Match |
|------|----------|---|-------|
| Init mask | 0x06 (word) / 0x02 (byte, but P cleared by set_flags_byte) | `st &= 0x06` | ✓ |
| Carry | `src==0 → carry; else dst>=result → carry` (borrow-NOT) | `b==0 \|\| a>=r` | ✓ |
| Overflow | Different-sign inputs, sign of result differs from dst | `((a^b)&MSB) && ((a^r)&MSB)` | ✓ |

#### `cmp16` / `cmp8`

| Flag | Assembly | C | Match |
|------|----------|---|-------|
| Preserved | C, OV (mask 0x1E word / 0x1A byte) | Same masks | ✓ |
| EQ | `CMP; BNE` → ADDS 0x20 | `first == second` | ✓ |
| LGT | Unsigned greater (BLO to skip) | `first > second` | ✓ |
| AGT | Signed greater (SXTH then CMP, BLT to skip) | `(int)first > (int)second` | ✓ |
| CB parity | PARITY table on SOURCE byte | `parity_tbl[src]` | ✓ |

### F18A Stack Operations (0x0C00-0x0FFF)

| Instruction | Verified Areas | Match |
|-------------|----------------|-------|
| RET (0x0C00) | Read PC from R15+2 (new SP), increment R15 by 2 | ✓ |
| CALL (0x0C40+) | Write PC at OLD R15, decrement R15 by 2, branch to source (mode-0 uses workspace addr) | ✓ |
| PUSH (0x0D00) | Write value at OLD R15, decrement R15 by 2 | ✓ |
| POP (0x0F00) | Increment R15 by 2, read value from new R15, store to dest | ✓ |

### Memory Model

| Area | Assembly | C | Match |
|------|----------|---|-------|
| Endianness | ARM LE loads + REV16 for BE conversion | `rd16`/`wr16` do manual BE access | ✓ |
| Address wrapping | R11 = absolute pointer; may exceed 64KB bounds | uint16_t arithmetic wraps naturally | ✓ |
| WP=0xFFFE | R1 at 0x0000 (wraps); assembly may read out-of-bounds on ARM | C wraps via uint16_t cast | ✓ |
| Byte access | `LDRB`/`STRB` at exact address | `rd8`/`wr8` at exact address | ✓ |
| Word access | `LDRH`/`STRH` (LE) at word-aligned address | `rd16`/`wr16` (BE) at word-aligned address | ✓ |

### Miscellaneous

| Area | Status | Notes |
|------|--------|-------|
| `cpu->st` type | ✓ | uint16_t in C, byte-sized in assembly; all flag ops produce values ≤ 0xFF |
| Dead code (`branch_cond`, `src_through_c`) | ✓ | Defined but never called; no impact |
| XOP/PIX (F18A) | ✓ | Each core computes the BL address for itself, so the PIX group in `test/tms9900` is what holds the three of them to one answer |
| LDCR/STCR mode-3 side effect | Minor | C's decode_operand auto-increments register; assembly skips decode entirely. Only matters for CRU ops which aren't used in GPU programs. |

---

## Remaining Possibilities If Code Still Fails

1. **Build/configuration**: verify `PICO9918_GPU_C_CORE` is defined at compile time (`-DPICO9918_GPU_C_CORE=ON`)
2. **Compiler optimization**: Aggressive inlining or optimization of `static inline` functions may cause unexpected behavior
3. **Glue code**: an issue in `gpu/gpu.c` or the surrounding infrastructure rather than in the interpreter
4. **XOP/PIX**: the BL address is derived separately in the C core and in each assembly core, so run the PIX group in `test/tms9900` on the board before suspecting the program
5. **Debug tracing**: Add trace output for first N instructions (PC, inst word, ST after execution) and compare against assembly behavior on real hardware
