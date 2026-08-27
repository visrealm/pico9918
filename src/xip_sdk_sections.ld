/*
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 */

/*
 * Additive linker script fragment: redirects specific, individually-vetted
 * Pico SDK library functions into flash (XIP) instead of RAM. These are
 * vendored SDK source we don't edit, so the __in_flash_func() attribute in
 * xip.h isn't an option - instead this relies on -ffunction-sections
 * already being enabled for the build (confirmed via objdump: each
 * function compiles to its own .text.<name> section), letting these exact
 * names be redirected here without touching the installed SDK.
 *
 * Unlike xip_sections.ld, there's no macro gating these (the SDK source
 * has no knowledge of PICO9918_COLD_IN_FLASH), so this whole file is only
 * linked in when the option is on - see src/CMakeLists.txt. SUBALIGN(8)
 * keeps each redirected XIP function 8-byte aligned.
 *
 * Each entry must satisfy the same rule as everything in xip.h: finishes
 * running strictly before pico9918_gpu_loop() starts, and is never reached again.
 *
 *   set_sys_clock_pll / clock_configure_undivided / pll_init: only called
 *   from main.c's cold clock bring-up (set_sys_clock_pll's two call sites,
 *   both in main()) or the SDK's own pre-main() runtime_init_clocks() -
 *   never from the clocks resus IRQ handler, which is dead code here (we
 *   never call clocks_enable_resus()).
 *
 *   runtime_init_clocks: the SDK's own pre-main() hardware init hook (run
 *   once from crt0, via the runtime init table - never called again). Runs
 *   no earlier than main()'s own cold code already does, so it's executing
 *   via XIP under exactly the same conditions we've already proven work.
 *
 *   pio_sm_init / pio_add_program / pio_sm_set_consecutive_pindirs: every
 *   call site (initClock/tmsPioInit in main.c, vgaInitSync/vgaInitRgb in
 *   vga.c) is already cold-marked.
 *
 *   multicore_launch_core1_raw: only called from multicore_launch_core1(),
 *   itself only called once from main().
 */

SECTIONS
{
    .flashcode_sdk : SUBALIGN(8) {
        . = ALIGN(8);
        *(.text.set_sys_clock_pll)
        *(.text.clock_configure_undivided)
        *(.text.pll_init)
        *(.text.runtime_init_clocks)
        *(.text.pio_sm_init)
        *(.text.pio_add_program)
        *(.text.pio_sm_set_consecutive_pindirs)
        *(.text.multicore_launch_core1_raw)
        . = ALIGN(8);
    }
}
INSERT AFTER .rodata;
