#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .extern LdrpInit
    .extern KiUserExceptionDispatcherWorker
    .extern KiUserEmulationDispatcherWorker
    .extern NtContinue
    .extern RtlExitUserThread
    .extern RtlRaiseStatus
    .extern ChpeRtlUserThreadStart
    .extern ChpeInvokeUserApcRoutine

    .global ChpeCallArm64Routine
    .seh_proc ChpeCallArm64Routine
ChpeCallArm64Routine:
    stp     x29, x30, [sp, #-48]!
    .seh_save_fplr_x 48
    stp     x23, x24, [sp, #16]
    .seh_save_regp x23, 16
    str     x28, [sp, #32]
    .seh_save_reg x28, 32
    mov     x29, sp
    .seh_set_fp
    .seh_endprologue
    mov     x16, x0
    mov     x0, x1
    mov     x1, x2
    mov     x2, x3
    mov     x3, x4
    mov     x4, x5
    blr     x16
    .seh_startepilogue
    ldr     x28, [sp, #32]
    .seh_save_reg x28, 32
    ldp     x23, x24, [sp, #16]
    .seh_save_regp x23, 16
    ldp     x29, x30, [sp], #48
    .seh_save_fplr_x 48
    .seh_endepilogue
    ret
    .seh_endproc

    /*
     * The kernel enters with sp pointing at a KUSER_EXCEPTION_STACK whose
     * first member is the interrupted CONTEXT. Describe that synthetic frame
     * explicitly so a second-pass unwind resumes from the interrupted state.
     */
    .global RtlpArm64ExecuteHandler
    .seh_proc RtlpArm64ExecuteHandler
RtlpArm64ExecuteHandler:
    stp     x29, x30, [sp, #-96]!
    .seh_save_fplr_x 96
    stp     x19, x20, [sp, #16]
    .seh_save_regp x19, 16
    stp     x21, x22, [sp, #32]
    .seh_save_regp x21, 32
    stp     x23, x24, [sp, #48]
    .seh_save_regp x23, 48
    stp     x25, x26, [sp, #64]
    .seh_save_regp x25, 64
    stp     x27, x28, [sp, #80]
    .seh_save_regp x27, 80
    .seh_endprologue
    ldp     x19, x20, [x3, #0]
    ldp     x21, x22, [x3, #16]
    ldp     x23, x24, [x3, #32]
    ldp     x25, x26, [x3, #48]
    ldp     x27, x28, [x3, #64]
    ldr     x1, [x3, #80]
    blr     x2
    ldp     x19, x20, [sp, #16]
    ldp     x21, x22, [sp, #32]
    ldp     x23, x24, [sp, #48]
    ldp     x25, x26, [sp, #64]
    ldp     x27, x28, [sp, #80]
    ldp     x29, x30, [sp], #96
    ret
    .seh_endproc

    .global KiUserExceptionDispatcher
    .seh_proc KiUserExceptionDispatcher
KiUserExceptionDispatcher:
    .seh_context
    .seh_endprologue
    /* The emulator can re-enter using only the synthetic stack frame. */
    mov     x1, sp
    add     x0, sp, #UesExceptionRecord
    bl      KiUserExceptionDispatcherWorker
    brk     #1
    .seh_endproc

    .global KiUserEmulationDispatcher
    .seh_proc KiUserEmulationDispatcher
KiUserEmulationDispatcher:
    .seh_context
    .seh_endprologue
    mov     x0, sp
    bl      KiUserEmulationDispatcherWorker
    brk     #1
    .seh_endproc

    .global LdrInitializeThunk
    .seh_proc LdrInitializeThunk
LdrInitializeThunk:
    .seh_endprologue
    /*
     * KiUserApcDispatcher calls the normal routine with:
     *   x0 = NormalContext
     *   x1 = SystemArgument1
     *   x2 = SystemArgument2
     *   x3 = saved CONTEXT
     *
     * LdrpInit expects:
     *   x0 = saved CONTEXT
     *   x1 = SystemArgument1
     *   x2 = SystemArgument2
     */
    mov     x0, x3
    b       LdrpInit
    .seh_endproc

    .global KiUserApcDispatcher
    .seh_proc KiUserApcDispatcher
KiUserApcDispatcher:
    .seh_context
    nop
    .seh_stackalloc 0x30
    .seh_endprologue
    ldp     x4, x0, [sp]
    ldp     x1, x2, [sp, #0x10]
    add     x3, sp, #0x30
    bl      ChpeInvokeUserApcRoutine

    add     x0, sp, #0x30
    ldr     w1, [sp, #0x20]
    bl      NtContinue

    mov     w19, w0
1:
    mov     w0, w19
    bl      RtlRaiseStatus
    b       1b
    .seh_endproc

    .global RtlUserThreadStart
    .seh_proc RtlUserThreadStart
RtlUserThreadStart:
    .seh_endprologue
    b       ChpeRtlUserThreadStart
    brk     #0
    .seh_endproc
