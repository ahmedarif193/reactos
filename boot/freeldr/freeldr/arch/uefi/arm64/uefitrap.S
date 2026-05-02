/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 UEFI exception/interrupt stubs and vector helpers
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

.extern Arm64HandleException

.text

/*
 * ARM64 exception vector table must be aligned to 2KB (0x800)
 * Each entry is 128 bytes (32 instructions) maximum
 */
.align 11
.global Arm64VectorTable
Arm64VectorTable:
    /* Current EL with SP0 - 0x000 */
    .align 7
    b   Arm64SyncSP0

    .align 7
    b   Arm64IrqSP0

    .align 7
    b   Arm64FiqSP0

    .align 7
    b   Arm64ErrorSP0

    /* Current EL with SPx - 0x200 */
    .align 7
    b   Arm64SyncSPx

    .align 7
    b   Arm64IrqSPx

    .align 7
    b   Arm64FiqSPx

    .align 7
    b   Arm64ErrorSPx

    /* Lower EL using AArch64 - 0x400 */
    .align 7
    b   Arm64SyncLower64

    .align 7
    b   Arm64IrqLower64

    .align 7
    b   Arm64FiqLower64

    .align 7
    b   Arm64ErrorLower64

    /* Lower EL using AArch32 - 0x600 */
    .align 7
    b   Arm64SyncLower32

    .align 7
    b   Arm64IrqLower32

    .align 7
    b   Arm64FiqLower32

    .align 7
    b   Arm64ErrorLower32

/*
 * Common exception entry macro
 * Saves all registers and calls C handler
 */
.set CTX_FIELDS, 40
.set CTX_SIZE, (CTX_FIELDS * 8)

.macro EXCEPTION_ENTRY
    /* Allocate trap context space */
    sub sp, sp, #CTX_SIZE

    /* Save general purpose registers X0-X29 */
    stp x0,  x1,  [sp, #(0 * 16)]
    stp x2,  x3,  [sp, #(1 * 16)]
    stp x4,  x5,  [sp, #(2 * 16)]
    stp x6,  x7,  [sp, #(3 * 16)]
    stp x8,  x9,  [sp, #(4 * 16)]
    stp x10, x11, [sp, #(5 * 16)]
    stp x12, x13, [sp, #(6 * 16)]
    stp x14, x15, [sp, #(7 * 16)]
    stp x16, x17, [sp, #(8 * 16)]
    stp x18, x19, [sp, #(9 * 16)]
    stp x20, x21, [sp, #(10 * 16)]
    stp x22, x23, [sp, #(11 * 16)]
    stp x24, x25, [sp, #(12 * 16)]
    stp x26, x27, [sp, #(13 * 16)]
    stp x28, x29, [sp, #(14 * 16)]

    /* Save LR (x30) */
    str x30, [sp, #(30 * 8)]

    /* Save current stack pointer (before allocation) */
    add x1, sp, #CTX_SIZE
    str x1, [sp, #(31 * 8)]

    /* Save special registers after GPRs */
    mrs x0, SP_EL0
    str x0, [sp, #(32 * 8)]

    mrs x0, ELR_EL1
    str x0, [sp, #(33 * 8)]

    mrs x0, SPSR_EL1
    str x0, [sp, #(34 * 8)]

    mrs x0, ESR_EL1
    str x0, [sp, #(35 * 8)]

    mrs x0, FAR_EL1
    str x0, [sp, #(36 * 8)]

    /* Pass trap frame pointer to handler */
    mov x0, sp
    bl  Arm64HandleException

    /* Restore stack pointer before halting */
    add sp, sp, #CTX_SIZE

    /* We don't return from exceptions in bootloader - halt */
1:
    wfe
    b   1b
.endm

/*
 * Exception handlers
 */
Arm64SyncSP0:
    EXCEPTION_ENTRY

Arm64IrqSP0:
    EXCEPTION_ENTRY

Arm64FiqSP0:
    EXCEPTION_ENTRY

Arm64ErrorSP0:
    EXCEPTION_ENTRY

Arm64SyncSPx:
    EXCEPTION_ENTRY

Arm64IrqSPx:
    EXCEPTION_ENTRY

Arm64FiqSPx:
    EXCEPTION_ENTRY

Arm64ErrorSPx:
    EXCEPTION_ENTRY

Arm64SyncLower64:
    EXCEPTION_ENTRY

Arm64IrqLower64:
    EXCEPTION_ENTRY

Arm64FiqLower64:
    EXCEPTION_ENTRY

Arm64ErrorLower64:
    EXCEPTION_ENTRY

Arm64SyncLower32:
    EXCEPTION_ENTRY

Arm64IrqLower32:
    EXCEPTION_ENTRY

Arm64FiqLower32:
    EXCEPTION_ENTRY

Arm64ErrorLower32:
    EXCEPTION_ENTRY

/*
 * Set exception vector table base address
 */
.global Arm64SetVbar
Arm64SetVbar:
    msr VBAR_EL1, x0
    isb
    ret

/*
 * Set VBAR for the current exception level (EL1/EL2/EL3)
 */
.global Arm64SetVbarAuto
Arm64SetVbarAuto:
    mrs x1, CurrentEL
    lsr x1, x1, #2
    cmp x1, #3
    b.eq 1f
    cmp x1, #2
    b.eq 2f
    /* Default to EL1 */
    msr VBAR_EL1, x0
    isb
    ret
1:
    msr VBAR_EL3, x0
    isb
    ret
2:
    msr VBAR_EL2, x0
    isb
    ret

/*
 * Read current exception level
 */
.global Arm64GetCurrentEL
Arm64GetCurrentEL:
    mrs x0, CurrentEL
    lsr x0, x0, #2
    and x0, x0, #3
    ret

/* EOF */
