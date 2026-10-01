/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Stage0 layout: a flat image run in place at the firmware load address
 */

ENTRY(_start)
SECTIONS
{
    . = 0x01000000;
    __stage0_start = .;
    .text : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) *(.sdata*) *(.got*) }
    __bss_start = .;
    .bss (NOLOAD) : { *(.sbss*) *(.bss*) *(COMMON) }
    . = ALIGN(4096);
    __stage0_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
