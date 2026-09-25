/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/fault/fault.c
 * PURPOSE:     Virtual memory fault handling
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

PMI_VAD
MiVadLocate(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress)
{
    PMI_VAD_NODE Node = MiVadFind(&Space->VadRoot, VirtualAddress >> PAGE_SHIFT);

    return (Node != NULL) ? CONTAINING_RECORD(Node, MI_VAD, Node) : NULL;
}

VOID
MiRepurposeStandbyPage(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    PMI_PFN Entry = &Db->Pfn[Frame];
    PMI_PTE Slot;

    if (Entry->PteAddress == 0)
        return;

    if (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PROTOTYPE)
    {
        Slot = (PMI_PTE)(ULONG_PTR)Entry->PteAddress;
    }
    else
    {
        Slot = (PMI_PTE)((PUCHAR)MiArchMapFrame(Entry->PteAddress >> PAGE_SHIFT) +
                         (Entry->PteAddress & (PAGE_SIZE - 1)));

        if (MiSoftKind(Entry->OriginalPte) == MiSoftDemandZero)
        {
            PMI_ADDRESS_SPACE Space = Db->Pfn[Entry->PteFrame].PageTableOwner;

            MI_ASSERT(Space != NULL);
            MI_ATOMIC_ADD64(&Space->PrivatePages, -1);
        }
    }

    MiArchPteWrite(Slot, Entry->OriginalPte);
    Entry->PteAddress = 0;
}

static
VOID
MiReleaseBackingStore(
    _Inout_ PMI_SYSTEM System,
    _In_ MI_PTE SoftPte)
{
    if (System->PageFile != NULL && MiSoftKind(SoftPte) == MiSoftPageFile)
        MiPageFileReleaseSlot(System->PageFile, MiSoftValue(SoftPte));
}

VOID
MiReleasePageBacking(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG Frame)
{
    PMI_PFN Entry = &System->Pfn.Pfn[Frame];
    BOOLEAN InFlight;
    MI_PTE Original;
    KIRQL OldIrql;

    OldIrql = MiPfnLock(&System->Pfn, Frame);
    InFlight = (BOOLEAN)((MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_IN_FLIGHT) != 0);
    MI_ATOMIC_OR8(&Entry->Flags, MI_PFN_FLAG_DELETED);
    Original = Entry->OriginalPte;
    MiPfnUnlock(&System->Pfn, Frame, OldIrql);

    if (!InFlight)
        MiReleaseBackingStore(System, Original);
}

VOID
MiDeletePte(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ MI_PTE NewValue)
{
    PMI_SYSTEM System = Space->System;
    ULONG64 SlotAddress = MiPtSlotAddress(Slot, TableFrame, VirtualAddress);
    MI_PTE Pte = MiArchPteRead(Slot);

    if (MiCloneDeletePage(Space, VirtualAddress, Slot, TableFrame, NewValue))
        return;

    if (Pte == 0 && NewValue == 0)
        return;

    if (MiArchPteIsValid(Pte))
    {
        ULONG Frame = (ULONG)MiArchPteFrame(Pte);
        PMI_PFN Entry = &System->Pfn.Pfn[Frame];
        BOOLEAN Private = !(MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PROTOTYPE);

        MiPtWrite(Space, VirtualAddress, Slot, TableFrame, NewValue);

        if (Private)
            MI_ATOMIC_ADD64(&Space->PrivatePages, -1);

        if (Space->TlbBatch != NULL)
        {
            MiTlbBatchAdd(Space, VirtualAddress, Frame, NULL, NULL, Private);
        }
        else
        {
            MiArchTlbInvalidate(VirtualAddress, 1, TRUE);

            if (Private)
                MiReleasePageBacking(System, Frame);

            MiPfnShareDecrement(&System->Pfn, Frame, Private);
        }

        MI_ATOMIC_ADD64(&Space->ResidentPages, -1);
        return;
    }

    if (MiSoftKind(Pte) == MiSoftTransition)
    {
        ULONG Frame = (ULONG)MiSoftValue(Pte);

        if (MiPfnReactivate(&System->Pfn, Frame, SlotAddress))
        {
            MI_ATOMIC_ADD64(&Space->PrivatePages, -1);
            MiReleasePageBacking(System, Frame);
            MiPtWrite(Space, VirtualAddress, Slot, TableFrame, NewValue);
            MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
            return;
        }

        Pte = MiArchPteRead(Slot);
    }

    if (Pte == 0 || MiSoftKind(Pte) == MiSoftDemandZero || MiSoftKind(Pte) == MiSoftDecommitted)
    {
        MiPtWrite(Space, VirtualAddress, Slot, TableFrame, NewValue);
        return;
    }

    MI_ATOMIC_ADD64(&Space->PageFileGeneration, 1);

    if (MiSoftKind(Pte) == MiSoftPageFile || MiSoftKind(Pte) == MiSoftTransition)
        MI_ATOMIC_ADD64(&Space->PrivatePages, -1);

    MiReleaseBackingStore(System, Pte);
    MiPtWrite(Space, VirtualAddress, Slot, TableFrame, NewValue);
}

static
NTSTATUS
MiCheckSoftAccess(
    _In_ ULONG Protection,
    _In_ MI_FAULT_ACCESS Access)
{
    if (!MI_PROT_IS_ACCESSIBLE(Protection))
        return STATUS_ACCESS_VIOLATION;

    if (Access == MiFaultWrite && !MI_PROT_IS_WRITABLE(Protection) && !MI_PROT_IS_COPY(Protection))
        return STATUS_ACCESS_VIOLATION;

    if (Access == MiFaultExecute && !MI_PROT_IS_EXECUTE(Protection))
        return STATUS_ACCESS_VIOLATION;

    return STATUS_SUCCESS;
}

static
NTSTATUS
MiResolveValidFault(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ MI_PTE Pte,
    _In_ MI_FAULT_ACCESS Access,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN ManagedWrite,
    _In_ BOOLEAN AllowExecutableWrite)
{
    ULONG Frame = (ULONG)MiArchPteFrame(Pte);
    MI_PTE Updated = Pte;

    if (UserMode && !MiArchPteIsUser(Pte))
        return STATUS_ACCESS_VIOLATION;

    if (Access == MiFaultExecute && !MiArchPteIsExecutable(Pte, UserMode))
        return STATUS_ACCESS_VIOLATION;

    if (Access == MiFaultWrite)
    {
        if (MiArchPteIsCopyOnWrite(Pte))
            return STATUS_PENDING_COPY;

        if (!MiArchPteIsWritable(Pte))
            return STATUS_ACCESS_VIOLATION;

        if (ManagedWrite && !AllowExecutableWrite &&
            !MiArchPteIsDirty(Pte) && MiArchPteIsExecutable(Pte, TRUE))
        {
            return STATUS_EXECUTABLE_MEMORY_WRITE;
        }

        if (!MiArchPteIsDirty(Pte))
        {
            Updated = MiArchPteSetDirty(Updated, TRUE);
            MiPfnSetModified(&Space->System->Pfn, Frame);
            MI_ATOMIC_ADD64(&Space->DirtyFaults, 1);
        }
    }

    if (!MiArchPteIsAccessed(Pte))
    {
        Updated = MiArchPteSetAccessed(Updated, TRUE);
        MI_ATOMIC_ADD64(&Space->AccessFaults, 1);
    }

    if (Updated != Pte)
        MiArchPteWrite(Slot, Updated);

    MiArchTlbInvalidate(VirtualAddress, 1, FALSE);
    return STATUS_SUCCESS;
}

NTSTATUS
MiMakePageValid(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ ULONG Frame,
    _In_ ULONG Protection,
    _In_ BOOLEAN Dirty)
{
    ULONG Flags = (Space->IsSystem ? MI_LEAF_GLOBAL : MI_LEAF_USER) | (Dirty ? MI_LEAF_DIRTY : 0);

    if (Dirty)
        MiPfnSetModified(&Space->System->Pfn, Frame);

    MiPtWrite(Space, VirtualAddress, Slot, TableFrame,
              MiArchPteMakeLeaf(Frame, Protection & ~MI_PROT_GUARD, Flags));
    MI_ATOMIC_ADD64(&Space->ResidentPages, 1);
    return STATUS_SUCCESS;
}

#define MI_FAULT_PAGE_IN_ATTEMPTS 64

NTSTATUS
MiFaultWithWriteAllowance(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ MI_FAULT_ACCESS Access,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN AllowExecutableWrite)
{
    PMI_SYSTEM System = Space->System;
    ULONG64 PageVa = VirtualAddress & ~((ULONG64)PAGE_SIZE - 1);
    NTSTATUS Status;
    ULONG TableFrame;
    ULONG Protection;
    ULONG Frame;
    PMI_PTE Slot;
    PMI_VAD Vad;
    MI_PTE Pte;
    MI_PTE RawPte;
    ULONG64 SlotAddress;
    ULONG Attempts = 0;
    ULONG PageIns = 0;
    KIRQL OldIrql;
    BOOLEAN ManagedWrite;
    BOOLEAN WasValid = FALSE;

    if (VirtualAddress < Space->LowestVa || VirtualAddress > Space->HighestVa ||
        MiArchIsSelfMapAddress(VirtualAddress))
    {
        return STATUS_ACCESS_VIOLATION;
    }

    MI_ATOMIC_ADD64(&Space->Faults, 1);

RetryAddress:
    MI_RW_ACQUIRE_SHARED(&Space->Lock);
    Vad = MiVadLocate(Space, PageVa);
    if (Vad == NULL)
    {
        MI_RW_RELEASE_SHARED(&Space->Lock);
        return STATUS_ACCESS_VIOLATION;
    }

    ManagedWrite = (BOOLEAN)(Access == MiFaultWrite && UserMode &&
                             Space->TrackExecutableWrites && !Vad->EcCode);

    if (!MI_ATOMIC_READ32(&Vad->PteTouched))
        MI_ATOMIC_CAS32(&Vad->PteTouched, TRUE, FALSE);

    if (MiCloneLookup(Space, PageVa) != NULL)
    {
        MI_RW_RELEASE_SHARED(&Space->Lock);
        Status = MiCloneFault(Space, PageVa, Access);
        if (Status == STATUS_PENDING_COPY)
            goto RetryAddress;
        return Status;
    }

    Slot = Vad->Type == MiVadLarge
        ? MiPtLookupLevel(Space, PageVa, System->Arch->LargePageLevel, &TableFrame)
        : MiPtLookup(Space, PageVa, &TableFrame);
    if (Slot == NULL)
    {
        if (MI_VAD_IS_DIRECT(Vad) || (Vad->Type == MiVadPrivate && !Vad->MemCommit))
        {
            MI_RW_RELEASE_SHARED(&Space->Lock);
            return STATUS_ACCESS_VIOLATION;
        }

        Protection = (Vad->Type == MiVadPrivate) ? Vad->Protection
                                                   : MiViewPageProtection(Space, Vad, PageVa, 0);
        Status = MiCheckSoftAccess(Protection, Access);
        if (!NT_SUCCESS(Status) && (Protection & MI_PROT_NOACCESS) != MI_PROT_GUARD)
        {
            MI_RW_RELEASE_SHARED(&Space->Lock);
            return Status;
        }

        Slot = MiPtEnsure(Space, PageVa, &TableFrame);
        if (Slot == NULL)
        {
            MI_RW_RELEASE_SHARED(&Space->Lock);
            Status = STATUS_NO_MEMORY;
            goto Failed;
        }
    }

    OldIrql = MiPfnLock(&System->Pfn, TableFrame);

RetryPage:
    RawPte = Pte = MiArchPteRead(Slot);

    if (MiArchPteIsValid(Pte))
    {
        WasValid = TRUE;
        if (!MiArchPteIsLeafDescriptor(Pte) &&
            !(Vad->Type == MiVadLarge && MiArchPteIsBlock(Pte, System->Arch->LargePageLevel)))
        {
            Status = STATUS_ACCESS_VIOLATION;
            goto Complete;
        }

        Status = MiResolveValidFault(Space, PageVa, Slot, Pte, Access, UserMode,
                                     ManagedWrite, AllowExecutableWrite);
        if (Status == STATUS_PENDING_COPY)
        {
            WasValid = FALSE;
            Status = MiCopyOnWrite(Space, Vad, PageVa, Slot, TableFrame, Pte);
        }
        goto Complete;
    }

    if (MI_VAD_IS_DIRECT(Vad))
    {
        Status = STATUS_ACCESS_VIOLATION;
        goto Complete;
    }

    if (Vad->Type != MiVadPrivate && (Pte == 0 || MiSoftKind(Pte) == MiSoftPrototype))
    {
        PMI_SEGMENT Segment = Vad->Segment;
        ULONG64 Page = Vad->SegmentPageOffset + ((PageVa >> PAGE_SHIFT) - Vad->Node.StartingVpn);

        Status = MiResolvePrototypeFault(Space, Vad, PageVa, Slot, TableFrame, Access);
        if (Status != STATUS_PENDING_PAGE_IN)
            goto Complete;

        MiSegmentReference(Segment);
        MiPfnUnlock(&System->Pfn, TableFrame, OldIrql);
        MI_RW_RELEASE_SHARED(&Space->Lock);

        Status = MiSegmentFaultIn(Segment, Page);
        MiSegmentDereference(Segment);
        if (!NT_SUCCESS(Status))
            goto Failed;
        if (++PageIns >= MI_FAULT_PAGE_IN_ATTEMPTS)
            return STATUS_NO_MEMORY;
        goto RetryAddress;
    }

    if (Pte == 0)
    {
        if (!Vad->MemCommit)
        {
            Status = STATUS_ACCESS_VIOLATION;
            goto Complete;
        }
        Pte = MiSoftMake(MiSoftDemandZero, Vad->Protection, 0);
    }

    if (MiSoftKind(Pte) == MiSoftDecommitted)
    {
        Status = STATUS_ACCESS_VIOLATION;
        goto Complete;
    }

    Protection = MiSoftProtection(Pte);
    if ((Protection & MI_PROT_NOACCESS) == MI_PROT_GUARD)
    {
        MI_PTE Cleared = MiSoftWithProtection(Pte, Protection & ~MI_PROT_GUARD);

        if (MiSoftKind(Pte) == MiSoftTransition)
        {
            KIRQL PageIrql;

            Frame = (ULONG)MiSoftValue(Pte);
            PageIrql = MiPfnLock(&System->Pfn, Frame);
            if (MiArchPteRead(Slot) == Pte)
                MiArchPteWrite(Slot, Cleared);
            MiPfnUnlock(&System->Pfn, Frame, PageIrql);
        }
        else
        {
            MiPtWrite(Space, PageVa, Slot, TableFrame, Cleared);
        }
        Status = STATUS_GUARD_PAGE_VIOLATION;
        goto Complete;
    }

    Status = MiCheckSoftAccess(Protection, Access);
    if (!NT_SUCCESS(Status))
        goto Complete;

    SlotAddress = MiPtSlotAddress(Slot, TableFrame, PageVa);
    switch (MiSoftKind(Pte))
    {
        case MiSoftDemandZero:
            MiPfnUnlock(&System->Pfn, TableFrame, OldIrql);
            Frame = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
            OldIrql = MiPfnLock(&System->Pfn, TableFrame);

            if (MiArchPteRead(Slot) != RawPte)
            {
                if (Frame != MI_FRAME_INVALID)
                    MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
                goto RetryPage;
            }
            if (Frame == MI_FRAME_INVALID)
            {
                Status = STATUS_NO_MEMORY;
                break;
            }

            MiPfnInitializePage(&System->Pfn, Frame, SlotAddress, TableFrame,
                                MiSoftMake(MiSoftDemandZero, Protection, 0), 0);
            Status = MiMakePageValid(Space, PageVa, Slot, TableFrame, Frame, Protection,
                                     (BOOLEAN)(Access == MiFaultWrite));
            MI_ATOMIC_ADD64(&Space->DemandZeroFaults, 1);
            MI_ATOMIC_ADD64(&Space->PrivatePages, 1);
            break;

        case MiSoftTransition:
            Frame = (ULONG)MiSoftValue(Pte);
            if (!MiPfnReactivate(&System->Pfn, Frame, SlotAddress))
            {
                if (++Attempts < 8)
                    goto RetryPage;
                Status = STATUS_IN_PAGE_ERROR;
                break;
            }

            System->Pfn.Pfn[Frame].PteFrame = TableFrame;
            Status = MiMakePageValid(Space, PageVa, Slot, TableFrame, Frame, Protection,
                                     (BOOLEAN)(Access == MiFaultWrite ||
                                               (MI_PFN_FLAGS(&System->Pfn.Pfn[Frame]) & MI_PFN_FLAG_MODIFIED)));
            MI_ATOMIC_ADD64(&Space->TransitionFaults, 1);
            break;

        case MiSoftPageFile:
        {
            LONG64 Generation;
            PVOID Mapping;

            if (System->PageFile == NULL)
            {
                Status = STATUS_IN_PAGE_ERROR;
                break;
            }

            MiPfnUnlock(&System->Pfn, TableFrame, OldIrql);
            Frame = MiPfnAllocatePage(&System->Pfn, 0);
            OldIrql = MiPfnLock(&System->Pfn, TableFrame);
            if (MiArchPteRead(Slot) != Pte)
            {
                if (Frame != MI_FRAME_INVALID)
                    MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
                goto RetryPage;
            }
            if (Frame == MI_FRAME_INVALID)
            {
                Status = STATUS_NO_MEMORY;
                break;
            }

            Generation = MI_ATOMIC_READ64(&Space->PageFileGeneration);
            MiPfnUnlock(&System->Pfn, TableFrame, OldIrql);
            MI_RW_RELEASE_SHARED(&Space->Lock);

            Mapping = MiArchMapFrame(Frame);
            Status = System->PageFile->Ops.Read(System->PageFile->Context, MiSoftValue(Pte), Mapping);
            MiArchUnmapFrame(Mapping);
            if (!NT_SUCCESS(Status))
            {
                MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
                return STATUS_IN_PAGE_ERROR;
            }

            MI_RW_ACQUIRE_SHARED(&Space->Lock);
            Vad = MiVadLocate(Space, PageVa);
            Slot = MiPtLookup(Space, PageVa, &TableFrame);
            if (Vad == NULL || Slot == NULL)
            {
                MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
                MI_RW_RELEASE_SHARED(&Space->Lock);
                if (Vad == NULL)
                    return STATUS_ACCESS_VIOLATION;
                if (++PageIns >= MI_FAULT_PAGE_IN_ATTEMPTS)
                    return STATUS_NO_MEMORY;
                goto RetryAddress;
            }

            OldIrql = MiPfnLock(&System->Pfn, TableFrame);
            if (Generation != MI_ATOMIC_READ64(&Space->PageFileGeneration) || MiArchPteRead(Slot) != Pte)
            {
                MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
                MiPfnUnlock(&System->Pfn, TableFrame, OldIrql);
                MI_RW_RELEASE_SHARED(&Space->Lock);
                if (++PageIns >= MI_FAULT_PAGE_IN_ATTEMPTS)
                    return STATUS_NO_MEMORY;
                goto RetryAddress;
            }

            SlotAddress = MiPtSlotAddress(Slot, TableFrame, PageVa);
            MI_ATOMIC_ADD64(&Space->PageFileGeneration, 1);
            MI_ATOMIC_ADD64(&System->PageFile->PagesRead, 1);
            MiPfnInitializePage(&System->Pfn, Frame, SlotAddress, TableFrame, Pte, 0);
            Status = MiMakePageValid(Space, PageVa, Slot, TableFrame, Frame, Protection,
                                     (BOOLEAN)(Access == MiFaultWrite));
            if (Access == MiFaultWrite)
            {
                MiPageFileReleaseSlot(System->PageFile, MiSoftValue(Pte));
                System->Pfn.Pfn[Frame].OriginalPte = MiSoftMake(MiSoftDemandZero, Protection, 0);
            }
            MI_ATOMIC_ADD64(&Space->PageFileFaults, 1);
            break;
        }

        default:
            Status = STATUS_ACCESS_VIOLATION;
            break;
    }

Complete:
    if (NT_SUCCESS(Status) && ManagedWrite && !AllowExecutableWrite && !WasValid)
    {
        Pte = MiArchPteRead(Slot);
        if (MiArchPteIsLeafDescriptor(Pte) && MiArchPteIsWritable(Pte) &&
            MiArchPteIsExecutable(Pte, TRUE))
        {
            if (MiArchPteIsDirty(Pte))
            {
                MiArchPteWrite(Slot, MiArchPteSetDirty(Pte, FALSE));
                MiArchTlbInvalidate(PageVa, 1, TRUE);
            }
            Status = STATUS_EXECUTABLE_MEMORY_WRITE;
        }
    }
    MiPfnUnlock(&System->Pfn, TableFrame, OldIrql);
    MI_RW_RELEASE_SHARED(&Space->Lock);
    if (NT_SUCCESS(Status) || Status == STATUS_GUARD_PAGE_VIOLATION ||
        Status == STATUS_EXECUTABLE_MEMORY_WRITE)
        return Status;

Failed:
    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    MiPtPruneEmpty(Space, PageVa);
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}

NTSTATUS
MiFault(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ MI_FAULT_ACCESS Access,
    _In_ BOOLEAN UserMode)
{
    return MiFaultWithWriteAllowance(Space, VirtualAddress, Access, UserMode, FALSE);
}
