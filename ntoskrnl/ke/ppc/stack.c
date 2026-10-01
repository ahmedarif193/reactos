/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC kernel stack switching
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Copy the live part of the kernel stack to the new allocation and relocate
 * everything that points into it: the back chain, saved frame pointers and
 * other callee-saved registers, the trap frame chain and pointers to locals.
 * KeSwitchKernelStack relocates the live nonvolatile registers. Returns the
 * old stack base. Called by KeSwitchKernelStack with interrupts disabled. */
PVOID
NTAPI
KiPpcPrepareKernelStack(
    _In_ PVOID StackBase,
    _In_ PVOID StackLimit,
    _In_ ULONG_PTR StackPointer)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR OldBase = (ULONG_PTR)Thread->StackBase;
    ULONG_PTR NewBase = (ULONG_PTR)StackBase;
    ULONG_PTR Delta = NewBase - OldBase;
    PULONG_PTR Word, End;
    PKTRAP_FRAME TrapFrame;

    if ((KiPpcReadMsr() & MSR_EE) || (KeGetCurrentIrql() > APC_LEVEL) || (StackPointer & 7) || (StackPointer < Thread->StackLimit) || (StackPointer >= OldBase) || (NewBase & (PAGE_SIZE - 1)) || ((ULONG_PTR)StackLimit & (PAGE_SIZE - 1)) || (NewBase <= (ULONG_PTR)StackLimit) || (OldBase - StackPointer > NewBase - (ULONG_PTR)StackLimit))
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_BAD_STACK, StackPointer, OldBase, NewBase);

    for (TrapFrame = Thread->TrapFrame; TrapFrame; TrapFrame = TrapFrame->PreviousTrapFrame)
    {
        if (((ULONG_PTR)TrapFrame & 15) || ((ULONG_PTR)TrapFrame < StackPointer) || ((ULONG_PTR)TrapFrame > OldBase - sizeof(*TrapFrame)))
            KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_BAD_STACK, (ULONG_PTR)TrapFrame, StackPointer, OldBase);
    }

    RtlCopyMemory((PVOID)(StackPointer + Delta), (PVOID)StackPointer, OldBase - StackPointer);

    /* A word holding an address in the live old stack is a stack pointer:
     * kernel stack addresses do not occur as ordinary data. */
    End = (PULONG_PTR)NewBase;
    for (Word = (PULONG_PTR)(StackPointer + Delta); Word < End; Word++)
    {
        if ((*Word >= StackPointer) && (*Word < OldBase))
            *Word += Delta;
    }

    if (Thread->TrapFrame)
        Thread->TrapFrame = (PKTRAP_FRAME)((ULONG_PTR)Thread->TrapFrame + Delta);
    Thread->InitialStack = (PVOID)((ULONG_PTR)Thread->InitialStack + Delta);
    Thread->KernelStack = (PVOID)(StackPointer + Delta);
    Thread->StackBase = StackBase;
    Thread->StackLimit = (ULONG_PTR)StackLimit;
    Thread->LargeStack = TRUE;
    KeGetPcr()->InitialStack = (ULONG_PTR)Thread->InitialStack;
    return (PVOID)OldBase;
}
