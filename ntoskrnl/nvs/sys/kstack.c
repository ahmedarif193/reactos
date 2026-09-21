/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/sys/kstack.c
 * PURPOSE:     Kernel stack allocation and management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/misys.h>

static
NTSTATUS
MiCommitStackPages(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Count)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;

    if (!MiChargeCommit(Space, Count))
        return STATUS_COMMITMENT_LIMIT;

    for (i = 0; i < Count && NT_SUCCESS(Status); i++)
    {
        ULONG Frame = MiPfnAllocatePage(&System->Pfn, 0);
        MI_FRAME_NUMBER Number = Frame;

        if (Frame == MI_FRAME_INVALID)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        Status = MiSystemMapFrames(System, VirtualAddress + (ULONG64)i * PAGE_SIZE, &Number, 1, MI_PROT_READWRITE, 0,
                                   TRUE);
        if (!NT_SUCCESS(Status))
            MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
    }

    if (!NT_SUCCESS(Status))
    {
        MiSystemUnmap(System, VirtualAddress, i, TRUE);
        MiReturnCommit(Space, Count);
    }

    return Status;
}

NTSTATUS
MiCreateKernelStack(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG ReservePages,
    _In_ ULONG CommitPages,
    _Out_ PULONG64 StackTop)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    NTSTATUS Status;
    ULONG64 Base;
    ULONG64 Top;

    *StackTop = 0;

    if (Ptes == NULL || ReservePages == 0 || CommitPages == 0 || CommitPages > ReservePages)
        return STATUS_INVALID_PARAMETER;

    if (ReservePages == Ptes->DefaultStackPages && CommitPages == ReservePages)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[MI_CURRENT_CPU() % Ptes->CacheCount];
        KIRQL OldIrql;

        if (MI_PEEK(Cache->StackDepth) != 0)
        {
            MI_SPIN_ACQUIRE(&Cache->Lock, &OldIrql);
            if (Cache->StackDepth != 0)
                *StackTop = Cache->Stack[--Cache->StackDepth];
            MI_SPIN_RELEASE(&Cache->Lock, OldIrql);

            if (*StackTop != 0)
                return STATUS_SUCCESS;
        }
    }

    Base = MiReserveSystemPtes(System, ReservePages + 1);
    if (Base == 0)
        return STATUS_INSUFFICIENT_RESOURCES;

    Top = Base + ((ULONG64)ReservePages + 1) * PAGE_SIZE;

    Status = MiCommitStackPages(System, Top - (ULONG64)CommitPages * PAGE_SIZE, CommitPages);
    if (!NT_SUCCESS(Status))
    {
        MiReleaseSystemPtes(System, Base, ReservePages + 1);
        return Status;
    }

    *StackTop = Top;
    return STATUS_SUCCESS;
}

static
ULONG
MiCommittedStackPages(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 StackTop,
    _In_ ULONG ReservePages)
{
    ULONG Pages = 0;

    while (Pages < ReservePages && MiGetPhysicalAddress(&System->SystemSpace,
                                                        StackTop - ((ULONG64)Pages + 1) * PAGE_SIZE) != 0)
    {
        Pages++;
    }

    return Pages;
}

VOID
MiDeleteKernelStack(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 StackTop,
    _In_ ULONG ReservePages)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG Committed = MiCommittedStackPages(System, StackTop, ReservePages);

    if (ReservePages == Ptes->DefaultStackPages && Committed == ReservePages)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[MI_CURRENT_CPU() % Ptes->CacheCount];
        BOOLEAN Cached = FALSE;
        KIRQL OldIrql;

        MI_SPIN_ACQUIRE(&Cache->Lock, &OldIrql);
        if (Cache->StackDepth < MI_KSTACK_CACHE_DEPTH)
        {
            Cache->Stack[Cache->StackDepth++] = StackTop;
            Cached = TRUE;
        }
        MI_SPIN_RELEASE(&Cache->Lock, OldIrql);

        if (Cached)
            return;
    }

    MiSystemUnmap(System, StackTop - (ULONG64)Committed * PAGE_SIZE, Committed, TRUE);
    MiReturnCommit(&System->SystemSpace, Committed);
    MiReleaseSystemPtes(System, StackTop - ((ULONG64)ReservePages + 1) * PAGE_SIZE, ReservePages + 1);
}

NTSTATUS
MiGrowKernelStack(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 StackTop,
    _In_ ULONG ReservePages,
    _In_ ULONG64 NewLimit)
{
    ULONG Committed = MiCommittedStackPages(System, StackTop, ReservePages);
    ULONG64 Limit = StackTop - (ULONG64)Committed * PAGE_SIZE;
    ULONG64 Lowest = StackTop - (ULONG64)ReservePages * PAGE_SIZE;
    ULONG Needed;

    NewLimit &= ~((ULONG64)PAGE_SIZE - 1);

    if (NewLimit >= Limit)
        return STATUS_SUCCESS;

    if (NewLimit < Lowest)
        return STATUS_STACK_OVERFLOW;

    Needed = (ULONG)((Limit - NewLimit) >> PAGE_SHIFT);
    return MiCommitStackPages(System, NewLimit, Needed);
}
