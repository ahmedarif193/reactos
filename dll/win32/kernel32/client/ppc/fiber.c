/*
 * PROJECT:     LiberNT System Libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC fiber context switch
 *
 * The fiber's register image is a CONTEXT captured by RtlCaptureContext and
 * resumed by RtlRestoreContext; the TEB-owned state is kept alongside it.
 */

#include <k32.h>

NTSYSAPI
DECLSPEC_NORETURN
VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _EXCEPTION_RECORD *ExceptionRecord);

DECLSPEC_NORETURN
VOID
WINAPI
BaseFiberStartup(VOID)
{
    PFIBER Fiber = (PFIBER)NtCurrentTeb()->NtTib.FiberData;

    BaseThreadStartup((LPTHREAD_START_ROUTINE)(ULONG_PTR)Fiber->FiberContext.Gpr3,
                      (LPVOID)(ULONG_PTR)Fiber->FiberContext.Gpr4);
}

VOID
WINAPI
SwitchToFiber(_In_ LPVOID Fiber)
{
    PTEB Teb = NtCurrentTeb();
    PFIBER CurrentFiber = (PFIBER)Teb->NtTib.FiberData;
    PFIBER NewFiber = (PFIBER)Fiber;
    volatile BOOLEAN Switched = FALSE;

    CurrentFiber->FlsData = Teb->FlsData;
    CurrentFiber->ActivationContextStackPointer = Teb->ActivationContextStackPointer;
    CurrentFiber->ExceptionList = Teb->NtTib.ExceptionList;
    CurrentFiber->StackBase = Teb->NtTib.StackBase;
    CurrentFiber->StackLimit = Teb->NtTib.StackLimit;
    CurrentFiber->DeallocationStack = Teb->DeallocationStack;
    CurrentFiber->GuaranteedStackBytes = Teb->GuaranteedStackBytes;

    /* RtlCaptureContext resumes after this call on the saved fiber stack. */
    CurrentFiber->FiberContext.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&CurrentFiber->FiberContext);
    if (Switched)
        return;
    Switched = TRUE;

    Teb->NtTib.FiberData = NewFiber;
    Teb->NtTib.ExceptionList = NewFiber->ExceptionList;
    Teb->NtTib.StackBase = NewFiber->StackBase;
    Teb->NtTib.StackLimit = NewFiber->StackLimit;
    Teb->DeallocationStack = NewFiber->DeallocationStack;
    Teb->GuaranteedStackBytes = NewFiber->GuaranteedStackBytes;
    Teb->ActivationContextStackPointer = NewFiber->ActivationContextStackPointer;
    Teb->FlsData = NewFiber->FlsData;

    /* r13 identifies the thread, not the fiber. Initial fiber contexts do
     * not have a TEB until they are first switched to on a thread. */
    NewFiber->FiberContext.Gpr13 = (ULONG_PTR)Teb;
    RtlRestoreContext(&NewFiber->FiberContext, NULL);
}
