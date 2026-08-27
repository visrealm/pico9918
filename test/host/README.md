# Host-side bus exercise

`pico9918test` is a **second Pico standing in for the host CPU**. It generates GROMCLK and CPUCLK,
drives the TMS9918A bus - `/CSR`, `/CSW`, `MODE`, `CD0-7` - and sets up a Graphics II screen with
sprites, then animates them from the vertical interrupt.

It is the only thing in `test/` that goes through the host interface. `live/` writes VDP registers
and VRAM straight into `tms9918Inst` over SWD, and `bench/` drives the VDP from a real console whose
bus is assumed to work; neither one can tell you the PIO interface, the buffers or the read-ahead
are behaving. This can, in the crudest possible way: if the picture appears and the sprites move,
writes, reads and the interrupt all work.

> This is a **separate RP2040 wired to the PICO9918**, not firmware for the PICO9918 itself. Its
> pin numbers are its own board's and have nothing to do with `src/`. Flashing it to a PICO9918
> replaces the firmware and produces no video.

## Building and running

It is part of the ordinary firmware configuration, so a normal build produces it:

```
cmake --build build --target pico9918test
```

Flash `pico9918test.uf2` to the Pico that will play the host, wire it to the PICO9918's TMS9918A
pins per the `GPIO_*` defines in `test.c` - which are that Pico's own, and put GROMCLK and CPUCLK on
GPIO 0 and 1 rather than anywhere the firmware uses - and give the PICO9918 its own power and a
display. The pin table in the file header is the TMS9918A socket mapping, not this program's wiring.

Expected picture: the BREAKOUT image in Graphics II, `Hello, World!` spelled out in sprites twice
over - a shadow pair two pixels up and left of the main one - drifting horizontally on a sine and
downward one pixel every two frames.

## How it works

`buildGpioState()` composes one whole GPIO word - both chip selects, `MODE`, and the data byte -
and `writeTo9918()` asserts and releases `/CSW` by writing that word twice. Reads mirror it through
`/CSR`, turning the data pins around first.

> **`CD0` is the MSB.** The TMS9918A numbers its data bus the TI way, so the host's `CD0-7` runs
> against the RP2040's GPIO 14-21. `reversed[]` is a 256-byte lookup that flips every byte on the
> way in and out; `REVERSE()` on a value that has already been reversed is what puts it back.
> `HARDWARE.md`, in the build workspace above this repo, records the pin mapping and which nets
> are the 3.3V side.

The two clocks come from `clocks.pio` - one PIO program, two state machines, a pulled `osr` holding
the half-period as a loop count. GROMCLK is the crystal over 24 and CPUCLK the crystal over 3, both
derived from `TMS_CRYSTAL_FREQ_HZ` rather than written out, and the system clock is set to 252 MHz
so the divisors land where they should.

The VDP-side API is a deliberate impersonation of `vrEmuTms9918`'s: `vrEmuTms9918WriteAddr`,
`WriteData`, `ReadStatus`, `SetAddressWrite`, `SetNameTableAddr` and the rest, with the same names
and the same argument order, implemented against real pins instead of a struct. Code written
against the library therefore reads the same here - which is the point, and also the trap, because
nothing warns you which one you are looking at.

## What it is not

**It is not an assertion.** Nothing here compares anything; the test is a person looking at a
screen. Failures show up as a wrong picture, a frozen one, or no picture at all, and it cannot say
which of the three it is.

**It does not test bus timing.** The delays in `writeTo9918()` are `sleep_us(0)`, with several
abandoned attempts at tighter ones commented out around them - `doFn()` exists only to burn cycles
and is called from nowhere. So this exercises the interface at whatever speed the compiler and the
GPIO block happen to produce, not at the datasheet's margins. Setup, hold and the deliberate delay
on `/CSR` are in `HARDWARE.md`, and testing against them properly is the gap
[`../../DEBUGGING.md`](../../DEBUGGING.md) records under "What is missing".

**It has no failure output.** `qc.c` next door prints over USB; this one does not open stdio at all.
