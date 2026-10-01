/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC CONTEXT and KTRAP_FRAME conversion
 */

#include <ntoskrnl.h>

static
VOID
KiPpcCopyContext(
    _Inout_ PCONTEXT Destination,
    _In_ const CONTEXT *Source,
    _In_ ULONG ContextFlags)
{
    if (ContextFlags & CONTEXT_CONTROL)
    {
        Destination->Msr = Source->Msr;
        Destination->Iar = Source->Iar;
        Destination->Lr = Source->Lr;
        Destination->Gpr1 = Source->Gpr1;
    }
    if (ContextFlags & CONTEXT_INTEGER)
    {
        ULONG Gpr1 = Destination->Gpr1;

        RtlCopyMemory(&Destination->Gpr0, &Source->Gpr0, 32 * sizeof(ULONG));
        Destination->Gpr1 = Gpr1;
        Destination->Cr = Source->Cr;
        Destination->Xer = Source->Xer;
        Destination->Ctr = Source->Ctr;
    }
    if (ContextFlags & CONTEXT_FLOATING_POINT)
    {
        RtlCopyMemory(&Destination->Fpr0, &Source->Fpr0, 32 * sizeof(double));
        Destination->Fpscr = Source->Fpscr;
    }
}

VOID
NTAPI
KeContextToTrapFrame(
    _In_ PCONTEXT Context,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    BOOLEAN Interrupts;
    ULONG Msr;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    ContextFlags &= CONTEXT_ALL;

    Interrupts = KeDisableInterrupts();
    Msr = TrapFrame->Context.Msr;
    KiPpcCopyContext(&TrapFrame->Context, Context, ContextFlags);

    /* A CONTEXT never supplies privileged MSR state. A user frame keeps the
     * problem-state MSR and may only change the trace bits. */
    if ((PreviousMode == UserMode) || (Msr & MSR_PR))
        TrapFrame->Context.Msr = PPC_USER_MSR | (TrapFrame->Context.Msr & PPC_USER_MSR_MASK);
    else
        TrapFrame->Context.Msr = Msr;

    TrapFrame->Context.ContextFlags |= ContextFlags;
    KeRestoreInterrupts(Interrupts);
}

VOID
NTAPI
KeTrapFrameToContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PCONTEXT Context)
{
    BOOLEAN Interrupts;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    Interrupts = KeDisableInterrupts();
    KiPpcCopyContext(Context, &TrapFrame->Context, Context->ContextFlags & CONTEXT_ALL);
    KeRestoreInterrupts(Interrupts);
}
