/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 nested exception handler thunk
 */

#include <asm.h>

    .text
    .align 4

/*
 * EXCEPTION_DISPOSITION
 * NTAPI
 * RtlpExecuteHandlerForException(
 *     PEXCEPTION_RECORD ExceptionRecord,     // x0
 *     PVOID EstablisherFrame,                // x1
 *     PCONTEXT ContextRecord,                // x2
 *     PDISPATCHER_CONTEXT DispatcherContext, // x3
 *     PEXCEPTION_ROUTINE ExceptionRoutine);  // x4
 *
 * Runs a language handler inside a frame that is itself guarded by
 * RtlpExceptionHandler. An exception raised by the language handler therefore
 * unwinds through this frame, which is how the new dispatch learns that it is
 * nested inside an active one.
 */
    .global RtlpExecuteHandlerForException
    .seh_proc RtlpExecuteHandlerForException
RtlpExecuteHandlerForException:
    .seh_handler RtlpExceptionHandler, @except
    stp     x29, x30, [sp, #-0x20]!
    .seh_save_fplr_x 0x20
    mov     x29, sp
    .seh_set_fp
    .seh_endprologue

    /* RtlpExceptionHandler reads this back at EstablisherFrame - 0x10 */
    str     x3, [x29, #0x10]

    blr     x4

    .seh_startepilogue
    ldp     x29, x30, [sp], #0x20
    .seh_save_fplr_x 0x20
    .seh_endepilogue
    ret
    .seh_endproc

/*
 * EXCEPTION_DISPOSITION
 * NTAPI
 * RtlpExecuteHandlerForUnwind(
 *     PEXCEPTION_RECORD ExceptionRecord,     // x0
 *     PVOID EstablisherFrame,                // x1
 *     PCONTEXT ContextRecord,                // x2
 *     PDISPATCHER_CONTEXT DispatcherContext, // x3
 *     PEXCEPTION_ROUTINE ExceptionRoutine);  // x4
 *
 * Same thunk for termination handlers, guarded by RtlpUnwindHandler so that a
 * second unwind or an exception walking through this frame is reported as a
 * collided unwind instead of running the handler a second time.
 */
    .global RtlpExecuteHandlerForUnwind
    .seh_proc RtlpExecuteHandlerForUnwind
RtlpExecuteHandlerForUnwind:
    .seh_handler RtlpUnwindHandler, @except
    stp     x29, x30, [sp, #-0x20]!
    .seh_save_fplr_x 0x20
    mov     x29, sp
    .seh_set_fp
    .seh_endprologue

    /* RtlpUnwindHandler reads this back at EstablisherFrame - 0x10 */
    str     x3, [x29, #0x10]

    blr     x4

    .seh_startepilogue
    ldp     x29, x30, [sp], #0x20
    .seh_save_fplr_x 0x20
    .seh_endepilogue
    ret
    .seh_endproc
