/*
 * PROJECT:   Registry manipulation library
 * LICENSE:   GPL - See COPYING in the top level directory
 * COPYRIGHT: Copyright 2005 Filip Navara <navaraf@reactos.org>
 *            Copyright 2005 Hartmut Birr
 *            Copyright 2001 - 2005 Eric Kohl
 */

#include "cmlib.h"

PHMAP_ENTRY CMAPI
HvpLookupCellMap(
    PHHIVE Hive,
    HSTORAGE_TYPE Storage,
    ULONG BlockIndex)
{
    PHMAP_DIRECTORY Map = Hive->Storage[Storage].Map;
    ULONG TableIndex = BlockIndex / (sizeof(((PHMAP_TABLE)0)->Table) / sizeof(HMAP_ENTRY));

    if (!Map || TableIndex >= (sizeof(Map->Directory) / sizeof(Map->Directory[0])) || !Map->Directory[TableIndex])
        return NULL;
    return &Map->Directory[TableIndex]->Table[BlockIndex % (sizeof(((PHMAP_TABLE)0)->Table) / sizeof(HMAP_ENTRY))];
}

PVOID CMAPI
HvpLookupBlock(
    PHHIVE Hive,
    HSTORAGE_TYPE Storage,
    ULONG BlockIndex)
{
    PHMAP_ENTRY Entry = HvpLookupCellMap(Hive, Storage, BlockIndex);

    if (!Entry || !Entry->PermanentBinAddress)
        return NULL;
    return (PVOID)(((ULONG_PTR)Entry->PermanentBinAddress & ~(ULONG_PTR)0xf) +
                   (ULONG_PTR)Entry->BlockOffset);
}

PHBIN CMAPI
HvpLookupBin(
    PHHIVE Hive,
    HSTORAGE_TYPE Storage,
    ULONG BlockIndex)
{
    PHMAP_ENTRY Entry = HvpLookupCellMap(Hive, Storage, BlockIndex);

    return Entry ? (PHBIN)((ULONG_PTR)Entry->PermanentBinAddress & ~(ULONG_PTR)0xf) : NULL;
}

static BOOLEAN CMAPI
HvpGrowFreeDisplay(
    PHHIVE Hive,
    HSTORAGE_TYPE Storage,
    ULONG PageCount)
{
    PULONG Buffers[24] = {0};
    ULONG Bytes = ROUND_UP(PageCount, 2048) / 8;
    ULONG Index;
    PFREE_DISPLAY Display;

    for (Index = 0; Index < 24; ++Index)
    {
        Display = &Hive->Storage[Storage].FreeDisplay[Index];
        if (Bytes <= Display->RealVectorSize)
            continue;
        Buffers[Index] = Hive->Allocate(Bytes, TRUE, TAG_CM);
        if (!Buffers[Index])
        {
            while (Index)
            {
                --Index;
                if (Buffers[Index])
                    Hive->Free(Buffers[Index], Bytes);
            }
            return FALSE;
        }
        RtlZeroMemory(Buffers[Index], Bytes);
        if (Display->Display.Buffer)
            RtlCopyMemory(Buffers[Index], Display->Display.Buffer, Display->RealVectorSize);
    }
    for (Index = 0; Index < 24; ++Index)
    {
        Display = &Hive->Storage[Storage].FreeDisplay[Index];
        if (Buffers[Index])
        {
            if (Display->Display.Buffer)
                Hive->Free(Display->Display.Buffer, Display->RealVectorSize);
            Display->Display.Buffer = Buffers[Index];
            Display->RealVectorSize = Bytes;
        }
        Display->Display.SizeOfBitMap = PageCount;
    }
    return TRUE;
}

BOOLEAN CMAPI
HvpCheckBinCells(
    PHBIN Bin)
{
    ULONG Offset = sizeof(HBIN);
    LONG CellSize;
    ULONG Size;

    while (Offset < Bin->Size)
    {
        if (Bin->Size - Offset < sizeof(HCELL))
            return FALSE;
        CellSize = ((PHCELL)((PUCHAR)Bin + Offset))->Size;
        if (!CellSize || CellSize == (LONG)0x80000000)
            return FALSE;
        Size = CellSize < 0 ? (ULONG)-CellSize : (ULONG)CellSize;
        if (Size < 8 || (Size & 7) || Size > Bin->Size - Offset)
            return FALSE;
        Offset += Size;
    }
    return Offset == Bin->Size;
}

BOOLEAN CMAPI
HvpMapHiveBin(
    PHHIVE Hive,
    HSTORAGE_TYPE Storage,
    PHBIN Bin)
{
    PHMAP_DIRECTORY Map = Hive->Storage[Storage].Map;
    ULONG FirstTable, EndTable, TableIndex, BlockIndex, BlockCount;
    ULONG TableBlocks = (sizeof(((PHMAP_TABLE)0)->Table) / sizeof(HMAP_ENTRY));
    PHMAP_ENTRY Entry;

    if (!Bin->Size || Bin->Size % HBLOCK_SIZE ||
        Bin->FileOffset != Hive->Storage[Storage].Length ||
        ((ULONG_PTR)Bin & 0xf) ||
        Bin->FileOffset > HCELL_TYPE_MASK ||
        Bin->Size > HCELL_TYPE_MASK - Bin->FileOffset ||
        !HvpCheckBinCells(Bin))
        return FALSE;

    BlockIndex = Bin->FileOffset / HBLOCK_SIZE;
    BlockCount = Bin->Size / HBLOCK_SIZE;
    FirstTable = (BlockIndex + TableBlocks - 1) / TableBlocks;
    EndTable = (BlockIndex + BlockCount + TableBlocks - 1) / TableBlocks;
    if (!Map)
    {
        Map = Hive->Allocate(sizeof(*Map), TRUE, TAG_CM);
        if (!Map)
            return FALSE;
        RtlZeroMemory(Map, sizeof(*Map));
    }

    for (TableIndex = FirstTable; TableIndex < EndTable; ++TableIndex)
    {
        Map->Directory[TableIndex] = Hive->Allocate(sizeof(HMAP_TABLE), TRUE, TAG_CM);
        if (!Map->Directory[TableIndex])
        {
            while (TableIndex > FirstTable)
            {
                --TableIndex;
                Hive->Free(Map->Directory[TableIndex], 0);
                Map->Directory[TableIndex] = NULL;
            }
            if (!Hive->Storage[Storage].Map)
                Hive->Free(Map, 0);
            return FALSE;
        }
        RtlZeroMemory(Map->Directory[TableIndex], sizeof(HMAP_TABLE));
    }

    if (!HvpGrowFreeDisplay(Hive, Storage, BlockIndex + BlockCount))
    {
        for (TableIndex = FirstTable; TableIndex < EndTable; ++TableIndex)
        {
            Hive->Free(Map->Directory[TableIndex], 0);
            Map->Directory[TableIndex] = NULL;
        }
        if (!Hive->Storage[Storage].Map)
            Hive->Free(Map, 0);
        return FALSE;
    }

    Hive->Storage[Storage].Map = Map;
    for (TableIndex = 0; TableIndex < BlockCount; ++TableIndex)
    {
        Entry = HvpLookupCellMap(Hive, Storage, BlockIndex + TableIndex);
        Entry->BlockOffset = TableIndex * HBLOCK_SIZE;
        Entry->PermanentBinAddress = (ULONG_PTR)Bin | MAP_ENTRY_DUMMY |
                                     (TableIndex ? 0 : MAP_ENTRY_NEW_ALLOC);
        Entry->MemAlloc = TableIndex ? 0 : Bin->Size;
    }
    Hive->Storage[Storage].Length += Bin->Size;
    return TRUE;
}

PHBIN CMAPI
HvpAddBin(
    PHHIVE RegistryHive,
    ULONG Size,
    HSTORAGE_TYPE Storage)
{
    PHBIN Bin;
    ULONG BinSize;
    ULONG BitmapSize;
    ULONG BlockCount;
    PHCELL Block;
    PULONG BitmapBuffer = NULL;

    if (RegistryHive->Storage[Storage].Length >= HCELL_TYPE_MASK ||
        Size > HCELL_TYPE_MASK - RegistryHive->Storage[Storage].Length - sizeof(HBIN))
        return NULL;

    BinSize = ROUND_UP(Size + sizeof(HBIN), HBLOCK_SIZE);
    BlockCount = BinSize / HBLOCK_SIZE;

    Bin = RegistryHive->Allocate(BinSize, TRUE, TAG_CM);
    if (Bin == NULL)
        return NULL;
    RtlZeroMemory(Bin, BinSize);

    Bin->Signature = HV_HBIN_SIGNATURE;
    Bin->FileOffset = RegistryHive->Storage[Storage].Length;
    Bin->Size = BinSize;
    Block = (PHCELL)(Bin + 1);
    Block->Size = (LONG)(BinSize - sizeof(HBIN));

    BitmapSize = ROUND_UP((RegistryHive->Storage[Storage].Length + BinSize) / HBLOCK_SIZE,
                          sizeof(ULONG) * 8) / 8;
    if (Storage == Stable && BitmapSize > RegistryHive->DirtyVector.SizeOfBitMap / 8)
    {
        BitmapBuffer = RegistryHive->Allocate(BitmapSize, TRUE, TAG_CM);
        if (!BitmapBuffer)
        {
            RegistryHive->Free(Bin, 0);
            return NULL;
        }
        RtlZeroMemory(BitmapBuffer, BitmapSize);
        if (RegistryHive->DirtyVector.SizeOfBitMap)
            RtlCopyMemory(BitmapBuffer, RegistryHive->DirtyVector.Buffer,
                          RegistryHive->DirtyVector.SizeOfBitMap / 8);
    }

    if (!HvpMapHiveBin(RegistryHive, Storage, Bin))
    {
        if (BitmapBuffer)
            RegistryHive->Free(BitmapBuffer, 0);
        RegistryHive->Free(Bin, 0);
        return NULL;
    }

    if (Storage == Stable)
    {
        if (BitmapBuffer)
        {
            if (RegistryHive->DirtyVector.Buffer)
                RegistryHive->Free(RegistryHive->DirtyVector.Buffer, 0);
            RtlInitializeBitMap(&RegistryHive->DirtyVector, BitmapBuffer, BitmapSize * 8);
        }

        /* Mark new bin dirty. */
        RtlSetBits(&RegistryHive->DirtyVector,
                   Bin->FileOffset / HBLOCK_SIZE,
                   BlockCount);

        /* Update size in the base block */
        RegistryHive->BaseBlock->Length += BinSize;
    }

    return Bin;
}
