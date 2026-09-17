/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS-private live kernel-stack migration for native GUI conversion. */
#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

PVOID
NTAPI
KiRiscvPrepareKernelStack(
    _In_ PVOID StackBase,
    _In_ PVOID StackLimit,
    _In_ ULONG_PTR StackPointer,
    _In_ ULONG_PTR FramePointer)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR OldBase = (ULONG_PTR)Thread->StackBase;
    ULONG_PTR NewBase = (ULONG_PTR)StackBase;
    ULONG_PTR Delta = NewBase - OldBase;
    ULONG_PTR Previous, Status;
    PKTRAP_FRAME TrapFrame, NewTrapFrame, PreviousTrapFrame;

    __asm__ __volatile__("csrr %0, sstatus" : "=r"(Status));
    if ((Status & RISCV_SSTATUS_SIE) || (KeGetCurrentIrql() > APC_LEVEL) ||
        (Thread->SpecialApcDisable >= 0) || (StackPointer & 15) ||
        (StackPointer < Thread->StackLimit) || (StackPointer >= OldBase) ||
        (NewBase & (PAGE_SIZE - 1)) || ((ULONG_PTR)StackLimit & (PAGE_SIZE - 1)) ||
        (NewBase <= (ULONG_PTR)StackLimit) ||
        (OldBase - StackPointer > NewBase - (ULONG_PTR)StackLimit) ||
        ((ULONG_PTR)Thread->InitialStack <= StackPointer) ||
        ((ULONG_PTR)Thread->InitialStack > OldBase))
    {
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_BAD_STACK, StackPointer, OldBase, NewBase);
    }

    /* The helper runs below StackPointer and is not part of the copy. Its
     * caller's assembly frame stays unchanged until this helper returns. */
    RtlCopyMemory((PVOID)(StackPointer + Delta), (PVOID)StackPointer, OldBase - StackPointer);

    while (FramePointer)
    {
        if ((FramePointer & 15) || (FramePointer < StackPointer + 2 * sizeof(ULONG_PTR)) ||
            (FramePointer > OldBase))
        {
            KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_BAD_STACK, FramePointer, StackPointer, OldBase);
        }
        Previous = ((PULONG_PTR)(FramePointer + Delta))[-2];
        if (Previous && (Previous <= FramePointer))
            KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_BAD_STACK, FramePointer, Previous, OldBase);
        ((PULONG_PTR)(FramePointer + Delta))[-2] = Previous ? Previous + Delta : 0;
        FramePointer = Previous;
    }

    TrapFrame = Thread->TrapFrame;
    while (TrapFrame)
    {
        if (((ULONG_PTR)TrapFrame & 15) || ((ULONG_PTR)TrapFrame < StackPointer) ||
            ((ULONG_PTR)TrapFrame > OldBase - sizeof(*TrapFrame)))
        {
            KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_BAD_STACK, (ULONG_PTR)TrapFrame, StackPointer, OldBase);
        }
        NewTrapFrame = (PKTRAP_FRAME)((ULONG_PTR)TrapFrame + Delta);
        PreviousTrapFrame = NewTrapFrame->PreviousTrapFrame;
        if (PreviousTrapFrame && ((ULONG_PTR)PreviousTrapFrame <= (ULONG_PTR)TrapFrame))
            KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_BAD_STACK, (ULONG_PTR)TrapFrame, (ULONG_PTR)PreviousTrapFrame, OldBase);
        NewTrapFrame->PreviousTrapFrame = PreviousTrapFrame ? (PKTRAP_FRAME)((ULONG_PTR)PreviousTrapFrame + Delta) : NULL;
        if (!KiUserTrap(NewTrapFrame))
        {
            if ((NewTrapFrame->Context.Sp >= StackPointer) && (NewTrapFrame->Context.Sp <= OldBase))
                NewTrapFrame->Context.Sp += Delta;
            if ((NewTrapFrame->Context.S0 >= StackPointer) && (NewTrapFrame->Context.S0 <= OldBase))
                NewTrapFrame->Context.S0 += Delta;
        }
        TrapFrame = PreviousTrapFrame;
    }

    /* No persistent PCR stack pointer exists: trap entry uses InitialStack,
     * and PCR TrapStack is only disposable entry scratch. */
    if (Thread->TrapFrame)
        Thread->TrapFrame = (PKTRAP_FRAME)((ULONG_PTR)Thread->TrapFrame + Delta);
    Thread->InitialStack = (PVOID)((ULONG_PTR)Thread->InitialStack + Delta);
    Thread->KernelStack = (PVOID)(StackPointer + Delta);
    Thread->StackBase = StackBase;
    Thread->StackLimit = (ULONG_PTR)StackLimit;
    Thread->LargeStack = TRUE;
    return (PVOID)OldBase;
}
