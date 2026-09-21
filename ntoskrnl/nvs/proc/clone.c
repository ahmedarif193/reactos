/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/proc/clone.c
 * PURPOSE:     Process address-space cloning
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

typedef struct _MI_CLONE_WORK
{
    struct _MI_CLONE_WORK *Next;
    PMI_CLONE_REF Source;
    PMI_CLONE_REF Target;
    PMI_CLONE_PAGE Page;
    ULONG CopyFrame;
} MI_CLONE_WORK, *PMI_CLONE_WORK;

PMI_CLONE_REF
MiCloneLookup(PMI_ADDRESS_SPACE Space, ULONG64 Va)
{
    PMI_VAD_NODE Node = MiVadFind(&Space->CloneRoot, Va >> PAGE_SHIFT);

    return Node != NULL ? CONTAINING_RECORD(Node, MI_CLONE_REF, Node) : NULL;
}

static ULONG
MiCloneLeafProtection(ULONG Protection)
{
    if (MI_PROT_IS_WRITABLE(Protection))
        return (Protection & ~MI_PROT_ACCESS_MASK) |
               (MI_PROT_IS_EXECUTE(Protection) ? MI_PROT_EXECUTE_WRITECOPY : MI_PROT_WRITECOPY);
    return Protection;
}

static VOID
MiCloneDropShare(PMI_CLONE_PAGE Page, ULONG Frame)
{
    MiPfnShareDecrementEx(&Page->System->Pfn, Frame, FALSE, &Page->Proto,
                          MiSoftMake(MiSoftTransition, MI_PROT_READWRITE, Frame));
}

static VOID
MiCloneDereference(PMI_CLONE_PAGE Page)
{
    PMI_SYSTEM System = Page->System;

    if (MI_ATOMIC_ADD32(&Page->References, -1) - 1 != 0)
        return;

    for (;;)
    {
        MI_PTE Pte = MiArchPteRead(&Page->Proto);
        ULONG Frame = (ULONG)MiSoftValue(Pte);

        MI_ASSERT(MiSoftKind(Pte) != MiSoftResident);
        if (MiSoftKind(Pte) == MiSoftTransition)
        {
            if (!MiPfnReactivate(&System->Pfn, Frame, (ULONG64)(ULONG_PTR)&Page->Proto))
                continue;
            MiReleasePageBacking(System, Frame);
            MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
        }
        else if (MiSoftKind(Pte) == MiSoftPageFile)
        {
            MiPageFileReleaseSlot(System->PageFile, MiSoftValue(Pte));
        }
        break;
    }
    MI_FREE(Page);
}

static VOID
MiCloneReleaseMapping(PMI_ADDRESS_SPACE Space, PMI_CLONE_REF Ref, ULONG64 Va,
                       PMI_PTE Slot, ULONG TableFrame, MI_PTE NewValue)
{
    MI_PTE Pte = MiArchPteRead(Slot);

    MiPtWrite(Space, Va, Slot, TableFrame, NewValue);
    if (MiArchPteIsValid(Pte))
    {
        ULONG Frame = (ULONG)MiArchPteFrame(Pte);

        MiArchTlbInvalidate(Va, 1, TRUE);
        if (MiArchPteIsDirty(Pte))
            MiPfnSetModified(&Space->System->Pfn, Frame);
        MiCloneDropShare(Ref->Page, Frame);
        MI_ATOMIC_ADD64(&Space->ResidentPages, -1);
    }
}

BOOLEAN
MiCloneDeletePage(PMI_ADDRESS_SPACE Space, ULONG64 Va, PMI_PTE Slot, ULONG TableFrame, MI_PTE NewValue)
{
    PMI_CLONE_REF Ref = MiCloneLookup(Space, Va);

    if (Ref == NULL)
        return FALSE;
    MiCloneReleaseMapping(Space, Ref, Va, Slot, TableFrame, NewValue);
    MiVadRemove(&Space->CloneRoot, &Ref->Node);
    MiCloneDereference(Ref->Page);
    MI_FREE(Ref);
    return TRUE;
}

BOOLEAN
MiCloneProtectPage(PMI_ADDRESS_SPACE Space, ULONG64 Va, PMI_PTE Slot, ULONG TableFrame, ULONG Protection)
{
    PMI_CLONE_REF Ref = MiCloneLookup(Space, Va);

    if (Ref == NULL)
        return FALSE;
    Ref->Protection = Protection;
    MiCloneReleaseMapping(Space, Ref, Va, Slot, TableFrame, MiSoftMake(MiSoftPrototype, Protection, 0));
    return TRUE;
}

BOOLEAN
MiCloneTrimPage(PMI_ADDRESS_SPACE Space, ULONG64 Va, PMI_PTE Slot, ULONG TableFrame)
{
    PMI_CLONE_REF Ref = MiCloneLookup(Space, Va);

    if (Ref == NULL)
        return FALSE;
    MiCloneReleaseMapping(Space, Ref, Va, Slot, TableFrame,
                          MiSoftMake(MiSoftPrototype, Ref->Protection, 0));
    return TRUE;
}

static BOOLEAN
MiCloneAcquirePage(PMI_CLONE_PAGE Page, PULONG Frame)
{
    MI_PTE Pte = MiArchPteRead(&Page->Proto);

    *Frame = (ULONG)MiSoftValue(Pte);
    if (MiSoftKind(Pte) == MiSoftResident)
        return MiPfnShareIncrementIfMapped(&Page->System->Pfn, *Frame, &Page->Proto, Pte);
    if (MiSoftKind(Pte) == MiSoftTransition)
        return MiPfnReactivateEx(&Page->System->Pfn, *Frame, (ULONG64)(ULONG_PTR)&Page->Proto,
                                 &Page->Proto, Pte, MiSoftMake(MiSoftResident, MI_PROT_READWRITE, *Frame));
    return FALSE;
}

static NTSTATUS
MiCloneMakeResident(PMI_CLONE_PAGE Page)
{
    PMI_SYSTEM System = Page->System;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Frame;
    MI_PTE Pte;

    MI_MUTEX_ACQUIRE(&Page->Lock);
    if (MiCloneAcquirePage(Page, &Frame))
    {
        MiCloneDropShare(Page, Frame);
        goto Done;
    }

    Pte = MiArchPteRead(&Page->Proto);
    if (MiSoftKind(Pte) != MiSoftDemandZero && MiSoftKind(Pte) != MiSoftPageFile)
        goto Done;

    Frame = MiPfnAllocatePage(&System->Pfn, MiSoftKind(Pte) == MiSoftDemandZero ? MI_ALLOCATE_ZEROED : 0);
    if (Frame == MI_FRAME_INVALID)
    {
        Status = STATUS_NO_MEMORY;
        goto Done;
    }
    if (MiSoftKind(Pte) == MiSoftPageFile)
    {
        PVOID Mapping = MiArchMapFrame(Frame);

        Status = System->PageFile != NULL
            ? System->PageFile->Ops.Read(System->PageFile->Context, MiSoftValue(Pte), Mapping)
            : STATUS_IN_PAGE_ERROR;
        MiArchUnmapFrame(Mapping);
        if (!NT_SUCCESS(Status))
        {
            MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
            Status = STATUS_IN_PAGE_ERROR;
            goto Done;
        }
        MI_ATOMIC_ADD64(&System->PageFile->PagesRead, 1);
    }
    MiPfnInitializePage(&System->Pfn, Frame, (ULONG64)(ULONG_PTR)&Page->Proto, 0, Pte, MI_PFN_FLAG_PROTOTYPE);
    MiArchPteWrite(&Page->Proto, MiSoftMake(MiSoftResident, MI_PROT_READWRITE, Frame));
    MiCloneDropShare(Page, Frame);
Done:
    MI_MUTEX_RELEASE(&Page->Lock);
    return Status;
}

NTSTATUS
MiCloneFault(PMI_ADDRESS_SPACE Space, ULONG64 Va, ULONG Access)
{
    PMI_SYSTEM System = Space->System;
    ULONG Attempts = 0;
    NTSTATUS Status;

    for (;;)
    {
        PMI_CLONE_REF Ref;
        PMI_CLONE_PAGE Page;
        PMI_PTE Slot;
        ULONG TableFrame, Frame, Protection;
        MI_PTE Pte;

        MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
        Ref = MiCloneLookup(Space, Va);
        if (Ref == NULL)
        {
            Status = STATUS_PENDING_COPY;
            break;
        }
        Page = Ref->Page;
        Slot = MiPtLookup(Space, Va, &TableFrame);
        MI_ASSERT(Slot != NULL);
        Protection = Ref->Protection;
        if ((Protection & MI_PROT_NOACCESS) == MI_PROT_GUARD)
        {
            MiCloneProtectPage(Space, Va, Slot, TableFrame, Protection & ~MI_PROT_GUARD);
            Status = STATUS_GUARD_PAGE_VIOLATION;
            break;
        }
        if (!MI_PROT_IS_ACCESSIBLE(Protection) ||
            (Access == MiFaultWrite && !MI_PROT_IS_WRITABLE(Protection) && !MI_PROT_IS_COPY(Protection)) ||
            (Access == MiFaultExecute && !MI_PROT_IS_EXECUTE(Protection)))
        {
            Status = STATUS_ACCESS_VIOLATION;
            break;
        }
        Pte = MiArchPteRead(Slot);
        if (!MiArchPteIsValid(Pte))
        {
            if (!MiCloneAcquirePage(Page, &Frame))
            {
                MI_ATOMIC_ADD32(&Page->References, 1);
                MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
                Status = MiCloneMakeResident(Page);
                MiCloneDereference(Page);
                if (!NT_SUCCESS(Status))
                    return Status;
                if (++Attempts == 64)
                    return STATUS_NO_MEMORY;
                continue;
            }
            MiMakePageValid(Space, Va, Slot, TableFrame, Frame, MiCloneLeafProtection(Protection), FALSE);
            Pte = MiArchPteRead(Slot);
            MI_ATOMIC_ADD64(&Space->PrototypeFaults, 1);
        }
        Frame = (ULONG)MiArchPteFrame(Pte);
        if (Access == MiFaultWrite)
        {
            ULONG NewFrame = MiPfnAllocatePage(&System->Pfn, 0);
            PVOID Source, Target;

            if (NewFrame == MI_FRAME_INVALID)
            {
                Status = STATUS_NO_MEMORY;
                break;
            }
            Source = MiArchMapFrame(Frame);
            Target = MiArchMapFrame(NewFrame);
            RtlCopyMemory(Target, Source, PAGE_SIZE);
            MiArchUnmapFrame(Target);
            MiArchUnmapFrame(Source);
            Protection = (Protection & ~MI_PROT_ACCESS_MASK) |
                         (MI_PROT_IS_EXECUTE(Protection) ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE);
            MiPfnInitializePage(&System->Pfn, NewFrame, MiPtSlotAddress(Slot, TableFrame, Va), TableFrame,
                                MiSoftMake(MiSoftDemandZero, Protection, 0), 0);
            MiCloneReleaseMapping(Space, Ref, Va, Slot, TableFrame,
                                  MiSoftMake(MiSoftPrototype, Protection, 0));
            MiMakePageValid(Space, Va, Slot, TableFrame, NewFrame, Protection, TRUE);
            MI_ATOMIC_ADD64(&Space->PrivatePages, 1);
            MI_ATOMIC_ADD64(&Space->CopyOnWriteFaults, 1);
            MiVadRemove(&Space->CloneRoot, &Ref->Node);
            MiCloneDereference(Page);
            MI_FREE(Ref);
        }
        else
        {
            MiArchPteWrite(Slot, MiArchPteSetAccessed(Pte, TRUE));
            MiArchTlbInvalidate(Va, 1, FALSE);
        }
        Status = STATUS_SUCCESS;
        break;
    }
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}

static BOOLEAN
MiClonePrivatePte(PMI_SYSTEM System, MI_PTE Pte)
{
    if (MiArchPteIsValid(Pte))
        return !(MI_PFN_FLAGS(&System->Pfn.Pfn[MiArchPteFrame(Pte)]) & MI_PFN_FLAG_PROTOTYPE);
    return MiSoftKind(Pte) == MiSoftTransition || MiSoftKind(Pte) == MiSoftPageFile;
}

static NTSTATUS
MiClonePreparePage(PMI_ADDRESS_SPACE Source, ULONG64 Va, MI_PTE Pte, PMI_CLONE_WORK *List)
{
    PMI_CLONE_WORK Work = MI_ALLOCATE(sizeof(*Work));
    PMI_CLONE_REF Existing = MiCloneLookup(Source, Va);

    if (Work == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Work, sizeof(*Work));
    Work->Next = *List;
    Work->CopyFrame = MI_FRAME_INVALID;
    *List = Work;
    Work->Target = MI_ALLOCATE(sizeof(*Work->Target));
    if (Work->Target == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Work->Target, sizeof(*Work->Target));
    Work->Target->Node.StartingVpn = Work->Target->Node.EndingVpn = Va >> PAGE_SHIFT;
    if (Existing != NULL)
    {
        Work->Target->Page = Existing->Page;
        Work->Target->Protection = Existing->Protection;
        return STATUS_SUCCESS;
    }

    Work->Source = MI_ALLOCATE(sizeof(*Work->Source));
    Work->Page = MI_ALLOCATE(sizeof(*Work->Page));
    if (Work->Source == NULL || Work->Page == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Work->Page, sizeof(*Work->Page));
    Work->Page->System = Source->System;
    MI_MUTEX_INIT(&Work->Page->Lock);
    if (MiArchPteIsValid(Pte) || MiSoftKind(Pte) == MiSoftTransition)
    {
        ULONG Frame = (ULONG)(MiArchPteIsValid(Pte) ? MiArchPteFrame(Pte) : MiSoftValue(Pte));
        PMI_PFN Entry = &Source->System->Pfn.Pfn[Frame];
        KIRQL OldIrql = MiPfnLock(&Source->System->Pfn, Frame);
        BOOLEAN Locked = Entry->ReferenceCount > (Entry->ShareCount != 0 ? 1 : 0) +
                                                ((MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_IN_FLIGHT) != 0);

        MiPfnUnlock(&Source->System->Pfn, Frame, OldIrql);
        if (Locked)
        {
            Work->CopyFrame = MiPfnAllocatePage(&Source->System->Pfn, 0);
            if (Work->CopyFrame == MI_FRAME_INVALID)
                return STATUS_NO_MEMORY;
        }
    }
    return STATUS_SUCCESS;
}

static VOID
MiClonePublishPage(PMI_ADDRESS_SPACE Source, PMI_ADDRESS_SPACE Target, PMI_CLONE_WORK Work)
{
    ULONG64 Va = Work->Target->Node.StartingVpn << PAGE_SHIFT;
    ULONG SourceTable, TargetTable;
    PMI_PTE SourceSlot = MiPtLookup(Source, Va, &SourceTable);
    PMI_PTE TargetSlot = MiPtLookup(Target, Va, &TargetTable);
    PMI_CLONE_PAGE Page = Work->Page;
    MI_PTE Pte;
    ULONG Frame = MI_FRAME_INVALID;
    ULONG Protection;
    BOOLEAN Valid;

    if (Page == NULL)
    {
        MI_ATOMIC_ADD32(&Work->Target->Page->References, 1);
        goto TargetReady;
    }

    for (;;)
    {
        Pte = MiArchPteRead(SourceSlot);
        Valid = MiArchPteIsValid(Pte);
        if (Valid)
            Frame = (ULONG)MiArchPteFrame(Pte);
        else if (MiSoftKind(Pte) == MiSoftTransition)
        {
            Frame = (ULONG)MiSoftValue(Pte);
            if (!MiPfnReactivate(&Source->System->Pfn, Frame, MiPtSlotAddress(SourceSlot, SourceTable, Va)))
                continue;
        }
        else
            Frame = MI_FRAME_INVALID;
        break;
    }
    if (!MiClonePrivatePte(Source->System, Pte))
    {
        MiPtWrite(Target, Va, TargetSlot, TargetTable, Pte);
        return;
    }

    Protection = Valid ? MiViewPageProtection(Source, MiVadLocate(Source, Va), Va, Pte) : MiSoftProtection(Pte);
    Work->Target->Protection = Protection;
    Work->Target->Page = Page;
    Page->References = 1;
    if (Frame != MI_FRAME_INVALID && Work->CopyFrame != MI_FRAME_INVALID)
    {
        PVOID From = MiArchMapFrame(Frame);
        PVOID To = MiArchMapFrame(Work->CopyFrame);

        RtlCopyMemory(To, From, PAGE_SIZE);
        MiArchUnmapFrame(To);
        MiArchUnmapFrame(From);
        if (!Valid)
            MiPfnShareDecrement(&Source->System->Pfn, Frame, FALSE);
        Frame = Work->CopyFrame;
        Work->CopyFrame = MI_FRAME_INVALID;
        MiPfnInitializePage(&Source->System->Pfn, Frame, (ULONG64)(ULONG_PTR)&Page->Proto, 0,
                            MiSoftMake(MiSoftDemandZero, MI_PROT_READWRITE, 0), MI_PFN_FLAG_PROTOTYPE);
        MiPfnSetModified(&Source->System->Pfn, Frame);
        MiArchPteWrite(&Page->Proto, MiSoftMake(MiSoftResident, MI_PROT_READWRITE, Frame));
        MiCloneDropShare(Page, Frame);
    }
    else
    {
        *Work->Source = *Work->Target;
        if (!MiVadInsert(&Source->CloneRoot, &Work->Source->Node))
            MI_ASSERT(FALSE);
        Work->Source = NULL;
        Page->References++;
        if (Valid)
        {
            MiPtWrite(Source, Va, SourceSlot, SourceTable,
                       MiArchPteMakeLeaf(Frame, MiCloneLeafProtection(Protection), MI_LEAF_USER));
            MiArchTlbInvalidate(Va, 1, TRUE);
            if (MiArchPteIsDirty(Pte))
                MiPfnSetModified(&Source->System->Pfn, Frame);
        }
        else
        {
            MiPtWrite(Source, Va, SourceSlot, SourceTable, MiSoftMake(MiSoftPrototype, Protection, 0));
            MI_ATOMIC_ADD64(&Source->PageFileGeneration, 1);
        }
        if (Frame != MI_FRAME_INVALID)
        {
            PMI_PFN Entry = &Source->System->Pfn.Pfn[Frame];
            KIRQL OldIrql;

            while (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_IN_FLIGHT)
                MI_PAUSE();
            OldIrql = MiPfnLock(&Source->System->Pfn, Frame);
            Entry->PteAddress = (ULONG64)(ULONG_PTR)&Page->Proto;
            Entry->PteFrame = 0;
            MI_ATOMIC_OR8(&Entry->Flags, MI_PFN_FLAG_PROTOTYPE);
            MiArchPteWrite(&Page->Proto, MiSoftMake(MiSoftResident, MI_PROT_READWRITE, Frame));
            MiPfnUnlock(&Source->System->Pfn, Frame, OldIrql);
            if (!Valid)
                MiCloneDropShare(Page, Frame);
        }
        else
            MiArchPteWrite(&Page->Proto, Pte);
        MI_ATOMIC_ADD64(&Source->PrivatePages, -1);
    }
    Work->Page = NULL;
TargetReady:
    MiPtWrite(Target, Va, TargetSlot, TargetTable,
              MiSoftMake(MiSoftPrototype, Work->Target->Protection, 0));
    if (!MiVadInsert(&Target->CloneRoot, &Work->Target->Node))
        MI_ASSERT(FALSE);
    Work->Target = NULL;
}

static VOID
MiCloneRollback(PMI_ADDRESS_SPACE Target)
{
    PMI_VAD_NODE Node;

    while ((Node = MiVadFirst(&Target->VadRoot)) != NULL)
    {
        PMI_VAD Vad = CONTAINING_RECORD(Node, MI_VAD, Node);
        ULONG64 Va = Node->StartingVpn << PAGE_SHIFT;
        ULONG64 End = (Node->EndingVpn + 1) << PAGE_SHIFT;

        if (Vad->Type == MiVadLarge)
        {
            PMI_SEGMENT Segment = MiReleaseLargePagesLocked(Target, Vad);

            if (Segment != NULL)
                MiSegmentDereference(Segment);
            continue;
        }
        while (Va < End)
        {
            ULONG64 Next = MiPtNextTableBoundary(Target, Va);

            MiPtPruneEmpty(Target, Va);
            Va = Next;
        }
        MiReturnCommit(Target, Vad->CommitCharge);
        if (Vad->Segment != NULL)
        {
            MI_ATOMIC_ADD32(&Vad->Segment->MappedViews, -1);
            if (!Vad->CacheView)
                MI_ATOMIC_ADD32(&Vad->Segment->TruncationViews, -1);
            MiSegmentDereference(Vad->Segment);
        }
        MiVadRemove(&Target->VadRoot, Node);
        MI_FREE(Vad);
    }
}

NTSTATUS
MiCloneAddressSpace(PMI_ADDRESS_SPACE Source, PMI_ADDRESS_SPACE Target)
{
    PMI_ADDRESS_SPACE First = (ULONG_PTR)Source < (ULONG_PTR)Target ? Source : Target;
    PMI_ADDRESS_SPACE Second = First == Source ? Target : Source;
    PMI_VAD_NODE Node;
    PMI_CLONE_WORK Work, List = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Source == Target || Source->System != Target->System || Source->IsSystem || Target->IsSystem)
        return STATUS_INVALID_PARAMETER;
    MI_RW_ACQUIRE_EXCLUSIVE(&First->Lock);
    MI_RW_ACQUIRE_EXCLUSIVE(&Second->Lock);
    if (Target->VadRoot.NodeCount != 0)
    {
        Status = STATUS_CONFLICTING_ADDRESSES;
        goto Done;
    }

    for (Node = MiVadFirst(&Source->VadRoot); Node != NULL; Node = MiVadNext(Node))
    {
        PMI_VAD Original = CONTAINING_RECORD(Node, MI_VAD, Node);
        PMI_VAD Vad;
        ULONG64 Va = Node->StartingVpn << PAGE_SHIFT;
        ULONG64 End = (Node->EndingVpn + 1) << PAGE_SHIFT;

        if (Original->Type == MiVadAwe || Original->Type == MiVadPhysical ||
            (Original->Segment != NULL && !Original->Inherit))
            continue;
        if (MI_VAD_IS_DIRECT(Original) && !(Original->Type == MiVadLarge && Original->Segment != NULL))
        {
            Status = STATUS_NOT_SUPPORTED;
            goto Rollback;
        }
        Vad = MI_ALLOCATE(sizeof(*Vad));
        if (Vad == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Rollback;
        }
        *Vad = *Original;
        if (!MiChargeCommit(Target, Vad->CommitCharge))
        {
            MI_FREE(Vad);
            Status = STATUS_COMMITMENT_LIMIT;
            goto Rollback;
        }
        if (!MiVadInsert(&Target->VadRoot, &Vad->Node))
            MI_ASSERT(FALSE);
        if (Vad->Segment != NULL)
        {
            MiSegmentReference(Vad->Segment);
            MI_ATOMIC_ADD32(&Vad->Segment->MappedViews, 1);
            if (!Vad->CacheView)
                MI_ATOMIC_ADD32(&Vad->Segment->TruncationViews, 1);
        }
        if (Vad->Type == MiVadLarge)
        {
            Status = MiCloneLargeViewLocked(Source, Target, Vad);
            if (!NT_SUCCESS(Status))
                goto Rollback;
            continue;
        }
        while (Va < End)
        {
            PMI_PTE Slot = MiPtLookup(Source, Va, NULL);
            MI_PTE Pte;
            ULONG TargetTable;

            if (Slot == NULL)
            {
                Va = MiPtNextTableBoundary(Source, Va);
                continue;
            }
            Pte = MiArchPteRead(Slot);
            if (Pte != 0)
            {
                if (MiPtEnsure(Target, Va, &TargetTable) == NULL)
                {
                    Status = STATUS_NO_MEMORY;
                    goto Rollback;
                }
                if (MiCloneLookup(Source, Va) != NULL || MiClonePrivatePte(Source->System, Pte))
                {
                    Status = MiClonePreparePage(Source, Va, Pte, &List);
                    if (!NT_SUCCESS(Status))
                        goto Rollback;
                }
            }
            Va += PAGE_SIZE;
        }
    }

    for (Node = MiVadFirst(&Target->VadRoot); Node != NULL; Node = MiVadNext(Node))
    {
        PMI_VAD Original = MiVadLocate(Source, Node->StartingVpn << PAGE_SHIFT);
        ULONG64 Va = Node->StartingVpn << PAGE_SHIFT;
        ULONG64 End = (Node->EndingVpn + 1) << PAGE_SHIFT;

        if (Original->Type == MiVadLarge)
            continue;
        while (Va < End)
        {
            ULONG Table;
            PMI_PTE Slot = MiPtLookup(Source, Va, NULL);
            PMI_PTE Destination = MiPtLookup(Target, Va, &Table);
            MI_PTE Pte;

            if (Slot == NULL || Destination == NULL)
            {
                Va = MiPtNextTableBoundary(Source, Va);
                continue;
            }
            Pte = MiArchPteRead(Slot);
            if (Pte != 0 && MiCloneLookup(Source, Va) == NULL && !MiClonePrivatePte(Source->System, Pte))
            {
                if (MiArchPteIsValid(Pte))
                    Pte = MiSoftMake(MiSoftPrototype, MiViewPageProtection(Source, Original, Va, Pte), 0);
                MiPtWrite(Target, Va, Destination, Table, Pte);
            }
            Va += PAGE_SIZE;
        }
    }
    for (Work = List; Work != NULL; Work = Work->Next)
        MiClonePublishPage(Source, Target, Work);
    goto Done;

Rollback:
    MiCloneRollback(Target);
Done:
    while ((Work = List) != NULL)
    {
        List = Work->Next;
        if (Work->CopyFrame != MI_FRAME_INVALID)
            MiPfnShareDecrement(&Source->System->Pfn, Work->CopyFrame, TRUE);
        if (Work->Page != NULL)
            MI_FREE(Work->Page);
        if (Work->Source != NULL)
            MI_FREE(Work->Source);
        if (Work->Target != NULL)
            MI_FREE(Work->Target);
        MI_FREE(Work);
    }
    MI_RW_RELEASE_EXCLUSIVE(&Second->Lock);
    MI_RW_RELEASE_EXCLUSIVE(&First->Lock);
    return Status;
}
