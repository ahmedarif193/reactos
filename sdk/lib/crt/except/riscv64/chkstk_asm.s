/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/*
 * ReactOS RISC-V64 stack-probe boundary.
 *
 * There is no selected Windows RISC-V stack-probe calling convention yet.
 * Stop if either compatibility symbol is reached instead of pretending that
 * guard pages were touched. See docs/riscv64/stack-probing.md.
 */

    .text
    .p2align 2

    .globl __chkstk
__chkstk:
    .word 0
    j __chkstk

    .p2align 2
    .globl __alloca_probe
__alloca_probe:
    .word 0
    j __alloca_probe
