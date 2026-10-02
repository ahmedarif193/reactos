/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/core/pfndb.c
 * PURPOSE:     Physical page database management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include <nvs/include/mipfndb.h>

FORCEINLINE
PMI_PFN_SHARD
MiPfnShardOf(PMI_PFN_DATABASE Db, ULONG Frame, UCHAR State)
{
    ULONG Zone = (Frame < Db->DirectFrames) ? 0 : MI_PFN_SHARDS;

    if (State == MiPageStandby || State == MiPageModified)
        return &Db->Shard[Zone + ((Frame >> MI_PFN_LIST_SHARD_SHIFT) & (MI_PFN_SHARDS - 1))];

    return &Db->Shard[Zone + ((Frame >> Db->RegionShift) & (MI_PFN_SHARDS - 1))];
}

FORCEINLINE
ULONG
MiPfnShardCount(PMI_PFN_DATABASE Db)
{
    return (Db->DirectFrames < Db->FrameCount) ? MI_PFN_ZONES * MI_PFN_SHARDS : MI_PFN_SHARDS;
}

static
VOID
MiPfnLockAtDispatch(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    volatile UCHAR *Flags = &Db->Pfn[Frame].Flags;

    while (MI_ATOMIC_OR8(Flags, MI_PFN_FLAG_LOCK) & MI_PFN_FLAG_LOCK)
    {
        do
        {
            MI_PAUSE();
        } while (MI_ATOMIC_READ8(Flags) & MI_PFN_FLAG_LOCK);
    }
}

static
VOID
MiPfnUnlockAtDispatch(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    MI_ASSERT(MI_PFN_FLAGS(&Db->Pfn[Frame]) & MI_PFN_FLAG_LOCK);
    MI_ATOMIC_AND8(&Db->Pfn[Frame].Flags, (UCHAR)~MI_PFN_FLAG_LOCK);
}

KIRQL
MiPfnLock(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    KIRQL OldIrql;

    MI_RAISE_TO_DISPATCH(&OldIrql);
    MiPfnLockAtDispatch(Db, Frame);
    return OldIrql;
}

VOID
MiPfnUnlock(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ KIRQL OldIrql)
{
    MiPfnUnlockAtDispatch(Db, Frame);
    MI_RESTORE_IRQL(OldIrql);
}

static
VOID
MiPfnListInsert(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ UCHAR State,
    _In_ BOOLEAN AtHead)
{
    PMI_PFN_SHARD Shard = MiPfnShardOf(Db, Frame, State);
    PMI_PFN_LIST List = &Shard->List[State];
    PMI_PFN Entry = &Db->Pfn[Frame];
    KIRQL OldIrql;

    MI_SPIN_ACQUIRE(&Shard->Lock, &OldIrql);

    Entry->State = State;
    if (AtHead)
    {
        Entry->Blink = MI_FRAME_INVALID;
        Entry->Flink = List->Head;
        if (List->Head != MI_FRAME_INVALID)
            Db->Pfn[List->Head].Blink = Frame;
        else
            List->Tail = Frame;
        List->Head = Frame;
    }
    else
    {
        Entry->Flink = MI_FRAME_INVALID;
        Entry->Blink = List->Tail;
        if (List->Tail != MI_FRAME_INVALID)
            Db->Pfn[List->Tail].Flink = Frame;
        else
            List->Head = Frame;
        List->Tail = Frame;
    }

    List->Count++;
    MI_SPIN_RELEASE(&Shard->Lock, OldIrql);
}

static
BOOLEAN
MiPfnListRemove(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ UCHAR ExpectedState)
{
    PMI_PFN_SHARD Shard = MiPfnShardOf(Db, Frame, ExpectedState);
    PMI_PFN Entry = &Db->Pfn[Frame];
    PMI_PFN_LIST List;
    KIRQL OldIrql;

    MI_SPIN_ACQUIRE(&Shard->Lock, &OldIrql);

    if (Entry->State != ExpectedState)
    {
        MI_SPIN_RELEASE(&Shard->Lock, OldIrql);
        return FALSE;
    }

    List = &Shard->List[ExpectedState];
    if (Entry->Blink != MI_FRAME_INVALID)
        Db->Pfn[Entry->Blink].Flink = Entry->Flink;
    else
        List->Head = Entry->Flink;

    if (Entry->Flink != MI_FRAME_INVALID)
        Db->Pfn[Entry->Flink].Blink = Entry->Blink;
    else
        List->Tail = Entry->Blink;

    List->Count--;
    Entry->State = MiPageTransition;
    Entry->Flink = Entry->Blink = MI_FRAME_INVALID;
    MI_SPIN_RELEASE(&Shard->Lock, OldIrql);
    return TRUE;
}

static
ULONG
MiPfnListPop(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ UCHAR State,
    _In_ ULONG StartShard,
    _In_ ULONG Zone)
{
    ULONG Attempt;

    for (Attempt = 0; Attempt < MI_PFN_SHARDS; Attempt++)
    {
        PMI_PFN_SHARD Shard = &Db->Shard[Zone + ((StartShard + Attempt) & (MI_PFN_SHARDS - 1))];
        ULONG Frame;
        KIRQL OldIrql;

        if (MI_PEEK(Shard->List[State].Count) == 0)
            continue;

        MI_SPIN_ACQUIRE(&Shard->Lock, &OldIrql);
        Frame = Shard->List[State].Head;
        MI_SPIN_RELEASE(&Shard->Lock, OldIrql);

        if (Frame != MI_FRAME_INVALID && MiPfnListRemove(Db, Frame, State))
            return Frame;
    }

    return MI_FRAME_INVALID;
}

static
VOID
MiPfnSetRegionShift(
    _Inout_ PMI_PFN_DATABASE Db)
{
    Db->RegionShift = MI_PFN_SHARD_SHIFT;
    if (!Db->RegionRotation)
        return;

    while (Db->DirectFrames != 0 && ((Db->DirectFrames - 1) >> Db->RegionShift) >= MI_PFN_SHARDS)
        Db->RegionShift++;
}

VOID
MiPfnDbInitialize(
    _Out_ PMI_PFN_DATABASE Db,
    _In_ PMI_PFN Array,
    _In_ ULONG FrameCount,
    _In_ ULONG CpuCount)
{
    ULONG i, j;

    RtlZeroMemory(Db, sizeof(*Db));
    Db->Pfn = Array;
    Db->FrameCount = FrameCount;
    Db->DirectFrames = FrameCount;
    Db->CacheCount = (CpuCount == 0) ? 1 : ((CpuCount > MI_PFN_CPU_CACHES) ? MI_PFN_CPU_CACHES : CpuCount);
    MiPfnSetRegionShift(Db);

    for (i = 0; i < MI_PFN_ZONES * MI_PFN_SHARDS; i++)
    {
        MI_SPIN_INIT(&Db->Shard[i].Lock);
        for (j = 0; j < MiPageListCount; j++)
            Db->Shard[i].List[j].Head = Db->Shard[i].List[j].Tail = MI_FRAME_INVALID;
    }

    for (i = 0; i < MI_PFN_CPU_CACHES; i++)
    {
        MI_SPIN_INIT(&Db->Cache[i].Lock);
        Db->Cache[i].Rotor = i;
    }

    for (i = 0; i < FrameCount; i++)
    {
        RtlZeroMemory(&Array[i], sizeof(MI_PFN));
        Array[i].State = MiPageUnusable;
        Array[i].Flink = Array[i].Blink = MI_FRAME_INVALID;
    }
}

VOID
MiPfnDbAddRange(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG FirstFrame,
    _In_ ULONG Count)
{
    ULONG Frame;

    for (Frame = FirstFrame; Frame < FirstFrame + Count && Frame < Db->FrameCount; Frame++)
    {
        if (Frame == 0 || Db->Pfn[Frame].State != MiPageUnusable)
            continue;

        MiPfnListInsert(Db, Frame, MiPageFree, FALSE);
    }
}

static
VOID
MiPfnZeroFrame(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    PVOID Mapping = MiPfnMapFrame(Db, Frame);

    RtlZeroMemory(Mapping, PAGE_SIZE);
    MiPfnUnmapFrame(Db, Mapping);
}

NTSTATUS
MiPfnSetCache(PMI_PFN_DATABASE Db, ULONG Frame, ULONG Flags)
{
    NTSTATUS Status;

    if (Flags & ~MI_LEAF_CACHE_MASK)
        return STATUS_INVALID_PARAMETER;
    Status = (Frame < Db->DirectFrames) ? MiArchSetFrameCache(Frame, Flags) : STATUS_SUCCESS;
    if (NT_SUCCESS(Status))
        MI_ATOMIC_WRITE32(&Db->Pfn[Frame].CacheFlags, Flags);
    return Status;
}

static VOID
MiPfnRestoreCache(PMI_PFN_DATABASE Db, ULONG Frame)
{
    PMI_PFN Entry = &Db->Pfn[Frame];

    if (!(MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PAGE_TABLE) && Entry->CacheFlags != 0)
    {
        NTSTATUS Status = MiPfnSetCache(Db, Frame, 0);

        MI_ASSERT(NT_SUCCESS(Status));
    }
}

static
ULONG
MiPfnCacheRefillRotated(
    _Inout_ PMI_PFN_DATABASE Db,
    _Inout_ PMI_PFN_CPU_CACHE Cache,
    _In_ UCHAR State,
    _Inout_ PULONG Frames,
    _Inout_ PULONG Depth)
{
    ULONG Run[MI_PFN_CACHE_BATCH];
    ULONG Start = Cache->Rotor;
    ULONG Wanted = (*Depth < MI_PFN_CACHE_BATCH) ? MI_PFN_CACHE_BATCH - *Depth : 0;
    ULONG Count = 0;
    ULONG Taken;

    do
    {
        ULONG Attempt;

        Taken = 0;
        for (Attempt = 0; Attempt < MI_PFN_SHARDS && Count < Wanted; Attempt++)
        {
            ULONG Index = Start + 2 * Attempt + Attempt / (MI_PFN_SHARDS / 2);
            PMI_PFN_SHARD Shard = &Db->Shard[Index & (MI_PFN_SHARDS - 1)];
            PMI_PFN_LIST List = &Shard->List[State];
            ULONG Limit = (Wanted - Count < MI_PFN_CACHE_RUN) ? Wanted : Count + MI_PFN_CACHE_RUN;
            KIRQL OldIrql;

            if (MI_PEEK(List->Count) == 0)
                continue;

            MI_SPIN_ACQUIRE(&Shard->Lock, &OldIrql);

            while (List->Head != MI_FRAME_INVALID && Count < Limit)
            {
                ULONG Frame = List->Head;
                PMI_PFN Entry = &Db->Pfn[Frame];

                List->Head = Entry->Flink;
                if (List->Head != MI_FRAME_INVALID)
                    Db->Pfn[List->Head].Blink = MI_FRAME_INVALID;
                else
                    List->Tail = MI_FRAME_INVALID;

                List->Count--;
                Entry->State = MiPageCached;
                Entry->Flink = Entry->Blink = MI_FRAME_INVALID;
                Run[Count++] = Frame;
                Taken++;
            }

            MI_SPIN_RELEASE(&Shard->Lock, OldIrql);
        }
    } while (Taken != 0 && Count < Wanted);

    if (Count != 0)
    {
        Cache->Rotor++;
        Cache->Refills++;
    }

    while (Count != 0)
        Frames[(*Depth)++] = Run[--Count];

    return *Depth;
}

static
ULONG
MiPfnCacheRefill(
    _Inout_ PMI_PFN_DATABASE Db,
    _Inout_ PMI_PFN_CPU_CACHE Cache,
    _In_ ULONG StartShard,
    _In_ UCHAR State,
    _Inout_ PULONG Frames,
    _Inout_ PULONG Depth)
{
    ULONG Attempt;

    if (Db->RegionRotation)
        return MiPfnCacheRefillRotated(Db, Cache, State, Frames, Depth);

    for (Attempt = 0; Attempt < MI_PFN_SHARDS && *Depth < MI_PFN_CACHE_BATCH; Attempt++)
    {
        PMI_PFN_SHARD Shard = &Db->Shard[(StartShard + Attempt) & (MI_PFN_SHARDS - 1)];
        PMI_PFN_LIST List = &Shard->List[State];
        KIRQL OldIrql;

        if (MI_PEEK(List->Count) == 0)
            continue;

        MI_SPIN_ACQUIRE(&Shard->Lock, &OldIrql);

        while (List->Head != MI_FRAME_INVALID && *Depth < MI_PFN_CACHE_BATCH)
        {
            ULONG Frame = List->Head;
            PMI_PFN Entry = &Db->Pfn[Frame];

            List->Head = Entry->Flink;
            if (List->Head != MI_FRAME_INVALID)
                Db->Pfn[List->Head].Blink = MI_FRAME_INVALID;
            else
                List->Tail = MI_FRAME_INVALID;

            List->Count--;
            Entry->State = MiPageCached;
            Entry->Flink = Entry->Blink = MI_FRAME_INVALID;
            Frames[(*Depth)++] = Frame;
        }

        MI_SPIN_RELEASE(&Shard->Lock, OldIrql);
    }

    Cache->Refills++;
    return *Depth;
}

static
VOID
MiPfnCacheDrain(
    _Inout_ PMI_PFN_DATABASE Db,
    _Inout_ PMI_PFN_CPU_CACHE Cache,
    _In_ ULONG Keep)
{
    while (Cache->Depth > Keep)
    {
        ULONG Frame = Cache->Frame[Cache->Depth - 1];
        PMI_PFN_SHARD Shard = MiPfnShardOf(Db, Frame, MiPageFree);
        PMI_PFN_LIST List = &Shard->List[MiPageFree];
        ULONG Index = Cache->Depth;
        KIRQL OldIrql;

        MI_SPIN_ACQUIRE(&Shard->Lock, &OldIrql);

        while (Index > Keep)
        {
            ULONG Candidate = Cache->Frame[Index - 1];
            PMI_PFN Entry = &Db->Pfn[Candidate];

            if (MiPfnShardOf(Db, Candidate, MiPageFree) != Shard)
            {
                Index--;
                continue;
            }

            Entry->State = MiPageFree;
            Entry->Blink = MI_FRAME_INVALID;
            Entry->Flink = List->Head;
            if (List->Head != MI_FRAME_INVALID)
                Db->Pfn[List->Head].Blink = Candidate;
            else
                List->Tail = Candidate;
            List->Head = Candidate;
            List->Count++;

            Cache->Frame[Index - 1] = Cache->Frame[Cache->Depth - 1];
            Cache->Depth--;
            Index--;
        }

        MI_SPIN_RELEASE(&Shard->Lock, OldIrql);
    }

    Cache->Drains++;
}

static
ULONG
MiPfnReclaimStandby(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG StartShard,
    _In_ ULONG Zone)
{
    ULONG Frame = MI_FRAME_INVALID;
    ULONG Attempt;
    KIRQL OldIrql;

    for (Attempt = 0; Attempt < MI_PFN_SHARDS && Frame == MI_FRAME_INVALID; Attempt++)
    {
        ULONG Candidate;
        PMI_PFN_SHARD Shard = &Db->Shard[Zone + ((StartShard + Attempt) & (MI_PFN_SHARDS - 1))];

        if (MI_PEEK(Shard->List[MiPageStandby].Count) == 0)
            continue;

        MI_SPIN_ACQUIRE(&Shard->Lock, &OldIrql);
        Candidate = Shard->List[MiPageStandby].Head;
        MI_SPIN_RELEASE(&Shard->Lock, OldIrql);

        if (Candidate == MI_FRAME_INVALID)
            continue;

        OldIrql = MiPfnLock(Db, Candidate);
        if (Db->Pfn[Candidate].ReferenceCount == 0 && MiPfnListRemove(Db, Candidate, MiPageStandby))
        {
            if (Db->Repurpose != NULL)
                Db->Repurpose(Db, Candidate);

            Frame = Candidate;
            MI_ATOMIC_ADD64(&Db->Repurposed, 1);
        }
        MiPfnUnlock(Db, Candidate, OldIrql);
    }

    return Frame;
}

ULONG
MiPfnAllocatePage(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Flags)
{
    ULONG Cpu = MI_CURRENT_CPU() % Db->CacheCount;
    PMI_PFN_CPU_CACHE Cache = &Db->Cache[Cpu];
    ULONG Frame = MI_FRAME_INVALID;
    BOOLEAN IsZero = FALSE;
    PMI_PFN Entry;
    KIRQL OldIrql;

    if ((Flags & MI_ALLOCATE_HIGH) && MI_PEEK(Db->Window.SlotCount) != 0)
    {
        if (Flags & MI_ALLOCATE_ZEROED)
        {
            Frame = MiPfnListPop(Db, MiPageZeroed, Cpu, MI_PFN_SHARDS);
            IsZero = (Frame != MI_FRAME_INVALID);
        }

        if (Frame == MI_FRAME_INVALID)
            Frame = MiPfnListPop(Db, MiPageFree, Cpu, MI_PFN_SHARDS);

        if (Frame == MI_FRAME_INVALID && !(Flags & MI_ALLOCATE_ZEROED))
        {
            Frame = MiPfnListPop(Db, MiPageZeroed, Cpu, MI_PFN_SHARDS);
            IsZero = (Frame != MI_FRAME_INVALID);
        }

        if (Frame == MI_FRAME_INVALID && !(Flags & MI_ALLOCATE_NO_RECLAIM))
            Frame = MiPfnReclaimStandby(Db, Cpu, MI_PFN_SHARDS);
    }

    if (Frame == MI_FRAME_INVALID)
    {
        MI_SPIN_ACQUIRE(&Cache->Lock, &OldIrql);
        if ((Flags & MI_ALLOCATE_ZEROED) &&
            (Cache->ZeroDepth != 0 ||
             MiPfnCacheRefill(Db, Cache, Cpu, MiPageZeroed, Cache->ZeroFrame, &Cache->ZeroDepth) != 0))
        {
            Frame = Cache->ZeroFrame[--Cache->ZeroDepth];
            IsZero = TRUE;
        }
        else if (Cache->Depth != 0 || MiPfnCacheRefill(Db, Cache, Cpu, MiPageFree, Cache->Frame, &Cache->Depth) != 0)
        {
            Frame = Cache->Frame[--Cache->Depth];
        }
        else if (!(Flags & MI_ALLOCATE_ZEROED) &&
                 (Cache->ZeroDepth != 0 ||
                  MiPfnCacheRefill(Db, Cache, Cpu, MiPageZeroed, Cache->ZeroFrame, &Cache->ZeroDepth) != 0))
        {
            Frame = Cache->ZeroFrame[--Cache->ZeroDepth];
            IsZero = TRUE;
        }
        Cache->Allocations++;
        MI_SPIN_RELEASE(&Cache->Lock, OldIrql);
    }

    if (Frame == MI_FRAME_INVALID)
    {
        ULONG Other;

        for (Other = 0; Other < Db->CacheCount && Frame == MI_FRAME_INVALID; Other++)
        {
            PMI_PFN_CPU_CACHE Victim = &Db->Cache[Other];

            if (MI_PEEK(Victim->Depth) == 0 && MI_PEEK(Victim->ZeroDepth) == 0)
                continue;

            MI_SPIN_ACQUIRE(&Victim->Lock, &OldIrql);
            if (Victim->Depth != 0)
            {
                Frame = Victim->Frame[--Victim->Depth];
            }
            else if (Victim->ZeroDepth != 0)
            {
                Frame = Victim->ZeroFrame[--Victim->ZeroDepth];
                IsZero = TRUE;
            }
            MI_SPIN_RELEASE(&Victim->Lock, OldIrql);
        }
    }

    if (Frame == MI_FRAME_INVALID && !(Flags & MI_ALLOCATE_NO_RECLAIM))
        Frame = MiPfnReclaimStandby(Db, Cpu, 0);

    if (Frame == MI_FRAME_INVALID)
        return MI_FRAME_INVALID;

    MiPfnRestoreCache(Db, Frame);
    if ((Flags & MI_ALLOCATE_ZEROED) && !IsZero)
    {
        MiPfnZeroFrame(Db, Frame);
        MI_ATOMIC_ADD64(&Db->ZeroedOnDemand, 1);
    }

    Entry = &Db->Pfn[Frame];
    OldIrql = MiPfnLock(Db, Frame);
    Entry->State = MiPageActive;
    Entry->ReferenceCount = 1;
    Entry->ShareCount = 1;
    Entry->PteAddress = 0;
    Entry->OriginalPte = 0;
    Entry->PteFrame = 0;
    Entry->UsedEntries = 0;
    MI_ATOMIC_AND8(&Entry->Flags, MI_PFN_FLAG_LOCK);
    MiPfnUnlock(Db, Frame, OldIrql);
    return Frame;
}

static
BOOLEAN
MiPfnClaimFrame(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    PMI_PFN Entry = &Db->Pfn[Frame];
    BOOLEAN Claimed;
    KIRQL OldIrql;

    OldIrql = MiPfnLock(Db, Frame);
    Claimed = MiPfnListRemove(Db, Frame, MiPageFree) || MiPfnListRemove(Db, Frame, MiPageZeroed);

    if (!Claimed && Entry->ReferenceCount == 0 && MiPfnListRemove(Db, Frame, MiPageStandby))
    {
        if (Db->Repurpose != NULL)
            Db->Repurpose(Db, Frame);

        MI_ATOMIC_ADD64(&Db->Repurposed, 1);
        Claimed = TRUE;
    }

    if (Claimed)
    {
        MiPfnRestoreCache(Db, Frame);
        Entry->State = MiPageActive;
        Entry->ReferenceCount = 1;
        Entry->ShareCount = 1;
        Entry->PteAddress = 0;
        Entry->OriginalPte = 0;
        Entry->PteFrame = 0;
        Entry->UsedEntries = 0;
        MI_ATOMIC_AND8(&Entry->Flags, MI_PFN_FLAG_LOCK);
    }

    MiPfnUnlock(Db, Frame, OldIrql);
    return Claimed;
}

static
ULONG
MiPfnScanContiguous(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Count,
    _In_ ULONG LowestFrame,
    _In_ ULONG HighestFrame,
    _In_ ULONG BoundaryFrames)
{
    ULONG Start = LowestFrame;

    while (Start + Count - 1 <= HighestFrame && Start + Count - 1 >= Start)
    {
        ULONG Claimed = 0;
        ULONG Index;

        if (BoundaryFrames != 0 && (Start / BoundaryFrames) != ((Start + Count - 1) / BoundaryFrames))
        {
            Start = (Start / BoundaryFrames + 1) * BoundaryFrames;
            continue;
        }

        for (Index = Count; Index != 0; Index--)
        {
            UCHAR State = MI_PEEK(Db->Pfn[Start + Index - 1].State);

            if (State != MiPageFree && State != MiPageZeroed && State != MiPageStandby && State != MiPageCached)
                break;
        }

        if (Index != 0)
        {
            Start += Index;
            continue;
        }

        while (Claimed < Count && MiPfnClaimFrame(Db, Start + Claimed))
            Claimed++;

        if (Claimed == Count)
            return Start;

        Index = Claimed;
        while (Claimed != 0)
            MiPfnShareDecrement(Db, Start + --Claimed, TRUE);

        Start += Index + 1;
    }

    return MI_FRAME_INVALID;
}

ULONG
MiPfnAllocateContiguous(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Count,
    _In_ ULONG LowestFrame,
    _In_ ULONG HighestFrame,
    _In_ ULONG BoundaryFrames)
{
    ULONG Found = MI_FRAME_INVALID;
    ULONG Pass;

    if (Count == 0 || HighestFrame >= Db->DirectFrames)
        HighestFrame = Db->DirectFrames - 1;

    if (Count == 0 || LowestFrame > HighestFrame || HighestFrame - LowestFrame + 1 < Count)
        return MI_FRAME_INVALID;

    for (Pass = 0; Pass < 2 && Found == MI_FRAME_INVALID; Pass++)
    {
        ULONG Hint = MI_PEEK(Db->ContiguousHint);

        if (Pass != 0)
            MiPfnDrainCaches(Db);

        if (Hint > LowestFrame && Hint <= HighestFrame)
        {
            Found = MiPfnScanContiguous(Db, Count, Hint, HighestFrame, BoundaryFrames);

            if (Found == MI_FRAME_INVALID)
            {
                ULONG Limit = (HighestFrame - Hint >= Count - 1) ? Hint + Count - 2 : HighestFrame;

                Found = MiPfnScanContiguous(Db, Count, LowestFrame, Limit, BoundaryFrames);
            }
        }
        else
        {
            Found = MiPfnScanContiguous(Db, Count, LowestFrame, HighestFrame, BoundaryFrames);
        }
    }

    if (Found != MI_FRAME_INVALID)
        Db->ContiguousHint = Found + Count;

    return Found;
}

VOID
MiPfnMarkInUse(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG FirstFrame,
    _In_ ULONG Count)
{
    ULONG Frame;

    for (Frame = FirstFrame; Frame < FirstFrame + Count && Frame < Db->FrameCount; Frame++)
    {
        PMI_PFN Entry = &Db->Pfn[Frame];

        if (Entry->State != MiPageUnusable)
            continue;

        Entry->State = MiPageActive;
        Entry->ReferenceCount = 1;
        Entry->ShareCount = 1;
    }
}

ULONG
MiPfnAllocatePageInRange(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG LowestFrame,
    _In_ ULONG HighestFrame,
    _Inout_ PULONG Cursor)
{
    ULONG Scanned;
    ULONG Span;

    if (HighestFrame >= Db->DirectFrames)
        HighestFrame = Db->DirectFrames - 1;

    if (LowestFrame > HighestFrame)
        return MI_FRAME_INVALID;

    Span = HighestFrame - LowestFrame + 1;

    if (*Cursor < LowestFrame || *Cursor > HighestFrame)
        *Cursor = LowestFrame;

    for (Scanned = 0; Scanned < Span; Scanned++)
    {
        ULONG Frame = *Cursor;
        UCHAR State = Db->Pfn[Frame].State;

        *Cursor = (Frame == HighestFrame) ? LowestFrame : Frame + 1;

        if (State != MiPageFree && State != MiPageZeroed && State != MiPageStandby)
            continue;

        if (MiPfnClaimFrame(Db, Frame))
            return Frame;
    }

    return MI_FRAME_INVALID;
}

VOID
MiPfnInitializePage(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ ULONG64 PteAddress,
    _In_ ULONG PteFrame,
    _In_ MI_PTE OriginalPte,
    _In_ UCHAR Flags)
{
    PMI_PFN Entry = &Db->Pfn[Frame];
    KIRQL OldIrql;

    OldIrql = MiPfnLock(Db, Frame);
    Entry->PteAddress = PteAddress;
    Entry->PteFrame = PteFrame;
    Entry->OriginalPte = OriginalPte;
    MI_ATOMIC_OR8(&Entry->Flags, Flags & (UCHAR)~MI_PFN_FLAG_LOCK);
    MiPfnUnlock(Db, Frame, OldIrql);
}

VOID
MiPfnSetModified(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    MI_ATOMIC_OR8(&Db->Pfn[Frame].Flags, MI_PFN_FLAG_MODIFIED);
}

ULONG
MiPfnMarkMappedPagesModified(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ PMI_PTE *Slots,
    _In_ ULONG Count)
{
    ULONG Done = 0;
    KIRQL OldIrql;
    ULONG i;

    MI_ASSERT(Count <= 32);
    MI_RAISE_TO_DISPATCH(&OldIrql);
    for (i = 0; i < Count; i++)
    {
        MI_PTE Pte = MiArchPteRead(Slots[i]);
        ULONG Frame = (ULONG)MiSoftValue(Pte);
        PMI_PFN Entry;

        if (MiSoftKind(Pte) != MiSoftResident || Frame >= Db->FrameCount)
            continue;

        MiPfnLockAtDispatch(Db, Frame);
        Entry = &Db->Pfn[Frame];
        if (MiArchPteRead(Slots[i]) == Pte && Entry->State == MiPageActive && Entry->ShareCount > 0 &&
            Entry->PteAddress == (ULONG64)(ULONG_PTR)Slots[i] && (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PROTOTYPE))
        {
            MiPfnSetModified(Db, Frame);
            Done |= 1UL << i;
        }
        MiPfnUnlockAtDispatch(Db, Frame);
    }
    MI_RESTORE_IRQL(OldIrql);
    return Done;
}

static
VOID
MiPfnFreeLocked(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ KIRQL LockIrql)
{
    PMI_PFN_CPU_CACHE Cache = &Db->Cache[MI_CURRENT_CPU() % Db->CacheCount];
    PMI_PFN Entry = &Db->Pfn[Frame];
    BOOLEAN Drained = FALSE;
    KIRQL OldIrql;

    MiPfnRestoreCache(Db, Frame);
    Entry->ReferenceCount = 0;
    Entry->ShareCount = 0;
    Entry->PteAddress = 0;
    Entry->OriginalPte = 0;
    Entry->PteFrame = 0;
    Entry->UsedEntries = 0;
    Entry->State = (Frame < Db->DirectFrames) ? MiPageCached : MiPageTransition;
    MI_ATOMIC_AND8(&Entry->Flags, MI_PFN_FLAG_LOCK);
    MiPfnUnlock(Db, Frame, LockIrql);

    if (Frame >= Db->DirectFrames)
    {
        MiPfnListInsert(Db, Frame, MiPageFree, TRUE);
        return;
    }

    MI_SPIN_ACQUIRE(&Cache->Lock, &OldIrql);
    if (Cache->Depth == MI_PFN_CACHE_DEPTH)
    {
        MiPfnCacheDrain(Db, Cache, MI_PFN_CACHE_DEPTH - MI_PFN_CACHE_BATCH);
        Drained = TRUE;
    }
    Cache->Frame[Cache->Depth++] = Frame;
    MI_SPIN_RELEASE(&Cache->Lock, OldIrql);

    if (Drained && Db->FreeNotify != NULL)
        Db->FreeNotify(Db);
}

VOID
MiPfnFreePage(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    KIRQL OldIrql = MiPfnLock(Db, Frame);

    MiPfnFreeLocked(Db, Frame, OldIrql);
}

VOID
MiPfnDrainCaches(
    _Inout_ PMI_PFN_DATABASE Db)
{
    ULONG i;

    for (i = 0; i < Db->CacheCount; i++)
    {
        KIRQL OldIrql;
        ULONG Slot;

        MI_SPIN_ACQUIRE(&Db->Cache[i].Lock, &OldIrql);
        MiPfnCacheDrain(Db, &Db->Cache[i], 0);
        for (Slot = 0; Slot < Db->Cache[i].ZeroDepth; Slot++)
            MiPfnListInsert(Db, Db->Cache[i].ZeroFrame[Slot], MiPageZeroed, TRUE);
        Db->Cache[i].ZeroDepth = 0;
        MI_SPIN_RELEASE(&Db->Cache[i].Lock, OldIrql);
    }
}

ULONG
MiPfnZeroFreePages(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG MaximumPages)
{
    ULONG RunLength = Db->RegionRotation ? MI_PFN_CACHE_RUN : MaximumPages;
    ULONG Zeroed = 0;
    ULONG Taken;

    do
    {
        ULONG Shard;

        Taken = 0;
        for (Shard = 0; Shard < MiPfnShardCount(Db) && Zeroed < MaximumPages; Shard++)
        {
            ULONG Run;

            for (Run = 0; Run < RunLength && Zeroed < MaximumPages; Run++)
            {
                PMI_PFN_LIST List = &Db->Shard[Shard].List[MiPageFree];
                ULONG Frame;
                KIRQL OldIrql;

                if (MI_PEEK(List->Count) == 0)
                    break;

                MI_SPIN_ACQUIRE(&Db->Shard[Shard].Lock, &OldIrql);
                Frame = List->Head;
                MI_SPIN_RELEASE(&Db->Shard[Shard].Lock, OldIrql);

                if (Frame == MI_FRAME_INVALID || !MiPfnListRemove(Db, Frame, MiPageFree))
                    break;

                MiPfnRestoreCache(Db, Frame);
                MiPfnZeroFrame(Db, Frame);
                MiPfnListInsert(Db, Frame, MiPageZeroed, FALSE);
                Zeroed++;
                Taken++;
            }
        }
    } while (Taken != 0 && Zeroed < MaximumPages);

    return Zeroed;
}

ULONG64
MiPfnListCount(
    _In_ PMI_PFN_DATABASE Db,
    _In_ UCHAR State)
{
    ULONG64 Total = 0;
    ULONG i;

    for (i = 0; i < MiPfnShardCount(Db); i++)
    {
        KIRQL OldIrql;

        MI_SPIN_ACQUIRE(&Db->Shard[i].Lock, &OldIrql);
        Total += Db->Shard[i].List[State].Count;
        MI_SPIN_RELEASE(&Db->Shard[i].Lock, OldIrql);
    }

    return Total;
}

ULONG64
MiPfnAvailablePages(
    _In_ PMI_PFN_DATABASE Db)
{
    ULONG64 Total = MiPfnListCount(Db, MiPageZeroed) + MiPfnListCount(Db, MiPageFree) +
                    MiPfnListCount(Db, MiPageStandby);
    ULONG i;

    for (i = 0; i < Db->CacheCount; i++)
    {
        KIRQL OldIrql;

        MI_SPIN_ACQUIRE(&Db->Cache[i].Lock, &OldIrql);
        Total += Db->Cache[i].Depth + Db->Cache[i].ZeroDepth;
        MI_SPIN_RELEASE(&Db->Cache[i].Lock, OldIrql);
    }

    return Total;
}

ULONG64
MiPfnAllocationCount(
    _In_ PMI_PFN_DATABASE Db)
{
    ULONG64 Total = 0;
    ULONG i;

    for (i = 0; i < Db->CacheCount; i++)
        Total += Db->Cache[i].Allocations;

    return Total;
}

static
VOID
MiPfnRetireLocked(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ KIRQL OldIrql)
{
    PMI_PFN Entry = &Db->Pfn[Frame];

    if (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_DELETED)
    {
        MiPfnFreeLocked(Db, Frame, OldIrql);
        return;
    }

    MiPfnListInsert(Db, Frame, (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_MODIFIED) ? MiPageModified : MiPageStandby, FALSE);
    MiPfnUnlock(Db, Frame, OldIrql);
}

VOID
MiPfnShareIncrement(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    MI_ATOMIC_ADD32(&Db->Pfn[Frame].ShareCount, 1);
}

VOID
MiPfnShareDecrementEx(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ BOOLEAN Delete,
    _Inout_opt_ PMI_PTE LastShareSlot,
    _In_ MI_PTE LastShareValue)
{
    PMI_PFN Entry = &Db->Pfn[Frame];
    KIRQL OldIrql;

    OldIrql = MiPfnLock(Db, Frame);
    MI_ASSERT(Entry->ShareCount > 0 && Entry->State == MiPageActive);

    if (Delete)
        MI_ATOMIC_OR8(&Entry->Flags, MI_PFN_FLAG_DELETED);

    if (Entry->ShareCount == 1 && LastShareSlot != NULL)
        MiArchPteWrite(LastShareSlot, LastShareValue);

    if (MI_ATOMIC_ADD32(&Entry->ShareCount, -1) - 1 != 0)
    {
        MiPfnUnlock(Db, Frame, OldIrql);
        return;
    }

    Entry->State = MiPageTransition;
    MI_ASSERT(Entry->ReferenceCount > 0);
    Entry->ReferenceCount--;

    if (Entry->ReferenceCount != 0)
    {
        MiPfnUnlock(Db, Frame, OldIrql);
        return;
    }

    MiPfnRetireLocked(Db, Frame, OldIrql);
}

VOID
MiPfnShareDecrement(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ BOOLEAN Delete)
{
    MiPfnShareDecrementEx(Db, Frame, Delete, NULL, 0);
}

BOOLEAN
MiPfnShareIncrementIfMapped(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ PMI_PTE Slot,
    _In_ MI_PTE Expected)
{
    BOOLEAN Taken = FALSE;
    KIRQL OldIrql;

    if (Frame >= Db->FrameCount)
        return FALSE;

    OldIrql = MiPfnLock(Db, Frame);
    if (MiArchPteRead(Slot) == Expected)
    {
        MI_ATOMIC_ADD32(&Db->Pfn[Frame].ShareCount, 1);
        Taken = TRUE;
    }
    MiPfnUnlock(Db, Frame, OldIrql);
    return Taken;
}

VOID
MiPfnReferenceLocked(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    Db->Pfn[Frame].ReferenceCount++;
}

VOID
MiPfnDereference(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    PMI_PFN Entry = &Db->Pfn[Frame];
    KIRQL OldIrql;

    OldIrql = MiPfnLock(Db, Frame);
    MI_ASSERT(Entry->ReferenceCount > 0);
    Entry->ReferenceCount--;

    if (Entry->ReferenceCount == 0 && Entry->ShareCount == 0)
    {
        MiPfnRetireLocked(Db, Frame, OldIrql);
        return;
    }

    MiPfnUnlock(Db, Frame, OldIrql);
}

BOOLEAN
MiPfnReactivateEx(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ ULONG64 PteAddress,
    _Inout_opt_ PMI_PTE Slot,
    _In_ MI_PTE Expected,
    _In_ MI_PTE NewValue)
{
    PMI_PFN Entry;
    BOOLEAN Taken;
    KIRQL OldIrql;

    if (Frame >= Db->FrameCount)
        return FALSE;

    Entry = &Db->Pfn[Frame];
    OldIrql = MiPfnLock(Db, Frame);

    if (Entry->PteAddress != PteAddress || (Slot != NULL && MiArchPteRead(Slot) != Expected))
    {
        MiPfnUnlock(Db, Frame, OldIrql);
        return FALSE;
    }

    Taken = MiPfnListRemove(Db, Frame, MiPageStandby) || MiPfnListRemove(Db, Frame, MiPageModified);

    if (!Taken && !(Entry->State == MiPageTransition && Entry->ReferenceCount != 0))
    {
        MiPfnUnlock(Db, Frame, OldIrql);
        return FALSE;
    }

    Entry->State = MiPageActive;
    Entry->ReferenceCount++;
    Entry->ShareCount = 1;

    if (Slot != NULL)
        MiArchPteWrite(Slot, NewValue);

    MiPfnUnlock(Db, Frame, OldIrql);
    return TRUE;
}

BOOLEAN
MiPfnReactivate(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ ULONG64 PteAddress)
{
    return MiPfnReactivateEx(Db, Frame, PteAddress, NULL, 0, 0);
}

ULONG
MiPfnTakeModified(
    _Inout_ PMI_PFN_DATABASE Db)
{
    ULONG Attempt;

    for (Attempt = 0; Attempt < MiPfnShardCount(Db); Attempt++)
    {
        PMI_PFN_SHARD Shard = &Db->Shard[Attempt];
        ULONG Frame;
        KIRQL OldIrql;

        MI_SPIN_ACQUIRE(&Shard->Lock, &OldIrql);
        Frame = Shard->List[MiPageModified].Head;
        MI_SPIN_RELEASE(&Shard->Lock, OldIrql);

        if (Frame == MI_FRAME_INVALID)
            continue;

        OldIrql = MiPfnLock(Db, Frame);
        if (MiPfnListRemove(Db, Frame, MiPageModified))
        {
            Db->Pfn[Frame].ReferenceCount++;
            MI_ATOMIC_OR8(&Db->Pfn[Frame].Flags, MI_PFN_FLAG_IN_FLIGHT);
            MI_ATOMIC_AND8(&Db->Pfn[Frame].Flags, (UCHAR)~MI_PFN_FLAG_MODIFIED);
            MiPfnUnlock(Db, Frame, OldIrql);
            return Frame;
        }
        MiPfnUnlock(Db, Frame, OldIrql);
    }

    return MI_FRAME_INVALID;
}

BOOLEAN
MiPfnClearModified(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    return (BOOLEAN)((MI_ATOMIC_AND8(&Db->Pfn[Frame].Flags, (UCHAR)~MI_PFN_FLAG_MODIFIED) &
                      MI_PFN_FLAG_MODIFIED) != 0);
}

BOOLEAN
MiPfnWriteComplete(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame,
    _In_ MI_PTE NewOriginalPte,
    _In_ BOOLEAN Success)
{
    PMI_PFN Entry = &Db->Pfn[Frame];
    BOOLEAN Deleted;
    KIRQL OldIrql;

    OldIrql = MiPfnLock(Db, Frame);
    MI_ATOMIC_AND8(&Entry->Flags, (UCHAR)~MI_PFN_FLAG_IN_FLIGHT);
    Deleted = (BOOLEAN)((MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_DELETED) != 0);

    if (Success)
        Entry->OriginalPte = NewOriginalPte;
    else
        MI_ATOMIC_OR8(&Entry->Flags, MI_PFN_FLAG_MODIFIED);

    MI_ASSERT(Entry->ReferenceCount > 0);
    Entry->ReferenceCount--;

    if (Entry->ReferenceCount == 0 && Entry->ShareCount == 0)
    {
        MiPfnRetireLocked(Db, Frame, OldIrql);
        return Deleted;
    }

    MiPfnUnlock(Db, Frame, OldIrql);
    return Deleted;
}

ULONG
MiPfnDbCheck(
    _In_ PMI_PFN_DATABASE Db)
{
    ULONG Errors = 0;
    ULONG s, l;

    for (s = 0; s < MI_PFN_ZONES * MI_PFN_SHARDS; s++)
    {
        for (l = 0; l < MiPageListCount; l++)
        {
            const MI_PFN_LIST *List = &Db->Shard[s].List[l];
            ULONG Frame = List->Head;
            ULONG Previous = MI_FRAME_INVALID;
            ULONG64 Walked = 0;

            while (Frame != MI_FRAME_INVALID && Walked <= Db->FrameCount)
            {
                if (Frame >= Db->FrameCount)
                {
                    Errors++;
                    break;
                }

                if (Db->Pfn[Frame].State != l || Db->Pfn[Frame].Blink != Previous ||
                    MiPfnShardOf(Db, Frame, (UCHAR)l) != &Db->Shard[s])
                {
                    Errors++;
                }

                Previous = Frame;
                Frame = Db->Pfn[Frame].Flink;
                Walked++;
            }

            if (List->Tail != Previous || Walked != List->Count)
                Errors++;
        }
    }

    for (s = 0; s < Db->CacheCount; s++)
    {
        if (Db->Cache[s].Depth > MI_PFN_CACHE_DEPTH)
        {
            Errors++;
            continue;
        }

        for (l = 0; l < Db->Cache[s].Depth; l++)
        {
            ULONG Frame = Db->Cache[s].Frame[l];

            if (Frame >= Db->FrameCount || Db->Pfn[Frame].State != MiPageCached)
                Errors++;
        }

        if (Db->Cache[s].ZeroDepth > MI_PFN_CACHE_BATCH)
        {
            Errors++;
            continue;
        }

        for (l = 0; l < Db->Cache[s].ZeroDepth; l++)
        {
            ULONG Frame = Db->Cache[s].ZeroFrame[l];

            if (Frame >= Db->FrameCount || Db->Pfn[Frame].State != MiPageCached)
                Errors++;
        }
    }

    return Errors;
}

VOID
MiPfnDbSetDirectFrames(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG64 DirectFrames)
{
    if (DirectFrames == 0 || DirectFrames > Db->FrameCount)
        DirectFrames = Db->FrameCount;

    Db->DirectFrames = (ULONG)DirectFrames;
    MiPfnSetRegionShift(Db);
}

VOID
MiPfnDbSetRegionRotation(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ BOOLEAN Enable)
{
    Db->RegionRotation = Enable;
    MiPfnSetRegionShift(Db);
}

#define MI_WINDOW_CLEAN 0
#define MI_WINDOW_BUSY  1
#define MI_WINDOW_STALE 2

/* TODO: verify against the future PPC32 and ARM32 backends. */
VOID
MiPfnWindowAttach(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG64 Base,
    _In_ PMI_PTE *Slots,
    _In_ PUCHAR States,
    _In_ ULONG SlotCount)
{
    PMI_PFN_WINDOW Window = &Db->Window;

    MI_SPIN_INIT(&Window->Lock);
    Window->Base = Base;
    Window->Slot = Slots;
    Window->State = States;
    Window->Hint = 0;
    Window->Busy = 0;
    Window->Stale = 0;
    RtlZeroMemory(States, SlotCount);
    Window->SlotCount = SlotCount;
}

PVOID
MiPfnWindowMap(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ ULONG Frame)
{
    PMI_PFN_WINDOW Window = &Db->Window;
    LONG Cache = (MI_PFN_FLAGS(&Db->Pfn[Frame]) & MI_PFN_FLAG_PAGE_TABLE)
                     ? 0 : MI_ATOMIC_READ32(&Db->Pfn[Frame].CacheFlags);
    PVOID Mapping;
    KIRQL OldIrql;
    ULONG Index;

    MI_ASSERT(Window->SlotCount != 0);
    if (Cache == MI_PFN_CACHE_UNASSIGNED)
        Cache = 0;

    for (;;)
    {
        MI_SPIN_ACQUIRE(&Window->Lock, &OldIrql);

        if (Window->Stale != 0 && Window->Busy + Window->Stale == Window->SlotCount)
        {
            MiArchTlbInvalidate(Window->Base, Window->SlotCount, TRUE);
            for (Index = 0; Index < Window->SlotCount; Index++)
            {
                if (Window->State[Index] == MI_WINDOW_STALE)
                    Window->State[Index] = MI_WINDOW_CLEAN;
            }
            Window->Stale = 0;
            Window->Flushes++;
        }

        if (Window->Busy != Window->SlotCount)
            break;

        MI_SPIN_RELEASE(&Window->Lock, OldIrql);
        MI_PAUSE();
    }

    Index = Window->Hint;
    while (Window->State[Index] != MI_WINDOW_CLEAN)
        Index = (Index + 1 == Window->SlotCount) ? 0 : Index + 1;

    Window->State[Index] = MI_WINDOW_BUSY;
    Window->Busy++;
    Window->Hint = (Index + 1 == Window->SlotCount) ? 0 : Index + 1;
    Window->Maps++;
    MiArchPteWrite(Window->Slot[Index],
                   MiArchPteMakeLeaf(Frame, MI_PROT_READWRITE,
                                     MI_LEAF_GLOBAL | MI_LEAF_DIRTY | ((ULONG)Cache & MI_LEAF_CACHE_MASK)));
    Mapping = MI_WINDOW_POINTER(Window, Index);
    MI_SPIN_RELEASE(&Window->Lock, OldIrql);
    return Mapping;
}

VOID
MiPfnWindowUnmap(
    _Inout_ PMI_PFN_DATABASE Db,
    _In_ PVOID Mapping)
{
    PMI_PFN_WINDOW Window = &Db->Window;
    ULONG64 Index = (Window->SlotCount != 0) ? MI_WINDOW_SLOT(Window, Mapping) : ~0ULL;
    KIRQL OldIrql;

    if (Index >= Window->SlotCount)
    {
        MiArchUnmapFrame(Mapping);
        return;
    }

    MI_SPIN_ACQUIRE(&Window->Lock, &OldIrql);
    MI_ASSERT(Window->State[Index] == MI_WINDOW_BUSY);
    MI_WINDOW_RELEASE(Window, (ULONG)Index);
    MiArchPteWrite(Window->Slot[Index], 0);
    Window->State[Index] = MI_WINDOW_STALE;
    Window->Busy--;
    Window->Stale++;
    MI_SPIN_RELEASE(&Window->Lock, OldIrql);
}
