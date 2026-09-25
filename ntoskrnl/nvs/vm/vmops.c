/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/vm/vmops.c
 * PURPOSE:     Virtual memory reservation, commitment, and protection
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

#define MI_PAGE_ALIGN_DOWN(a)   ((a) & ~((ULONG64)PAGE_SIZE - 1))
#define MI_PAGE_ALIGN_UP(a)     (((a) + PAGE_SIZE - 1) & ~((ULONG64)PAGE_SIZE - 1))
#define MI_VAD_START(v)         ((v)->Node.StartingVpn << PAGE_SHIFT)
#define MI_VAD_END(v)           (((v)->Node.EndingVpn + 1) << PAGE_SHIFT)

static
BOOLEAN
MiProtectionIsValid(
    _In_ ULONG Protection)
{
    ULONG Access = Protection & MI_PROT_ACCESS_MASK;

    if (Protection & ~MI_PROT_MASK)
        return FALSE;

    if ((Protection & MI_PROT_NOACCESS) == MI_PROT_NOACCESS)
        return (Access == MI_PROT_NONE);

    return (Access != MI_PROT_NONE);
}

static
VOID
MiPageStatusLocked(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress,
    _In_ MI_PTE Pte,
    _Out_ PBOOLEAN Committed,
    _Out_ PULONG Protection)
{
    if (Vad->Type == MiVadAwe)
    {
        *Committed = FALSE;
        *Protection = 0;
        return;
    }

    if (Vad->Type == MiVadLarge)
    {
        PMI_PTE Large = MiPtLookupLevel(Space, VirtualAddress, Space->System->Arch->LargePageLevel, NULL);
        MI_PTE Entry = Large != NULL ? MiArchPteRead(Large) : 0;

        *Committed = TRUE;
        *Protection = MiArchPteIsValid(Entry) ? MiViewPageProtection(Space, Vad, VirtualAddress, Entry)
                                            : MiSoftProtection(Entry);
        return;
    }

    if (MiArchPteIsValid(Pte) && MI_VAD_IS_DIRECT(Vad))
    {
        *Committed = TRUE;
        *Protection = MiViewPageProtection(Space, Vad, VirtualAddress, Pte);
        return;
    }

    if (MiArchPteIsValid(Pte))
    {
        ULONG Frame = (ULONG)MiArchPteFrame(Pte);
        PMI_PFN Entry = &Space->System->Pfn.Pfn[Frame];
        KIRQL OldIrql = MiPfnLock(&Space->System->Pfn, Frame);

        *Committed = TRUE;
        *Protection = (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PROTOTYPE) ? MiViewPageProtection(Space, Vad, VirtualAddress, Pte)
                                                             : MiSoftProtection(Entry->OriginalPte);
        MiPfnUnlock(&Space->System->Pfn, Frame, OldIrql);
        return;
    }

    if (Pte == 0)
    {
        *Committed = (BOOLEAN)(!MI_VAD_IS_DIRECT(Vad) && (Vad->Type != MiVadPrivate || Vad->MemCommit) &&
                               (Vad->Type == MiVadPrivate || MiViewPageCommitted(Vad, VirtualAddress)));
        *Protection = !*Committed ? 0 : (Vad->Type != MiVadPrivate)
                                            ? MiViewPageProtection(Space, Vad, VirtualAddress, Pte)
                                            : Vad->Protection;
        return;
    }

    if (MiSoftKind(Pte) == MiSoftDecommitted ||
        (Vad->Type != MiVadPrivate && MiSoftKind(Pte) == MiSoftPrototype && !MiViewPageCommitted(Vad, VirtualAddress)))
    {
        *Committed = FALSE;
        *Protection = 0;
        return;
    }

    *Committed = TRUE;
    *Protection = MiSoftProtection(Pte);
}

static
VOID
MiPageStatus(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress,
    _In_ BOOLEAN Shared,
    _Out_ PBOOLEAN Committed,
    _Out_ PULONG Protection)
{
    ULONG TableFrame;
    PMI_PTE Slot = MiPtLookup(Space, VirtualAddress, &TableFrame);
    MI_PTE Pte = (Slot != NULL) ? MiArchPteRead(Slot) : 0;

    if (Shared && MiArchPteIsValid(Pte) && !MI_VAD_IS_DIRECT(Vad))
    {
        KIRQL OldIrql = MiPfnLock(&Space->System->Pfn, TableFrame);

        MiPageStatusLocked(Space, Vad, VirtualAddress, MiArchPteRead(Slot), Committed, Protection);
        MiPfnUnlock(&Space->System->Pfn, TableFrame, OldIrql);
    }
    else
    {
        MiPageStatusLocked(Space, Vad, VirtualAddress, Pte, Committed, Protection);
    }
}

static
NTSTATUS
MiSetPrivatePageProtectionAt(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ ULONG Protection);

NTSTATUS
MiSetPrivatePageProtection(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Protection)
{
    ULONG TableFrame;
    PMI_PTE Slot = MiPtEnsure(Space, VirtualAddress, &TableFrame);

    if (Slot == NULL)
        return STATUS_NO_MEMORY;

    return MiSetPrivatePageProtectionAt(Space, VirtualAddress, Slot, TableFrame, Protection);
}

static
NTSTATUS
MiSetPrivateRangeProtection(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Protection)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG64 Va = Start;
    MI_PTE DemandZero = MiSoftMake(MiSoftDemandZero, Protection, 0);
    BOOLEAN NoClones = Space->CloneRoot.NodeCount == 0;

    while (Va < End && NT_SUCCESS(Status))
    {
        ULONG64 TableEnd = MiPtNextTableBoundary(Space, Va);
        ULONG TableFrame;
        PMI_PTE Slot = MiPtEnsure(Space, Va, &TableFrame);
        LONG Added = 0;

        if (Slot == NULL)
            return STATUS_NO_MEMORY;

        if (TableEnd > End)
            TableEnd = End;

        for (; Va < TableEnd && NT_SUCCESS(Status); Va += PAGE_SIZE, Slot++)
        {
            MI_PTE Pte = MiArchPteRead(Slot);

            if (NoClones && MiSoftIsUnbacked(Pte))
            {
                Added += Pte == 0;
                MiArchPteWrite(Slot, DemandZero);
            }
            else
            {
                Status = MiSetPrivatePageProtectionAt(Space, Va, Slot, TableFrame, Protection);
            }
        }
        if (Added != 0 && !(MI_PFN_FLAGS(&Space->System->Pfn.Pfn[TableFrame]) & MI_PFN_FLAG_PINNED))
            MI_ATOMIC_ADD32(&Space->System->Pfn.Pfn[TableFrame].UsedEntries, Added);
    }

    return Status;
}

static
LONG64
MiCountUncommittedPrivate(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    LONG64 Count = 0;
    ULONG64 Va = Start;

    while (Va < End)
    {
        ULONG64 TableEnd = MiPtNextTableBoundary(Space, Va);
        PMI_PTE Slot = MiPtLookup(Space, Va, NULL);

        if (TableEnd > End)
            TableEnd = End;

        if (Slot == NULL)
        {
            if (!Vad->MemCommit)
                Count += (LONG64)((TableEnd - Va) >> PAGE_SHIFT);

            Va = TableEnd;
            continue;
        }

        for (; Va < TableEnd; Va += PAGE_SIZE, Slot++)
        {
            MI_PTE Pte = MiArchPteRead(Slot);

            if (Pte == 0)
                Count += Vad->MemCommit ? 0 : 1;
            else if (!MiArchPteIsValid(Pte) && MiSoftKind(Pte) == MiSoftDecommitted)
                Count++;
        }
    }

    return Count;
}

static
NTSTATUS
MiSetPrivatePageProtectionAt(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ ULONG Protection)
{
    PMI_SYSTEM System = Space->System;
    MI_PTE Pte;

    Pte = MiArchPteRead(Slot);

    if (MiCloneProtectPage(Space, VirtualAddress, Slot, TableFrame, Protection))
        return STATUS_SUCCESS;

    if (MiArchPteIsValid(Pte))
    {
        ULONG Frame = (ULONG)MiArchPteFrame(Pte);
        PMI_PFN Entry = &System->Pfn.Pfn[Frame];
        ULONG Flags = (Space->IsSystem ? MI_LEAF_GLOBAL : MI_LEAF_USER) |
                      (MiArchPteIsDirty(Pte) ? MI_LEAF_DIRTY : 0);

        if (MiArchPteIsDirty(Pte))
            MiPfnSetModified(&System->Pfn, Frame);

        if (!(MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PROTOTYPE))
            Entry->OriginalPte = MiSoftWithProtection(Entry->OriginalPte, Protection);

        if (!MI_PROT_IS_ACCESSIBLE(Protection) || (Protection & MI_PROT_GUARD))
        {
            MI_PTE Transition = MiSoftMake(MiSoftTransition, Protection, Frame);

            MiPtWrite(Space, VirtualAddress, Slot, TableFrame, Transition);
            MiArchTlbInvalidate(VirtualAddress, 1, TRUE);
            MiPfnShareDecrement(&System->Pfn, Frame, FALSE);
            MI_ATOMIC_ADD64(&Space->ResidentPages, -1);
            return STATUS_SUCCESS;
        }

        MiPtWrite(Space, VirtualAddress, Slot, TableFrame, MiArchPteMakeLeaf(Frame, Protection, Flags));
        MiArchTlbInvalidate(VirtualAddress, 1, TRUE);
        return STATUS_SUCCESS;
    }

    if (MiSoftKind(Pte) == MiSoftTransition)
    {
        ULONG Frame = (ULONG)MiSoftValue(Pte);
        KIRQL OldIrql;

        OldIrql = MiPfnLock(&System->Pfn, Frame);
        if (MiArchPteRead(Slot) == Pte)
        {
            System->Pfn.Pfn[Frame].OriginalPte = MiSoftWithProtection(System->Pfn.Pfn[Frame].OriginalPte, Protection);
            MiArchPteWrite(Slot, MiSoftWithProtection(Pte, Protection));
            MiPfnUnlock(&System->Pfn, Frame, OldIrql);
            return STATUS_SUCCESS;
        }
        MiPfnUnlock(&System->Pfn, Frame, OldIrql);
        Pte = MiArchPteRead(Slot);
    }

    if (Pte == 0 || MiSoftKind(Pte) == MiSoftDecommitted)
        Pte = MiSoftMake(MiSoftDemandZero, Protection, 0);

    MiPtWrite(Space, VirtualAddress, Slot, TableFrame, MiSoftWithProtection(Pte, Protection));
    return STATUS_SUCCESS;
}

static
LONG64
MiDeleteRange(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ BOOLEAN Decommit)
{
    MI_PTE NewValue = (Decommit && Vad->MemCommit) ? MiSoftMake(MiSoftDecommitted, 0, 0) : 0;
    LONG64 Committed = 0;
    ULONG64 Va = Start;
    ULONG64 TableEnd;
    MI_TLB_BATCH Batch;
    BOOLEAN Owner;
    BOOLEAN Deferred;
    BOOLEAN NoClones = Space->CloneRoot.NodeCount == 0;

    if (!Decommit)
        MiPtFlushEmpty(Space);

    if (Vad->Type == MiVadPrivate && !Vad->PteTouched && NewValue == 0)
        return Vad->MemCommit ? (LONG64)((End - Start) >> PAGE_SHIFT) : 0;

    Vad->PteTouched = TRUE;
    Owner = MiTlbBatchBegin(Space, &Batch);
    Deferred = Space->TlbBatch->DeferTables;
    Space->TlbBatch->DeferTables = TRUE;

    while (Va < End)
    {
        ULONG64 TableVa = Va;
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);
        MI_PTE Pte;
        LONG UsedDelta = 0;

        if (Slot == NULL)
        {
            ULONG64 Next = MiPtNextTableBoundary(Space, Va);
            ULONG64 Skipped = ((Next < End ? Next : End) - Va) >> PAGE_SHIFT;

            if (Vad->Type == MiVadPrivate && Vad->MemCommit)
            {
                if (Decommit)
                {
                    Slot = MiPtEnsure(Space, Va, &TableFrame);
                    if (Slot == NULL)
                        goto Done;
                }
                else
                {
                    Committed += (LONG64)Skipped;
                }
            }

            if (Slot == NULL)
            {
                Va = (Next < End) ? Next : End;
                continue;
            }
        }

        TableEnd = MiPtNextTableBoundary(Space, Va);
        if (TableEnd > End)
            TableEnd = End;

        while (Va < TableEnd)
        {
            Pte = MiArchPteRead(Slot);

            if (Vad->Type == MiVadPrivate)
            {
                if (Pte == 0)
                    Committed += Vad->MemCommit ? 1 : 0;
                else if (MiSoftKind(Pte) != MiSoftDecommitted || MiArchPteIsValid(Pte))
                    Committed++;
            }

            Va += PAGE_SIZE;

            if (Pte == 0 && NewValue == 0)
            {
                Slot++;
                continue;
            }

            if (NoClones && MiSoftIsUnbacked(Pte))
            {
                UsedDelta += (NewValue != 0) - (Pte != 0);
                MiArchPteWrite(Slot, NewValue);
            }
            else
            {
                MiDeletePte(Space, Va - PAGE_SIZE, Slot, TableFrame, NewValue);
            }

            Slot++;
        }

        if (UsedDelta != 0 && !(MI_PFN_FLAGS(&Space->System->Pfn.Pfn[TableFrame]) & MI_PFN_FLAG_PINNED))
            MI_ATOMIC_ADD32(&Space->System->Pfn.Pfn[TableFrame].UsedEntries, UsedDelta);
        if (NewValue == 0 && !Deferred)
        {
            if (Decommit && !Space->IsSystem)
                MiPtCacheEmpty(Space, TableFrame, TableVa);
            else
                MiPtPruneEmpty(Space, TableVa);
        }
    }

Done:
    Space->TlbBatch->DeferTables = Deferred;
    MiTlbBatchEnd(Space, Owner);
    return Committed;
}

static
PMI_VAD
MiVadCreate(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Protection,
    _In_ UCHAR Type)
{
    PMI_VAD Vad = MI_ALLOCATE(sizeof(MI_VAD));

    if (Vad == NULL)
        return NULL;

    RtlZeroMemory(Vad, sizeof(*Vad));
    Vad->Node.StartingVpn = Start >> PAGE_SHIFT;
    Vad->Node.EndingVpn = (End >> PAGE_SHIFT) - 1;
    Vad->Protection = Protection;
    Vad->Type = Type;

    if (!MiVadInsert(&Space->VadRoot, &Vad->Node))
    {
        MI_FREE(Vad);
        return NULL;
    }

    return Vad;
}

BOOLEAN
MiSpaceFindEmptyRange(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 PageCount,
    _In_ ULONG64 Alignment,
    _In_ ULONG64 LowestVpn,
    _In_ ULONG64 HighestVpn,
    _In_ BOOLEAN TopDown,
    _Out_ PULONG64 StartingVpn)
{
    ULONG64 Lowest = (LowestVpn > (Space->LowestVa >> PAGE_SHIFT)) ? LowestVpn : (Space->LowestVa >> PAGE_SHIFT);

    if (!TopDown && (Space->BottomUpVa >> PAGE_SHIFT) > Lowest &&
        MiVadFindEmptyRangeEx(&Space->VadRoot, PageCount, Alignment, Space->BottomUpVa >> PAGE_SHIFT, HighestVpn,
                              FALSE, StartingVpn))
    {
        return TRUE;
    }

    if (TopDown && Space->TopDownVa != 0 && (Space->TopDownVa >> PAGE_SHIFT) < HighestVpn &&
        MiVadFindEmptyRangeEx(&Space->VadRoot, PageCount, Alignment, Lowest,
                              Space->TopDownVa >> PAGE_SHIFT, TRUE, StartingVpn))
    {
        return TRUE;
    }

    return MiVadFindEmptyRangeEx(&Space->VadRoot, PageCount, Alignment, Lowest, HighestVpn,
                                 TopDown, StartingVpn);
}

static
NTSTATUS
MiReserveVirtualMemory(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PULONG64 BaseAddress,
    _Inout_ PULONG64 RegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG Protection,
    _In_ ULONG64 LowestAddress,
    _In_ ULONG64 HighestAddress,
    _In_ ULONG64 Alignment)
{
    PMI_VAD Vad = NULL;
    ULONG64 Start, End;
    ULONG64 Granularity = (Alignment != 0) ? Alignment : MI_ALLOCATION_GRANULARITY;
    LONG64 Charged = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    Vad = MiVadAllocateAndLock(Space);
    if (Vad == NULL)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (*BaseAddress == 0)
    {
        ULONG64 StartVpn;

        End = MI_PAGE_ALIGN_UP(*RegionSize);
        if (!MiSpaceFindEmptyRange(Space, End >> PAGE_SHIFT, Granularity >> PAGE_SHIFT,
                                   MI_PAGE_ALIGN_UP(LowestAddress) >> PAGE_SHIFT, HighestAddress >> PAGE_SHIFT,
                                   (BOOLEAN)((AllocationType & MI_MEM_TOP_DOWN) != 0),
                                   &StartVpn))
        {
            Status = STATUS_NO_MEMORY;
            goto Done;
        }
        Start = StartVpn << PAGE_SHIFT;
        End += Start;
    }
    else
    {
        Start = *BaseAddress & ~(Granularity - 1);
        End = MI_PAGE_ALIGN_UP(*BaseAddress + *RegionSize);
        if (End <= Start || Start < Space->LowestVa || Start < LowestAddress ||
            End - 1 > Space->HighestVa || End - 1 > HighestAddress)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Done;
        }
        if (MiVadFindOverlap(&Space->VadRoot, Start >> PAGE_SHIFT, (End >> PAGE_SHIFT) - 1) != NULL)
        {
            Status = STATUS_CONFLICTING_ADDRESSES;
            goto Done;
        }
    }
    if (AllocationType & MI_MEM_COMMIT)
    {
        Charged = (LONG64)((End - Start) >> PAGE_SHIFT);
        if (!MiChargeCommit(Space, Charged))
        {
            Status = STATUS_COMMITMENT_LIMIT;
            goto Done;
        }
    }
    Vad->Node.StartingVpn = Start >> PAGE_SHIFT;
    Vad->Node.EndingVpn = (End >> PAGE_SHIFT) - 1;
    Vad->Protection = Protection;
    Vad->Type = (AllocationType & MI_MEM_PHYSICAL) ? MiVadAwe
                : (AllocationType & MI_MEM_ROTATE) ? MiVadRotate : MiVadPrivate;
    Vad->MemCommit = (BOOLEAN)((AllocationType & MI_MEM_COMMIT) != 0);
    Vad->CommitCharge = Charged;
    if (!MiVadInsert(&Space->VadRoot, &Vad->Node))
    {
        MiReturnCommit(Space, Charged);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    *BaseAddress = Start;
    *RegionSize = End - Start;

Done:
    if (!NT_SUCCESS(Status) && MiVadCacheFree(Space, Vad))
        Vad = NULL;
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    if (!NT_SUCCESS(Status) && Vad != NULL)
        MI_FREE(Vad);
    return Status;
}

NTSTATUS
MiAllocateVirtualMemory(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PULONG64 BaseAddress,
    _Inout_ PULONG64 RegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG Protection)
{
    return MiAllocateVirtualMemoryEx(Space, BaseAddress, RegionSize, AllocationType, Protection, ~0ULL);
}

NTSTATUS
MiAllocateVirtualMemoryEx(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PULONG64 BaseAddress,
    _Inout_ PULONG64 RegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG Protection,
    _In_ ULONG64 HighestAddress)
{
    return MiAllocateVirtualMemoryBounded(Space, BaseAddress, RegionSize, AllocationType, Protection, 0,
                                          HighestAddress, 0, FALSE);
}

NTSTATUS
MiAllocateVirtualMemoryBounded(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PULONG64 BaseAddress,
    _Inout_ PULONG64 RegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG Protection,
    _In_ ULONG64 LowestAddress,
    _In_ ULONG64 HighestAddress,
    _In_ ULONG64 Alignment,
    _In_ BOOLEAN DenyDynamicCode)
{
    ULONG64 Start;
    ULONG64 End;
    PMI_VAD Vad;
    NTSTATUS Status = STATUS_SUCCESS;
    LONG64 Charged = 0;

    if (!MiProtectionIsValid(Protection) || MI_PROT_IS_COPY(Protection))
        return STATUS_INVALID_PAGE_PROTECTION;

    if (DenyDynamicCode && MI_PROT_IS_EXECUTE(Protection) && (AllocationType & MI_MEM_RESERVE))
        return STATUS_DYNAMIC_CODE_BLOCKED;

    if (AllocationType & MI_MEM_LARGE_PAGES)
    {
        if (LowestAddress != 0 || Alignment != 0)
            return STATUS_NOT_SUPPORTED;

        return MiAllocateLargePages(Space, BaseAddress, RegionSize, AllocationType, Protection, HighestAddress);
    }

    if (AllocationType & MI_MEM_PHYSICAL)
    {
        if (AllocationType != (MI_MEM_RESERVE | MI_MEM_PHYSICAL) ||
            *RegionSize > ~0ULL - PAGE_SIZE + 1 || *BaseAddress > ~0ULL - *RegionSize)
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (Protection != MI_PROT_READWRITE || Space->IsSystem)
            return STATUS_INVALID_PAGE_PROTECTION;
    }

    if (*RegionSize == 0 || !(AllocationType & (MI_MEM_COMMIT | MI_MEM_RESERVE)) ||
        (AllocationType & ~(MI_MEM_COMMIT | MI_MEM_RESERVE | MI_MEM_TOP_DOWN | MI_MEM_PHYSICAL | MI_MEM_ROTATE)) ||
        ((AllocationType & MI_MEM_ROTATE) && (!(AllocationType & MI_MEM_RESERVE) || (AllocationType & MI_MEM_PHYSICAL))))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (AllocationType & MI_MEM_RESERVE)
    {
        Status = MiReserveVirtualMemory(Space, BaseAddress, RegionSize, AllocationType, Protection, LowestAddress,
                                        HighestAddress, Alignment);
        if (NT_SUCCESS(Status) && (AllocationType & MI_MEM_ROTATE) && (AllocationType & MI_MEM_COMMIT))
            Status = MiRotatePopulate(Space, *BaseAddress, *RegionSize);
        return Status;
    }

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Start = MI_PAGE_ALIGN_DOWN(*BaseAddress);
    End = MI_PAGE_ALIGN_UP(*BaseAddress + *RegionSize);
    Vad = MiVadLocate(Space, Start);

    if (*BaseAddress == 0 || Vad == NULL || MI_VAD_IS_DIRECT(Vad) || End > MI_VAD_END(Vad) || End <= Start)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    if (DenyDynamicCode && MI_PROT_IS_EXECUTE(Protection) && Vad->Type != MiVadImage && !Vad->EcCode)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_DYNAMIC_CODE_BLOCKED;
    }

    if (Vad->Type != MiVadPrivate)
    {
        PMI_SEGMENT Segment = Vad->Segment;
        ULONG64 FirstPage = Vad->SegmentPageOffset + ((Start - MI_VAD_START(Vad)) >> PAGE_SHIFT);

        *BaseAddress = Start;
        *RegionSize = End - Start;
        if (Segment == NULL || !Segment->Reserved)
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return STATUS_SUCCESS;
        }

        MiSegmentReference(Segment);
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        Status = MiSegmentCommitPages(Segment, FirstPage, (End - Start) >> PAGE_SHIFT);
        if (NT_SUCCESS(Status))
        {
            MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
            Vad = MiVadLocate(Space, Start);
            Status = (Vad != NULL && Vad->Segment == Segment && End <= MI_VAD_END(Vad))
                         ? MiSetMappedViewProtection(Space, Vad, Start, End, Protection)
                         : STATUS_CONFLICTING_ADDRESSES;
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        }
        MiSegmentDereference(Segment);
        return Status;
    }

    Charged = Vad->CommitCharge == 0 ? (LONG64)((End - Start) >> PAGE_SHIFT)
                                    : MiCountUncommittedPrivate(Space, Vad, Start, End);

    if (Charged != 0 && !MiChargeCommit(Space, Charged))
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_COMMITMENT_LIMIT;
    }

    Vad->CommitCharge += Charged;
    Vad->PteTouched = TRUE;
    Status = MiSetPrivateRangeProtection(Space, Start, End, Protection);

    *BaseAddress = Start;
    *RegionSize = End - Start;
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}

NTSTATUS
MiFreeVirtualMemory(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PULONG64 BaseAddress,
    _Inout_ PULONG64 RegionSize,
    _In_ ULONG FreeType)
{
    ULONG64 Start = MI_PAGE_ALIGN_DOWN(*BaseAddress);
    ULONG64 End;
    LONG64 Returned;
    PMI_VAD Vad;
    PMI_VAD ReleasedVad = NULL;

    if (FreeType != MI_MEM_DECOMMIT && FreeType != MI_MEM_RELEASE)
        return STATUS_INVALID_PARAMETER;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Vad = MiVadLocate(Space, Start);
    if (Vad == NULL)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_MEMORY_NOT_ALLOCATED;
    }

    if (Vad->Type == MiVadAwe)
    {
        if (FreeType != MI_MEM_RELEASE || *BaseAddress != MI_VAD_START(Vad) || *RegionSize != 0)
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return FreeType == MI_MEM_RELEASE ? STATUS_UNABLE_TO_FREE_VM : STATUS_UNABLE_TO_DECOMMIT_VM;
        }
        *RegionSize = MI_VAD_END(Vad) - MI_VAD_START(Vad);
        MiAweReleaseWindowLocked(Space, Vad);
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_SUCCESS;
    }

    if (Vad->Type == MiVadRotate)
    {
        if (FreeType != MI_MEM_RELEASE || *BaseAddress != MI_VAD_START(Vad) || *RegionSize != 0)
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return FreeType == MI_MEM_RELEASE ? STATUS_UNABLE_TO_FREE_VM : STATUS_UNABLE_TO_DECOMMIT_VM;
        }
        *RegionSize = MI_VAD_END(Vad) - MI_VAD_START(Vad);
        MiRotateReleaseLocked(Space, Vad);
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_SUCCESS;
    }

    if (Vad->Type == MiVadLarge)
    {
        if (Vad->Segment != NULL)
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return STATUS_UNABLE_TO_DELETE_SECTION;
        }
        if (FreeType != MI_MEM_RELEASE || *BaseAddress != MI_VAD_START(Vad) || *RegionSize != 0)
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return FreeType == MI_MEM_RELEASE ? STATUS_UNABLE_TO_FREE_VM : STATUS_UNABLE_TO_DECOMMIT_VM;
        }
        *RegionSize = MI_VAD_END(Vad) - MI_VAD_START(Vad);
        MiReleaseLargePagesLocked(Space, Vad);
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_SUCCESS;
    }

    if (Vad->Type != MiVadPrivate)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_UNABLE_TO_DELETE_SECTION;
    }

    if (*RegionSize == 0)
    {
        if (Start != MI_VAD_START(Vad))
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return STATUS_FREE_VM_NOT_AT_BASE;
        }

        End = MI_VAD_END(Vad);
    }
    else
    {
        End = MI_PAGE_ALIGN_UP(*BaseAddress + *RegionSize);
        if (End > MI_VAD_END(Vad) || End <= Start)
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return (FreeType == MI_MEM_RELEASE) ? STATUS_UNABLE_TO_FREE_VM : STATUS_UNABLE_TO_DECOMMIT_VM;
        }
    }

    if (FreeType == MI_MEM_DECOMMIT)
    {
        Returned = MiDeleteRange(Space, Vad, Start, End, TRUE);
        Vad->CommitCharge -= Returned;
        MiReturnCommit(Space, Returned);
        *BaseAddress = Start;
        *RegionSize = End - Start;
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_SUCCESS;
    }

    if (Start != MI_VAD_START(Vad) && End != MI_VAD_END(Vad))
    {
        PMI_VAD Tail = MI_ALLOCATE(sizeof(MI_VAD));
        ULONG64 OldEndVpn = Vad->Node.EndingVpn;
        ULONG64 Va;
        LONG64 TailCommit = 0;

        if (Tail == NULL)
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        for (Va = End; Va < MI_VAD_END(Vad); Va += PAGE_SIZE)
        {
            BOOLEAN Committed;
            ULONG Current;

            MiPageStatus(Space, Vad, Va, FALSE, &Committed, &Current);
            TailCommit += Committed ? 1 : 0;
        }

        Returned = MiDeleteRange(Space, Vad, Start, End, FALSE);

        *Tail = *Vad;
        MiVadRemove(&Space->VadRoot, &Vad->Node);
        Vad->Node.EndingVpn = (Start >> PAGE_SHIFT) - 1;
        Vad->CommitCharge -= Returned + TailCommit;
        MiVadInsert(&Space->VadRoot, &Vad->Node);

        RtlZeroMemory(&Tail->Node, sizeof(Tail->Node));
        Tail->Node.StartingVpn = End >> PAGE_SHIFT;
        Tail->Node.EndingVpn = OldEndVpn;
        Tail->CommitCharge = TailCommit;
        MiVadInsert(&Space->VadRoot, &Tail->Node);
    }
    else
    {
        Returned = MiDeleteRange(Space, Vad, Start, End, FALSE);
        Vad->CommitCharge -= Returned;

        MiVadRemove(&Space->VadRoot, &Vad->Node);

        if (Start == MI_VAD_START(Vad) && End == MI_VAD_END(Vad))
        {
            if (!MiVadCacheFree(Space, Vad))
                ReleasedVad = Vad;
        }
        else
        {
            if (Start == MI_VAD_START(Vad))
                Vad->Node.StartingVpn = End >> PAGE_SHIFT;
            else
                Vad->Node.EndingVpn = (Start >> PAGE_SHIFT) - 1;

            MiVadInsert(&Space->VadRoot, &Vad->Node);
        }
    }

    MiReturnCommit(Space, Returned);
    *BaseAddress = Start;
    *RegionSize = End - Start;
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    if (ReleasedVad != NULL)
        MI_FREE(ReleasedVad);
    return STATUS_SUCCESS;
}

NTSTATUS
MiProtectVirtualMemoryEx(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PULONG64 BaseAddress,
    _Inout_ PULONG64 RegionSize,
    _In_ ULONG NewProtection,
    _Out_ PULONG OldProtection,
    _In_ BOOLEAN DenyDynamicCode)
{
    ULONG64 Start = MI_PAGE_ALIGN_DOWN(*BaseAddress);
    ULONG64 End = MI_PAGE_ALIGN_UP(*BaseAddress + *RegionSize);
    NTSTATUS Status = STATUS_SUCCESS;
    PMI_VAD Vad;
    ULONG64 Va;

    *OldProtection = 0;

    if (!MiProtectionIsValid(NewProtection))
        return STATUS_INVALID_PAGE_PROTECTION;

    if (*RegionSize == 0 || End <= Start)
        return STATUS_INVALID_PARAMETER;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Vad = MiVadLocate(Space, Start);
    if (Vad == NULL || End > MI_VAD_END(Vad))
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    if (Vad->Type == MiVadAwe)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    if (DenyDynamicCode && MI_PROT_IS_EXECUTE(NewProtection) && Vad->Type != MiVadImage && !Vad->EcCode)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_DYNAMIC_CODE_BLOCKED;
    }

    if (Vad->Type == MiVadPrivate && MI_PROT_IS_COPY(NewProtection))
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    if (Vad->Type == MiVadLarge)
    {
        Status = MiProtectLargePagesLocked(Space, Start, End, NewProtection, OldProtection);
        if (NT_SUCCESS(Status))
        {
            *BaseAddress = Start;
            *RegionSize = End - Start;
        }
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return Status;
    }

    for (Va = Start; Va < End; Va += PAGE_SIZE)
    {
        BOOLEAN Committed;
        ULONG Current;

        MiPageStatus(Space, Vad, Va, FALSE, &Committed, &Current);
        if (!Committed)
        {
            BOOLEAN Reserved = (BOOLEAN)(Vad->Type != MiVadPrivate && !MiViewPageCommitted(Vad, Va));

            if (Reserved && Va == Start)
                *OldProtection = Vad->Protection;

            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return Reserved ? STATUS_SECTION_PROTECTION : STATUS_NOT_COMMITTED;
        }

        if (Va == Start)
            *OldProtection = Current;
    }

    if (MI_VAD_IS_DIRECT(Vad))
    {
        Status = STATUS_CONFLICTING_ADDRESSES;
    }
    else if (Vad->Type != MiVadPrivate)
    {
        Status = MiProtectMappedView(Space, Vad, Start, End, NewProtection);
    }
    else
    {
        Vad->PteTouched = TRUE;
        Status = MiSetPrivateRangeProtection(Space, Start, End, NewProtection);
    }

    if (NT_SUCCESS(Status) && Space->TrackExecutableWrites &&
        MI_PROT_IS_EXECUTE(NewProtection) && MI_PROT_IS_WRITABLE(NewProtection))
    {
        MiArmExecutableWriteRangeLocked(Space, Start, End);
    }

    *BaseAddress = Start;
    *RegionSize = End - Start;
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}

NTSTATUS
MiProtectVirtualMemory(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PULONG64 BaseAddress,
    _Inout_ PULONG64 RegionSize,
    _In_ ULONG NewProtection,
    _Out_ PULONG OldProtection)
{
    return MiProtectVirtualMemoryEx(Space, BaseAddress, RegionSize, NewProtection, OldProtection, FALSE);
}

NTSTATUS
MiQueryVirtualMemory(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Address,
    _Out_ PMI_MEMORY_INFORMATION Information)
{
    ULONG64 Start = MI_PAGE_ALIGN_DOWN(Address);
    BOOLEAN Committed;
    ULONG Protection;
    PMI_VAD Vad;
    ULONG64 Va;

    RtlZeroMemory(Information, sizeof(*Information));

    if (Address < Space->LowestVa || Address > Space->HighestVa)
        return STATUS_INVALID_PARAMETER;

    MI_RW_ACQUIRE_SHARED(&Space->Lock);

    Vad = MiVadLocate(Space, Start);
    if (Vad == NULL)
    {
        PMI_VAD_NODE Node;
        ULONG64 NextStart = Space->HighestVa + 1;

        for (Node = MiVadFirst(&Space->VadRoot); Node != NULL; Node = MiVadNext(Node))
        {
            if ((Node->StartingVpn << PAGE_SHIFT) > Start)
            {
                NextStart = Node->StartingVpn << PAGE_SHIFT;
                break;
            }
        }

        Information->BaseAddress = Start;
        Information->RegionSize = NextStart - Start;
        Information->State = MI_MEM_FREE;
        MI_RW_RELEASE_SHARED(&Space->Lock);
        return STATUS_SUCCESS;
    }

    MiPageStatus(Space, Vad, Start, TRUE, &Committed, &Protection);
    Information->BaseAddress = Start;
    Information->AllocationBase = MI_VAD_START(Vad);
    Information->AllocationProtect = Vad->Protection;
    Information->State = Committed ? MI_MEM_COMMIT : MI_MEM_RESERVE;
    Information->Protect = Protection;
    Information->Type = (Vad->Type == MiVadPrivate || (Vad->Type == MiVadLarge && Vad->Segment == NULL) ||
                         Vad->Type == MiVadAwe || Vad->Type == MiVadRotate || Vad->LockedPages) ? MI_MEM_PRIVATE
                                                    : ((Vad->Type == MiVadImage) ? MI_MEM_IMAGE : MI_MEM_MAPPED);

    Va = Start + PAGE_SIZE;
    while (Va < MI_VAD_END(Vad))
    {
        BOOLEAN NextCommitted;
        ULONG NextProtection;

        MiPageStatus(Space, Vad, Va, TRUE, &NextCommitted, &NextProtection);
        if (NextCommitted != Committed || NextProtection != Protection)
            break;

        if (Vad->Type != MiVadImage && (Vad->Segment == NULL || !Vad->Segment->Reserved) &&
            MiPtLookup(Space, Va, NULL) == NULL)
        {
            ULONG64 Next = MiPtNextTableBoundary(Space, Va);

            Va = (Next < MI_VAD_END(Vad)) ? Next : MI_VAD_END(Vad);
            continue;
        }

        Va += PAGE_SIZE;
    }

    Information->RegionSize = Va - Start;
    MI_RW_RELEASE_SHARED(&Space->Lock);
    return STATUS_SUCCESS;
}

VOID
MiCleanAddressSpace(
    _Inout_ PMI_ADDRESS_SPACE Space)
{
    PMI_VAD_NODE Node;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    MiPtFlushEmpty(Space);

    while ((Node = MiVadFirst(&Space->VadRoot)) != NULL)
    {
        PMI_VAD Vad = CONTAINING_RECORD(Node, MI_VAD, Node);

        if (Vad->Type == MiVadAwe)
        {
            MiAweReleaseWindowLocked(Space, Vad);
        }
        else if (Vad->Type == MiVadLarge)
        {
            PMI_SEGMENT Segment = MiReleaseLargePagesLocked(Space, Vad);

            if (Segment != NULL)
                MiSegmentDereference(Segment);
        }
        else if (Vad->Type == MiVadPhysical)
        {
            MiUnmapFramesUserLocked(Space, Vad);
        }
        else if (Vad->Type == MiVadRotate)
        {
            MiRotateReleaseLocked(Space, Vad);
        }
        else if (Vad->Type == MiVadSystem)
        {
            MiVadRemove(&Space->VadRoot, &Vad->Node);
            MI_FREE(Vad);
        }
        else if (Vad->Type == MiVadPrivate)
        {
            LONG64 Returned = MiDeleteRange(Space, Vad, MI_VAD_START(Vad), MI_VAD_END(Vad), FALSE);

            MiReturnCommit(Space, Returned);
            MiVadRemove(&Space->VadRoot, &Vad->Node);
            MI_FREE(Vad);
        }
        else
        {
            MiRemoveMappedView(Space, Vad);
        }
    }

    MiAweDestroyPagesLocked(Space);
    MiVadFlushCache(Space);
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
}

VOID
MiUnmapFramesUserLocked(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_VAD Vad)
{
    ULONG64 Va;

    for (Va = MI_VAD_START(Vad); Va < MI_VAD_END(Vad); Va += PAGE_SIZE)
    {
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);

        if (Slot != NULL && MiArchPteRead(Slot) != 0)
            MiPtWrite(Space, Va, Slot, TableFrame, 0);
    }

    MiArchTlbInvalidate(MI_VAD_START(Vad), (MI_VAD_END(Vad) - MI_VAD_START(Vad)) >> PAGE_SHIFT, TRUE);
    MiVadRemove(&Space->VadRoot, &Vad->Node);
    MI_FREE(Vad);
}

NTSTATUS
MiMapFramesUser(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ const MI_FRAME_NUMBER *Frames,
    _In_ ULONG PageCount,
    _In_ ULONG Protection,
    _In_ ULONG LeafFlags,
    _In_ BOOLEAN LockedPages,
    _Inout_ PULONG64 BaseAddress)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG64 Start = *BaseAddress;
    PMI_VAD Vad;
    ULONG i;

    if (PageCount == 0 || !MI_PROT_IS_ACCESSIBLE(Protection) || (Start & (PAGE_SIZE - 1)))
        return STATUS_INVALID_PARAMETER;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    if (Start == 0)
    {
        ULONG64 Vpn;

        if (!MiSpaceFindEmptyRange(Space, PageCount, MI_ALLOCATION_GRANULARITY >> PAGE_SHIFT, 0, ~0ULL, FALSE, &Vpn))
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return STATUS_NO_MEMORY;
        }

        Start = Vpn << PAGE_SHIFT;
    }
    else if (Start < Space->LowestVa || Start + ((ULONG64)PageCount << PAGE_SHIFT) - 1 > Space->HighestVa ||
             MiVadFindOverlap(&Space->VadRoot, Start >> PAGE_SHIFT, (Start >> PAGE_SHIFT) + PageCount - 1) != NULL)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    Vad = MiVadCreate(Space, Start, Start + ((ULONG64)PageCount << PAGE_SHIFT), Protection, MiVadPhysical);
    if (Vad == NULL)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Vad->LockedPages = LockedPages;

    for (i = 0; i < PageCount; i++)
    {
        ULONG64 Va = Start + (ULONG64)i * PAGE_SIZE;
        ULONG TableFrame;
        ULONG PageFlags = LockedPages ? MiPfnMappingFlags(&Space->System->Pfn, Frames[i], LeafFlags) : LeafFlags;
        PMI_PTE Slot = MiPtEnsure(Space, Va, &TableFrame);

        if (Slot == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        MiPtWrite(Space, Va, Slot, TableFrame,
                  MiArchPteMakeLeaf(Frames[i], Protection,
                                    PageFlags | MI_LEAF_DIRTY | (Space->IsSystem ? MI_LEAF_GLOBAL : MI_LEAF_USER)));
    }

    if (!NT_SUCCESS(Status))
        MiUnmapFramesUserLocked(Space, Vad);
    else
        *BaseAddress = Start;

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}

NTSTATUS
MiUnmapFramesUser(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 BaseAddress,
    _In_ BOOLEAN LockedPages)
{
    PMI_VAD Vad;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Vad = MiVadLocate(Space, BaseAddress);
    if (Vad == NULL || Vad->Type != MiVadPhysical)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_NOT_MAPPED_VIEW;
    }

    if (Vad->LockedPages != LockedPages)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return LockedPages ? STATUS_NOT_MAPPED_VIEW : STATUS_INVALID_PAGE_PROTECTION;
    }

    MiUnmapFramesUserLocked(Space, Vad);
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return STATUS_SUCCESS;
}
