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
 * Additive linker script fragment for the copy_to_ram binary type.
 *
 * The SDK's memmap_copy_to_ram.ld copies every .text* input section to RAM,
 * with no equivalent to __in_flash() for code. This fragment adds a
 * .flashcode output section so functions marked with __in_flash_func()
 * (see xip.h) stay resident in flash and execute via XIP instead.
 *
 * INSERT AFTER .rodata places this section in the FLASH region, immediately
 * ahead of the .text output section - input sections bind to the first
 * output section that matches, so .flashcode must precede .text to claim its
 * inputs before the catch-all *(.text*) rule does. SUBALIGN(8) keeps each
 * XIP function 8-byte aligned.
 *
 * No explicit "> FLASH" here: this fragment is parsed before the SDK script
 * that declares the FLASH memory region, so naming it would only produce an
 * "undeclared region" warning. The section instead inherits the location
 * counter from .rodata (already in flash), and the SDK script's own
 * "> FLASH" sections continue on from it.
 */

SECTIONS
{
    .flashcode : SUBALIGN(8) {
        . = ALIGN(8);
        __flashcode_start__ = .;
        KEEP (*(.flashcode .flashcode.*))
        . = ALIGN(8);
        __flashcode_end__ = .;
    }
}
INSERT AFTER .rodata;
