/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/sys/sysregion.c
 * PURPOSE:     System virtual address region management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/misys.h>

NTSTATUS
MiSystemRegionCreate(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 PageCount,
    _In_ BOOLEAN PinTables,
    _Out_ PMI_VAD *VadOut,
    _Out_ PULONG64 Base)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    ULONG64 TableSpanPages = 1ULL << (System->Arch->Level[1].Shift - PAGE_SHIFT);
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG64 Vpn;
    PMI_VAD Vad;

    *VadOut = NULL;
    *Base = 0;

    if (PinTables)
        PageCount = (PageCount + TableSpanPages - 1) & ~(TableSpanPages - 1);

    Vad = MI_ALLOCATE(sizeof(*Vad));
    if (Vad == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Vad, sizeof(*Vad));
    Vad->Type = MiVadSystem;
    Vad->Protection = MI_PROT_READWRITE;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    if (!MiVadFindEmptyRange(&Space->VadRoot, PageCount, TableSpanPages, &Vpn))
    {
        Status = STATUS_NO_MEMORY;
    }
    else
    {
        Vad->Node.StartingVpn = Vpn;
        Vad->Node.EndingVpn = Vpn + PageCount - 1;

        if (!MiVadInsert(&Space->VadRoot, &Vad->Node))
            Status = STATUS_CONFLICTING_ADDRESSES;
    }

    if (NT_SUCCESS(Status) && PinTables)
    {
        Status = MiPtPinRange(Space, Vpn << PAGE_SHIFT, PageCount << PAGE_SHIFT);
        if (!NT_SUCCESS(Status))
        {
            MiPtUnpinRange(Space, Vpn << PAGE_SHIFT, PageCount << PAGE_SHIFT);
            MiVadRemove(&Space->VadRoot, &Vad->Node);
        }
    }

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);

    if (!NT_SUCCESS(Status))
    {
        MI_FREE(Vad);
        return Status;
    }

    *VadOut = Vad;
    *Base = Vpn << PAGE_SHIFT;
    return STATUS_SUCCESS;
}

VOID
MiSystemRegionDelete(
    _Inout_ PMI_SYSTEM System,
    _Inout_ PMI_VAD Vad,
    _In_ BOOLEAN PinnedTables)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    if (PinnedTables)
    {
        MiPtUnpinRange(Space, Vad->Node.StartingVpn << PAGE_SHIFT,
                       (Vad->Node.EndingVpn - Vad->Node.StartingVpn + 1) << PAGE_SHIFT);
    }

    MiVadRemove(&Space->VadRoot, &Vad->Node);
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    MI_FREE(Vad);
}

NTSTATUS
MiSystemMapFrames(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ const MI_FRAME_NUMBER *Frames,
    _In_ ULONG Count,
    _In_ ULONG Protection,
    _In_ ULONG LeafFlags,
    _In_ BOOLEAN OwnFrames)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        ULONG64 Va = VirtualAddress + (ULONG64)i * PAGE_SIZE;
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);

        if (Slot == NULL)
        {
            MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
            Slot = MiPtEnsure(Space, Va, &TableFrame);
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);

            if (Slot == NULL)
            {
                MiSystemUnmap(System, VirtualAddress, i, FALSE);
                return STATUS_NO_MEMORY;
            }
        }

        MI_ASSERT(MiArchPteRead(Slot) == 0);

        if (OwnFrames)
        {
            MiPfnInitializePage(&System->Pfn, (ULONG)Frames[i], MiPtSlotAddress(Slot, TableFrame, Va), TableFrame,
                                MiSoftMake(MiSoftDemandZero, Protection, 0), 0);
        }

        MiPtWrite(Space, Va, Slot, TableFrame,
                  MiArchPteMakeLeaf(Frames[i], Protection, LeafFlags | MI_LEAF_GLOBAL | MI_LEAF_DIRTY));
    }

    return STATUS_SUCCESS;
}

VOID
MiSystemUnmap(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Count,
    _In_ BOOLEAN FreeFrames)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        ULONG64 Va = VirtualAddress + (ULONG64)i * PAGE_SIZE;
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);
        MI_PTE Pte;

        if (Slot == NULL)
            continue;

        Pte = MiArchPteRead(Slot);
        if (!MiArchPteIsValid(Pte))
            continue;

        MiPtWrite(Space, Va, Slot, TableFrame, 0);

        if (FreeFrames)
            MiPfnShareDecrement(&System->Pfn, (ULONG)MiArchPteFrame(Pte), TRUE);
    }

    if (Count != 0)
        MiArchTlbInvalidate(VirtualAddress, Count, TRUE);
}

static
ULONG
MiSystemDecommitLocked(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Count)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    ULONG Freed = 0;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        ULONG64 Va = VirtualAddress + (ULONG64)i * PAGE_SIZE;
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);
        MI_PTE Pte;

        if (Slot == NULL)
            continue;

        Pte = MiArchPteRead(Slot);
        if (!MiArchPteIsValid(Pte))
            continue;

        MiPtWrite(Space, Va, Slot, TableFrame, 0);
        MiPfnShareDecrement(&System->Pfn, (ULONG)MiArchPteFrame(Pte), TRUE);
        Freed++;
    }

    if (Freed != 0)
        MiArchTlbInvalidate(VirtualAddress, Count, TRUE);

    return Freed;
}

NTSTATUS
MiSystemCommitPages(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Count,
    _In_ ULONG Protection)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;

    if (!MiChargeCommit(Space, Count))
        return STATUS_COMMITMENT_LIMIT;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    for (i = 0; i < Count; i++)
    {
        ULONG64 Va = VirtualAddress + (ULONG64)i * PAGE_SIZE;
        ULONG TableFrame;
        PMI_PTE Slot;
        ULONG Frame;

        Slot = MiPtEnsure(Space, Va, &TableFrame);
        if (Slot == NULL || MiArchPteRead(Slot) != 0)
        {
            Status = (Slot == NULL) ? STATUS_NO_MEMORY : STATUS_CONFLICTING_ADDRESSES;
            break;
        }

        Frame = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
        if (Frame == MI_FRAME_INVALID)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        MiPfnInitializePage(&System->Pfn, Frame, MiPtSlotAddress(Slot, TableFrame, Va), TableFrame,
                            MiSoftMake(MiSoftDemandZero, Protection, 0), 0);
        MiPtWrite(Space, Va, Slot, TableFrame,
                  MiArchPteMakeLeaf(Frame, Protection, MI_LEAF_GLOBAL | MI_LEAF_DIRTY));
    }

    if (!NT_SUCCESS(Status))
        MiSystemDecommitLocked(System, VirtualAddress, i);

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);

    if (!NT_SUCCESS(Status))
    {
        MiReturnCommit(Space, Count);
        return Status;
    }

    MI_ATOMIC_ADD64(&Space->ResidentPages, Count);
    return STATUS_SUCCESS;
}

VOID
MiSystemDecommitPages(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Count)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    ULONG Freed;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    Freed = MiSystemDecommitLocked(System, VirtualAddress, Count);
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);

    MiReturnCommit(Space, Freed);
    MI_ATOMIC_ADD64(&Space->ResidentPages, -(LONG64)Freed);
}

ULONG64
MiGetPhysicalAddress(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress)
{
    ULONG64 Physical;

    return MiPtTranslate(Space, VirtualAddress, &Physical, NULL) ? Physical : 0;
}

NTSTATUS
MiSystemProtect(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Count,
    _In_ ULONG Protection)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    ULONG i;

    if (!MI_PROT_IS_ACCESSIBLE(Protection) && (Protection & MI_PROT_NOACCESS) != MI_PROT_NOACCESS)
        return STATUS_INVALID_PAGE_PROTECTION;

    for (i = 0; i < Count; i++)
    {
        ULONG64 Va = VirtualAddress + (ULONG64)i * PAGE_SIZE;
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);
        MI_PTE Pte = (Slot != NULL) ? MiArchPteRead(Slot) : 0;

        if (!MiArchPteIsValid(Pte))
            return STATUS_NOT_COMMITTED;

        MiPtWrite(Space, Va, Slot, TableFrame,
                  MiArchPteMakeLeaf(MiArchPteFrame(Pte), Protection, MI_LEAF_GLOBAL | MI_LEAF_DIRTY));
    }

    MiArchTlbInvalidate(VirtualAddress, Count, TRUE);
    return STATUS_SUCCESS;
}

NTSTATUS
MiSystemCommitPinned(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Count,
    _In_ ULONG Protection)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;

    if (!MiChargeCommit(Space, Count))
        return STATUS_COMMITMENT_LIMIT;

    for (i = 0; i < Count; i++)
    {
        ULONG64 Va = VirtualAddress + (ULONG64)i * PAGE_SIZE;
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);
        ULONG Frame;

        if (Slot == NULL || MiArchPteRead(Slot) != 0)
        {
            Status = STATUS_CONFLICTING_ADDRESSES;
            break;
        }

        Frame = MiPfnAllocatePage(&System->Pfn, 0);
        if (Frame == MI_FRAME_INVALID)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        MiPfnInitializePage(&System->Pfn, Frame, MiPtSlotAddress(Slot, TableFrame, Va), TableFrame,
                            MiSoftMake(MiSoftDemandZero, Protection, 0), 0);
        MiPtWrite(Space, Va, Slot, TableFrame,
                  MiArchPteMakeLeaf(Frame, Protection, MI_LEAF_GLOBAL | MI_LEAF_DIRTY));
    }

    if (!NT_SUCCESS(Status))
    {
        MiSystemUnmap(System, VirtualAddress, i, TRUE);
        MiReturnCommit(Space, Count);
        return Status;
    }

    MI_ATOMIC_ADD64(&Space->ResidentPages, Count);
    return STATUS_SUCCESS;
}

VOID
MiSystemDecommitPinned(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Count)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;

    MiSystemUnmap(System, VirtualAddress, Count, TRUE);
    MiReturnCommit(Space, Count);
    MI_ATOMIC_ADD64(&Space->ResidentPages, -(LONG64)Count);
}
