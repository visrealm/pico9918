# Quality control

`pico9918qc` checks that an assembled board is wired the way the schematic says. It is a **loopback
test**: three jumpers tie an output to an input, and the tool drives one end and reads the other.

It answers the question no software test can - *is this particular board built correctly* - and it
is the first thing to run on a board that behaves strangely, before spending time on the firmware.

> This **replaces the firmware** on the board under test. Flash it, run it, then flash the real
> firmware back.

## Wiring

Three jumpers, and nothing else:

| Drive | Read back |
|---|---|
| CPUCLK | /CSR |
| GROMCLK | /CSW |
| /INT | MODE |

Each pair puts an output on one side of the board's buffers and an input on the other, so a
successful read proves the pin, the buffer and the trace between them.

## Running

Part of the ordinary firmware configuration:

```
cmake --build build --target pico9918qc
```

Flash `pico9918qc.uf2` and open the USB serial port - `pico_enable_stdio_usb` is on, and the output
is the whole result. It runs forever, restarting from the top after each pass.

## What it does

**Six loopback checks**, two per pair: drive high, expect high; drive low, expect low. Each prints
`OK` or `FAILED!`, half a second apart so a person can follow along. If any of the six fails the
tool prints `Initial check failed. Halting.` and stops - there is no point walking the data bus on a
board whose control lines are wrong.

**Then a walking-1 on CD0-7**, three times round, a quarter-second a step, printing the byte it is
driving. That runs twice, with `/CSR` asserted the first time and released the second, so the two
passes differ in the **direction** U5 is driving:

- `/CSR` low sets `DIR` for MCU to host, so each bit should appear on the host-side pins;
- `/CSR` high flips `DIR` to host to MCU, so nothing the MCU drives should reach them.

The second pass catches a transceiver whose direction control is dead or inverted - a fault the
first pass alone would report as a clean board. Both need a meter, a scope or a logic analyser on
the host-side pins; the tool prints what it is driving and cannot see the result itself.

> The source calls the second pass "OE disabled", and that is not what it does. Per
> `HARDWARE.md`, `/BOE` is **static** - U9 drives it low whenever `RST`
> is high, so it floats only during reset - and `DIR` (= `/CSR_L`) is the only per-cycle bus
> control. U5 is always driving one way or the other. Read the pass as a direction test, not an
> enable test, or a board that fails it gets diagnosed for the wrong fault.

## Board revisions

`PCB_MAJOR_VERSION` and `PCB_MINOR_VERSION` at the top of `qc.c` select the pin map. GROMCLK and
CPUCLK moved at v0.4 so that `MODE` and `MODE1` came out sequential, and the v0.3 branch is
`#error`'d rather than silently wrong. Check the constants match the board in front of you before
believing a failure - the pin map is the one thing here that a wrong answer looks exactly like a
fault.

`HARDWARE.md` - in the build workspace above this repo, not in it - has the authoritative
pin mapping and buffer topology per revision, including which nets are the 3.3V MCU side and which
the 5V host side. Read it before changing anything in this file.
