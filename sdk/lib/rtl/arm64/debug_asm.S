/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 debug service entry points
 */

#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .global RtlCaptureContext
    .seh_proc RtlCaptureContext
RtlCaptureContext:
    .seh_endprologue

    /*
     * Capture the caller-visible register state without calling back into C.
     * x0 is both the incoming argument and the value that belongs in Context.X0.
     * Save x9-x11 before using them as scratch so the context keeps the true
     * volatile register values seen by this routine's caller.
     */
    str     x9,  [x0, #CxX9]
    str     x10, [x0, #CxX10]
    str     x11, [x0, #CxX11]

    mov     x9, x0

    stp     x0,  x1,  [x9, #CxX0]
    stp     x2,  x3,  [x9, #CxX2]
    stp     x4,  x5,  [x9, #CxX4]
    stp     x6,  x7,  [x9, #CxX6]
    str     x8,        [x9, #CxX8]
    stp     x12, x13, [x9, #CxX12]
    stp     x14, x15, [x9, #CxX14]
    stp     x16, x17, [x9, #CxX16]
    stp     x18, x19, [x9, #CxX18]
    stp     x20, x21, [x9, #CxX20]
    stp     x22, x23, [x9, #CxX22]
    stp     x24, x25, [x9, #CxX24]
    stp     x26, x27, [x9, #CxX26]
    stp     x28, x29, [x9, #CxX28]
    str     x30,       [x9, #CxLr]

    mov     x10, sp
    str     x10, [x9, #CxSp]
    str     x30, [x9, #CxPc]

    mov     w10, #0x7
    movk    w10, #0x40, lsl #16
    str     w10, [x9, #CxContextFlags]

    mrs     x10, nzcv
    str     w10, [x9, #CxCpsr]

    /*
     * Clear FP/vector and debug state so callers do not see stale stack data
     * in portions of CONTEXT that are not captured here.
     */
    add     x10, x9, #CxV
    mov     x11, #(CONTEXT_FRAME_LENGTH - CxV)
1:
    stp     xzr, xzr, [x10], #16
    subs    x11, x11, #16
    b.ne    1b

    mrs     x10, fpcr
    str     w10, [x9, #CxFpcr]
    mrs     x10, fpsr
    str     w10, [x9, #CxFpsr]

    ret
    .seh_endproc

    .global DbgUserBreakPoint
DbgUserBreakPoint:

    .global DbgBreakPoint
    .seh_proc DbgBreakPoint
DbgBreakPoint:
    .seh_endprologue
    brk     #0
    ret
    .seh_endproc

    .global RtlpBreakWithStatusInstruction
RtlpBreakWithStatusInstruction:

    .global DbgBreakPointWithStatus
    .seh_proc DbgBreakPointWithStatus
DbgBreakPointWithStatus:
    .seh_endprologue
    brk     #0
    ret
    .seh_endproc

    /*
     * ULONG NTAPI DebugService(
     *     ULONG Service, PVOID Argument1, PVOID Argument2,
     *     PVOID Argument3, PVOID Argument4);
     *
     * The kernel ARM64 BRK handler expects:
     *   x0 = service, x1-x4 = service arguments.
     */
    .global DebugService
    .seh_proc DebugService
DebugService:
    .seh_endprologue
    brk     #0xf003
    ret
    .seh_endproc

    /*
     * VOID NTAPI DebugService2(PVOID Argument1, PVOID Argument2, ULONG Service);
     */
    .global DebugService2
    .seh_proc DebugService2
DebugService2:
    .seh_endprologue
    mov     x3, x2
    mov     x2, x1
    mov     x1, x0
    mov     x0, x3
    brk     #0xf003
    ret
    .seh_endproc
