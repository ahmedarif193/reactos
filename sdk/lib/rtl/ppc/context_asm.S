/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC context capture, restore and handler calls
 */

#include <asm/ksppc.h>
#include <kxppc.h>

/* VOID RtlCaptureContext(PCONTEXT Context) - r3: Context.
 * The captured state is the state at the call: Iar is the return address. */
NESTED_ENTRY(RtlCaptureContext)
    stw     0, CxGpr0(3)
    stw     1, CxGpr0 + 4(3)
    stw     2, CxGpr0 + 8(3)
    stw     3, CxGpr0 + 12(3)
    stw     4, CxGpr0 + 16(3)
    stw     5, CxGpr0 + 20(3)
    stw     6, CxGpr0 + 24(3)
    stw     7, CxGpr0 + 28(3)
    stw     8, CxGpr0 + 32(3)
    stw     9, CxGpr0 + 36(3)
    stw     10, CxGpr0 + 40(3)
    stw     11, CxGpr0 + 44(3)
    stw     12, CxGpr0 + 48(3)
    stw     13, CxGpr0 + 52(3)
    stw     14, CxGpr0 + 56(3)
    stw     15, CxGpr0 + 60(3)
    stw     16, CxGpr0 + 64(3)
    stw     17, CxGpr0 + 68(3)
    stw     18, CxGpr0 + 72(3)
    stw     19, CxGpr0 + 76(3)
    stw     20, CxGpr0 + 80(3)
    stw     21, CxGpr0 + 84(3)
    stw     22, CxGpr0 + 88(3)
    stw     23, CxGpr0 + 92(3)
    stw     24, CxGpr0 + 96(3)
    stw     25, CxGpr0 + 100(3)
    stw     26, CxGpr0 + 104(3)
    stw     27, CxGpr0 + 108(3)
    stw     28, CxGpr0 + 112(3)
    stw     29, CxGpr0 + 116(3)
    stw     30, CxGpr0 + 120(3)
    stw     31, CxGpr0 + 124(3)
    stfd    0, CxFpr0 + 0(3)
    stfd    1, CxFpr0 + 8(3)
    stfd    2, CxFpr0 + 16(3)
    stfd    3, CxFpr0 + 24(3)
    stfd    4, CxFpr0 + 32(3)
    stfd    5, CxFpr0 + 40(3)
    stfd    6, CxFpr0 + 48(3)
    stfd    7, CxFpr0 + 56(3)
    stfd    8, CxFpr0 + 64(3)
    stfd    9, CxFpr0 + 72(3)
    stfd    10, CxFpr0 + 80(3)
    stfd    11, CxFpr0 + 88(3)
    stfd    12, CxFpr0 + 96(3)
    stfd    13, CxFpr0 + 104(3)
    stfd    14, CxFpr0 + 112(3)
    stfd    15, CxFpr0 + 120(3)
    stfd    16, CxFpr0 + 128(3)
    stfd    17, CxFpr0 + 136(3)
    stfd    18, CxFpr0 + 144(3)
    stfd    19, CxFpr0 + 152(3)
    stfd    20, CxFpr0 + 160(3)
    stfd    21, CxFpr0 + 168(3)
    stfd    22, CxFpr0 + 176(3)
    stfd    23, CxFpr0 + 184(3)
    stfd    24, CxFpr0 + 192(3)
    stfd    25, CxFpr0 + 200(3)
    stfd    26, CxFpr0 + 208(3)
    stfd    27, CxFpr0 + 216(3)
    stfd    28, CxFpr0 + 224(3)
    stfd    29, CxFpr0 + 232(3)
    stfd    30, CxFpr0 + 240(3)
    stfd    31, CxFpr0 + 248(3)
    mffs    0
    stfd    0, CxFpscr(3)
    lfd     0, CxFpr0(3)
    mfcr    4
    stw     4, CxCr(3)
    mfxer   4
    stw     4, CxXer(3)
    mflr    4
    stw     4, CxLr(3)
    stw     4, CxIar(3)
    mfctr   4
    stw     4, CxCtr(3)
    li      4, 0
    stw     4, CxMsr(3)
    li      4, CONTEXT_FULL
    stw     4, CxContextFlags(3)
    lwz     4, CxGpr0 + 16(3)
    blr
NESTED_END(RtlCaptureContext)

/* VOID RtlpPpcRestoreContext(PCONTEXT Context) - r3: Context.
 * Continue at Context->Iar through CTR; CTR itself is not preserved. */
LEAF_ENTRY(RtlpPpcRestoreContext)
    lwz     4, CxContextFlags(3)
    andi.   4, 4, 2                     /* CONTEXT_FLOATING_POINT */
    beq     0, 1f
    lfd     0, CxFpscr(3)
    mtfsf   0xff, 0
    lfd     0, CxFpr0 + 0(3)
    lfd     1, CxFpr0 + 8(3)
    lfd     2, CxFpr0 + 16(3)
    lfd     3, CxFpr0 + 24(3)
    lfd     4, CxFpr0 + 32(3)
    lfd     5, CxFpr0 + 40(3)
    lfd     6, CxFpr0 + 48(3)
    lfd     7, CxFpr0 + 56(3)
    lfd     8, CxFpr0 + 64(3)
    lfd     9, CxFpr0 + 72(3)
    lfd     10, CxFpr0 + 80(3)
    lfd     11, CxFpr0 + 88(3)
    lfd     12, CxFpr0 + 96(3)
    lfd     13, CxFpr0 + 104(3)
    lfd     14, CxFpr0 + 112(3)
    lfd     15, CxFpr0 + 120(3)
    lfd     16, CxFpr0 + 128(3)
    lfd     17, CxFpr0 + 136(3)
    lfd     18, CxFpr0 + 144(3)
    lfd     19, CxFpr0 + 152(3)
    lfd     20, CxFpr0 + 160(3)
    lfd     21, CxFpr0 + 168(3)
    lfd     22, CxFpr0 + 176(3)
    lfd     23, CxFpr0 + 184(3)
    lfd     24, CxFpr0 + 192(3)
    lfd     25, CxFpr0 + 200(3)
    lfd     26, CxFpr0 + 208(3)
    lfd     27, CxFpr0 + 216(3)
    lfd     28, CxFpr0 + 224(3)
    lfd     29, CxFpr0 + 232(3)
    lfd     30, CxFpr0 + 240(3)
    lfd     31, CxFpr0 + 248(3)
1:
    lwz     4, CxCr(3)
    mtcrf   0xff, 4
    lwz     4, CxXer(3)
    mtxer   4
    lwz     4, CxLr(3)
    mtlr    4
    lwz     1, CxGpr0 + 4(3)
    lwz     2, CxGpr0 + 8(3)
    lwz     4, CxGpr0 + 16(3)
    lwz     5, CxGpr0 + 20(3)
    lwz     6, CxGpr0 + 24(3)
    lwz     7, CxGpr0 + 28(3)
    lwz     8, CxGpr0 + 32(3)
    lwz     9, CxGpr0 + 36(3)
    lwz     10, CxGpr0 + 40(3)
    lwz     11, CxGpr0 + 44(3)
    lwz     12, CxGpr0 + 48(3)
    lwz     13, CxGpr0 + 52(3)
    lwz     14, CxGpr0 + 56(3)
    lwz     15, CxGpr0 + 60(3)
    lwz     16, CxGpr0 + 64(3)
    lwz     17, CxGpr0 + 68(3)
    lwz     18, CxGpr0 + 72(3)
    lwz     19, CxGpr0 + 76(3)
    lwz     20, CxGpr0 + 80(3)
    lwz     21, CxGpr0 + 84(3)
    lwz     22, CxGpr0 + 88(3)
    lwz     23, CxGpr0 + 92(3)
    lwz     24, CxGpr0 + 96(3)
    lwz     25, CxGpr0 + 100(3)
    lwz     26, CxGpr0 + 104(3)
    lwz     27, CxGpr0 + 108(3)
    lwz     28, CxGpr0 + 112(3)
    lwz     29, CxGpr0 + 116(3)
    lwz     30, CxGpr0 + 120(3)
    lwz     31, CxGpr0 + 124(3)
    lwz     0, CxIar(3)
    mtctr   0
    lwz     0, CxGpr0(3)
    lwz     3, CxGpr3(3)
    bctr
LEAF_END(RtlpPpcRestoreContext)

/* Breakpoints: twi 31,0,0x16 is the Windows NT PowerPC breakpoint. */
LEAF_ENTRY(DbgBreakPoint)
    twi     31, 0, 0x16
    blr
LEAF_END(DbgBreakPoint)

LEAF_ENTRY(DbgUserBreakPoint)
    twi     31, 0, 0x16
    blr
LEAF_END(DbgUserBreakPoint)

LEAF_ENTRY(DbgBreakPointWithStatus)
    .globl  RtlpBreakWithStatusInstruction
RtlpBreakWithStatusInstruction:
    twi     31, 0, 0x16
    blr
LEAF_END(DbgBreakPointWithStatus)

/* Debug service: r3 selects the operation, r4-r7 carry its arguments. The
 * trap immediate 0x19 identifies a debug service request. */
LEAF_ENTRY(DebugService)
    twi     31, 0, 0x19
    blr
LEAF_END(DebugService)

/* DebugService2(Argument1, Argument2, Service): move Service to r3. */
LEAF_ENTRY(DebugService2)
    mr      6, 4
    mr      4, 3
    mr      3, 5
    mr      5, 6
    twi     31, 0, 0x19
    blr
LEAF_END(DebugService2)

/* EXCEPTION_DISPOSITION RtlpExecuteHandlerFor{Exception,Unwind}(
 *     Record r3, EstablisherFrame r4, Context r5, DispatcherContext r6,
 *     Handler r7 (a function descriptor)).
 * The wrapper's own frame is covered by a guard handler that detects nested
 * exceptions and collided unwinds. It finds the active DISPATCHER_CONTEXT at
 * its establisher frame (the entry SP) minus 8. */
    .text
    .p2align 2
    .globl ..RtlpExecuteHandlerForException
    .seh_proc ..RtlpExecuteHandlerForException
..RtlpExecuteHandlerForException:
    .seh_handler RtlpPpcExceptionHandler, @except
    mflr    0
    stwu    1, -32(1)
    stw     0, 40(1)
    stw     6, 24(1)
    .seh_endprologue
    lwz     0, 0(7)
    stw     2, 4(1)
    lwz     2, 4(7)
    mtctr   0
    bctrl
    lwz     2, 4(1)
    lwz     0, 40(1)
    addi    1, 1, 32
    mtlr    0
    blr
    .seh_endproc
LEAF_END(RtlpExecuteHandlerForException)

    .text
    .p2align 2
    .globl ..RtlpExecuteHandlerForUnwind
    .seh_proc ..RtlpExecuteHandlerForUnwind
..RtlpExecuteHandlerForUnwind:
    .seh_handler RtlpPpcUnwindHandler, @except, @unwind
    mflr    0
    stwu    1, -32(1)
    stw     0, 40(1)
    stw     6, 24(1)
    .seh_endprologue
    lwz     0, 0(7)
    stw     2, 4(1)
    lwz     2, 4(7)
    mtctr   0
    bctrl
    lwz     2, 4(1)
    lwz     0, 40(1)
    addi    1, 1, 32
    mtlr    0
    blr
    .seh_endproc
LEAF_END(RtlpExecuteHandlerForUnwind)

/* LONG RtlpPpcCallFunclet(Argument r3, EstablisherFrame r4, CodeAddress r5,
 *                         R2 r6).
 * Enter a filter or termination funclet at its code address with r3 = the
 * argument, r4 = the establisher frame and r2 = r6. The helper needs its own
 * function-table entry: exceptions leaving a funclet must unwind its frame. */
NESTED_ENTRY(RtlpPpcCallFunclet)
    .seh_proc ..RtlpPpcCallFunclet
    mflr    0
    stwu    1, -32(1)
    stw     0, 40(1)
    stw     2, 24(1)
    .seh_endprologue
    mtctr   5
    mr      2, 6
    bctrl
    lwz     2, 24(1)
    lwz     0, 40(1)
    addi    1, 1, 32
    mtlr    0
    blr
    .seh_endproc
NESTED_END(RtlpPpcCallFunclet)
