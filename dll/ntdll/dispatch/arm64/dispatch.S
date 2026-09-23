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

    /*
     * The kernel enters with sp pointing at a KUSER_EXCEPTION_STACK whose
     * first member is the interrupted CONTEXT. Describe that synthetic frame
     * explicitly so a second-pass unwind resumes from the interrupted state.
     */
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
