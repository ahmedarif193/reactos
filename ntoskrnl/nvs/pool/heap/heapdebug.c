/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/heap/heapdebug.c
 * PURPOSE:     Pool heap diagnostics and validation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/poolenv.h"
#include "../include/poolheap.h"

FORCEINLINE
BOOLEAN
PoolDebugLfhBit(
    _In_ PPOOL_LFH_SUBSEGMENT Subsegment,
    _In_ ULONG Bit)
{
    return (BOOLEAN)((Subsegment->Bitmap[Bit >> 6] >> (Bit & 63)) & 1);
}

static
VOID
PoolDebugLfhInfo(
    _In_ PPOOL_LFH_SUBSEGMENT Subsegment,
    _In_ ULONG Bit,
    _Out_ PPOOL_BLOCK_INFO Info)
{
    Info->Size = Subsegment->BlockSize;
    Info->Tag = Subsegment->Tags[Bit];
    Info->Flags = Subsegment->Flags[Bit];
    Info->Kind = PoolBlockLfh;
    Info->Trailer = 0;
}

static
BOOLEAN
PoolDebugLarge(
    _In_ PPOOL_VA_REGION Region,
    _In_ PVOID Address,
    _Out_ PVOID *Block,
    _Out_ PPOOL_BLOCK_INFO Info)
{
    SIZE_T Index = ((ULONG_PTR)Address - Region->Base) >> POOL_UNIT_SHIFT;
    PPOOL_UNIT Unit;

    while (Index != 0 && Region->Directory[Index].State == PoolUnitTail)
        Index--;

    Unit = &Region->Directory[Index];
    if (Unit->State != PoolUnitLarge || Unit->PageCount == 0)
        return FALSE;

    *Block = (PVOID)(Region->Base + (Index << POOL_UNIT_SHIFT));
    if ((ULONG_PTR)Address >= (ULONG_PTR)*Block + ((SIZE_T)Unit->PageCount << PAGE_SHIFT))
        return FALSE;

    Info->Size = (SIZE_T)Unit->PageCount << PAGE_SHIFT;
    Info->Tag = Unit->Tag;
    Info->Flags = Unit->Flags;
    Info->Kind = PoolBlockLarge;
    Info->Trailer = 0;
    return TRUE;
}

BOOLEAN
PoolHeapDescribe(
    _In_ PPOOL_HEAP Heap,
    _In_ PVOID Address,
    _Out_ PVOID *Block,
    _Out_ PPOOL_BLOCK_INFO Info,
    _Out_ PBOOLEAN Allocated)
{
    PPOOL_VA_REGION Region = PoolHeapRegionFromAddress(Heap, Address);
    PPOOL_SEGMENT Segment;
    PPOOL_PAGE_RANGE Range;
    PVOID Owner;
    UCHAR State;

    *Block = NULL;
    *Allocated = FALSE;
    RtlZeroMemory(Info, sizeof(*Info));

    if (Region == NULL)
        return FALSE;

    State = PoolVaUnit(Region, Address)->State;
    if (State == PoolUnitLarge || State == PoolUnitTail)
    {
        *Allocated = PoolDebugLarge(Region, Address, Block, Info);
        return *Allocated;
    }

    if (State != PoolUnitSegment)
        return FALSE;

    Segment = PoolSegFromAddress(Address);
    if (Segment->Signature != POOL_SEGMENT_SIGNATURE || Segment->Heap != Heap)
        return FALSE;

    Range = &Segment->Range[PoolSegPageIndex(Address)];
    Owner = PoolSegPageAddress(Segment, Range->First);

    if (Range->State == PoolRangeBlock)
    {
        Range = &Segment->Range[Range->First];
        *Block = Owner;
        *Allocated = TRUE;
        Info->Size = (SIZE_T)Range->Pages << PAGE_SHIFT;
        Info->Tag = Range->Tag;
        Info->Flags = Range->Flags;
        Info->Kind = PoolBlockRange;
        return TRUE;
    }

    if (Range->State == PoolRangeLfh)
    {
        PPOOL_LFH_SUBSEGMENT Subsegment = Owner;
        ULONG Bit;

        if (Subsegment->Signature != POOL_LFH_SIGNATURE || !PoolLfhBlockIndex(Subsegment, Address, FALSE, &Bit))
            return FALSE;

        *Block = PoolLfhBlockAddress(Subsegment, Bit);
        *Allocated = PoolDebugLfhBit(Subsegment, Bit);
        PoolDebugLfhInfo(Subsegment, Bit, Info);
        return TRUE;
    }

    if (Range->State == PoolRangeVs)
    {
        PVOID Cursor = NULL;

        while (PoolVsWalk(Heap, Owner, &Cursor, Block, Info, Allocated))
        {
            if ((ULONG_PTR)Address >= (ULONG_PTR)*Block - sizeof(POOL_VS_CHUNK) &&
                (ULONG_PTR)Address < (ULONG_PTR)*Block + Info->Size)
            {
                return TRUE;
            }
        }
    }

    *Block = NULL;
    *Allocated = FALSE;
    return FALSE;
}

static
BOOLEAN
PoolDebugEnumerateSegment(
    _In_ PPOOL_HEAP Heap,
    _In_ PPOOL_SEGMENT Segment,
    _In_ POOL_ENUMERATE_ROUTINE Routine,
    _In_opt_ PVOID Context)
{
    ULONG Page = Segment->HeaderPages;

    while (Page < POOL_SEGMENT_PAGES)
    {
        PPOOL_PAGE_RANGE Range = &Segment->Range[Page];
        PVOID Base = PoolSegPageAddress(Segment, Page);
        POOL_BLOCK_INFO Info;

        if (Range->First != Page || Range->Pages == 0 ||
            (Range->State != PoolRangeBlock && Range->State != PoolRangeLfh && Range->State != PoolRangeVs))
        {
            Page++;
            continue;
        }

        if (Range->State == PoolRangeBlock)
        {
            Info.Size = (SIZE_T)Range->Pages << PAGE_SHIFT;
            Info.Tag = Range->Tag;
            Info.Flags = Range->Flags;
            Info.Kind = PoolBlockRange;
            Info.Trailer = 0;

            if (!Routine(Context, Base, &Info))
                return FALSE;
        }
        else if (Range->State == PoolRangeLfh)
        {
            PPOOL_LFH_SUBSEGMENT Subsegment = Base;
            ULONG Bit;

            if (Subsegment->Signature == POOL_LFH_SIGNATURE)
            {
                for (Bit = 0; Bit < Subsegment->BlockCount; Bit++)
                {
                    if (!PoolDebugLfhBit(Subsegment, Bit))
                        continue;

                    PoolDebugLfhInfo(Subsegment, Bit, &Info);
                    if (!Routine(Context, PoolLfhBlockAddress(Subsegment, Bit), &Info))
                        return FALSE;
                }
            }
        }
        else
        {
            PVOID Cursor = NULL;
            BOOLEAN Allocated;
            PVOID Block;

            while (PoolVsWalk(Heap, Base, &Cursor, &Block, &Info, &Allocated))
            {
                if (Allocated && !Routine(Context, Block, &Info))
                    return FALSE;
            }
        }

        Page += Range->Pages;
    }

    return TRUE;
}

VOID
PoolHeapEnumerate(
    _In_ PPOOL_HEAP Heap,
    _In_ POOL_ENUMERATE_ROUTINE Routine,
    _In_opt_ PVOID Context)
{
    ULONG RegionIndex;

    for (RegionIndex = 0; RegionIndex < Heap->RegionCount; RegionIndex++)
    {
        PPOOL_VA_REGION Region = Heap->Region[RegionIndex];
        SIZE_T Index;

        for (Index = 0; Index < Region->UnitCount; Index++)
        {
            PPOOL_UNIT Unit = &Region->Directory[Index];
            PVOID Base = (PVOID)(Region->Base + (Index << POOL_UNIT_SHIFT));

            if (Unit->State == PoolUnitLarge && Unit->PageCount != 0)
            {
                POOL_BLOCK_INFO Info;

                Info.Size = (SIZE_T)Unit->PageCount << PAGE_SHIFT;
                Info.Tag = Unit->Tag;
                Info.Flags = Unit->Flags;
                Info.Kind = PoolBlockLarge;
                Info.Trailer = 0;

                if (!Routine(Context, Base, &Info))
                    return;
            }
            else if (Unit->State == PoolUnitSegment)
            {
                PPOOL_SEGMENT Segment = Base;

                if (Segment->Signature != POOL_SEGMENT_SIGNATURE || Segment->Heap != Heap)
                    continue;

                if (!PoolDebugEnumerateSegment(Heap, Segment, Routine, Context))
                    return;
            }
        }
    }
}
