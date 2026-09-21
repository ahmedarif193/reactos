/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/vm/large.c
 * PURPOSE:     Large-page virtual memory operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

static ULONG
MiLargeFrame(MI_PTE Pte)
{
    return (ULONG)(MiArchPteIsValid(Pte) ? MiArchPteFrame(Pte) : MiSoftValue(Pte));
}

PMI_SEGMENT
MiReleaseLargePagesLocked(PMI_ADDRESS_SPACE Space, PMI_VAD Vad)
{
    const MI_ARCH_DESCRIPTOR *Arch = Space->System->Arch;
    PMI_PFN_DATABASE Db = &Space->System->Pfn;
    PMI_SEGMENT Segment = Vad->Segment;
    ULONG Pages = (ULONG)(Arch->LargePageSize >> PAGE_SHIFT);
    ULONG64 Va, End = (Vad->Node.EndingVpn + 1) << PAGE_SHIFT;

    for (Va = Vad->Node.StartingVpn << PAGE_SHIFT; Va < End; Va += Arch->LargePageSize)
    {
        ULONG Table, Frame, i;
        PMI_PTE Slot = MiPtLookupLevel(Space, Va, Arch->LargePageLevel, &Table);
        MI_PTE Pte = Slot != NULL ? MiArchPteRead(Slot) : 0;

        if (Pte != 0)
        {
            Frame = MiLargeFrame(Pte);
            MiArchPteWrite(Slot, 0);
            MiArchTlbInvalidate(Va, Pages, TRUE);
            MI_ATOMIC_ADD32(&Db->Pfn[Table].UsedEntries, -1);
            if (Segment == NULL)
            {
                for (i = 0; i < Pages; i++)
                    MiPfnShareDecrement(Db, Frame + i, TRUE);
                MI_ATOMIC_ADD64(&Space->PrivatePages, -(LONG64)Pages);
            }
            MI_ATOMIC_ADD64(&Space->LargePages, -(LONG64)Pages);
        }
        MiPtPruneEmpty(Space, Va);
    }
    MiReturnCommit(Space, Vad->CommitCharge);
    MiVadRemove(&Space->VadRoot, &Vad->Node);
    MI_FREE(Vad);
    if (Segment != NULL)
    {
        MI_ATOMIC_ADD32(&Segment->MappedViews, -1);
        MI_ATOMIC_ADD32(&Segment->TruncationViews, -1);
    }
    return Segment;
}

static NTSTATUS
MiCreateLargeView(PMI_ADDRESS_SPACE Space, PULONG64 BaseAddress, PULONG64 RegionSize,
                   ULONG AllocationType, ULONG Protection, ULONG64 HighestAddress, PMI_SEGMENT Segment,
                   ULONG64 SectionOffset, ULONG MaximumProtection, BOOLEAN Inherit)
{
    const MI_ARCH_DESCRIPTOR *Arch = Space->System->Arch;
    PMI_PFN_DATABASE Db = &Space->System->Pfn;
    ULONG64 Large = Arch->LargePageSize, Start = *BaseAddress, Size = *RegionSize, Va;
    ULONG Pages;
    PMI_VAD Vad;
    PMI_SEGMENT Released = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Arch->SupportsLargePages || Large == 0 || Arch->LargePageLevel == 0)
        return STATUS_NOT_SUPPORTED;
    if ((Segment == NULL &&
         ((AllocationType & (MI_MEM_RESERVE | MI_MEM_COMMIT)) != (MI_MEM_RESERVE | MI_MEM_COMMIT) ||
          (AllocationType & ~(MI_MEM_RESERVE | MI_MEM_COMMIT | MI_MEM_LARGE_PAGES | MI_MEM_TOP_DOWN)))) ||
        (Segment != NULL && (AllocationType & ~(MI_MEM_LARGE_PAGES | MI_MEM_TOP_DOWN))) ||
        Size == 0 || ((Start | Size) & (Large - 1)) != 0 ||
        Size > Space->HighestVa - Space->LowestVa + 1)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if ((!MI_PROT_IS_ACCESSIBLE(Protection) && (Segment == NULL || Protection != MI_PROT_NOACCESS)) ||
        MI_PROT_IS_COPY(Protection) || ((Protection & MI_PROT_GUARD) && Protection != MI_PROT_NOACCESS))
        return STATUS_INVALID_PAGE_PROTECTION;
    if (HighestAddress > Space->HighestVa)
        HighestAddress = Space->HighestVa;
    if (HighestAddress < Space->LowestVa || Size - 1 > HighestAddress - Space->LowestVa)
        return STATUS_NO_MEMORY;

    Pages = (ULONG)(Large >> PAGE_SHIFT);
    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    if (Start == 0)
    {
        ULONG64 Vpn;
        if (!MiVadFindEmptyRangeEx(&Space->VadRoot, Size >> PAGE_SHIFT, Pages,
                                  Space->LowestVa >> PAGE_SHIFT, HighestAddress >> PAGE_SHIFT,
                                  (BOOLEAN)((AllocationType & MI_MEM_TOP_DOWN) != 0), &Vpn))
        {
            Status = STATUS_NO_MEMORY;
            goto Done;
        }
        Start = Vpn << PAGE_SHIFT;
    }
    else if (Start < Space->LowestVa || Start > HighestAddress || Size - 1 > HighestAddress - Start)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }
    if (MiVadFindOverlap(&Space->VadRoot, Start >> PAGE_SHIFT, (Start + Size - 1) >> PAGE_SHIFT) != NULL)
    {
        Status = STATUS_CONFLICTING_ADDRESSES;
        goto Done;
    }
    Vad = MI_ALLOCATE(sizeof(*Vad));
    if (Vad == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    if (Segment == NULL && !MiChargeCommit(Space, (LONG64)(Size >> PAGE_SHIFT)))
    {
        MI_FREE(Vad);
        Status = STATUS_COMMITMENT_LIMIT;
        goto Done;
    }
    RtlZeroMemory(Vad, sizeof(*Vad));
    Vad->Type = MiVadLarge;
    Vad->Protection = Protection;
    Vad->MaximumProtection = (UCHAR)(MaximumProtection & MI_PROT_ACCESS_MASK);
    Vad->MemCommit = TRUE;
    Vad->Inherit = Inherit;
    Vad->Segment = Segment;
    Vad->SegmentPageOffset = SectionOffset >> PAGE_SHIFT;
    Vad->CommitCharge = Segment == NULL ? (LONG64)(Size >> PAGE_SHIFT) : 0;
    Vad->Node.StartingVpn = Start >> PAGE_SHIFT;
    Vad->Node.EndingVpn = (Start + Size - 1) >> PAGE_SHIFT;
    if (!MiVadInsert(&Space->VadRoot, &Vad->Node))
    {
        MiReturnCommit(Space, Vad->CommitCharge);
        MI_FREE(Vad);
        Status = STATUS_CONFLICTING_ADDRESSES;
        goto Done;
    }
    if (Segment != NULL)
    {
        MiSegmentReference(Segment);
        MI_ATOMIC_ADD32(&Segment->MappedViews, 1);
        MI_ATOMIC_ADD32(&Segment->TruncationViews, 1);
    }
    for (Va = Start; Va < Start + Size; Va += Large)
    {
        ULONG Table, Frame, i;
        PMI_PTE Slot = MiPtEnsureLevel(Space, Va, Arch->LargePageLevel, &Table);

        if (Slot == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        MI_ASSERT(MiArchPteRead(Slot) == 0);
        Frame = Segment != NULL ? Segment->LargeFrames[(SectionOffset + Va - Start) / Large]
                                : MiPfnAllocateContiguous(Db, Pages, 0, Db->FrameCount - 1, Pages);
        if (Frame == MI_FRAME_INVALID)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        for (i = 0; Segment == NULL && i < Pages; i++)
        {
            PVOID Mapping = MiArchMapFrame(Frame + i);
            RtlZeroMemory(Mapping, PAGE_SIZE);
            MiArchUnmapFrame(Mapping);
            MiPfnInitializePage(Db, Frame + i, MiPtSlotAddress(Slot, Table, Va), Table,
                                MiSoftMake(MiSoftDemandZero, Protection, 0), 0);
        }
        MiPtWrite(Space, Va, Slot, Table, MI_PROT_IS_ACCESSIBLE(Protection)
            ? MiArchPteMakeBlock(Frame, Protection,
                (Space->IsSystem ? MI_LEAF_GLOBAL : MI_LEAF_USER) | MI_LEAF_DIRTY)
            : MiSoftMake(MiSoftResident, Protection, Frame));
        if (Segment == NULL)
            MI_ATOMIC_ADD64(&Space->PrivatePages, Pages);
        MI_ATOMIC_ADD64(&Space->LargePages, Pages);
    }
    if (!NT_SUCCESS(Status))
    {
        Released = MiReleaseLargePagesLocked(Space, Vad);
        goto Done;
    }
    *BaseAddress = Start;
    *RegionSize = Size;
Done:
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    if (Released != NULL)
        MiSegmentDereference(Released);
    return Status;
}

NTSTATUS
MiAllocateLargePages(PMI_ADDRESS_SPACE Space, PULONG64 BaseAddress, PULONG64 RegionSize,
                     ULONG AllocationType, ULONG Protection, ULONG64 HighestAddress)
{
    return MiCreateLargeView(Space, BaseAddress, RegionSize, AllocationType, Protection, HighestAddress,
                             NULL, 0, 0, TRUE);
}

NTSTATUS
MiMapLargeSection(PMI_ADDRESS_SPACE Space, PMI_SEGMENT Segment, PULONG64 BaseAddress, ULONG64 SectionOffset,
                   PULONG64 ViewSize, ULONG Protection, ULONG AllocationType, ULONG64 HighestAddress,
                   ULONG MaximumProtection, BOOLEAN Inherit)
{
    ULONG64 SectionSize = (ULONG64)MI_ATOMIC_READ64(&Segment->SizeInBytes);
    ULONG64 Size = *ViewSize;
    NTSTATUS Status;

    if (Segment->System != Space->System || Segment->LargeFrames == NULL ||
        (SectionOffset & (Space->System->Arch->LargePageSize - 1)) != 0 || SectionOffset >= SectionSize)
        return STATUS_INVALID_PARAMETER;
    if (Size == 0)
        Size = SectionSize - SectionOffset;
    if (Size > SectionSize - SectionOffset)
        return STATUS_INVALID_VIEW_SIZE;
    if (!MiViewProtectionAllowed(Segment, MaximumProtection, Protection))
        return STATUS_SECTION_PROTECTION;
    Status = MiCreateLargeView(Space, BaseAddress, &Size, AllocationType, Protection, HighestAddress,
                               Segment, SectionOffset, MaximumProtection, Inherit);
    if (NT_SUCCESS(Status))
        *ViewSize = Size;
    return Status;
}

NTSTATUS
MiCloneLargeViewLocked(PMI_ADDRESS_SPACE Source, PMI_ADDRESS_SPACE Target, PMI_VAD Vad)
{
    const MI_ARCH_DESCRIPTOR *Arch = Target->System->Arch;
    ULONG64 Va, End = (Vad->Node.EndingVpn + 1) << PAGE_SHIFT;

    for (Va = Vad->Node.StartingVpn << PAGE_SHIFT; Va < End; Va += Arch->LargePageSize)
    {
        ULONG Table;
        PMI_PTE Original = MiPtLookupLevel(Source, Va, Arch->LargePageLevel, NULL);
        PMI_PTE Slot = MiPtEnsureLevel(Target, Va, Arch->LargePageLevel, &Table);

        if (Slot == NULL)
            return STATUS_NO_MEMORY;
        MI_ASSERT(Original != NULL && MiArchPteRead(Original) != 0);
        MiPtWrite(Target, Va, Slot, Table, MiArchPteRead(Original));
        MI_ATOMIC_ADD64(&Target->LargePages, Arch->LargePageSize >> PAGE_SHIFT);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
MiProtectLargePagesLocked(PMI_ADDRESS_SPACE Space, ULONG64 Start, ULONG64 End,
                          ULONG Protection, PULONG OldProtection)
{
    const MI_ARCH_DESCRIPTOR *Arch = Space->System->Arch;
    ULONG64 Large = Arch->LargePageSize, Va;
    PMI_VAD Vad = MiVadLocate(Space, Start);

    if (((Start | End) & (Large - 1)) != 0)
        return STATUS_CONFLICTING_ADDRESSES;
    if (MI_PROT_IS_COPY(Protection) ||
        ((Protection & MI_PROT_GUARD) && Protection != MI_PROT_NOACCESS))
    {
        return STATUS_INVALID_PAGE_PROTECTION;
    }
    if (Vad->Segment != NULL && !MiViewProtectionAllowed(Vad->Segment, Vad->MaximumProtection, Protection))
        return STATUS_SECTION_PROTECTION;
    for (Va = Start; Va < End; Va += Large)
    {
        PMI_PTE Slot = MiPtLookupLevel(Space, Va, Arch->LargePageLevel, NULL);
        MI_PTE Pte, Value;

        MI_ASSERT(Slot != NULL);
        Pte = MiArchPteRead(Slot);
        if (Va == Start)
            *OldProtection = MiArchPteIsValid(Pte) ? MiViewPageProtection(Space, NULL, Va, Pte)
                                                  : MiSoftProtection(Pte);
        Value = MI_PROT_IS_ACCESSIBLE(Protection)
            ? MiArchPteMakeBlock(MiLargeFrame(Pte), Protection,
                (Space->IsSystem ? MI_LEAF_GLOBAL : MI_LEAF_USER) | MI_LEAF_DIRTY)
            : MiSoftMake(MiSoftResident, Protection, MiLargeFrame(Pte));
        MiArchPteWrite(Slot, Value);
        MiArchTlbInvalidate(Va, Large >> PAGE_SHIFT, TRUE);
    }
    return STATUS_SUCCESS;
}
