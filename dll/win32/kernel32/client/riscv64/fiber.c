/*
 * PROJECT:     ReactOS System Libraries
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 fiber context switch
 *
 * RISC-V has no published Windows fiber ABI. The port therefore uses the
 * private CONTEXT layout already used by RtlCaptureContext/RtlRestoreContext
 * and keeps the TEB-owned state alongside the register image. The native
 * context includes the RV64D FPR bank and FCSR. The switch follows the
 * ARM64 C-provider pattern with RISC-V register handling in RTL.
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

    BaseThreadStartup((LPTHREAD_START_ROUTINE)(ULONG_PTR)Fiber->FiberContext.A0,
                      (LPVOID)(ULONG_PTR)Fiber->FiberContext.A1);
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

    /* tp identifies the thread, not the fiber. Initial fiber contexts do
     * not have a TEB until they are first switched to on a thread. */
    NewFiber->FiberContext.Tp = (ULONG_PTR)Teb;
    RtlRestoreContext(&NewFiber->FiberContext, NULL);
}
