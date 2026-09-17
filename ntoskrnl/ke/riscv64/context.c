/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V base context and private trap-frame conversion
 */

#include <ntoskrnl.h>

static
VOID
KiRiscvCopyContext(
    _Inout_ PCONTEXT Destination,
    _In_ const CONTEXT *Source,
    _In_ ULONG ContextFlags)
{
    ULONG Index;

    ASSERT((ContextFlags & CONTEXT_RISCV64) != 0);
    ASSERT((ContextFlags & ~(CONTEXT_ALL | CONTEXT_UNWOUND_TO_CALL)) == 0);
    ContextFlags &= ~CONTEXT_RISCV64;

    if (ContextFlags & (CONTEXT_CONTROL & ~CONTEXT_RISCV64))
    {
        Destination->Pc = Source->Pc;
        Destination->Ra = Source->Ra;
        Destination->Sp = Source->Sp;
    }
    if (ContextFlags & (CONTEXT_INTEGER & ~CONTEXT_RISCV64))
    {
        Destination->Zero = 0;
        for (Index = 3; Index < RTL_NUMBER_OF(Source->X); Index++)
            Destination->X[Index] = Source->X[Index];
    }
    if (ContextFlags & (CONTEXT_FLOATING_POINT & ~CONTEXT_RISCV64))
    {
        RtlCopyMemory(Destination->F, Source->F, sizeof(Source->F));
        Destination->Fcsr = Source->Fcsr & 0xff;
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

    UNREFERENCED_PARAMETER(ExceptionFrame);
    Interrupts = KeDisableInterrupts();
    /* Public CONTEXT contains registers, never a user-supplied sstatus.
     * Also preserve the privilege boundary when a kernel caller edits an
     * existing user frame. Invalid user addresses fault in U mode; they
     * must not turn this operation into a supervisor-context restore. */
    if ((PreviousMode == UserMode) || KiUserTrap(TrapFrame))
        TrapFrame->Sstatus = RISCV_USER_SSTATUS;
    KiRiscvCopyContext(&TrapFrame->Context, Context, ContextFlags);
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
    ULONG Flags;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    Interrupts = KeDisableInterrupts();
    Flags = Context->ContextFlags;
    if (!(TrapFrame->Context.ContextFlags & (CONTEXT_FLOATING_POINT & ~CONTEXT_RISCV64)))
        Flags &= ~(CONTEXT_FLOATING_POINT & ~CONTEXT_RISCV64);
    KiRiscvCopyContext(Context, &TrapFrame->Context, Flags);
    Context->ContextFlags = Flags;
    KeRestoreInterrupts(Interrupts);
}
