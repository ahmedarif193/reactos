/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Observe x18 after restoring a migrated context
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .global RtlArm64ContextRestore
    .seh_proc RtlArm64ContextRestore
RtlArm64ContextRestore:
    stp     x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    stp     x19, x20, [sp, #16]
    .seh_save_regp x19, 16
    sub     sp, sp, #CONTEXT_FRAME_LENGTH
    .seh_stackalloc CONTEXT_FRAME_LENGTH
    .seh_endprologue

    mov     x19, x0
    mov     x20, x1
    mov     x0, sp
    bl      RtlCaptureContext
    ldr     x8, [sp, #CxX18]
    str     x8, [x19]
    adr     x8, RtlArm64ContextResumed
    str     x8, [sp, #CxPc]
    mov     x0, x20
    bl      KeSetSystemAffinityThreadEx
    str     x18, [x19, #8]
    mov     x0, sp
    mov     x1, xzr
    bl      RtlRestoreContext
    brk     #0xf001

RtlArm64ContextResumed:
    /* Observe before any C code can dereference a stale PCR. Repair it so
     * the test reports an assertion failure on a broken kernel safely. */
    str     x18, [x19, #16]
    mrs     x18, tpidr_el1
    str     x18, [x19, #24]
    add     sp, sp, #CONTEXT_FRAME_LENGTH
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret
    .seh_endproc

    .global RtlArm64ContextReadAndRepair
    .seh_proc RtlArm64ContextReadAndRepair
RtlArm64ContextReadAndRepair:
    .seh_endprologue
    str     x18, [x0, #16]
    mrs     x18, tpidr_el1
    str     x18, [x0, #24]
    ret
    .seh_endproc

    .global RtlArm64ContextReadPcr
    .seh_proc RtlArm64ContextReadPcr
RtlArm64ContextReadPcr:
    .seh_endprologue
    mov     x0, x18
    ret
    .seh_endproc
