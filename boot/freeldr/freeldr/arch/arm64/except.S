/*
 * PROJECT:     FreeLoader for ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Exception Handling
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* ARM64 Exception Handling - GNU AS syntax */
.text

/* Exception vector table aligned to 2KB */
.align 11
.global VectorTable
/* .type VectorTable, %function */
VectorTable:
    /* Current EL with SP0 */
    .align 7
    b SynchronousExceptionSP0
    .align 7
    b IRQExceptionSP0
    .align 7
    b FIQExceptionSP0
    .align 7
    b SErrorExceptionSP0

    /* Current EL with SPx */
    .align 7
    b SynchronousExceptionSPx
    .align 7
    b IRQExceptionSPx
    .align 7
    b FIQExceptionSPx
    .align 7
    b SErrorExceptionSPx

    /* Lower EL using AArch64 */
    .align 7
    b SynchronousExceptionLowerEL
    .align 7
    b IRQExceptionLowerEL
    .align 7
    b FIQExceptionLowerEL
    .align 7
    b SErrorExceptionLowerEL

    /* Lower EL using AArch32 */
    .align 7
    b SynchronousExceptionLowerEL32
    .align 7
    b IRQExceptionLowerEL32
    .align 7
    b FIQExceptionLowerEL32
    .align 7
    b SErrorExceptionLowerEL32

/* Exception handlers - minimal implementation */
SynchronousExceptionSP0:
SynchronousExceptionSPx:
SynchronousExceptionLowerEL:
SynchronousExceptionLowerEL32:
    /* Save context */
    stp x29, x30, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x17, x18, [sp, #-16]!
    stp x15, x16, [sp, #-16]!
    stp x13, x14, [sp, #-16]!
    stp x11, x12, [sp, #-16]!
    stp x9, x10, [sp, #-16]!
    stp x7, x8, [sp, #-16]!
    stp x5, x6, [sp, #-16]!
    stp x3, x4, [sp, #-16]!
    stp x1, x2, [sp, #-16]!
    stp xzr, x0, [sp, #-16]!
    
    /* Call exception handler */
    bl HandleException
    
    /* Restore context */
    ldp xzr, x0, [sp], #16
    ldp x1, x2, [sp], #16
    ldp x3, x4, [sp], #16
    ldp x5, x6, [sp], #16
    ldp x7, x8, [sp], #16
    ldp x9, x10, [sp], #16
    ldp x11, x12, [sp], #16
    ldp x13, x14, [sp], #16
    ldp x15, x16, [sp], #16
    ldp x17, x18, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x29, x30, [sp], #16
    
    eret

IRQExceptionSP0:
IRQExceptionSPx:
IRQExceptionLowerEL:
IRQExceptionLowerEL32:
FIQExceptionSP0:
FIQExceptionSPx:
FIQExceptionLowerEL:
FIQExceptionLowerEL32:
SErrorExceptionSP0:
SErrorExceptionSPx:
SErrorExceptionLowerEL:
SErrorExceptionLowerEL32:
    /* Hang on unhandled exceptions */
1:  b 1b
