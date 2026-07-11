/*
 * PROJECT:     ReactOS System Libraries
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     ARM64 fiber context switch
 *
 * The other architectures implement the fiber context switch in hand-written
 * assembly (i386/fiber.S).  On ARM64 we build it on top of the already-present
 * RtlCaptureContext / RtlRestoreContext primitives so the whole switch is
 * expressed in terms of the public CONTEXT structure.
 *
 * IMPORTANT: on Windows ARM64 x18 holds the TEB pointer and is *per-thread*,
 * never per-fiber.  RtlRestoreContext() restores x18 from the CONTEXT, so before
 * resuming a fiber we overwrite its saved x18 with the current thread's TEB.
 * Without this a fiber resumed on any worker thread (or a freshly created fiber,
 * whose context has x18 == 0) would run with a stale/NULL TEB and silently
 * corrupt everything reached through the TEB - the process heap and TLS in
 * particular.
 */

#include <k32.h>

#define NDEBUG
#include <debug.h>

NTSYSAPI
DECLSPEC_NORETURN
VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _EXCEPTION_RECORD *ExceptionRecord);

/*
 * @implemented
 *
 * Entry point of a freshly scheduled fiber.  BaseInitializeContext() seeded the
 * fiber context with the start routine in X0 and its parameter in X1.
 */
DECLSPEC_NORETURN
VOID
WINAPI
BaseFiberStartup(VOID)
{
    PFIBER Fiber = (PFIBER)NtCurrentTeb()->NtTib.FiberData;

    BaseThreadStartup((LPTHREAD_START_ROUTINE)(ULONG_PTR)Fiber->FiberContext.X0,
                      (LPVOID)(ULONG_PTR)Fiber->FiberContext.X1);
}

/*
 * @implemented
 */
VOID
WINAPI
SwitchToFiber(_In_ LPVOID Fiber)
{
    PTEB Teb = NtCurrentTeb();
    PFIBER CurrentFiber = (PFIBER)Teb->NtTib.FiberData;
    PFIBER NewFiber = (PFIBER)Fiber;
    volatile BOOLEAN Switched = FALSE;

    /* Save the running fiber's TEB-derived state */
    CurrentFiber->FlsData = Teb->FlsData;
    CurrentFiber->ActivationContextStackPointer = Teb->ActivationContextStackPointer;
    CurrentFiber->ExceptionList = Teb->NtTib.ExceptionList;
    CurrentFiber->StackBase = Teb->NtTib.StackBase;
    CurrentFiber->StackLimit = Teb->NtTib.StackLimit;
    CurrentFiber->DeallocationStack = Teb->DeallocationStack;
    CurrentFiber->GuaranteedStackBytes = Teb->GuaranteedStackBytes;

    /* Capture the running fiber's registers/stack. When this fiber is later
     * scheduled again, RtlRestoreContext() returns execution right here - with
     * Switched already set to TRUE on our (preserved) stack frame. */
    CurrentFiber->FiberContext.ContextFlags = CONTEXT_FULL | CONTEXT_FLOATING_POINT;
    RtlCaptureContext(&CurrentFiber->FiberContext);

    if (Switched)
        return;
    Switched = TRUE;

    /* Make the target fiber current and load its TEB-derived state */
    Teb->NtTib.FiberData = NewFiber;
    Teb->NtTib.ExceptionList = NewFiber->ExceptionList;
    Teb->NtTib.StackBase = NewFiber->StackBase;
    Teb->NtTib.StackLimit = NewFiber->StackLimit;
    Teb->DeallocationStack = NewFiber->DeallocationStack;
    Teb->GuaranteedStackBytes = NewFiber->GuaranteedStackBytes;
    Teb->ActivationContextStackPointer = NewFiber->ActivationContextStackPointer;
    Teb->FlsData = NewFiber->FlsData;

    /* Keep the current thread's TEB pointer (x18) - see file header */
    NewFiber->FiberContext.X18 = (ULONG64)(ULONG_PTR)Teb;

    /* Switch into the target fiber; this does not return */
    RtlRestoreContext(&NewFiber->FiberContext, NULL);
}
