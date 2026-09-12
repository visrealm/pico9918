# Putting pico9918-core in an emulator

This is the host-side guide. The API reference says what each call does; this page says
which calls belong together, who owns the bits around them, and where the easy traps are.

There are two useful stopping points:

- A TMS9918A emulator needs the four bus calls, the indexed scanline renderer and an
  interrupt check. If the emulator already owns the raster and its palette, stop there.
- An F18A or PICO9918 emulator wants the frame layer as well. It publishes the raster
  state the GPU reads, handles horizontal interrupts and triggers, applies the live
  palette, and keeps the taller modes inside a fixed output frame.

The examples are deliberately small.
[`host_bus.c`](https://github.com/visrealm/pico9918-core/blob/main/examples/host_bus.c)
is the first shape,
[`gpu_program.c`](https://github.com/visrealm/pico9918-core/blob/main/examples/gpu_program.c)
is the second, and
[`f18a_modes.c`](https://github.com/visrealm/pico9918-core/blob/main/examples/f18a_modes.c)
is a useful register-level tour between them.

The ownership line is simple once it is written down:

| pico9918-core owns | the emulator owns |
|---|---|
| VRAM, registers, status, the address latch and read-ahead | the machine's port decode and CPU timing |
| mode decode, tiles, sprites, palette RAM and scanline pixels | the output texture, scaling and presentation |
| F18A unlock, GPU execution and VDP-side triggers | how often the raster and GPU are advanced |
| the 256-byte config layout and VDP-side apply | config storage and settings that affect the host |
| non-destructive debugger access | the debugger UI, disassembler and breakpoint scheduler |

## Build the library

The default is the normal emulator build:

```sh
cmake -S pico9918-core -B build/pico9918
cmake --build build/pico9918
```

It carries the 64KB map, enhanced renderer and GPU, and defaults to the runtime chip
selector, PRO line width and debugger surface an emulator is likely to want.
`PICO9918_RUNTIME_CHIP` adds the personality enum and `pico9918_set_chip()`. The wide
80-column option is what lets that selector reach the PRO tier; without the 512-byte
line, a request for PRO is clamped to PICO9918. A release build with no debugger can use
`-DPICO9918_DEBUG_API=OFF`.

If the emulator only wants an original TMS9918A and the smaller 16KB allocation matters,
use `vrEmuTms9918`. pico9918-core deliberately carries the PICO9918 feature set.

Leave `PICO9918_SINGLE_INSTANCE` at its default `0`. Its `1` form removes the instance
argument from nearly every function and exists for firmware with exactly one VDP at a
fixed address. It buys a desktop emulator very little and makes a second VDP impossible.

The [complete build-option table](https://github.com/visrealm/pico9918-core/blob/main/BUILDING.md#library-and-emulator-options)
also covers the Python binding, diagnostic counter, Pico GPU core choice and the test
harness switches.

Vendored and installed builds use the same target:

```cmake
# vendored
add_subdirectory(external/pico9918-core)
target_link_libraries(my_emulator PRIVATE pico9918::core)

# or installed
find_package(pico9918_core CONFIG REQUIRED)
target_link_libraries(my_emulator PRIVATE pico9918::core)
```

Do not copy the build flags into the consumer. The installed
`pico9918_build_config.h` records the archive's instance ABI, line width, chip switch
and debug surface. In particular, defining `PICO9918_SINGLE_INSTANCE` differently in
the emulator would change the argument list without changing the C symbol name. The
header rejects that mismatch before it becomes a very strange crash.

## One instance, from power-on to shutdown

This guide uses the normal multi-instance API:

```c
#include <pico9918/pico9918.h>

pico9918_t *vdp = pico9918_new();
if (!vdp)
  return false;

/* Choose the chip fitted to this emulated machine. */
pico9918_set_chip(vdp, PICO9918_CHIP_PICO9918);

/* pico9918_new() has already reset the instance. Do this again on a guest reset. */
pico9918_reset(vdp);

/* ...run the machine... */

pico9918_destroy(vdp);
```

`pico9918_reset()` resets the VDP state and deliberately leaves VRAM alone. That is what
the chip does. If an emulator promises cleared VRAM, that is an emulator policy rather
than a VDP reset; write it through the data port before the guest starts.

For an F18A or PICO9918 personality, initialise the GPU after the VDP reset and before
a guest can arm it:

```c
#include <pico9918/gpu/gpu.h>

pico9918_reset(vdp);
pico9918_gpu_init(vdp);
```

The `PICO9918_INST*` macros in the headers are there for code that genuinely has to
compile in both instance modes. An emulator built against one known package is clearer
with the ordinary calls above.

## The host bus

The guest sees two ports and four operations. pico9918-core does not decide where those
ports live in the host machine's I/O map.

| guest operation | call |
|---|---|
| data-port write | `pico9918_write_data(vdp, value)` |
| data-port read | `pico9918_read_data(vdp)` |
| address-port write | `pico9918_write_addr(vdp, value)` |
| status-port read | `pico9918_read_status(vdp)` |

A bus adapter is usually no more than this:

```c
void machine_vdp_write(uint16_t port, uint8_t value)
{
  if (port & 1)
    pico9918_write_addr(vdp, value);
  else
    pico9918_write_data(vdp, value);
}

uint8_t machine_vdp_read(uint16_t port)
{
  return (port & 1) ? pico9918_read_status(vdp)
                    : pico9918_read_data(vdp);
}
```

Change the decode, not the calls, for a machine whose ports are reversed or mirrored.

An address-port command is two bytes. The payload or low address byte arrives first;
the second byte tells the VDP what the pair meant:

| second byte | operation |
|---|---|
| `10rrrrrr` | write the first byte to register `r` |
| `01aaaaaa` | set a VRAM write address |
| `00aaaaaa` | set a VRAM read address and prefetch |

The read prefetch is already modelled. After a guest sets a read address, its first
`pico9918_read_data()` returns the byte at that address, then the address advances.
Do not add another dummy read around it.

The address latch is also real state. Data-port access and status reads cancel a
half-written pair, and an out-of-band tool must not inject its own two-byte command
while the guest is between bytes. A debugger uses the debug API for this reason; it
does not borrow the guest's ports.

### Interrupts belong on the emulated CPU's line

Poll `pico9918_interrupt_status()` wherever the machine updates its interrupt inputs:

```c
machine_set_vdp_irq(pico9918_interrupt_status(vdp));
```

That answer is the line, not just SR0's F bit. On a TMS9918A it is R1 interrupt-enable
AND the frame flag. On an unlocked F18A it also includes the independent R19 horizontal
interrupt under R0's enable bit.

`pico9918_read_status()` is the guest's acknowledgement. Reading SR0 clears the flags
it returned and can drop `/INT`; reading selected SR1 clears its horizontal-interrupt
flag. `pico9918_peek_status()` is an inspection call for old code and returns SR0
without clearing it. It is not what a guest port read should call.

A host built from source may define `PICO9918_HOST_SET_INT(active)` and have the library
drive the line at the exact change instead. Put that and any critical-section overrides
in a `PICO9918_HOST_OPS_HEADER` that is compiled into the library itself. Defining a
macro only in the emulator translation unit cannot change a library that is already an
archive. Polling is simpler and is a perfectly good desktop integration.

### Timing is scanline-grained

pico9918-core is a scanline renderer, not a dot-clock simulation. Bus calls happen when
the emulator makes them, and a call to render line `y` samples the VDP state for that
line. A register or VRAM write ordered before the call affects it; one ordered after the
call cannot change pixels already returned. There is no half-rendered line to revisit.

The host therefore owns PAL/NTSC timing, total lines, CPU-to-raster scheduling and the
point at which it presents a frame. This fits machines that schedule devices at a line
or event boundary. An emulator promising pixel-exact mid-line effects needs a finer VDP
model than this library exposes.

## The small renderer: active lines as palette indexes

`pico9918_scan_line(vdp, y)` draws one active VDP line into an internal buffer. Read it
before asking for the next one:

```c
for (uint16_t y = 0; y < 192; ++y)
{
  uint8_t raised = pico9918_scan_line(vdp, y);
  pico9918_frame_update_interrupts(vdp, raised);

  const uint8_t *src = pico9918_line_source(vdp);
  uint32_t bytes = pico9918_line_bytes(vdp);
  copy_indexed_line(y, src, bytes);
}
```

Include `pico9918_frame.h` for `pico9918_frame_update_interrupts()`. The return from
`pico9918_scan_line()` is what this line raised--sprite collision, fifth sprite and its
number--not a complete replacement for the latched status register. The frame helper
merges it with flags from earlier lines. Ignoring the return still draws the right
picture, which is why a picture-only example can get away with it, but the guest then
gets the wrong status.

Raise the frame interrupt at the point the emulated raster reaches it, not when the
window happens to present:

```c
pico9918_frame_raise_end_of_frame_int(vdp);
```

For a plain TMS9918A host that already owns all video timing, those calls are enough.
The active height is 192. PAL or NTSC changes the surrounding scan count and frame rate,
not the 192 lines handed to the renderer.

There are two formats hiding behind the word "indexed":

- Graphics I, Graphics II, Multicolor and 40-column text return 256 bytes, one palette
  index per VDP pixel. An F18A index uses six bits; a base index uses the low four.
- Narrow 80-column text returns 256 bytes holding 512 pixels. The high nibble is the
  left pixel and the low nibble the right. With `PICO9918_TEXT80_8BPP=ON`, an unlocked
  PRO personality returns 512 bytes, one six-bit palette index per pixel instead.

`pico9918_line_bytes()` is the answer for this line. `PICO9918_SCANLINE_BYTES_MAX` is
the answer for an allocation. Do not assume either is 256 in a general F18A host.

`pico9918_default_palette()` converts an index through the power-on palette. It does not
read palette RAM. That is fine for a base-chip emulator or a screenshot made before a
guest changes the palette. A complete F18A host either tracks palette writes itself or
uses the frame renderer below, which expands through the live palette for you.

## The full frame renderer

`pico9918_frame_scanline()` is the device-facing renderer. In addition to the picture
it owns the bits that become easy to miss when F18A software starts using them:

- the left and right border;
- the live 64-entry palette and its dirty rebuild;
- the raster and blanking registers;
- the R19 horizontal interrupt;
- per-line and per-frame GPU triggers;
- GPU slices when the library owns GPU pacing;
- the PICO9918 splash and diagnostics overlays.

It writes `PICO9918_PIXEL_T`, which is the library's 16-bit BGR12 format: blue in bits
11-8, green in 7-4 and red in 3-0. Convert it at the edge of the emulator:

```c
uint32_t argb = 0xff000000u | pico9918_pixel_rgb888(line[x]);
```

The picture is always a 512-pixel window. A 256-wide VDP pixel is doubled; wide
80-column text already has 512 pixels. The border splits whatever is left:

```text
| (width - 512) / 2 |          512 picture pixels          | (width - 512) / 2 |
```

The supplied width must be at least 512 and a multiple of four, and the output buffer
must be four-byte aligned. The renderer is the video-rate path and deliberately does
not check those conditions on every line.

### Driving the same sequence as the board

The frame geometry returned at the end of one frame is used by the next. Start with the
progressive defaults shown here:

```c
static uint8_t v_scale = 2;
static uint16_t v_virtual = 240;
static uint32_t trigger_line = 192;

void render_frame(void)
{
  _Alignas(4) PICO9918_PIXEL_T line[640];
  pico9918_scanline_params_t scan = {640, v_virtual, false, 0};

  for (uint16_t y = 0; y < v_virtual; ++y)
  {
    bool border = pico9918_frame_scanline(vdp, y, &scan, line);
    present_bgr12_line(y, line, 640, border);

    if (y == trigger_line)
      pico9918_frame_end_of_scanline(vdp);
  }

  pico9918_frame_porch(vdp);

  pico9918_frame_display_t display = {480, false, v_scale, v_virtual};
  pico9918_frame_geometry_t geometry =
      pico9918_frame_end(vdp, host_temperature_c(), 60.0f, &display);

  v_scale = display.vPixelScale;
  v_virtual = display.vVirtualPixels;
  trigger_line = geometry.triggerScanline;
}
```

`pico9918_frame_end_of_scanline()` is the frame-interrupt trigger, despite the slightly
awkward name inherited from the scanline callback that calls it. `pico9918_frame_end()`
has a fallback, so a host that cannot schedule the trigger still gets an interrupt at
true end of frame. It will be later than the device, which can matter to raster work.

The temperature is a PICO9918 status value and diagnostics input. A desktop host with
no useful sensor can pass a stable room-temperature value. The frame rate should be the
timing the emulated display is actually running, not how often the UI managed to paint.

For an interlaced host, set `interlaced=true`. Bit 12 of the scanline number carries the
field and bits 11-0 carry the line within that field; `interlacedFieldOrder` is XORed
with the field. In that mode the host owns `vPixelScale` and `vVirtualPixels` rather
than taking progressive values back from the library.

### A fixed 480-line surface

Most desktop front ends want 480 output rows every frame and do not want to understand
double rows, 30-row mode or vertical repeats. `pico9918_frame_output_line()` is that
adapter:

```c
pico9918_scanline_params_t scan = {640, 240, false, 0};
_Alignas(4) PICO9918_PIXEL_T line[640];

for (uint32_t y = 0; y < 480; ++y)
{
  if (pico9918_frame_output_line(vdp, y, &scan, line))
    convert_bgr12_line(line, converted, 640);
  present_line(y, converted);
}
```

The return says whether `line` changed. A repeated line normally returns false, so the
previous conversion still stands. A dimmed CRT-scanline repeat returns true because its
pixels really did change. Keep the porch, trigger and `pico9918_frame_end()` calls around
this loop just as you would around `pico9918_frame_scanline()`; output-line is a vertical
mapping helper, not a frame clock.

`geometry.triggerScanline` is in virtual-line coordinates. In a fixed output loop its
progressive output row is `triggerScanline * display.vPixelScale`; if that is exactly
the output height, fire the trigger after the last row and before the porch.

## Running the F18A GPU

The GPU is a TMS9900 whose workspace and program live in the VDP's memory. Guest
software loads it through the ordinary VDP ports and normally starts it by writing the
low PC byte to R55. The host must initialise it, then choose exactly one pacing model.

### Let the library budget it

This is the usual emulator integration:

```c
pico9918_gpu_init(vdp);
pico9918_gpu_set_clock(vdp, PICO9918_GPU_IPS_F18A);
```

After that, call no stepping function. The library runs a budgeted slice from the
arming register write and one from each full-frame scanline. Running from the arming
write is important: F18A detection programs can write a tiny program and read its
answer a few host instructions later. Servicing the GPU only at the next scanline makes
that detection depend on where in the line the probe happened to run.

The three supplied rates are measured approximations, not claims about an exact clock:
`PICO9918_GPU_IPS_F18A`, `_CLASSIC` and `_PRO`. Start with the personality being
emulated. A host can expose its own rate control without changing anything else.

This pacing model needs the full frame renderer. `pico9918_scan_line()` alone neither
publishes the raster value a GPU program reads nor calls the budgeted service.

### One host thread, explicitly interleaved

Leave the clock at zero and run bounded slices yourself:

```c
while (pico9918_gpu_step_n(vdp, 20000))
  advance_some_video();
```

This is useful when the emulator scheduler already works in quanta. The boolean says
the current program still has work, and the PC and status are kept for the next slice.
`pico9918_gpu_step()` is unbounded: it returns on GPU `IDLE` or when the program clears
its run flag. A program is allowed to wait for the raster, so calling the unbounded form
on the only thread that can advance that raster is a tidy-looking deadlock.

### A dedicated thread

With a thread to give it, leave the clock at zero and run:

```c
pico9918_gpu_loop(vdp); /* does not return */
```

That is the board's shape. The thread also dispatches flash and configuration requests.
An emulator that needs a stoppable worker will usually write its own loop around
`pico9918_gpu_step_n()` instead, since the library's dedicated loop is intentionally an
infinite firmware loop.

Do not mix the three models. A non-zero library clock plus a host stepper gives one GPU
program two owners.

## Choosing a chip personality

With `PICO9918_RUNTIME_CHIP=ON`, one archive can back a machine selector:

```c
pico9918_t *vdp = pico9918_new();
pico9918_set_chip(vdp, PICO9918_CHIP_F18A);
pico9918_reset(vdp);                 /* a clean power-on as the selected part */
pico9918_gpu_init(vdp);
```

The selection survives later resets. A new instance starts at `PICO9918_CHIP_MAX`, and
an unsupported request clamps to that build's maximum, so read `pico9918_chip()` back
if the choice came from a config file.

The enum is a capability ladder:

| personality | what changes |
|---|---|
| `PICO9918_CHIP_TMS9918` | pre-A part; M3 is not decoded, so no Graphics II |
| `PICO9918_CHIP_TMS9918A` | Graphics II, eight registers, 4K/16K DRAM addressing |
| `PICO9918_CHIP_F18A` | unlock, 64-register file, enhanced renderer and GPU; real F18A memory windows and ID |
| `PICO9918_CHIP_PICO9918` | F18A plus flat GPU RAM, config port, firmware command and overlays |
| `PICO9918_CHIP_PICO9918_PRO` | PICO9918 plus wide 80-column features and the PRO splash |

Locked is not the same as a base personality. An F18A powers on locked and behaves like
a TMS9918A at the guest interface until the two R57 unlock writes, but it still has the
F18A's SRAM and identity waiting behind that gate. Select the actual chip and let the
guest decide whether to unlock it.

Stepping down from an unlocked personality relocks it. It does not rebuild the instance
or erase VRAM. For a user-visible machine change, stop the emulation thread, select the
personality, then reset. Changing it from a GUI thread while bus or rendering calls are
in flight is a data race.

## The PICO9918 configuration block

`pico9918_config.h` describes the 256-byte block reached by the PICO9918's R58/R59
configuration port. This is not part of a real F18A. The core gates it by personality,
so an F18A guest cannot accidentally reach board settings even though the same archive
contains them.

The library owns the byte layout, validation, migrations and VDP-side effects. The host
owns storage and anything outside the VDP--display-driver choice, host clocks and the
pending-settings boot flow.

### Loading it

On startup, copy a stored block into the instance or make a proper default block. Do not
zero it: the default palette is not zero.

```c
#include <pico9918/pico9918_config.h>

uint8_t *config = pico9918_config(vdp);
if (!load_exactly_256_bytes(config))
  pico9918_config_defaults(config);

bool changed = pico9918_config_validate(config, emulated_hardware_version);
pico9918_config_apply_now(vdp, true);

if (changed)
{
  pico9918_config_prepare_save(config, emulated_hardware_version);
  save_exactly_256_bytes(config);
}
```

The board revision is the only identity byte a host supplies, because it is the only one
the library cannot know. Pass the revision belonging to the device the emulator claims to
be, keep it stable across runs, and use the encoding the configurator decodes: major in
the high nibble, minor in the low - `0x03`, `0x10` or `0x20` for a v0.3, v1.x or v2.x
board. Byte 0, the MCU the configurator picks a firmware image by, is derived from it: a
major of 2 or above is the PRO tier on the RP2350.

The version pair is the library's own, from the build it was compiled at. That is
deliberate: it is compared against the field table that decides which settings a firmware
upgrade re-defaults, and a caller naming a different version leaves the fields added
since at whatever the stored block held.

Validation stamps all four bytes, clears stored command bytes, resets a foreign or
damaged block, and defaults fields introduced after its stored version. Its return means
the resulting bytes ought to be persisted.

`pico9918_config_apply_now(vdp, true)` seeds R50, R30, the first sixteen palette entries
and the render base where the selected personality has the PICO9918 config feature. It
does not keep ownership of those registers: later guest writes win.

This and `pico9918_set_chip()` may be called in either order. Selecting a personality that
has no settings block - an F18A or either TMS9918 - takes R30 to that chip's own scanline
sprite limit, because a TMS9918A has no register to raise it with and must not inherit a
PICO9918's. Selecting one that does have a block leaves R30 to the block, so a later apply
is not needed to undo the step.

The palette goes the same way, and deliberately. The sixteen entries in the block are the
PICO9918's power-on palette, not a preference that follows the user from chip to chip: a
TMS9918A personality shows the colours a TMS9918A has, and an F18A its own. Selecting the
PICO9918 first and then stepping down does leave those entries in place, because nothing
clears them - but that is a side effect, not a supported way to carry a palette onto a
lesser chip, and it does not carry R30 with it. A host that wants one palette everywhere
owns that choice and should write the entries itself.

### Settings changed while running

After changing bytes in the block, either apply immediately:

```c
config[PICO9918_CONF_CRT_SCANLINES] = enabled;
pico9918_config_apply_now(vdp, true);
```

or defer VDP-visible changes to the frame boundary:

```c
pico9918_config_schedule_apply(vdp, true);
```

The deferred form avoids changing the palette or seeded registers halfway down a
frame. Pass `false` when only host-side or derived effects need refreshing. Register a
`pico9918_config_set_applied_callback()` if the host has effects to keep in step; it is
called last, after the VDP-side apply.

### Guest requests to save or confirm

Configurator software writes command bytes in the block. The GPU service clears a
command and reports it through `pico9918_gpu_set_config_save_callback()`:

```c
static void config_action(pico9918_t *which, uint8_t *config,
                          uint8_t key, void *opaque)
{
  struct emulator *emu = opaque;
  queue_config_action_copy(emu, which, config, CONFIG_BYTES, key);
}

pico9918_gpu_set_config_save_callback(vdp, config_action, emulator);
```

`key` is one of `PICO9918_CONF_SAVE_TO_FLASH`, `_SAVE_FORCED`, `_PENDING_CONFIRM` or
`_PENDING_CANCEL`. A small PICO9918 emulation can treat the first two as "prepare and
save the 256 bytes". Device-faithful pending display changes also keep a
`PICO9918_PENDING_RECORD_BYTES` record and use
`pico9918_config_pending_capture()`, `pico9918_config_pending_restore()` and
`pico9918_config_refresh_pending_mirror()` to move it through CONFIRMED, PENDING and
ARMED. That state lives in host storage because it decides what happens across a reboot.

The callback may fire from inside `pico9918_frame_scanline()` when a non-zero GPU clock
lets the library pace the GPU. Do not write a file or block on a UI lock there. Copy the
256-byte block or queue the request, return, and persist it elsewhere.

These actions are dispatched by the GPU service even when no GPU program is running.
That means a PICO9918 host still needs one of the pacing models above: a non-zero clock
with the full frame renderer, a call to `pico9918_gpu_step_n()` from its scheduler, or
the dedicated loop. With the clock left at zero and no host service calls, the command
byte sits there and the configurator waits for a save that nobody has picked up.

`pico9918_frame_set_config_reload_callback()` is the other persistence seam. The
PICO9918 diagnostics screen temporarily forces panel settings when no guest enables the
display for 900 frames. If the display finally appears after that, the callback lets a
host reload the user's stored block. An emulator that does not emulate that board boot
experience can leave it unset.

### Firmware-flash requests

R63 belongs to the PICO9918 personality too. A host that supports its firmware-update
protocol registers `pico9918_gpu_set_flash_callback()`, moves the work off the scanline
thread if necessary, then finishes every request with
`pico9918_gpu_flash_complete()`. A host with no callback reports unsupported
automatically. Do not register a stub that forgets to complete: the guest will wait on
the busy bit forever.

## Building a debugger

Build with `PICO9918_DEBUG_API=ON` and include `pico9918_debug.h`. This surface exists
so a debugger never has to include `impl/pico9918_priv.h`, and so inspection does not
change the machine it is inspecting.

### A memory pane

The published map is backing storage: each byte once, including the GPU workspace that
spills just past `0xffff`. It stays put when the user changes personality. A running
F18A program sees decoded and mirrored windows over that storage; the debugger can
derive that view, but the stable backing view is the useful default.

Walk its regions instead of copying address constants into the UI:

```c
for (uint32_t at = 0, end = 0; ; at = end)
{
  uint32_t flags = pico9918_debug_region(at, &end);
  if (!flags)
    break;
  add_memory_region(at, end, flags);
}
```

`pico9918_debug_read()` reads a span without moving the guest address, consuming
read-ahead or clearing status. `pico9918_debug_write()` stops short at the register and
status windows. A short return is part of the contract, so split a bulk operation on
the regions above rather than treating it as an I/O error.

Palette RAM is writable. A write there republishes the palette so the next render sees
the edit. `pico9918_debug_palette()` reads one live entry as the `0x0rgb` value a person
wants to see rather than the byte-swapped backing word.

### Registers and status

Use `pico9918_debug_reg()` for the physical 64-byte register file and
`pico9918_debug_reg_write()` for an editor. The latter is a store, not a guest register
write: editing R55 does not start a program, R50 does not reset the VDP, and R57 does
not pretend one typed byte completed a two-write unlock handshake. It does refresh the
derived mode, palette and interrupt state needed to keep the instance coherent.

`pico9918_reg_value()` is different: it is the guest's decoded view. On a locked device,
asking it for R30 reads the R6 alias. That is useful in a bus trace and wrong in a
64-register editor.

Use `pico9918_status_value()` to show any status register without clearing it.
`pico9918_debug_vram_address()` gives the effective next guest address, including the
4K DRAM address permutation. Neither belongs on the guest bus.

### A GPU pane and disassembler

The read side needed by a TMS9900 view is public even without the rest of the debug API:

- `pico9918_gpu_pc()` -- the next instruction address;
- `pico9918_gpu_reg_value()` -- R0-R15 through the fixed workspace;
- `pico9918_gpu_status()` -- ST in architectural bit positions;
- `pico9918_gpu_mem_value()` and `_mem_size()` -- the backing map.

With the debug API, use the span read for disassembly and
`pico9918_debug_gpu_set_pc()` to move the PC without arming or disarming the program.
`pico9918_debug_gpu_armed()` says whether a trigger is waiting to run.

For single stepping, the debugger must own GPU pacing: leave the automatic clock at
zero and do not run the dedicated loop, then call `pico9918_gpu_step_n(vdp, 1)` while an
armed program is stopped in your scheduler. The portable C core honours that one
instruction budget. The hand-written Pico cores run to completion and do not expose an
instruction boundary, but those are not used by a normal desktop build.

There is no breakpoint callback hidden in the API. Breakpoints belong in the host
scheduler: budget the GPU, inspect its PC between slices, and stop calling it when the
address matches. `pico9918_debug_gpu_set_pc()` deliberately does not arm a stopped GPU;
starting one is device behaviour and should go through the guest register path if a
debugger explicitly asks to perform it.

Run debugger reads and writes on the emulation thread, or stop that thread first. The
calls are non-invasive to the emulated machine, not magically atomic against a renderer
or GPU running on another host thread.

### Save states are not a debugger span

There is no stable save-state API yet. The debug map covers memory, registers, status
and the GPU workspace, but not every piece of the machine: the half-written address
command, data read-ahead, unlock count, palette write stage, frame counters, GPU armed
state and renderer caches all live outside that map.

Do not `memcpy` the opaque instance and call it a format. Its layout is private, changes
with build options and may change between releases. An emulator can offer cold boots
and persistent PICO9918 settings entirely through the public API today; a durable,
resume-in-the-middle save state needs an explicit snapshot surface added to the library
first. Reaching into `impl/` makes the state file a contract with one exact source
revision, which is sometimes useful for an experiment and not something to ship as a
portable format.

## Threading and more than one VDP

Bus, VRAM, registers, status, callbacks and configuration are per instance in the
normal build. Rendering still has some shared scratch and cache state:

- never render two instances concurrently;
- alternating instances on one render thread is allowed, but a palette or mode change
  between them can leave one stale scanline because three renderer caches are shared;
- do not let bus/frame calls race unless the host supplies the critical section the
  platform contract describes;
- register the four callbacks separately on each instance--the callback receives both
  the instance and its `userdata` so one function can serve all of them.

For the usual one-VDP machine, keep bus execution, frame advancement and debugger work
on the emulation thread. A UI thread sends commands to it and receives copied frames or
debugger snapshots. That arrangement satisfies the contract without teaching the VDP
about the emulator's locks.

## What to test in the host

A useful integration test does not need a game ROM. The repository examples cover most
of the awkward edges in a form that is easy to lift into a host test:

- write and read two VRAM bytes through the ports; the first read after the address
  command must be the byte at that address;
- raise a frame interrupt, observe `/INT`, read SR0 and observe it drop;
- render a tile and compare both an indexed line and the converted BGR12 line;
- unlock an F18A, write R49 and prove an enhanced mode changes the picture;
- arm the tiny GPU program from `gpu_program.c` and prove it ran before the guest reads
  its answer;
- if chip selection is exposed, prove a TMS9918 refuses Graphics II, a real F18A cannot
  reach R58/R59, and a PICO9918 can;
- if a debugger is exposed, read status twice without clearing it and prove a register
  edit does not trigger the device behaviour attached to that register.

That set catches almost every integration mistake this API has managed to make easy.
The renderer's own goldens and scene suite then cover the pixels behind it.
