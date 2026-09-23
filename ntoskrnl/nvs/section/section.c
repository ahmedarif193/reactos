/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/section/section.c
 * PURPOSE:     Memory section and mapped-view management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

#define MI_SEGMENT_CHUNK_SHIFT  9
#define MI_SEGMENT_CHUNK_PAGES  (1ULL << MI_SEGMENT_CHUNK_SHIFT)
#define MI_PAGE_ALIGN_DOWN(a)   ((a) & ~((ULONG64)PAGE_SIZE - 1))
#define MI_PAGE_ALIGN_UP(a)     (((a) + PAGE_SIZE - 1) & ~((ULONG64)PAGE_SIZE - 1))
#define MI_VAD_START(v)         ((v)->Node.StartingVpn << PAGE_SHIFT)
#define MI_VAD_END(v)           (((v)->Node.EndingVpn + 1) << PAGE_SHIFT)

FORCEINLINE
ULONG64
MiSegmentPages(
    _In_ PMI_SEGMENT Segment)
{
    return (ULONG64)MI_ATOMIC_READ64(&Segment->PageCount);
}

FORCEINLINE
ULONG64
MiSegmentSize(
    _In_ PMI_SEGMENT Segment)
{
    return (ULONG64)MI_ATOMIC_READ64(&Segment->SizeInBytes);
}

static VOID MiSegmentReleasePage(_Inout_ PMI_SEGMENT Segment, _Inout_ PMI_PTE Proto, _In_ ULONG Frame);

static
PMI_PTE
MiSegmentProto(
    _In_ PMI_SEGMENT Segment,
    _In_ ULONG64 Page)
{
    PMI_PTE *Directory = MI_ATOMIC_READ_POINTER(&Segment->Chunk);

    return &Directory[Page >> MI_SEGMENT_CHUNK_SHIFT][Page & (MI_SEGMENT_CHUNK_PAGES - 1)];
}

static
MI_PTE
MiSegmentDefaultProto(
    _In_ PMI_SEGMENT Segment,
    _In_ ULONG64 Page)
{
    if (Segment->Kind == MiSegmentDataFile)
        return MiSoftMake(MiSoftSubsection, Segment->Protection, Page << (PAGE_SHIFT - MI_SECTOR_SHIFT));

    if (Segment->Kind == MiSegmentPageFileBacked)
        return MiSoftMake(Segment->Reserved ? MiSoftDecommitted : MiSoftDemandZero, Segment->Protection, 0);

    return 0;
}

static
NTSTATUS
MiSegmentGrow(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 NewPageCount)
{
    ULONG64 Needed = (NewPageCount + MI_SEGMENT_CHUNK_PAGES - 1) >> MI_SEGMENT_CHUNK_SHIFT;
    ULONG64 Page;

    if (Needed > Segment->ChunkCapacity)
    {
        ULONG64 Capacity = (Segment->ChunkCapacity != 0) ? Segment->ChunkCapacity : 4;
        PMI_PTE *Directory;

        while (Capacity < Needed)
            Capacity *= 2;

        Directory = MI_ALLOCATE((Capacity + 1) * sizeof(PMI_PTE));
        if (Directory == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory(Directory, (Capacity + 1) * sizeof(PMI_PTE));
        Directory[0] = (PMI_PTE)Segment->RetiredDirectories;
        Segment->RetiredDirectories = Directory;
        Directory++;

        if (Segment->Chunk != NULL)
            RtlCopyMemory(Directory, Segment->Chunk, Segment->ChunkCount * sizeof(PMI_PTE));

        MI_ATOMIC_WRITE_POINTER(&Segment->Chunk, Directory);
        Segment->ChunkCapacity = Capacity;
    }

    while (Segment->ChunkCount < Needed)
    {
        PMI_PTE Chunk = MI_ALLOCATE(MI_SEGMENT_CHUNK_PAGES * sizeof(MI_PTE));

        if (Chunk == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory(Chunk, MI_SEGMENT_CHUNK_PAGES * sizeof(MI_PTE));
        Segment->Chunk[Segment->ChunkCount++] = Chunk;
    }

    for (Page = MiSegmentPages(Segment); Page < NewPageCount; Page++)
        MiArchPteWrite(MiSegmentProto(Segment, Page), MiSegmentDefaultProto(Segment, Page));

    MI_ATOMIC_WRITE64(&Segment->PageCount, (LONG64)NewPageCount);
    return STATUS_SUCCESS;
}

static
BOOLEAN
MiSegmentChargeCommit(
    _Inout_ PMI_SEGMENT Segment,
    _In_ LONG64 Pages)
{
    if (!MiChargeSystemCommit(Segment->System, Pages, TRUE))
        return FALSE;

    Segment->CommitCharge += Pages;
    return TRUE;
}

static
VOID
MiSegmentDiscardPage(
    _Inout_ PMI_SEGMENT Segment,
    _Inout_ PMI_PTE Proto)
{
    PMI_SYSTEM System = Segment->System;

    for (;;)
    {
        MI_PTE Pte = MiArchPteRead(Proto);
        MI_SOFT_KIND Kind = MiSoftKind(Pte);
        ULONG Frame;

        if (Kind == MiSoftPageFile)
        {
            if (System->PageFile != NULL)
                MiPageFileReleaseSlot(System->PageFile, MiSoftValue(Pte));

            MiArchPteWrite(Proto, MiSoftMake(MiSoftDemandZero, MiSoftProtection(Pte), 0));
            return;
        }

        if (Kind != MiSoftTransition)
            return;

        Frame = (ULONG)MiSoftValue(Pte);
        if (!MiPfnReactivate(&System->Pfn, Frame, (ULONG64)(ULONG_PTR)Proto))
            continue;

        if (MiSoftKind(System->Pfn.Pfn[Frame].OriginalPte) == MiSoftPageFile)
            MiArchPteWrite(Proto, MiSoftMake(MiSoftDemandZero, MiSoftProtection(Pte), 0));
        else
            MiArchPteWrite(Proto, System->Pfn.Pfn[Frame].OriginalPte);

        MiReleasePageBacking(System, Frame);
        MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
        return;
    }
}

static
VOID
MiSegmentWaitForWriters(
    _Inout_ PMI_SEGMENT Segment)
{
    PMI_SYSTEM System = Segment->System;
    ULONG64 Page;

    for (Page = 0; Page < MiSegmentPages(Segment); Page++)
    {
        PMI_PTE Proto = MiSegmentProto(Segment, Page);

        for (;;)
        {
            MI_PTE Pte = MiArchPteRead(Proto);
            PMI_PFN Entry;
            BOOLEAN Busy;
            KIRQL OldIrql;

            if (MiSoftKind(Pte) != MiSoftTransition)
                break;

            Entry = &System->Pfn.Pfn[MiSoftValue(Pte)];
            OldIrql = MiPfnLock(&System->Pfn, (ULONG)MiSoftValue(Pte));
            Busy = (BOOLEAN)(Entry->PteAddress == (ULONG64)(ULONG_PTR)Proto && (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_IN_FLIGHT));
            MiPfnUnlock(&System->Pfn, (ULONG)MiSoftValue(Pte), OldIrql);

            if (!Busy)
                break;

            MI_PAUSE();
        }
    }
}

static
VOID
MiSegmentDelete(
    _Inout_ PMI_SEGMENT Segment,
    _In_ BOOLEAN Listed)
{
    PMI_SYSTEM System = Segment->System;
    BOOLEAN Created = Listed;
    ULONG64 Page;
    ULONG64 i;

    if (Segment->Kind == MiSegmentDataFile &&
        (Segment->FileOps.Write != NULL || Segment->FileOps.WriteFrames != NULL))
    {
        MiSegmentFlush(Segment, 0, MiSegmentSize(Segment));
        MiSegmentWaitForWriters(Segment);
        MiSegmentFlush(Segment, 0, MiSegmentSize(Segment));
    }

    while (Listed)
    {
        KIRQL OldIrql;

        MI_SPIN_ACQUIRE(&System->SegmentListLock, &OldIrql);
        if (Segment->ActiveWriters == 0)
        {
            RemoveEntryList(&Segment->SystemLink);
            Listed = FALSE;
        }
        MI_SPIN_RELEASE(&System->SegmentListLock, OldIrql);

        if (Listed)
            MI_PAUSE();
    }

    MI_MUTEX_ACQUIRE(&Segment->Lock);

    for (Page = 0; Page < MiSegmentPages(Segment); Page++)
    {
        PMI_PTE Proto = MiSegmentProto(Segment, Page);

        if (Segment->LargeFrames != NULL && MiSoftKind(MiArchPteRead(Proto)) == MiSoftResident)
        {
            MiPfnShareDecrement(&System->Pfn, (ULONG)MiSoftValue(MiArchPteRead(Proto)), TRUE);
            MiArchPteWrite(Proto, 0);
        }
        else
        {
            MI_ASSERT(MiSoftKind(MiArchPteRead(Proto)) != MiSoftResident);
            MiSegmentDiscardPage(Segment, Proto);
        }
    }

    MI_MUTEX_RELEASE(&Segment->Lock);

    MI_ATOMIC_ADD64(&Segment->System->CommittedPages, -Segment->CommitCharge);

    for (i = 0; i < Segment->ChunkCount; i++)
        MI_FREE(Segment->Chunk[i]);

    if (Segment->Layout != NULL)
        MI_FREE(Segment->Layout);
    if (Segment->LargeFrames != NULL)
        MI_FREE(Segment->LargeFrames);

    while (Segment->RetiredDirectories != NULL)
    {
        PMI_PTE *Directory = Segment->RetiredDirectories;

        Segment->RetiredDirectories = Directory[0];
        MI_FREE(Directory);
    }

    if (Created && Segment->FileOps.Release != NULL)
        Segment->FileOps.Release(Segment->FileContext);

    MI_FREE(Segment);
}

static
NTSTATUS
MiSegmentCreateEx(
    _Inout_ PMI_SYSTEM System,
    _In_ UCHAR Kind,
    _In_ ULONG64 SizeInBytes,
    _In_ ULONG Protection,
    _In_opt_ PMI_FILE_OPS FileOps,
    _In_opt_ PVOID FileContext,
    _In_opt_ PMI_SEGMENT_LAYOUT Layout,
    _In_ ULONG LayoutCount,
    _In_ BOOLEAN Reserved,
    _Out_ PMI_SEGMENT *SegmentOut)
{
    ULONG64 PageCount = MI_PAGE_ALIGN_UP(SizeInBytes) >> PAGE_SHIFT;
    PMI_SEGMENT Segment;
    NTSTATUS Status;
    KIRQL OldIrql;
    ULONG i;

    *SegmentOut = NULL;

    if (SizeInBytes == 0 || PageCount > (1ULL << 40))
        return STATUS_SECTION_TOO_BIG;

    if (!MI_PROT_IS_ACCESSIBLE(Protection))
        return STATUS_INVALID_PAGE_PROTECTION;

    if (Kind != MiSegmentPageFileBacked && (FileOps == NULL || FileOps->Read == NULL))
        return STATUS_INVALID_PARAMETER;

    if (Kind == MiSegmentImage && (Layout == NULL || LayoutCount == 0))
        return STATUS_INVALID_PARAMETER;

    Segment = MI_ALLOCATE(sizeof(*Segment));
    if (Segment == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Segment, sizeof(*Segment));
    Segment->System = System;
    Segment->Kind = Kind;
    Segment->Reserved = Reserved;
    Segment->Protection = Protection & MI_PROT_ACCESS_MASK;
    MI_ATOMIC_WRITE64(&Segment->SizeInBytes, (LONG64)SizeInBytes);
    Segment->ReferenceCount = 1;
    Segment->FileContext = FileContext;
    MI_MUTEX_INIT(&Segment->Lock);
    MI_MUTEX_INIT(&Segment->FlushLock);
    InitializeListHead(&Segment->PendingReads);
    InitializeListHead(&Segment->ReadDrains);

    if (FileOps != NULL)
        Segment->FileOps = *FileOps;

    if (Kind == MiSegmentPageFileBacked && !Reserved && !MiSegmentChargeCommit(Segment, (LONG64)PageCount))
    {
        MI_FREE(Segment);
        return STATUS_COMMITMENT_LIMIT;
    }

    Status = MiSegmentGrow(Segment, PageCount);
    if (!NT_SUCCESS(Status))
    {
        MiSegmentDelete(Segment, FALSE);
        return Status;
    }

    if (Kind == MiSegmentImage)
    {
        Segment->Layout = MI_ALLOCATE(LayoutCount * sizeof(MI_SEGMENT_LAYOUT));
        if (Segment->Layout == NULL)
        {
            MiSegmentDelete(Segment, FALSE);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(Segment->Layout, Layout, LayoutCount * sizeof(MI_SEGMENT_LAYOUT));
        Segment->LayoutCount = LayoutCount;
    }

    for (i = 0; i < LayoutCount && Kind == MiSegmentImage; i++)
    {
        ULONG64 Page;

        if (Layout[i].FirstPage + Layout[i].PageCount > PageCount ||
            Layout[i].FileBytes > (Layout[i].PageCount << PAGE_SHIFT) ||
            (Layout[i].FileOffset & ((1u << MI_SECTOR_SHIFT) - 1)))
        {
            MiSegmentDelete(Segment, FALSE);
            return STATUS_INVALID_PARAMETER;
        }

        for (Page = 0; Page < Layout[i].PageCount; Page++)
        {
            ULONG PageProtection = Layout[i].Protection & MI_PROT_ACCESS_MASK;

            if (MI_PROT_IS_WRITABLE(PageProtection))
                PageProtection = MI_PROT_IS_EXECUTE(PageProtection) ? MI_PROT_EXECUTE_WRITECOPY : MI_PROT_WRITECOPY;

            MiArchPteWrite(MiSegmentProto(Segment, Layout[i].FirstPage + Page),
                           ((Page << PAGE_SHIFT) < Layout[i].FileBytes)
                               ? MiSoftMake(MiSoftSubsection, PageProtection,
                                            (Layout[i].FileOffset + (Page << PAGE_SHIFT)) >> MI_SECTOR_SHIFT)
                               : MiSoftMake(MiSoftDemandZero, PageProtection, 0));
        }
    }

    MI_SPIN_ACQUIRE(&System->SegmentListLock, &OldIrql);
    InsertTailList(&System->SegmentList, &Segment->SystemLink);
    MI_SPIN_RELEASE(&System->SegmentListLock, OldIrql);

    *SegmentOut = Segment;
    return STATUS_SUCCESS;
}

NTSTATUS
MiSegmentCreate(
    _Inout_ PMI_SYSTEM System,
    _In_ UCHAR Kind,
    _In_ ULONG64 SizeInBytes,
    _In_ ULONG Protection,
    _In_opt_ PMI_FILE_OPS FileOps,
    _In_opt_ PVOID FileContext,
    _In_opt_ PMI_SEGMENT_LAYOUT Layout,
    _In_ ULONG LayoutCount,
    _Out_ PMI_SEGMENT *SegmentOut)
{
    return MiSegmentCreateEx(System, Kind, SizeInBytes, Protection, FileOps, FileContext, Layout, LayoutCount, FALSE,
                             SegmentOut);
}

NTSTATUS
MiSegmentCreateReserved(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 SizeInBytes,
    _In_ ULONG Protection,
    _In_opt_ PMI_FILE_OPS FileOps,
    _In_opt_ PVOID FileContext,
    _Out_ PMI_SEGMENT *SegmentOut)
{
    return MiSegmentCreateEx(System, MiSegmentPageFileBacked, SizeInBytes, Protection, FileOps, FileContext, NULL, 0,
                             TRUE, SegmentOut);
}

NTSTATUS
MiSegmentCommitPages(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 FirstPage,
    _In_ ULONG64 PageCount)
{
    NTSTATUS Status = STATUS_SUCCESS;
    LONG64 Needed = 0;
    ULONG64 Last;
    ULONG64 Page;

    if (Segment->Kind != MiSegmentPageFileBacked || !Segment->Reserved)
        return STATUS_SUCCESS;

    MI_MUTEX_ACQUIRE(&Segment->Lock);

    Last = MiSegmentPages(Segment);
    if (FirstPage < Last && PageCount < Last - FirstPage)
        Last = FirstPage + PageCount;

    for (Page = FirstPage; Page < Last; Page++)
    {
        if (MiSoftKind(MiArchPteRead(MiSegmentProto(Segment, Page))) == MiSoftDecommitted)
            Needed++;
    }

    if (Needed != 0 && !MiSegmentChargeCommit(Segment, Needed))
    {
        Status = STATUS_COMMITMENT_LIMIT;
    }
    else
    {
        for (Page = FirstPage; Page < Last; Page++)
        {
            PMI_PTE Proto = MiSegmentProto(Segment, Page);
            MI_PTE Pte = MiArchPteRead(Proto);

            if (MiSoftKind(Pte) == MiSoftDecommitted)
                MiArchPteWrite(Proto, MiSoftMake(MiSoftDemandZero, MiSoftProtection(Pte), 0));
        }
    }

    MI_MUTEX_RELEASE(&Segment->Lock);
    return Status;
}

NTSTATUS
MiSegmentCreateLarge(PMI_SYSTEM System, ULONG64 SizeInBytes, ULONG Protection,
                      PMI_FILE_OPS FileOps, PVOID FileContext, PMI_SEGMENT *SegmentOut)
{
    ULONG64 Large = System->Arch->LargePageSize, Count, Block;
    ULONG Pages;
    PMI_SEGMENT Segment;
    NTSTATUS Status;

    *SegmentOut = NULL;
    if (!System->Arch->SupportsLargePages || Large == 0 || System->Arch->LargePageLevel == 0)
        return STATUS_NOT_SUPPORTED;
    if (SizeInBytes == 0 || (SizeInBytes & (Large - 1)) != 0)
        return STATUS_INVALID_PARAMETER;
    if (!MI_PROT_IS_ACCESSIBLE(Protection) || MI_PROT_IS_COPY(Protection) || (Protection & MI_PROT_GUARD))
        return STATUS_INVALID_PAGE_PROTECTION;
    Count = SizeInBytes / Large;
    if (Count > (~(SIZE_T)0) / sizeof(ULONG))
        return STATUS_SECTION_TOO_BIG;
    Status = MiSegmentCreate(System, MiSegmentPageFileBacked, SizeInBytes, Protection,
                             NULL, NULL, NULL, 0, &Segment);
    if (!NT_SUCCESS(Status))
        return Status;
    Segment->LargeFrames = MI_ALLOCATE((SIZE_T)Count * sizeof(ULONG));
    if (Segment->LargeFrames == NULL)
    {
        MiSegmentDereferenceAndClose(Segment);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Pages = (ULONG)(Large >> PAGE_SHIFT);
    for (Block = 0; Block < Count; Block++)
    {
        ULONG Frame = MiPfnAllocateContiguous(&System->Pfn, Pages, 0, System->Pfn.FrameCount - 1, Pages);
        ULONG i;

        if (Frame == MI_FRAME_INVALID)
        {
            MiSegmentDereferenceAndClose(Segment);
            return STATUS_NO_MEMORY;
        }
        Segment->LargeFrames[Block] = Frame;
        for (i = 0; i < Pages; i++)
        {
            PMI_PTE Proto = MiSegmentProto(Segment, Block * Pages + i);
            PVOID Mapping = MiArchMapFrame(Frame + i);

            RtlZeroMemory(Mapping, PAGE_SIZE);
            MiArchUnmapFrame(Mapping);
            MiPfnInitializePage(&System->Pfn, Frame + i, (ULONG64)(ULONG_PTR)Proto, 0,
                                MiArchPteRead(Proto), MI_PFN_FLAG_PROTOTYPE);
            MiArchPteWrite(Proto, MiSoftMake(MiSoftResident, Protection, Frame + i));
        }
    }
    if (FileOps != NULL)
        Segment->FileOps = *FileOps;
    Segment->FileContext = FileContext;
    *SegmentOut = Segment;
    return STATUS_SUCCESS;
}

NTSTATUS
MiSegmentAdoptFrame(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Page,
    _In_ ULONG Frame)
{
    PMI_SYSTEM System = Segment->System;
    NTSTATUS Status = STATUS_SUCCESS;
    PMI_PTE Proto;
    MI_PTE Pte;

    if (Segment->Kind != MiSegmentPageFileBacked || Frame >= System->Pfn.FrameCount)
        return STATUS_INVALID_PARAMETER;

    MI_MUTEX_ACQUIRE(&Segment->Lock);

    if (Page >= MiSegmentPages(Segment))
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        Proto = MiSegmentProto(Segment, Page);
        Pte = MiArchPteRead(Proto);

        if (MiSoftKind(Pte) != MiSoftDemandZero)
        {
            Status = STATUS_CONFLICTING_ADDRESSES;
        }
        else
        {
            MiPfnInitializePage(&System->Pfn, Frame, (ULONG64)(ULONG_PTR)Proto, 0, Pte, MI_PFN_FLAG_PROTOTYPE);
            MiPfnSetModified(&System->Pfn, Frame);
            MiArchPteWrite(Proto, MiSoftMake(MiSoftResident, MiSoftProtection(Pte), Frame));
        }
    }

    MI_MUTEX_RELEASE(&Segment->Lock);
    return Status;
}

VOID
MiSegmentReleaseAdoptedFrame(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Page)
{
    PMI_PTE Proto = MiSegmentProto(Segment, Page);
    MI_PTE Pte = MiArchPteRead(Proto);

    if (MiSoftKind(Pte) == MiSoftResident)
        MiSegmentReleasePage(Segment, Proto, (ULONG)MiSoftValue(Pte));
}

VOID
MiSegmentReference(
    _Inout_ PMI_SEGMENT Segment)
{
    MI_ATOMIC_ADD32(&Segment->ReferenceCount, 1);
}

BOOLEAN
MiSegmentTryReference(
    _Inout_ PMI_SEGMENT Segment)
{
    PMI_SYSTEM System = Segment->System;
    KIRQL OldIrql;

    for (;;)
    {
        LONG Count = MI_ATOMIC_READ32(&Segment->ReferenceCount);

        if (Count != 0)
        {
            if (MI_ATOMIC_CAS32(&Segment->ReferenceCount, Count + 1, Count) == Count)
                return TRUE;

            continue;
        }

        MI_SPIN_ACQUIRE(&System->SegmentListLock, &OldIrql);

        if (Segment->State == MI_SEGMENT_UNUSED)
        {
            RemoveEntryList(&Segment->UnusedLink);
            System->UnusedSegmentCount--;
            Segment->State = MI_SEGMENT_ACTIVE;
            MI_ATOMIC_ADD32(&Segment->ReferenceCount, 1);
            MI_SPIN_RELEASE(&System->SegmentListLock, OldIrql);
            MI_ATOMIC_ADD64(&System->UnusedSegmentHits, 1);
            return TRUE;
        }

        if (Segment->State == MI_SEGMENT_DELETING)
        {
            MI_SPIN_RELEASE(&System->SegmentListLock, OldIrql);
            return FALSE;
        }

        MI_SPIN_RELEASE(&System->SegmentListLock, OldIrql);
        MI_PAUSE();
    }
}

static
BOOLEAN
MiSegmentRelease(
    _Inout_ PMI_SEGMENT Segment,
    _In_ BOOLEAN AllowCache,
    _Out_opt_ PMI_SEGMENT *DeferredDelete)
{
    PMI_SYSTEM System = Segment->System;
    PMI_SEGMENT Victim = NULL;
    BOOLEAN Closed = FALSE;
    KIRQL OldIrql;

    if (DeferredDelete != NULL)
        *DeferredDelete = NULL;

    for (;;)
    {
        LONG Count = MI_ATOMIC_READ32(&Segment->ReferenceCount);

        if (Count <= 1)
            break;

        if (MI_ATOMIC_CAS32(&Segment->ReferenceCount, Count - 1, Count) == Count)
            return FALSE;
    }

    MI_SPIN_ACQUIRE(&System->SegmentListLock, &OldIrql);

    if (MI_ATOMIC_ADD32(&Segment->ReferenceCount, -1) - 1 != 0)
    {
        MI_SPIN_RELEASE(&System->SegmentListLock, OldIrql);
        return FALSE;
    }

    if (AllowCache && System->UnusedSegmentLimit != 0 && Segment->Kind != MiSegmentPageFileBacked &&
        Segment->FileOps.Release != NULL)
    {
        Segment->State = MI_SEGMENT_UNUSED;
        InsertTailList(&System->UnusedSegmentList, &Segment->UnusedLink);
        System->UnusedSegmentCount++;

        if (System->UnusedSegmentCount > System->UnusedSegmentLimit)
        {
            Victim = CONTAINING_RECORD(System->UnusedSegmentList.Flink, MI_SEGMENT, UnusedLink);
            RemoveEntryList(&Victim->UnusedLink);
            System->UnusedSegmentCount--;
            Victim->State = MI_SEGMENT_DELETING;
        }
    }
    else
    {
        Segment->State = MI_SEGMENT_DELETING;
        Victim = Segment;
        Closed = TRUE;
    }

    MI_SPIN_RELEASE(&System->SegmentListLock, OldIrql);

    if (DeferredDelete != NULL)
        *DeferredDelete = Victim;
    else if (Victim != NULL)
        MiSegmentDelete(Victim, TRUE);

    return Closed;
}

VOID
MiSegmentDereference(
    _Inout_ PMI_SEGMENT Segment)
{
    MiSegmentRelease(Segment, TRUE, NULL);
}

BOOLEAN
MiSegmentDereferenceAndClose(
    _Inout_ PMI_SEGMENT Segment)
{
    return MiSegmentRelease(Segment, FALSE, NULL);
}

ULONG
MiSegmentPurgeUnused(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG MaximumCount)
{
    ULONG Purged = 0;

    while (Purged < MaximumCount)
    {
        PMI_SEGMENT Victim = NULL;
        KIRQL OldIrql;

        MI_SPIN_ACQUIRE(&System->SegmentListLock, &OldIrql);

        if (!IsListEmpty(&System->UnusedSegmentList))
        {
            Victim = CONTAINING_RECORD(System->UnusedSegmentList.Flink, MI_SEGMENT, UnusedLink);
            RemoveEntryList(&Victim->UnusedLink);
            System->UnusedSegmentCount--;
            Victim->State = MI_SEGMENT_DELETING;
        }

        MI_SPIN_RELEASE(&System->SegmentListLock, OldIrql);

        if (Victim == NULL)
            break;

        MiSegmentDelete(Victim, TRUE);
        Purged++;
    }

    return Purged;
}

static
PMI_SEGMENT
MiSegmentFromPrototype(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 PteAddress,
    _Out_ PULONG64 Page)
{
    PMI_SEGMENT Found = NULL;
    PLIST_ENTRY Link;
    KIRQL OldIrql;

    MI_SPIN_ACQUIRE(&System->SegmentListLock, &OldIrql);

    for (Link = System->SegmentList.Flink; Link != &System->SegmentList && Found == NULL; Link = Link->Flink)
    {
        PMI_SEGMENT Segment = CONTAINING_RECORD(Link, MI_SEGMENT, SystemLink);
        ULONG64 i;

        for (i = 0; i < Segment->ChunkCount; i++)
        {
            ULONG64 Base = (ULONG64)(ULONG_PTR)Segment->Chunk[i];

            if (PteAddress >= Base && PteAddress < Base + MI_SEGMENT_CHUNK_PAGES * sizeof(MI_PTE))
            {
                *Page = (i << MI_SEGMENT_CHUNK_SHIFT) + (PteAddress - Base) / sizeof(MI_PTE);
                Segment->ActiveWriters++;
                Found = Segment;
                break;
            }
        }
    }

    MI_SPIN_RELEASE(&System->SegmentListLock, OldIrql);
    return Found;
}

static
ULONG
MiSegmentPageBytes(
    _In_ PMI_SEGMENT Segment,
    _In_ ULONG64 FileOffset)
{
    ULONG64 End = MiSegmentSize(Segment);
    ULONG i;

    if (Segment->Kind == MiSegmentImage)
    {
        End = 0;
        for (i = 0; i < Segment->LayoutCount; i++)
        {
            if (FileOffset >= Segment->Layout[i].FileOffset &&
                FileOffset < Segment->Layout[i].FileOffset + Segment->Layout[i].FileBytes)
            {
                End = Segment->Layout[i].FileOffset + Segment->Layout[i].FileBytes;
                break;
            }
        }
    }

    if (FileOffset >= End)
        return 0;

    return (End - FileOffset < PAGE_SIZE) ? (ULONG)(End - FileOffset) : PAGE_SIZE;
}

static
ULONG
MiSegmentReadBytes(
    _In_ PMI_SEGMENT Segment,
    _In_ ULONG64 FileOffset)
{
    ULONG Bytes = MiSegmentPageBytes(Segment, FileOffset);

    if (Bytes != 0 && Segment->Kind != MiSegmentImage && Segment->FileOps.WholePageReads)
        return PAGE_SIZE;
    return Bytes;
}

static
NTSTATUS
MiSegmentMaterialize(
    _Inout_ PMI_SEGMENT Segment,
    _Inout_ PMI_PTE Proto,
    _In_ ULONG64 ZeroFrom,
    _Out_ PULONG FrameOut)
{
    PMI_SYSTEM System = Segment->System;
    MI_PTE Pte = MiArchPteRead(Proto);
    MI_SOFT_KIND Kind = MiSoftKind(Pte);
    NTSTATUS Status = STATUS_SUCCESS;
    MI_PTE Original = Pte;
    PVOID Mapping;
    ULONG Frame;
    BOOLEAN Zero;

    if (Kind != MiSoftSubsection && Kind != MiSoftDemandZero && Kind != MiSoftPageFile)
        return STATUS_ACCESS_VIOLATION;

    Zero = (BOOLEAN)(Kind == MiSoftDemandZero ||
                     (Kind == MiSoftSubsection && (MiSoftValue(Pte) << MI_SECTOR_SHIFT) >= ZeroFrom));
    Frame = MiPfnAllocatePage(&System->Pfn, Zero ? MI_ALLOCATE_ZEROED : 0);
    if (Frame == MI_FRAME_INVALID)
        return STATUS_NO_MEMORY;

    if (Kind == MiSoftSubsection && !Zero)
    {
        ULONG64 Offset = MiSoftValue(Pte) << MI_SECTOR_SHIFT;
        ULONG Bytes = MiSegmentReadBytes(Segment, Offset);

        Mapping = MiArchMapFrame(Frame);
        if (Bytes != 0)
            Status = Segment->FileOps.Read(Segment->FileContext, Offset, Bytes, Mapping);

        if (NT_SUCCESS(Status) && Bytes < PAGE_SIZE)
            RtlZeroMemory((PUCHAR)Mapping + Bytes, PAGE_SIZE - Bytes);

        MiArchUnmapFrame(Mapping);
        MI_ATOMIC_ADD64(&Segment->PagesRead, 1);
    }
    else if (Kind == MiSoftPageFile)
    {
        if (System->PageFile == NULL)
        {
            Status = STATUS_IN_PAGE_ERROR;
        }
        else
        {
            Mapping = MiArchMapFrame(Frame);
            Status = System->PageFile->Ops.Read(System->PageFile->Context, MiSoftValue(Pte), Mapping);
            MiArchUnmapFrame(Mapping);
            MI_ATOMIC_ADD64(&System->PageFile->PagesRead, 1);
        }
    }

    if (!NT_SUCCESS(Status))
    {
        MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
        return STATUS_IN_PAGE_ERROR;
    }

    MiPfnInitializePage(&System->Pfn, Frame, (ULONG64)(ULONG_PTR)Proto, 0, Original, MI_PFN_FLAG_PROTOTYPE);
    MiArchPteWrite(Proto, MiSoftMake(MiSoftResident, MiSoftProtection(Pte), Frame));
    *FrameOut = Frame;
    return STATUS_SUCCESS;
}

static
NTSTATUS
MiSegmentAcquirePage(
    _Inout_ PMI_SEGMENT Segment,
    _Inout_ PMI_PTE Proto,
    _In_ BOOLEAN AllowIo,
    _Out_ PULONG FrameOut)
{
    PMI_SYSTEM System = Segment->System;

    for (;;)
    {
        MI_PTE Pte = MiArchPteRead(Proto);
        ULONG Frame;

        switch (MiSoftKind(Pte))
        {
            case MiSoftResident:
                Frame = (ULONG)MiSoftValue(Pte);
                if (!MiPfnShareIncrementIfMapped(&System->Pfn, Frame, Proto, Pte))
                    continue;

                *FrameOut = Frame;
                return STATUS_SUCCESS;

            case MiSoftTransition:
                Frame = (ULONG)MiSoftValue(Pte);
                if (!MiPfnReactivateEx(&System->Pfn, Frame, (ULONG64)(ULONG_PTR)Proto, Proto, Pte,
                                       MiSoftMake(MiSoftResident, MiSoftProtection(Pte), Frame)))
                {
                    continue;
                }

                *FrameOut = Frame;
                return STATUS_SUCCESS;

            default:
                if (!AllowIo && (MiSoftKind(Pte) == MiSoftSubsection || MiSoftKind(Pte) == MiSoftPageFile))
                    return STATUS_PENDING_PAGE_IN;

                return MiSegmentMaterialize(Segment, Proto, ~0ULL, FrameOut);
        }
    }
}

static
VOID
MiSegmentReleasePage(
    _Inout_ PMI_SEGMENT Segment,
    _Inout_ PMI_PTE Proto,
    _In_ ULONG Frame)
{
    MI_PTE Pte = MiArchPteRead(Proto);

    MI_ASSERT(MiSoftKind(Pte) == MiSoftResident && MiSoftValue(Pte) == Frame);
    MiPfnShareDecrementEx(&Segment->System->Pfn, Frame, FALSE, Proto,
                          MiSoftMake(MiSoftTransition, MiSoftProtection(Pte), Frame));
}

static
ULONG
MiPteProtection(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ MI_PTE Pte)
{
    BOOLEAN Execute = MiArchPteIsExecutable(Pte, (BOOLEAN)!Space->IsSystem);

    if (MiArchPteIsWritable(Pte))
        return Execute ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE;

    if (MiArchPteIsCopyOnWrite(Pte))
        return Execute ? MI_PROT_EXECUTE_WRITECOPY : MI_PROT_WRITECOPY;

    return Execute ? MI_PROT_EXECUTE_READ : MI_PROT_READONLY;
}

static
ULONG
MiViewDefaultProtection(
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress)
{
    PMI_SEGMENT Segment = Vad->Segment;
    ULONG64 Page = Vad->SegmentPageOffset + ((VirtualAddress - MI_VAD_START(Vad)) >> PAGE_SHIFT);

    if (Vad->Type != MiVadImage)
        return Vad->Protection;

    if (Page >= MiSegmentPages(Segment) || MiArchPteRead(MiSegmentProto(Segment, Page)) == 0)
        return MI_PROT_NOACCESS;

    return MiSoftProtection(MiArchPteRead(MiSegmentProto(Segment, Page))) & MI_PROT_ACCESS_MASK;
}

BOOLEAN
MiViewPageCommitted(
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress)
{
    PMI_SEGMENT Segment = Vad->Segment;
    ULONG64 Page;

    if (Segment == NULL || !Segment->Reserved)
        return TRUE;

    Page = Vad->SegmentPageOffset + ((VirtualAddress - MI_VAD_START(Vad)) >> PAGE_SHIFT);
    return (BOOLEAN)(Page < MiSegmentPages(Segment) &&
                     MiSoftKind(MiArchPteRead(MiSegmentProto(Segment, Page))) != MiSoftDecommitted);
}

ULONG
MiViewPageProtection(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress,
    _In_ MI_PTE Pte)
{
    ULONG Protection;
    PMI_CLONE_REF Clone = MiCloneLookup(Space, VirtualAddress);

    if (Clone != NULL)
        return Clone->Protection;

    if (MiArchPteIsValid(Pte))
        return MiPteProtection(Space, Pte);

    if (MiSoftKind(Pte) == MiSoftPrototype)
        return MiSoftProtection(Pte);

    Protection = MiViewDefaultProtection(Vad, VirtualAddress);
    return Protection;
}

static
VOID
MiReleaseViewPage(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ MI_PTE NewValue)
{
    PMI_SYSTEM System = Space->System;
    PMI_SEGMENT Segment = Vad->Segment;
    MI_PTE Pte = MiArchPteRead(Slot);
    ULONG64 Page;
    ULONG Frame;

    if (MiCloneDeletePage(Space, VirtualAddress, Slot, TableFrame, NewValue))
        return;

    if (!MiArchPteIsValid(Pte) || !(MI_PFN_FLAGS(&System->Pfn.Pfn[MiArchPteFrame(Pte)]) & MI_PFN_FLAG_PROTOTYPE))
    {
        if (Pte != NewValue)
            MiDeletePte(Space, VirtualAddress, Slot, TableFrame, NewValue);

        return;
    }

    Frame = (ULONG)MiArchPteFrame(Pte);
    Page = Vad->SegmentPageOffset + ((VirtualAddress - MI_VAD_START(Vad)) >> PAGE_SHIFT);

    MiPtWrite(Space, VirtualAddress, Slot, TableFrame, NewValue);

    if (MiArchPteIsDirty(Pte))
        MiPfnSetModified(&System->Pfn, Frame);

    if (Space->TlbBatch != NULL)
    {
        MiTlbBatchAdd(Space, VirtualAddress, Frame, Segment, MiSegmentProto(Segment, Page), FALSE);
    }
    else
    {
        MiArchTlbInvalidate(VirtualAddress, 1, TRUE);
        MiSegmentReleasePage(Segment, MiSegmentProto(Segment, Page), Frame);
    }

    MI_ATOMIC_ADD64(&Space->ResidentPages, -1);
}

static
VOID
MiTlbBatchFlush(
    _Inout_ PMI_ADDRESS_SPACE Space)
{
    PMI_TLB_BATCH Batch = Space->TlbBatch;
    PMI_SYSTEM System = Space->System;
    ULONG First = 0;
    ULONG i;

    while (First < Batch->Count)
    {
        ULONG Last = First + 1;

        while (Last < Batch->Count && Batch->Entry[Last].Va - Batch->Entry[Last - 1].Va <= PAGE_SIZE)
            Last++;

        MiArchTlbInvalidate(Batch->Entry[First].Va,
                            ((Batch->Entry[Last - 1].Va - Batch->Entry[First].Va) >> PAGE_SHIFT) + 1, TRUE);
        First = Last;
    }

    for (i = 0; i < Batch->Count; i++)
    {
        PMI_TLB_BATCH_ENTRY Entry = &Batch->Entry[i];

        if (Entry->Segment != NULL)
        {
            MiSegmentReleasePage(Entry->Segment, Entry->Proto, Entry->Frame);
        }
        else
        {
            if (Entry->Private && !(MI_PFN_FLAGS(&System->Pfn.Pfn[Entry->Frame]) & MI_PFN_FLAG_PAGE_TABLE))
                MiReleasePageBacking(System, Entry->Frame);

            MiPfnShareDecrement(&System->Pfn, Entry->Frame, Entry->Private);
        }
    }

    Batch->Count = 0;
}

BOOLEAN
MiTlbBatchBegin(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Out_ PMI_TLB_BATCH Batch)
{
    Batch->Count = 0;
    Batch->DeferTables = FALSE;

    if (Space->TlbBatch != NULL)
        return FALSE;

    Space->TlbBatch = Batch;
    return TRUE;
}

VOID
MiTlbBatchEnd(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ BOOLEAN Owner)
{
    if (!Owner)
        return;

    MiTlbBatchFlush(Space);
    Space->TlbBatch = NULL;
}

VOID
MiTlbBatchAdd(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Frame,
    _In_opt_ PMI_SEGMENT Segment,
    _In_opt_ PMI_PTE Proto,
    _In_ BOOLEAN Private)
{
    PMI_TLB_BATCH Batch = Space->TlbBatch;
    ULONG Index = Batch->Count++;
    PMI_TLB_BATCH_ENTRY Entry;

    while (Index != 0 && Batch->Entry[Index - 1].Va > VirtualAddress)
    {
        Batch->Entry[Index] = Batch->Entry[Index - 1];
        Index--;
    }
    Entry = &Batch->Entry[Index];

    Entry->Va = VirtualAddress;
    Entry->Frame = Frame;
    Entry->Segment = Segment;
    Entry->Proto = Proto;
    Entry->Private = Private;

    if (Batch->Count == MI_TLB_BATCH_SIZE)
        MiTlbBatchFlush(Space);
}

NTSTATUS
MiResolvePrototypeFault(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ MI_FAULT_ACCESS Access)
{
    PMI_SEGMENT Segment = Vad->Segment;
    ULONG64 Page = Vad->SegmentPageOffset + ((VirtualAddress - MI_VAD_START(Vad)) >> PAGE_SHIFT);
    ULONG Protection;
    NTSTATUS Status;
    MI_PTE ProtoPte;
    PMI_PTE Proto;
    MI_PTE Pte;
    ULONG Frame;

    Pte = MiArchPteRead(Slot);

    if (Page >= MiSegmentPages(Segment))
        return STATUS_ACCESS_VIOLATION;

    Protection = (MiSoftKind(Pte) == MiSoftPrototype) ? MiSoftProtection(Pte)
                                                      : MiViewDefaultProtection(Vad, VirtualAddress);

    if ((Protection & MI_PROT_NOACCESS) == MI_PROT_GUARD)
    {
        MiPtWrite(Space, VirtualAddress, Slot, TableFrame,
                  MiSoftMake(MiSoftPrototype, Protection & ~MI_PROT_GUARD, 0));
        return STATUS_GUARD_PAGE_VIOLATION;
    }

    if (!MI_PROT_IS_ACCESSIBLE(Protection) ||
        (Access == MiFaultWrite && !MI_PROT_IS_WRITABLE(Protection) && !MI_PROT_IS_COPY(Protection)) ||
        (Access == MiFaultExecute && !MI_PROT_IS_EXECUTE(Protection)))
    {
        return STATUS_ACCESS_VIOLATION;
    }

    Proto = MiSegmentProto(Segment, Page);
    ProtoPte = MiArchPteRead(Proto);
    Frame = (ULONG)MiSoftValue(ProtoPte);

    if (MiSoftKind(ProtoPte) == MiSoftDecommitted)
        return STATUS_ACCESS_VIOLATION;

    if (!((MiSoftKind(ProtoPte) == MiSoftResident &&
           MiPfnShareIncrementIfMapped(&Space->System->Pfn, Frame, Proto, ProtoPte)) ||
          (MiSoftKind(ProtoPte) == MiSoftTransition &&
           MiPfnReactivateEx(&Space->System->Pfn, Frame, (ULONG64)(ULONG_PTR)Proto, Proto, ProtoPte,
                             MiSoftMake(MiSoftResident, MiSoftProtection(ProtoPte), Frame)))))
    {
        return STATUS_PENDING_PAGE_IN;
    }

    Status = MiMakePageValid(Space, VirtualAddress, Slot, TableFrame, Frame, Protection,
                             (BOOLEAN)(Access == MiFaultWrite && MI_PROT_IS_WRITABLE(Protection)));
    MI_ATOMIC_ADD64(&Space->PrototypeFaults, 1);
    return Status;
}

NTSTATUS
MiCopyOnWrite(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ MI_PTE Pte)
{
    PMI_SYSTEM System = Space->System;
    ULONG OldFrame = (ULONG)MiArchPteFrame(Pte);
    ULONG Protection = MiArchPteIsExecutable(Pte, (BOOLEAN)!Space->IsSystem) ? MI_PROT_EXECUTE_READWRITE
                                                                            : MI_PROT_READWRITE;
    PVOID Source;
    PVOID Target;
    ULONG Frame;

    if (Vad->Type == MiVadPrivate || !(MI_PFN_FLAGS(&System->Pfn.Pfn[OldFrame]) & MI_PFN_FLAG_PROTOTYPE))
        return STATUS_ACCESS_VIOLATION;

    if (!MiChargeCommit(Space, 1))
        return STATUS_COMMITMENT_LIMIT;

    Frame = MiPfnAllocatePage(&System->Pfn, 0);
    if (Frame == MI_FRAME_INVALID)
    {
        MiReturnCommit(Space, 1);
        return STATUS_NO_MEMORY;
    }

    Source = MiArchMapFrame(OldFrame);
    Target = MiArchMapFrame(Frame);
    RtlCopyMemory(Target, Source, PAGE_SIZE);
    MiArchUnmapFrame(Target);
    MiArchUnmapFrame(Source);

    MiPfnInitializePage(&System->Pfn, Frame, MiPtSlotAddress(Slot, TableFrame, VirtualAddress), TableFrame,
                        MiSoftMake(MiSoftDemandZero, Protection, 0), 0);

    MiReleaseViewPage(Space, Vad, VirtualAddress, Slot, TableFrame, MiSoftMake(MiSoftPrototype, Protection, 0));
    MiMakePageValid(Space, VirtualAddress, Slot, TableFrame, Frame, Protection, TRUE);

    MI_ATOMIC_ADD64(&Vad->CommitCharge, 1);
    MI_ATOMIC_ADD64(&Space->PrivatePages, 1);
    MI_ATOMIC_ADD64(&Space->CopyOnWriteFaults, 1);
    return STATUS_SUCCESS;
}

BOOLEAN
MiTrimPrototypePage(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame)
{
    MI_PTE Pte = MiArchPteRead(Slot);
    ULONG Protection = MiPteProtection(Space, Pte);
    ULONG Default;

    if (MiCloneTrimPage(Space, VirtualAddress, Slot, TableFrame))
        return TRUE;

    Default = MiViewDefaultProtection(Vad, VirtualAddress);

    MiReleaseViewPage(Space, Vad, VirtualAddress, Slot, TableFrame,
                      (Protection == Default) ? 0 : MiSoftMake(MiSoftPrototype, Protection, 0));
    return TRUE;
}

BOOLEAN
MiViewProtectionAllowed(
    _In_ PMI_SEGMENT Segment,
    _In_ ULONG Maximum,
    _In_ ULONG Protection)
{
    if (!MI_PROT_IS_ACCESSIBLE(Protection))
        return TRUE;

    if (Segment->Kind == MiSegmentImage)
        return TRUE;

    if (Maximum == 0)
        Maximum = Segment->Protection;

    if (MI_PROT_IS_READABLE(Protection) &&
        (!MI_PROT_IS_READABLE(Maximum) || !MI_PROT_IS_READABLE(Segment->Protection)))
    {
        return FALSE;
    }

    if (MI_PROT_IS_WRITABLE(Protection) &&
        (!MI_PROT_IS_WRITABLE(Maximum) || !MI_PROT_IS_WRITABLE(Segment->Protection)))
    {
        return FALSE;
    }

    if (MI_PROT_IS_EXECUTE(Protection) &&
        (!MI_PROT_IS_EXECUTE(Maximum) || !MI_PROT_IS_EXECUTE(Segment->Protection)))
    {
        return FALSE;
    }

    return TRUE;
}

NTSTATUS
MiSetMappedViewProtection(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Protection)
{
    PMI_SYSTEM System = Space->System;
    ULONG64 Va;

    if (Vad->Type == MiVadImage && MI_PROT_IS_WRITABLE(Protection))
        Protection = (Protection & ~MI_PROT_ACCESS_MASK) |
                     (MI_PROT_IS_EXECUTE(Protection) ? MI_PROT_EXECUTE_WRITECOPY : MI_PROT_WRITECOPY);

    for (Va = Start; Va < End; Va += PAGE_SIZE)
    {
        ULONG TableFrame;
        PMI_PTE Slot = MiPtEnsure(Space, Va, &TableFrame);
        MI_PTE Pte;

        if (Slot == NULL)
            return STATUS_NO_MEMORY;

        Pte = MiArchPteRead(Slot);

        if (MiCloneProtectPage(Space, Va, Slot, TableFrame, Protection))
            continue;

        if (Pte == 0 || MiSoftKind(Pte) == MiSoftPrototype ||
            (MiArchPteIsValid(Pte) && (MI_PFN_FLAGS(&System->Pfn.Pfn[MiArchPteFrame(Pte)]) & MI_PFN_FLAG_PROTOTYPE)))
        {
            MiReleaseViewPage(Space, Vad, Va, Slot, TableFrame, MiSoftMake(MiSoftPrototype, Protection, 0));
        }
        else
        {
            ULONG Private = Protection;
            NTSTATUS Status;

            if (MI_PROT_IS_COPY(Private))
                Private = (Private & ~MI_PROT_ACCESS_MASK) |
                          (MI_PROT_IS_EXECUTE(Private) ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE);

            Status = MiSetPrivatePageProtection(Space, Va, Private);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MiProtectMappedView(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Protection)
{
    if (!MiViewProtectionAllowed(Vad->Segment, Vad->MaximumProtection, Protection))
        return STATUS_SECTION_PROTECTION;

    return MiSetMappedViewProtection(Space, Vad, Start, End, Protection);
}

static
PMI_SEGMENT
MiDetachMappedView(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_VAD Vad,
    _In_ BOOLEAN RetainTables)
{
    PMI_SEGMENT Segment = Vad->Segment;
    ULONG64 End = MI_VAD_END(Vad);
    ULONG64 Va = MI_VAD_START(Vad);
    MI_TLB_BATCH Batch;
    BOOLEAN Owner = MiTlbBatchBegin(Space, &Batch);
    BOOLEAN Deferred = Space->TlbBatch->DeferTables;

    Space->TlbBatch->DeferTables = TRUE;

    while (Va < End)
    {
        ULONG64 TableVa = Va;
        ULONG64 TableEnd = MiPtNextTableBoundary(Space, Va);
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);

        if (TableEnd > End)
            TableEnd = End;
        if (Slot == NULL)
        {
            Va = TableEnd;
            continue;
        }

        for (; Va < TableEnd; Va += PAGE_SIZE, Slot++)
        {
            if (MiArchPteRead(Slot) != 0)
                MiReleaseViewPage(Space, Vad, Va, Slot, TableFrame, 0);
        }
        if (!Deferred)
        {
            if (RetainTables && !Space->IsSystem)
                MiPtCacheEmpty(Space, TableFrame, TableVa);
            else
                MiPtPruneEmpty(Space, TableVa);
        }
    }

    Space->TlbBatch->DeferTables = Deferred;
    MiTlbBatchEnd(Space, Owner);
    MiReturnCommit(Space, Vad->CommitCharge);
    MiVadRemove(&Space->VadRoot, &Vad->Node);

    MI_ATOMIC_ADD32(&Segment->MappedViews, -1);
    if (!Vad->CacheView)
        MI_ATOMIC_ADD32(&Segment->TruncationViews, -1);
    if (Vad->WritableUser)
        MI_ATOMIC_ADD32(&Segment->WritableUserViews, -1);
    return Segment;
}

VOID
MiRemoveMappedView(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_VAD Vad)
{
    PMI_SEGMENT Segment = MiDetachMappedView(Space, Vad, FALSE);

    MI_FREE(Vad);
    MiSegmentDereference(Segment);
}

NTSTATUS
MiMapView(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_SEGMENT Segment,
    _Inout_ PULONG64 BaseAddress,
    _In_ ULONG64 SectionOffset,
    _Inout_ PULONG64 ViewSize,
    _In_ ULONG Protection,
    _In_ ULONG AllocationType)
{
    return MiMapViewEx(Space, Segment, BaseAddress, SectionOffset, ViewSize, Protection, AllocationType, ~0ULL, 0, TRUE);
}

static
NTSTATUS
MiMapViewInternal(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_SEGMENT Segment,
    _Inout_ PULONG64 BaseAddress,
    _In_ ULONG64 SectionOffset,
    _Inout_ PULONG64 ViewSize,
    _In_ ULONG Protection,
    _In_ ULONG AllocationType,
    _In_ ULONG64 HighestAddress,
    _In_ ULONG MaximumProtection,
    _In_ BOOLEAN Inherit,
    _In_ BOOLEAN CacheView)
{
    ULONG64 Size = *ViewSize;
    ULONG64 Start = *BaseAddress;
    ULONG64 SegmentSize;
    ULONG64 Pages;
    PMI_VAD Vad;

    if ((SectionOffset & (MI_ALLOCATION_GRANULARITY - 1)) || (Start & (MI_ALLOCATION_GRANULARITY - 1)))
        return STATUS_INVALID_PARAMETER;

    if ((Protection & ~MI_PROT_MASK) ||
        (Protection != MI_PROT_NOACCESS &&
         (!MI_PROT_IS_ACCESSIBLE(Protection) || (Protection & MI_PROT_NOCACHE))))
        return STATUS_INVALID_PAGE_PROTECTION;

    if (!MiViewProtectionAllowed(Segment, MaximumProtection, Protection))
        return STATUS_SECTION_PROTECTION;

    SegmentSize = MiSegmentSize(Segment);
    if (SectionOffset >= SegmentSize)
        return STATUS_INVALID_VIEW_SIZE;

    if (Size == 0)
        Size = SegmentSize - SectionOffset;

    if (!(AllocationType & MI_MEM_RESERVE) && Size > MI_PAGE_ALIGN_UP(SegmentSize) - SectionOffset)
        return STATUS_INVALID_VIEW_SIZE;

    if (AllocationType & MI_MEM_LARGE_PAGES)
    {
        if (Segment->LargeFrames == NULL)
            return STATUS_INVALID_PARAMETER;
        return MiMapLargeSection(Space, Segment, BaseAddress, SectionOffset, ViewSize, Protection,
                                 AllocationType, HighestAddress, MaximumProtection, Inherit);
    }

    Vad = MiVadAllocateAndLock(Space);
    if (Vad == NULL)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_NO_MEMORY;
    }

    MI_ATOMIC_ADD32(&Segment->MappedViews, 1);
    MiSegmentReference(Segment);

    Size = MI_PAGE_ALIGN_UP(Size);
    Pages = Size >> PAGE_SHIFT;

    if (Start == 0)
    {
        ULONG64 Vpn;
        BOOLEAN Found = MiSpaceFindEmptyRange(Space, Pages, MI_ALLOCATION_GRANULARITY >> PAGE_SHIFT, 0,
                                              HighestAddress >> PAGE_SHIFT,
                                              (BOOLEAN)((AllocationType & MI_MEM_TOP_DOWN) != 0), &Vpn);

        if (!Found)
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            goto Fail;
        }

        Start = Vpn << PAGE_SHIFT;
    }
    else if (Start < Space->LowestVa || Start + Size - 1 > Space->HighestVa || Start + Size < Start ||
             Start + Size - 1 > HighestAddress ||
             MiVadFindOverlap(&Space->VadRoot, Start >> PAGE_SHIFT, (Start + Size - 1) >> PAGE_SHIFT) != NULL)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        MI_FREE(Vad);
        MI_ATOMIC_ADD32(&Segment->MappedViews, -1);
        MiSegmentDereference(Segment);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    Vad->Node.StartingVpn = Start >> PAGE_SHIFT;
    Vad->Node.EndingVpn = ((Start + Size) >> PAGE_SHIFT) - 1;
    Vad->Protection = Protection;
    Vad->MaximumProtection = (UCHAR)(MaximumProtection & MI_PROT_ACCESS_MASK);
    Vad->Type = (Segment->Kind == MiSegmentImage) ? MiVadImage : MiVadMapped;
    Vad->CopyOnWrite = (BOOLEAN)MI_PROT_IS_COPY(Protection);
    Vad->Inherit = Inherit;
    Vad->CacheView = CacheView;
    Vad->WritableUser = (BOOLEAN)(!Space->IsSystem && !CacheView && MI_PROT_IS_WRITABLE(Protection));
    Vad->Segment = Segment;
    Vad->SegmentPageOffset = SectionOffset >> PAGE_SHIFT;

    if (!MiVadInsert(&Space->VadRoot, &Vad->Node))
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        goto Fail;
    }

    if (!CacheView)
        MI_ATOMIC_ADD32(&Segment->TruncationViews, 1);
    if (Vad->WritableUser)
        MI_ATOMIC_ADD32(&Segment->WritableUserViews, 1);
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);

    *BaseAddress = Start;
    *ViewSize = Size;
    return STATUS_SUCCESS;

Fail:
    MI_FREE(Vad);
    MI_ATOMIC_ADD32(&Segment->MappedViews, -1);
    MiSegmentDereference(Segment);
    return STATUS_NO_MEMORY;
}

NTSTATUS
MiMapViewEx(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_SEGMENT Segment,
    _Inout_ PULONG64 BaseAddress,
    _In_ ULONG64 SectionOffset,
    _Inout_ PULONG64 ViewSize,
    _In_ ULONG Protection,
    _In_ ULONG AllocationType,
    _In_ ULONG64 HighestAddress,
    _In_ ULONG MaximumProtection,
    _In_ BOOLEAN Inherit)
{
    return MiMapViewInternal(Space, Segment, BaseAddress, SectionOffset, ViewSize,
                             Protection, AllocationType, HighestAddress, MaximumProtection, Inherit, FALSE);
}

NTSTATUS
MiMapCacheView(
    _Inout_ PMI_SEGMENT Segment,
    _Inout_ PULONG64 BaseAddress,
    _In_ ULONG64 SectionOffset,
    _Inout_ PULONG64 ViewSize)
{
    return MiMapViewInternal(&Segment->System->SystemSpace, Segment, BaseAddress, SectionOffset, ViewSize,
                             MI_PROT_READWRITE, MI_MEM_RESERVE, ~0ULL, 0, FALSE, TRUE);
}

NTSTATUS
MiUnmapView(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 BaseAddress)
{
    PMI_SEGMENT Segment;
    PMI_VAD Vad;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Vad = MiVadLocate(Space, BaseAddress);
    if (Vad != NULL && Vad->Type == MiVadLarge && Vad->Segment != NULL)
    {
        Segment = MiReleaseLargePagesLocked(Space, Vad);
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        MiSegmentDereference(Segment);
        return STATUS_SUCCESS;
    }
    if (Vad == NULL || Vad->Type == MiVadPrivate || MI_VAD_IS_DIRECT(Vad))
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_NOT_MAPPED_VIEW;
    }

    Segment = MiDetachMappedView(Space, Vad, TRUE);
    if (MiVadCacheFree(Space, Vad))
        Vad = NULL;
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);

    if (Vad != NULL)
        MI_FREE(Vad);
    MiSegmentDereference(Segment);
    return STATUS_SUCCESS;
}

NTSTATUS
MiSetRangeModified(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 BaseAddress,
    _In_ ULONG64 Length)
{
    PMI_SYSTEM System = Space->System;
    ULONG64 End = MI_PAGE_ALIGN_UP(BaseAddress + Length);
    ULONG64 Va = MI_PAGE_ALIGN_DOWN(BaseAddress);

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    while (Va < End)
    {
        PMI_PTE Slot = MiPtLookup(Space, Va, NULL);
        MI_PTE Pte;

        if (Slot == NULL)
        {
            ULONG64 Next = MiPtNextTableBoundary(Space, Va);

            Va = (Next < End) ? Next : End;
            continue;
        }

        Pte = MiArchPteRead(Slot);
        if (MiArchPteIsValid(Pte))
        {
            MiPfnSetModified(&System->Pfn, (ULONG)MiArchPteFrame(Pte));

            if (MiArchPteIsDirty(Pte))
            {
                MiArchPteWrite(Slot, MiArchPteSetDirty(Pte, FALSE));
                MiArchTlbInvalidate(Va, 1, TRUE);
            }
        }

        Va += PAGE_SIZE;
    }

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return STATUS_SUCCESS;
}

NTSTATUS
MiFlushVirtualMemory(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PULONG64 BaseAddress,
    _Inout_ PULONG64 RegionSize)
{
    ULONG64 Start = MI_PAGE_ALIGN_DOWN(*BaseAddress);
    NTSTATUS Status = STATUS_SUCCESS;
    PMI_SEGMENT Segment;
    BOOLEAN Writable;
    ULONG64 Offset;
    ULONG64 End;
    PMI_VAD Vad;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Vad = MiVadLocate(Space, Start);
    if (Vad == NULL || (Vad->Type != MiVadMapped && Vad->Type != MiVadImage &&
                        !(Vad->Type == MiVadLarge && Vad->Segment != NULL)))
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_NOT_MAPPED_VIEW;
    }

    End = (*RegionSize == 0) ? MI_VAD_END(Vad) : MI_PAGE_ALIGN_UP(*BaseAddress + *RegionSize);
    if (End > MI_VAD_END(Vad))
        End = MI_VAD_END(Vad);

    Segment = Vad->Segment;
    Writable = (BOOLEAN)(Segment->Kind == MiSegmentDataFile && !Vad->CopyOnWrite);
    Offset = (Vad->SegmentPageOffset << PAGE_SHIFT) + (Start - MI_VAD_START(Vad));
    MiSegmentReference(Segment);
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);

    *BaseAddress = Start;
    *RegionSize = End - Start;

    if (Writable)
    {
        MiSetRangeModified(Space, Start, End - Start);
        Status = MiSegmentFlush(Segment, Offset, End - Start);
    }

    MiSegmentDereference(Segment);
    return Status;
}

NTSTATUS
MiSegmentExtend(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 NewSizeInBytes)
{
    ULONG64 NewPages = MI_PAGE_ALIGN_UP(NewSizeInBytes) >> PAGE_SHIFT;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Segment->Kind == MiSegmentImage || Segment->LargeFrames != NULL)
        return STATUS_INVALID_PARAMETER;

    if (NewSizeInBytes > (1ULL << 40) * PAGE_SIZE)
        return STATUS_SECTION_TOO_BIG;

    MI_MUTEX_ACQUIRE(&Segment->Lock);

    if (NewSizeInBytes > MiSegmentSize(Segment))
    {
        if (NewPages > MiSegmentPages(Segment))
        {
            LONG64 Extra = (LONG64)(NewPages - MiSegmentPages(Segment));

            if (Segment->Kind == MiSegmentPageFileBacked && !Segment->Reserved &&
                !MiSegmentChargeCommit(Segment, Extra))
            {
                Status = STATUS_COMMITMENT_LIMIT;
            }
            else
            {
                KIRQL OldIrql;

                MI_SPIN_ACQUIRE(&Segment->System->SegmentListLock, &OldIrql);
                Status = MiSegmentGrow(Segment, NewPages);
                MI_SPIN_RELEASE(&Segment->System->SegmentListLock, OldIrql);
            }
        }

        if (NT_SUCCESS(Status))
        {
            Segment->ReadGeneration++;
            MI_ATOMIC_WRITE64(&Segment->SizeInBytes, (LONG64)NewSizeInBytes);
        }
    }

    MI_MUTEX_RELEASE(&Segment->Lock);
    return Status;
}

static
NTSTATUS
MiSegmentWriteFrames(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Offset,
    _In_ ULONG Bytes,
    _In_ const ULONG *Frames,
    _In_ ULONG PageCount)
{
    NTSTATUS Status;

    if (Bytes == 0 || (Segment->FileOps.Write == NULL && Segment->FileOps.WriteFrames == NULL))
        return STATUS_SUCCESS;

    if (Segment->FileOps.WriteFrames != NULL)
    {
        Status = Segment->FileOps.WriteFrames(Segment->FileContext, Offset, Bytes, Frames, PageCount);
    }
    else
    {
        PVOID Mapping;

        MI_ASSERT(PageCount == 1);
        Mapping = MiArchMapFrame(Frames[0]);
        Status = Segment->FileOps.Write(Segment->FileContext, Offset, Bytes, Mapping);
        MiArchUnmapFrame(Mapping);
    }

    if (NT_SUCCESS(Status))
        MI_ATOMIC_ADD64(&Segment->PagesWritten, PageCount);

    return Status;
}

static
NTSTATUS
MiSegmentWritePage(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG Frame,
    _In_ MI_PTE Original)
{
    ULONG64 Offset = MiSoftValue(Original) << MI_SECTOR_SHIFT;

    return MiSegmentWriteFrames(Segment, Offset, MiSegmentPageBytes(Segment, Offset), &Frame, 1);
}

NTSTATUS
MiSegmentFlush(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length)
{
    PMI_SYSTEM System = Segment->System;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG64 First = Offset >> PAGE_SHIFT;
    ULONG64 Last;
    ULONG64 Page;
    ULONG Limit = Segment->FileOps.WriteFrames != NULL ? MI_MAX_FILE_WRITE_PAGES : 1;

    if (Segment->Kind != MiSegmentDataFile || Length == 0)
        return STATUS_SUCCESS;

    if (Offset + Length < Offset || Offset + Length > ~(ULONG64)(PAGE_SIZE - 1))
        return STATUS_INVALID_PARAMETER;

    MI_MUTEX_ACQUIRE(&Segment->FlushLock);
    MI_MUTEX_ACQUIRE(&Segment->Lock);

    Last = MI_PAGE_ALIGN_UP(Offset + Length) >> PAGE_SHIFT;
    if (Last > MiSegmentPages(Segment))
        Last = MiSegmentPages(Segment);

    for (Page = First; Page < Last;)
    {
        ULONG Frames[MI_MAX_FILE_WRITE_PAGES];
        ULONG Count = 0;
        ULONG Bytes = 0;
        ULONG64 RunPage = Page;
        ULONG64 RunOffset = 0;
        ULONG64 BridgeEnd = Page;
        NTSTATUS WriteStatus;
        ULONG i;

        while (Page < Last && Count < Limit)
        {
            PMI_PTE Proto = MiSegmentProto(Segment, Page);
            MI_PTE Pte = MiArchPteRead(Proto);
            MI_SOFT_KIND Kind = MiSoftKind(Pte);
            ULONG64 FileOffset;
            ULONG PageBytes;
            ULONG Frame;

            if (Kind != MiSoftResident && Kind != MiSoftTransition)
                break;

            if (!(MI_PFN_FLAGS(&System->Pfn.Pfn[MiSoftValue(Pte)]) & MI_PFN_FLAG_MODIFIED) &&
                Page >= BridgeEnd)
            {
                ULONG64 Next;
                ULONG64 End = (Last < RunPage + Limit) ? Last : RunPage + Limit;

                if (Count == 0)
                    break;
                for (Next = Page + 1; Next < End; Next++)
                {
                    MI_PTE NextPte = MiArchPteRead(MiSegmentProto(Segment, Next));
                    MI_SOFT_KIND NextKind = MiSoftKind(NextPte);

                    if (NextKind != MiSoftResident && NextKind != MiSoftTransition)
                        break;
                    if (MI_PFN_FLAGS(&System->Pfn.Pfn[MiSoftValue(NextPte)]) & MI_PFN_FLAG_MODIFIED)
                    {
                        BridgeEnd = Next;
                        break;
                    }
                }
                if (Page >= BridgeEnd)
                    break;
            }

            WriteStatus = MiSegmentAcquirePage(Segment, Proto, TRUE, &Frame);
            if (!NT_SUCCESS(WriteStatus))
            {
                Status = WriteStatus;
                break;
            }

            FileOffset = MiSoftValue(System->Pfn.Pfn[Frame].OriginalPte) << MI_SECTOR_SHIFT;
            if (Count != 0 && FileOffset != RunOffset + Count * PAGE_SIZE)
            {
                MiSegmentReleasePage(Segment, Proto, Frame);
                break;
            }
            if (!MiPfnClearModified(&System->Pfn, Frame) && Count == 0)
            {
                MiSegmentReleasePage(Segment, Proto, Frame);
                break;
            }

            if (Count == 0)
                RunOffset = FileOffset;
            Frames[Count++] = Frame;
            PageBytes = MiSegmentPageBytes(Segment, FileOffset);
            Bytes += PageBytes;
            Page++;
            if (PageBytes != PAGE_SIZE)
                break;
        }

        if (Count == 0)
        {
            Page++;
            continue;
        }

        MI_MUTEX_RELEASE(&Segment->Lock);
        WriteStatus = MiSegmentWriteFrames(Segment, RunOffset, Bytes, Frames, Count);
        MI_MUTEX_ACQUIRE(&Segment->Lock);
        for (i = 0; i < Count; i++)
        {
            if (!NT_SUCCESS(WriteStatus))
                MiPfnSetModified(&System->Pfn, Frames[i]);
            MiSegmentReleasePage(Segment, MiSegmentProto(Segment, RunPage + i), Frames[i]);
        }
        if (!NT_SUCCESS(WriteStatus))
            Status = WriteStatus;
    }

    MI_MUTEX_RELEASE(&Segment->Lock);
    MI_MUTEX_RELEASE(&Segment->FlushLock);
    return Status;
}

BOOLEAN
MiSegmentPurge(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length)
{
    BOOLEAN Complete = TRUE;
    ULONG64 First = MI_PAGE_ALIGN_UP(Offset) >> PAGE_SHIFT;
    ULONG64 Last;
    ULONG64 Page;

    MI_MUTEX_ACQUIRE(&Segment->Lock);
    Segment->ReadGeneration++;

    Last = (Length == 0) ? MiSegmentPages(Segment) : ((Offset + Length) >> PAGE_SHIFT);
    if (Last > MiSegmentPages(Segment))
        Last = MiSegmentPages(Segment);

    for (Page = First; Page < Last; Page++)
    {
        PMI_PTE Proto = MiSegmentProto(Segment, Page);

        MI_SOFT_KIND Kind = MiSoftKind(MiArchPteRead(Proto));

        if (Kind == MiSoftResident)
        {
            Complete = FALSE;
            continue;
        }

        if (Kind == MiSoftTransition)
            MiSegmentDiscardPage(Segment, Proto);
    }

    MI_MUTEX_RELEASE(&Segment->Lock);
    return Complete;
}

BOOLEAN
MiSegmentIsResident(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length)
{
    BOOLEAN Resident = TRUE;
    ULONG64 Last;
    ULONG64 Page;

    if (Length == 0)
        return TRUE;
    if (Offset + Length < Offset || Offset + Length > ~(ULONG64)(PAGE_SIZE - 1))
        return FALSE;
    if (!MI_MUTEX_TRY_ACQUIRE(&Segment->Lock))
        return FALSE;

    Last = MI_PAGE_ALIGN_UP(Offset + Length) >> PAGE_SHIFT;
    if (Last > MiSegmentPages(Segment))
    {
        Last = MiSegmentPages(Segment);
        Resident = FALSE;
    }

    for (Page = Offset >> PAGE_SHIFT; Page < Last && Resident; Page++)
    {
        MI_SOFT_KIND Kind = MiSoftKind(MiArchPteRead(MiSegmentProto(Segment, Page)));

        Resident = (BOOLEAN)(Kind == MiSoftResident || Kind == MiSoftTransition);
    }

    MI_MUTEX_RELEASE(&Segment->Lock);
    return Resident;
}

typedef struct _MI_SEGMENT_READ
{
    LIST_ENTRY Link;
    PMI_SEGMENT Segment;
    PMI_PTE Proto;
    MI_PTE Original;
    ULONG64 Generation;
    PVOID Mapping;
    ULONG Frame;
    ULONG Bytes;
} MI_SEGMENT_READ, *PMI_SEGMENT_READ;

VOID
MiSegmentDrainReads(
    _Inout_ PMI_SEGMENT Segment,
    _Inout_ PMI_ASYNC_DRAIN Drain)
{
    BOOLEAN Pending;

    MI_MUTEX_ACQUIRE(&Segment->Lock);
    Pending = (BOOLEAN)(Segment->PendingReadCount != 0);
    if (Pending)
        InsertTailList(&Segment->ReadDrains, &Drain->Link);
    MI_MUTEX_RELEASE(&Segment->Lock);
    if (!Pending)
        Drain->Complete(Drain->Context);
}

static
VOID
MiSegmentReadComplete(
    _In_opt_ PVOID Context,
    _In_ NTSTATUS Status)
{
    PMI_SEGMENT_READ Read = Context;
    PMI_SEGMENT Segment = Read->Segment;
    PMI_SYSTEM System = Segment->System;
    PMI_SEGMENT Victim;
    LIST_ENTRY Drains;

    InitializeListHead(&Drains);

    if (NT_SUCCESS(Status) && Read->Bytes < PAGE_SIZE)
        RtlZeroMemory((PUCHAR)Read->Mapping + Read->Bytes, PAGE_SIZE - Read->Bytes);
    MiArchUnmapFrame(Read->Mapping);

    MI_MUTEX_ACQUIRE(&Segment->Lock);
    RemoveEntryList(&Read->Link);
    Segment->PendingReadCount--;
    if (NT_SUCCESS(Status) && Segment->ReadGeneration == Read->Generation &&
        MiArchPteRead(Read->Proto) == Read->Original)
    {
        MiPfnInitializePage(&System->Pfn, Read->Frame, (ULONG64)(ULONG_PTR)Read->Proto, 0,
                             Read->Original, MI_PFN_FLAG_PROTOTYPE);
        MiArchPteWrite(Read->Proto, MiSoftMake(MiSoftResident, MiSoftProtection(Read->Original), Read->Frame));
        MiSegmentReleasePage(Segment, Read->Proto, Read->Frame);
        MI_ATOMIC_ADD64(&Segment->PagesRead, 1);
    }
    else
    {
        MiPfnShareDecrement(&System->Pfn, Read->Frame, TRUE);
    }
    if (Segment->PendingReadCount == 0)
    {
        while (!IsListEmpty(&Segment->ReadDrains))
        {
            PLIST_ENTRY Entry = Segment->ReadDrains.Flink;

            RemoveEntryList(Entry);
            InsertTailList(&Drains, Entry);
        }
    }
    MiSegmentRelease(Segment, FALSE, &Victim);
    MI_MUTEX_RELEASE(&Segment->Lock);
    MI_FREE(Read);
    if (Victim != NULL)
        MiSegmentDelete(Victim, TRUE);
    while (!IsListEmpty(&Drains))
    {
        PMI_ASYNC_DRAIN Drain = CONTAINING_RECORD(Drains.Flink, MI_ASYNC_DRAIN, Link);

        RemoveEntryList(&Drain->Link);
        Drain->Complete(Drain->Context);
    }
}

NTSTATUS
MiSegmentPrefetch(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length)
{
    ULONG64 Page;
    ULONG64 Last;

    if (Length == 0)
        return STATUS_SUCCESS;
    if (Offset + Length < Offset || Offset + Length > ~(ULONG64)(PAGE_SIZE - 1))
        return STATUS_INVALID_PARAMETER;
    if (Segment->Kind != MiSegmentDataFile || Segment->FileOps.ReadAsync == NULL)
        return STATUS_NOT_SUPPORTED;

    Last = MI_PAGE_ALIGN_UP(Offset + Length) >> PAGE_SHIFT;
    for (Page = Offset >> PAGE_SHIFT; Page < Last; Page++)
    {
        PMI_SEGMENT_READ Read;
        PMI_PTE Proto;
        MI_PTE Original;
        PLIST_ENTRY Link;
        BOOLEAN Pending = FALSE;
        NTSTATUS Status;
        ULONG64 FileOffset;

        if (!MI_MUTEX_TRY_ACQUIRE(&Segment->Lock))
            return STATUS_CANT_WAIT;
        if (Page >= MiSegmentPages(Segment))
        {
            MI_MUTEX_RELEASE(&Segment->Lock);
            return STATUS_INVALID_PARAMETER;
        }
        Proto = MiSegmentProto(Segment, Page);
        Original = MiArchPteRead(Proto);
        for (Link = Segment->PendingReads.Flink; Link != &Segment->PendingReads; Link = Link->Flink)
        {
            Read = CONTAINING_RECORD(Link, MI_SEGMENT_READ, Link);
            if (Read->Proto == Proto && Read->Generation == Segment->ReadGeneration)
            {
                Pending = TRUE;
                break;
            }
        }
        if (Pending || MiSoftKind(Original) != MiSoftSubsection)
        {
            MI_MUTEX_RELEASE(&Segment->Lock);
            continue;
        }
        if (Segment->PendingReadCount >= MI_MAX_FILE_IO_PAGES)
        {
            MI_MUTEX_RELEASE(&Segment->Lock);
            return STATUS_CANT_WAIT;
        }
        Read = MI_ALLOCATE(sizeof(*Read));
        if (Read == NULL)
        {
            MI_MUTEX_RELEASE(&Segment->Lock);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Read->Frame = MiPfnAllocatePage(&Segment->System->Pfn, 0);
        if (Read->Frame == MI_FRAME_INVALID)
        {
            MI_FREE(Read);
            MI_MUTEX_RELEASE(&Segment->Lock);
            return STATUS_NO_MEMORY;
        }
        FileOffset = MiSoftValue(Original) << MI_SECTOR_SHIFT;
        Read->Segment = Segment;
        Read->Proto = Proto;
        Read->Original = Original;
        Read->Generation = Segment->ReadGeneration;
        Read->Mapping = MiArchMapFrame(Read->Frame);
        Read->Bytes = MiSegmentReadBytes(Segment, FileOffset);
        MiSegmentReference(Segment);
        InsertTailList(&Segment->PendingReads, &Read->Link);
        Segment->PendingReadCount++;
        MI_MUTEX_RELEASE(&Segment->Lock);

        Status = Segment->FileOps.ReadAsync(Segment->FileContext, FileOffset, Read->Frame, Read->Mapping,
                                             MiSegmentReadComplete, Read);
        if (!NT_SUCCESS(Status))
        {
            MiSegmentReadComplete(Read, Status);
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
MiSegmentMakeResident(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length)
{
    return MiSegmentMakeResidentBeyond(Segment, Offset, Length, ~0ULL);
}

/* Segment->Lock is held. Only complete, contiguous file pages participate.
 * A failed speculative read must not make the demanded page fail: leave all
 * prototypes untouched and let the caller retry that page on its own. */
static
ULONG
MiSegmentReadCluster(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Page,
    _In_ ULONG64 Last,
    _In_ ULONG64 ValidDataLength)
{
    ULONG Frames[MI_MAX_FILE_IO_PAGES];
    MI_PTE Originals[MI_MAX_FILE_IO_PAGES];
    ULONG Count = 0, i;
    ULONG64 Offset;
    NTSTATUS Status;

    if (Segment->FileOps.ReadPages == NULL)
        return 0;
    Originals[0] = MiArchPteRead(MiSegmentProto(Segment, Page));
    if (MiSoftKind(Originals[0]) != MiSoftSubsection)
        return 0;
    Offset = MiSoftValue(Originals[0]) << MI_SECTOR_SHIFT;
    /* The image header uses the single-page normalization callback. */
    if (Segment->Kind == MiSegmentImage && Offset == 0)
        return 0;

    while (Count < MI_MAX_FILE_IO_PAGES && Page + Count < Last)
    {
        MI_PTE Pte = MiArchPteRead(MiSegmentProto(Segment, Page + Count));
        ULONG64 FileOffset = Offset + Count * PAGE_SIZE;
        ULONG Frame;

        if (MiSoftKind(Pte) != MiSoftSubsection ||
            (MiSoftValue(Pte) << MI_SECTOR_SHIFT) != FileOffset ||
            MiSegmentPageBytes(Segment, FileOffset) != PAGE_SIZE ||
            FileOffset >= ValidDataLength || ValidDataLength - FileOffset < PAGE_SIZE)
            break;

        Frame = MiPfnAllocatePage(&Segment->System->Pfn, 0);
        if (Frame == MI_FRAME_INVALID)
            break;
        Frames[Count] = Frame;
        Originals[Count++] = Pte;
    }

    if (Count > 1)
    {
        Status = Segment->FileOps.ReadPages(Segment->FileContext, Offset, Frames, Count);
        if (NT_SUCCESS(Status))
        {
            for (i = 0; i < Count; i++)
            {
                PMI_PTE Proto = MiSegmentProto(Segment, Page + i);

                MiPfnInitializePage(&Segment->System->Pfn, Frames[i], (ULONG64)(ULONG_PTR)Proto,
                                    0, Originals[i], MI_PFN_FLAG_PROTOTYPE);
                MiArchPteWrite(Proto, MiSoftMake(MiSoftResident, MiSoftProtection(Originals[i]), Frames[i]));
                MiSegmentReleasePage(Segment, Proto, Frames[i]);
            }
            MI_ATOMIC_ADD64(&Segment->PagesRead, Count);
            return Count;
        }
    }

    for (i = 0; i < Count; i++)
        MiPfnShareDecrement(&Segment->System->Pfn, Frames[i], TRUE);
    return 0;
}

NTSTATUS
MiSegmentFaultIn(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Page)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Frame;

    MI_MUTEX_ACQUIRE(&Segment->Lock);
    if (Page >= MiSegmentPages(Segment))
    {
        Status = STATUS_ACCESS_VIOLATION;
    }
    else
    {
        PMI_PTE Proto = MiSegmentProto(Segment, Page);
        MI_SOFT_KIND Kind = MiSoftKind(MiArchPteRead(Proto));

        if (Kind != MiSoftResident && Kind != MiSoftTransition &&
            MiSegmentReadCluster(Segment, Page, MiSegmentPages(Segment), ~0ULL) == 0)
        {
            Status = MiSegmentMaterialize(Segment, Proto, ~0ULL, &Frame);
            if (NT_SUCCESS(Status))
                MiSegmentReleasePage(Segment, Proto, Frame);
        }
    }
    MI_MUTEX_RELEASE(&Segment->Lock);
    return Status;
}

NTSTATUS
MiSegmentMakeResidentBeyond(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length,
    _In_ ULONG64 ValidDataLength)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG64 Last;
    ULONG64 Page;

    MI_MUTEX_ACQUIRE(&Segment->Lock);

    Last = MI_PAGE_ALIGN_UP(Offset + Length) >> PAGE_SHIFT;
    if (Last > MiSegmentPages(Segment))
        Last = MiSegmentPages(Segment);

    for (Page = Offset >> PAGE_SHIFT; Page < Last && NT_SUCCESS(Status); Page++)
    {
        PMI_PTE Proto = MiSegmentProto(Segment, Page);
        MI_PTE Pte = MiArchPteRead(Proto);
        MI_SOFT_KIND Kind = MiSoftKind(Pte);
        ULONG Frame;
        ULONG Cluster;

        if (Kind == MiSoftResident || Kind == MiSoftTransition || Pte == 0)
            continue;

        Cluster = MiSegmentReadCluster(Segment, Page, Last, ValidDataLength);
        if (Cluster != 0)
        {
            Page += Cluster - 1;
            continue;
        }

        Status = MiSegmentMaterialize(Segment, Proto, ValidDataLength, &Frame);
        if (NT_SUCCESS(Status))
            MiSegmentReleasePage(Segment, Proto, Frame);
    }

    MI_MUTEX_RELEASE(&Segment->Lock);
    return Status;
}

NTSTATUS
MiSegmentMarkDirty(
    _Inout_ PMI_SEGMENT Segment,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length)
{
    PMI_SYSTEM System = Segment->System;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG64 Last;
    ULONG64 Page;

    MI_MUTEX_ACQUIRE(&Segment->Lock);

    Last = MI_PAGE_ALIGN_UP(Offset + Length) >> PAGE_SHIFT;
    if (Last > MiSegmentPages(Segment))
        Last = MiSegmentPages(Segment);

    for (Page = Offset >> PAGE_SHIFT; Page < Last && NT_SUCCESS(Status);)
    {
        PMI_PTE Slots[MI_MAX_FILE_IO_PAGES];
        ULONG Count = (Last - Page > MI_MAX_FILE_IO_PAGES) ? MI_MAX_FILE_IO_PAGES : (ULONG)(Last - Page);
        ULONG Done;
        ULONG i;

        for (i = 0; i < Count; i++)
            Slots[i] = MiSegmentProto(Segment, Page + i);
        Done = MiPfnMarkMappedPagesModified(&System->Pfn, Slots, Count);
        for (i = 0; i < Count; i++)
        {
            ULONG Frame;

            if ((Done & (1UL << i)) != 0 || MiArchPteRead(Slots[i]) == 0)
                continue;
            Status = MiSegmentAcquirePage(Segment, Slots[i], TRUE, &Frame);
            if (!NT_SUCCESS(Status))
                break;
            MiPfnSetModified(&System->Pfn, Frame);
            MiSegmentReleasePage(Segment, Slots[i], Frame);
        }
        Page += Count;
    }

    MI_MUTEX_RELEASE(&Segment->Lock);
    return Status;
}

NTSTATUS
MiWritePrototypePage(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG Frame)
{
    PMI_PFN Entry = &System->Pfn.Pfn[Frame];
    NTSTATUS Status = STATUS_UNEXPECTED_IO_ERROR;
    PMI_SEGMENT Segment;
    ULONG64 Page;

    Segment = MiSegmentFromPrototype(System, Entry->PteAddress, &Page);
    if (Segment != NULL)
    {
        KIRQL OldIrql;

        MI_MUTEX_ACQUIRE(&Segment->FlushLock);
        Status = MiSegmentWritePage(Segment, Frame, Entry->OriginalPte);
        MiPfnWriteComplete(&System->Pfn, Frame, Entry->OriginalPte, (BOOLEAN)NT_SUCCESS(Status));
        MI_MUTEX_RELEASE(&Segment->FlushLock);

        MI_SPIN_ACQUIRE(&System->SegmentListLock, &OldIrql);
        Segment->ActiveWriters--;
        MI_SPIN_RELEASE(&System->SegmentListLock, OldIrql);
        return Status;
    }

    MiPfnWriteComplete(&System->Pfn, Frame, Entry->OriginalPte, FALSE);
    return Status;
}
