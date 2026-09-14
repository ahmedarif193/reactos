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
    .seh_endprologue
    /*
     * The kernel returns here with sp pointing at a UAPC_FRAME. The CONTEXT is
     * the first field, so the incoming sp is also the NtContinue context.
     */
    mov     x19, sp
    mov     x4, x3
    mov     x3, x19
    bl      ChpeInvokeUserApcRoutine

    mov     x0, x19
    mov     w1, #1
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
