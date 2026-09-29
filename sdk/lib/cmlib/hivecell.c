/*
 * PROJECT:   Registry manipulation library
 * LICENSE:   GPL - See COPYING in the top level directory
 * COPYRIGHT: Copyright 2005 Filip Navara <navaraf@reactos.org>
 *            Copyright 2001 - 2005 Eric Kohl
 */

#include "cmlib.h"
#define NDEBUG
#include <debug.h>

/* DECLARATIONS *************************************************************/

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
VOID
NTAPI
CmpLazyFlush(VOID);
#else
#define HvLockHiveWriter(Hive) ((void)(Hive))
#define HvUnlockHiveWriter(Hive) ((void)(Hive))
#define HvLockHiveReader(Hive) ((void)(Hive), FALSE)
#define HvUnlockHiveReader(Hive, LockAcquired) \
    do { (void)(Hive); (void)(LockAcquired); } while (0)
#endif

/* FUNCTIONS *****************************************************************/

static __inline PHCELL CMAPI
HvpGetCellHeaderLocked(
    PHHIVE RegistryHive,
    HCELL_INDEX CellIndex)
{
    PVOID Block;

    CMLTRACE(CMLIB_HCELL_DEBUG, "%s - Hive 0x%p, CellIndex 0x%x\n",
             __FUNCTION__, RegistryHive, CellIndex);

    ASSERT(CellIndex != HCELL_NIL);
    if (!RegistryHive->Flat)
    {
        ULONG CellType   = HvGetCellType(CellIndex);
        ULONG CellBlock  = HvGetCellBlock(CellIndex);
        ULONG CellOffset = (CellIndex & HCELL_OFFSET_MASK) >> HCELL_OFFSET_SHIFT;

        ASSERT(CellBlock < RegistryHive->Storage[CellType].Length / HBLOCK_SIZE);
        Block = HvpLookupBlock(RegistryHive, CellType, CellBlock);
        ASSERT(Block != NULL);
        return (PHCELL)((ULONG_PTR)Block + CellOffset);
    }
    else
    {
        ASSERT(HvGetCellType(CellIndex) == Stable);
        return (PHCELL)((ULONG_PTR)RegistryHive->BaseBlock + HBLOCK_SIZE +
                        CellIndex);
    }
}

BOOLEAN CMAPI
HvIsCellAllocated(IN PHHIVE RegistryHive,
                  IN HCELL_INDEX CellIndex)
{
    BOOLEAN IsAllocated;
    BOOLEAN LockAcquired;
    ULONG Type, Block;

    /* If it's a flat hive, the cell is always allocated */
    if (RegistryHive->Flat)
        return TRUE;

    /* Otherwise, get the type and make sure it's valid */
    Type = HvGetCellType(CellIndex);
    Block = HvGetCellBlock(CellIndex);
    LockAcquired = HvLockHiveReader(RegistryHive);
    if (Block >= RegistryHive->Storage[Type].Length / HBLOCK_SIZE)
    {
        HvUnlockHiveReader(RegistryHive, LockAcquired);
        return FALSE;
    }

    /* Try to get the cell block */
    IsAllocated = !!HvpLookupBlock(RegistryHive, Type, Block);
    HvUnlockHiveReader(RegistryHive, LockAcquired);

    return IsAllocated;
}

PCELL_DATA CMAPI
HvpGetCellData(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX CellIndex)
{
    BOOLEAN LockAcquired = FALSE;
    PCELL_DATA Data;

    if (!Hive->Flat)
        LockAcquired = HvLockHiveReader(Hive);
    Data = (PCELL_DATA)(HvpGetCellHeaderLocked(Hive, CellIndex) + 1);
    HvUnlockHiveReader(Hive, LockAcquired);
    return Data;
}

LONG CMAPI
HvGetCellSize(IN PHHIVE Hive,
              IN PVOID Address)
{
    PHCELL CellHeader;
    LONG Size;

    UNREFERENCED_PARAMETER(Hive);

    CellHeader = (PHCELL)Address - 1;
    Size = CellHeader->Size * -1;
    Size -= sizeof(HCELL);
    return Size;
}

BOOLEAN CMAPI
HvMarkCellDirty(
    PHHIVE RegistryHive,
    HCELL_INDEX CellIndex,
    BOOLEAN HoldingLock)
{
    BOOLEAN LockAcquired = FALSE;
    ULONG CellBlock;
    ULONG CellLastBlock;

    ASSERT(RegistryHive->ReadOnly == FALSE);

    CMLTRACE(CMLIB_HCELL_DEBUG, "%s - Hive 0x%p, CellIndex 0x%x, HoldingLock %u\n",
             __FUNCTION__, RegistryHive, CellIndex, HoldingLock);

    if (HvGetCellType(CellIndex) != Stable)
        return TRUE;

    if (!HoldingLock)
    {
        HvLockHiveWriter(RegistryHive);
        LockAcquired = TRUE;
    }

    CellBlock     = HvGetCellBlock(CellIndex);
    CellLastBlock = HvGetCellBlock(CellIndex + HBLOCK_SIZE - 1);

    RtlSetBits(&RegistryHive->DirtyVector,
               CellBlock, CellLastBlock - CellBlock);
    RegistryHive->DirtyCount++;

    /*
     * FIXME: Querying a lazy flush operation is needed to
     * ensure that the dirty data is being flushed to disk
     * accordingly. However, this operation has to be done
     * in a helper like HvMarkDirty that marks specific parts
     * of the hive as dirty. Since we do not have such kind
     * of helper we have to perform an eventual lazy flush
     * when marking cells as dirty here for the moment being,
     * so that not only we flush dirty data but also write
     * logs.
     */
#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    if (!(RegistryHive->HiveFlags & HIVE_NOLAZYFLUSH))
    {
        CmpLazyFlush();
    }
#endif

    if (LockAcquired)
        HvUnlockHiveWriter(RegistryHive);

    return TRUE;
}

BOOLEAN CMAPI
HvIsCellDirty(IN PHHIVE Hive,
              IN HCELL_INDEX Cell)
{
    BOOLEAN IsDirty = FALSE;
    BOOLEAN LockAcquired;

    /* Sanity checks */
    ASSERT(Hive->ReadOnly == FALSE);

    /* Volatile cells are always "dirty" */
    if (HvGetCellType(Cell) == Volatile)
        return TRUE;

    LockAcquired = HvLockHiveReader(Hive);

    /* Check if the dirty bit is set */
    if (RtlCheckBit(&Hive->DirtyVector, Cell / HBLOCK_SIZE))
        IsDirty = TRUE;

    HvUnlockHiveReader(Hive, LockAcquired);

    /* Return result as boolean*/
    return IsDirty;
}

static __inline ULONG CMAPI
HvpComputeFreeListIndex(
    ULONG Size)
{
    ULONG Index, Limit;

    ASSERT(Size >= 8);
    if (Size <= 128)
        return Size / 8 - 1;
    for (Index = 16, Limit = 256; Index < 23 && Size > Limit; ++Index)
        Limit *= 2;
    return Index;
}

static VOID CMAPI
HvpRebuildBinFreeDisplay(
    PHHIVE Hive,
    HSTORAGE_TYPE Storage,
    PHBIN Bin)
{
    ULONG Index, Offset, Size, FirstPage, PageCount;
    PHCELL Cell;
    PFREE_DISPLAY Display;

    if (!HvpCheckBinCells(Bin))
    {
        ASSERT(FALSE);
        return;
    }

    FirstPage = Bin->FileOffset / HBLOCK_SIZE;
    PageCount = Bin->Size / HBLOCK_SIZE;
    for (Index = 0; Index < 24; ++Index)
        RtlClearBits(&Hive->Storage[Storage].FreeDisplay[Index].Display, FirstPage, PageCount);
    for (Offset = sizeof(HBIN); Offset < Bin->Size; Offset += Size)
    {
        Cell = (PHCELL)((PUCHAR)Bin + Offset);
        Size = Cell->Size < 0 ? (ULONG)-Cell->Size : (ULONG)Cell->Size;
        ASSERT(Size >= 8 && !(Size & 7) && Size <= Bin->Size - Offset);
        if (Cell->Size < 0)
            continue;
        Index = HvpComputeFreeListIndex(Size);
        FirstPage = (Bin->FileOffset + Offset) / HBLOCK_SIZE;
        PageCount = (Bin->FileOffset + Offset + Size - 1) / HBLOCK_SIZE - FirstPage + 1;
        RtlSetBits(&Hive->Storage[Storage].FreeDisplay[Index].Display, FirstPage, PageCount);
    }
    for (Index = 0; Index < 24; ++Index)
    {
        Display = &Hive->Storage[Storage].FreeDisplay[Index];
        if (RtlFindSetBits(&Display->Display, 1, 0) == MAXULONG)
            Hive->Storage[Storage].FreeSummary &= ~(1UL << Index);
        else
            Hive->Storage[Storage].FreeSummary |= 1UL << Index;
    }
}

static HCELL_INDEX CMAPI
HvpFindFree(
    PHHIVE Hive,
    ULONG Size,
    HSTORAGE_TYPE Storage)
{
    ULONG Index, Page, Offset, CellSize, NextPage;
    PHBIN Bin;
    PHCELL Cell;
    PFREE_DISPLAY Display;

    for (Index = HvpComputeFreeListIndex(Size); Index < 24; ++Index)
    {
        if (!(Hive->Storage[Storage].FreeSummary & (1UL << Index)))
            continue;
        Display = &Hive->Storage[Storage].FreeDisplay[Index];
        Page = RtlFindSetBits(&Display->Display, 1, 0);
        while (Page != MAXULONG)
        {
            Bin = HvpLookupBin(Hive, Storage, Page);
            if (!Bin || !HvpCheckBinCells(Bin))
                return HCELL_NIL;
            for (Offset = sizeof(HBIN); Offset < Bin->Size; Offset += CellSize)
            {
                Cell = (PHCELL)((PUCHAR)Bin + Offset);
                CellSize = Cell->Size < 0 ? (ULONG)-Cell->Size : (ULONG)Cell->Size;
                ASSERT(CellSize >= 8 && !(CellSize & 7) && CellSize <= Bin->Size - Offset);
                if (Cell->Size > 0 && CellSize >= Size && HvpComputeFreeListIndex(CellSize) == Index)
                    return (Bin->FileOffset + Offset) | ((ULONG)Storage << HCELL_TYPE_SHIFT);
            }
            NextPage = (Bin->FileOffset + Bin->Size) / HBLOCK_SIZE;
            if (NextPage >= Display->Display.SizeOfBitMap)
                break;
            Page = RtlFindSetBits(&Display->Display, 1, NextPage);
            if (Page < NextPage)
                break;
        }
    }
    return HCELL_NIL;
}

NTSTATUS CMAPI
HvpCreateHiveFreeCellList(
    PHHIVE Hive)
{
    ULONG BlockIndex;
    PHBIN Bin;

    for (BlockIndex = 0; BlockIndex < Hive->Storage[Stable].Length / HBLOCK_SIZE;)
    {
        Bin = HvpLookupBin(Hive, Stable, BlockIndex);
        if (!Bin || !HvpCheckBinCells(Bin))
            return STATUS_REGISTRY_CORRUPT;
        HvpRebuildBinFreeDisplay(Hive, Stable, Bin);
        BlockIndex += Bin->Size / HBLOCK_SIZE;
    }
    return STATUS_SUCCESS;
}

static HCELL_INDEX CMAPI
HvpDoAllocateCell(
    PHHIVE RegistryHive,
    ULONG Size,
    HSTORAGE_TYPE Storage,
    HCELL_INDEX Vicinity)
{
    PHCELL FreeCell;
    HCELL_INDEX FreeCellOffset;
    PHCELL NewCell;
    PHBIN Bin;

    ASSERT(RegistryHive->ReadOnly == FALSE);

    CMLTRACE(CMLIB_HCELL_DEBUG, "%s - Hive 0x%p, Size 0x%x, %s, Vicinity 0x%x\n",
             __FUNCTION__, RegistryHive, Size, (Storage == 0) ? "Stable" : "Volatile", Vicinity);

    if (Size > ((MAXULONG >> 1) & ~15UL) - sizeof(HCELL))
        return HCELL_NIL;

    /* Round to 16 bytes multiple. */
    Size = ROUND_UP(Size + sizeof(HCELL), 16);

    /* First search in free blocks. */
    FreeCellOffset = HvpFindFree(RegistryHive, Size, Storage);

    /* If no free cell was found we need to extend the hive file. */
    if (FreeCellOffset == HCELL_NIL)
    {
        Bin = HvpAddBin(RegistryHive, Size, Storage);
        if (Bin == NULL)
            return HCELL_NIL;
        FreeCellOffset = Bin->FileOffset + sizeof(HBIN);
        FreeCellOffset |= (ULONG)Storage << HCELL_TYPE_SHIFT;
    }

    FreeCell = HvpGetCellHeaderLocked(RegistryHive, FreeCellOffset);

    /* Split the block in two parts */

    /* The free block that is created has to be at least
       sizeof(HCELL) + sizeof(HCELL_INDEX) big, so that free
       cell list code can work. Moreover we round cell sizes
       to 16 bytes, so creating a smaller block would result in
       a cell that would never be allocated. */
    if ((ULONG)FreeCell->Size > Size + 16)
    {
        NewCell = (PHCELL)((ULONG_PTR)FreeCell + Size);
        NewCell->Size = FreeCell->Size - Size;
        FreeCell->Size = Size;
        if (Storage == Stable)
            HvMarkCellDirty(RegistryHive, FreeCellOffset + Size, TRUE);
    }

    if (Storage == Stable)
        HvMarkCellDirty(RegistryHive, FreeCellOffset, TRUE);

    FreeCell->Size = -FreeCell->Size;
    RtlZeroMemory(FreeCell + 1, Size - sizeof(HCELL));
    HvpRebuildBinFreeDisplay(RegistryHive, Storage,
                            HvpLookupBin(RegistryHive, Storage, HvGetCellBlock(FreeCellOffset)));

    CMLTRACE(CMLIB_HCELL_DEBUG, "%s - CellIndex 0x%x\n",
             __FUNCTION__, FreeCellOffset);

    return FreeCellOffset;
}

HCELL_INDEX CMAPI
HvAllocateCell(
    PHHIVE RegistryHive,
    ULONG Size,
    HSTORAGE_TYPE Storage,
    HCELL_INDEX Vicinity)
{
    HCELL_INDEX CellIndex;

    HvLockHiveWriter(RegistryHive);
    CellIndex = HvpDoAllocateCell(RegistryHive, Size, Storage, Vicinity);
    HvUnlockHiveWriter(RegistryHive);

    return CellIndex;
}

static VOID CMAPI
HvpDoFreeCell(
    PHHIVE RegistryHive,
    HCELL_INDEX CellIndex);

HCELL_INDEX CMAPI
HvReallocateCell(
    PHHIVE RegistryHive,
    HCELL_INDEX CellIndex,
    ULONG Size)
{
    PVOID OldCell;
    PVOID NewCell;
    LONG OldCellSize;
    HCELL_INDEX NewCellIndex;
    HSTORAGE_TYPE Storage;

    ASSERT(CellIndex != HCELL_NIL);

    CMLTRACE(CMLIB_HCELL_DEBUG, "%s - Hive 0x%p, CellIndex 0x%x, Size 0x%x\n",
             __FUNCTION__, RegistryHive, CellIndex, Size);

    HvLockHiveWriter(RegistryHive);

    Storage = HvGetCellType(CellIndex);

    OldCell = HvpGetCellHeaderLocked(RegistryHive, CellIndex) + 1;
    OldCellSize = HvGetCellSize(RegistryHive, OldCell);
    ASSERT(OldCellSize > 0);

    /*
     * If new data size is larger than the current, destroy current
     * data block and allocate a new one.
     *
     * FIXME: Merge with adjacent free cell if possible.
     * FIXME: Implement shrinking.
     */
    if (Size > (ULONG)OldCellSize)
    {
        NewCellIndex = HvpDoAllocateCell(RegistryHive, Size, Storage, HCELL_NIL);
        if (NewCellIndex == HCELL_NIL)
            goto Exit;

        NewCell = HvpGetCellHeaderLocked(RegistryHive, NewCellIndex) + 1;
        RtlCopyMemory(NewCell, OldCell, (SIZE_T)OldCellSize);

        HvpDoFreeCell(RegistryHive, CellIndex);
    }
    else
    {
        NewCellIndex = CellIndex;
    }

Exit:
    HvUnlockHiveWriter(RegistryHive);
    return NewCellIndex;
}

static VOID CMAPI
HvpDoFreeCell(
    PHHIVE RegistryHive,
    HCELL_INDEX CellIndex)
{
    PHCELL Free;
    PHCELL Neighbor;
    PHBIN Bin;
    ULONG CellType;
    ULONG CellBlock;

    ASSERT(RegistryHive->ReadOnly == FALSE);

    CMLTRACE(CMLIB_HCELL_DEBUG, "%s - Hive 0x%p, CellIndex 0x%x\n",
             __FUNCTION__, RegistryHive, CellIndex);

    Free = HvpGetCellHeaderLocked(RegistryHive, CellIndex);

    ASSERT(Free->Size < 0);

    Free->Size = -Free->Size;

    CellType = HvGetCellType(CellIndex);
    CellBlock = HvGetCellBlock(CellIndex);

    /* FIXME: Merge free blocks */
    Bin = HvpLookupBin(RegistryHive, CellType, CellBlock);

    if ((CellIndex & ~HCELL_TYPE_MASK) + Free->Size <
        Bin->FileOffset + Bin->Size)
    {
        Neighbor = (PHCELL)((ULONG_PTR)Free + Free->Size);
        if (Neighbor->Size > 0)
        {
            Free->Size += Neighbor->Size;
        }
    }

    Neighbor = (PHCELL)(Bin + 1);
    while (Neighbor < Free)
    {
        if (Neighbor->Size > 0)
        {
            if ((ULONG_PTR)Neighbor + Neighbor->Size == (ULONG_PTR)Free)
            {
                HCELL_INDEX NeighborCellIndex =
                    ((HCELL_INDEX)((ULONG_PTR)Neighbor - (ULONG_PTR)Bin +
                     Bin->FileOffset)) | (CellIndex & HCELL_TYPE_MASK);

                Neighbor->Size += Free->Size;
                HvpRebuildBinFreeDisplay(RegistryHive, CellType, Bin);

                if (CellType == Stable)
                    HvMarkCellDirty(RegistryHive, NeighborCellIndex, TRUE);

                return;
            }
            Neighbor = (PHCELL)((ULONG_PTR)Neighbor + Neighbor->Size);
        }
        else
        {
            Neighbor = (PHCELL)((ULONG_PTR)Neighbor - Neighbor->Size);
        }
    }

    /* Add block to the list of free blocks */
    HvpRebuildBinFreeDisplay(RegistryHive, CellType, Bin);

    if (CellType == Stable)
        HvMarkCellDirty(RegistryHive, CellIndex, TRUE);
}

VOID CMAPI
HvFreeCell(
    PHHIVE RegistryHive,
    HCELL_INDEX CellIndex)
{
    HvLockHiveWriter(RegistryHive);
    HvpDoFreeCell(RegistryHive, CellIndex);
    HvUnlockHiveWriter(RegistryHive);
}


#define CELL_REF_INCREMENT  10

BOOLEAN
CMAPI
HvTrackCellRef(
    IN OUT PHV_TRACK_CELL_REF CellRef,
    IN PHHIVE Hive,
    IN HCELL_INDEX Cell)
{
    PHV_HIVE_CELL_PAIR NewCellArray;

    PAGED_CODE();

    /* Sanity checks */
    ASSERT(CellRef);
    ASSERT(Hive);
    ASSERT(Cell != HCELL_NIL);

    /* NOTE: The hive cell is already referenced! */

    /* Less than 4? Use the static array */
    if (CellRef->StaticCount < STATIC_CELL_PAIR_COUNT)
    {
        /* Add the reference */
        CellRef->StaticArray[CellRef->StaticCount].Hive = Hive;
        CellRef->StaticArray[CellRef->StaticCount].Cell = Cell;
        CellRef->StaticCount++;
        return TRUE;
    }

    DPRINT("HvTrackCellRef: Static array full, use dynamic array.\n");

    /* Sanity checks */
    if (CellRef->Max == 0)
    {
        /* The dynamic array must not have been allocated already */
        ASSERT(CellRef->CellArray == NULL);
        ASSERT(CellRef->Count == 0);
    }
    else
    {
        /* The dynamic array must be allocated */
        ASSERT(CellRef->CellArray);
    }
    ASSERT(CellRef->Count <= CellRef->Max);

    if (CellRef->Count == CellRef->Max)
    {
        /* Allocate a new reference table */
        NewCellArray = CmpAllocate((CellRef->Max + CELL_REF_INCREMENT) * sizeof(HV_HIVE_CELL_PAIR),
                                   TRUE,
                                   TAG_CM);
        if (!NewCellArray)
        {
            DPRINT1("HvTrackCellRef: Cannot reallocate the reference table.\n");
            /* We failed, dereference the hive cell */
            HvReleaseCell(Hive, Cell);
            return FALSE;
        }

        /* Free the old reference table and use the new one */
        if (CellRef->CellArray)
        {
            /* Copy the handles from the old table to the new one */
            RtlCopyMemory(NewCellArray,
                          CellRef->CellArray,
                          CellRef->Max * sizeof(HV_HIVE_CELL_PAIR));
            CmpFree(CellRef->CellArray, 0); // TAG_CM
        }
        CellRef->CellArray = NewCellArray;
        CellRef->Max += CELL_REF_INCREMENT;
    }

    // ASSERT(CellRef->Count < CellRef->Max);

    /* Add the reference */
    CellRef->CellArray[CellRef->Count].Hive = Hive;
    CellRef->CellArray[CellRef->Count].Cell = Cell;
    CellRef->Count++;
    return TRUE;
}

VOID
CMAPI
HvReleaseFreeCellRefArray(
    IN OUT PHV_TRACK_CELL_REF CellRef)
{
    ULONG i;

    PAGED_CODE();

    ASSERT(CellRef);

    /* Any references in the static array? */
    if (CellRef->StaticCount > 0)
    {
        /* Sanity check */
        ASSERT(CellRef->StaticCount <= STATIC_CELL_PAIR_COUNT);

        /* Loop over them and release them */
        for (i = 0; i < CellRef->StaticCount; i++)
        {
            HvReleaseCell(CellRef->StaticArray[i].Hive,
                          CellRef->StaticArray[i].Cell);
        }

        /* We can reuse the static array */
        CellRef->StaticCount = 0;
    }

    /* Any references in the dynamic array? */
    if (CellRef->Count > 0)
    {
        /* Sanity checks */
        ASSERT(CellRef->Count <= CellRef->Max);
        ASSERT(CellRef->CellArray);

        /* Loop over them and release them */
        for (i = 0; i < CellRef->Count; i++)
        {
            HvReleaseCell(CellRef->CellArray[i].Hive,
                          CellRef->CellArray[i].Cell);
        }

        /* We can reuse the dynamic array */
        CmpFree(CellRef->CellArray, 0); // TAG_CM
        CellRef->CellArray = NULL;
        CellRef->Count = CellRef->Max = 0;
    }
}
