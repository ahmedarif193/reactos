/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VHDX dynamic block allocator
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Manages dynamic block allocation for VHDX virtual disks. When a guest
 * writes to a block that is NOT_PRESENT, ZERO, or UNMAPPED, this module
 * allocates a new 1MB-aligned region in the file, zero-fills it, updates
 * the BAT entry, and tracks allocation state.
 *
 * For v1 (memory-mapped mode), the VHDX file must be pre-sized large
 * enough to contain all potential blocks. Dynamic file extension is not
 * supported in memory-mapped mode.
 */

#include <rosv/rosv.h>
#include <rosv/vhdx.h>

/* ========================================================================== */
/* Internal helpers                                                            */
/* ========================================================================== */

/**
 * Scan all BAT entries to find the highest allocated file offset.
 * Sets State->HighestAllocatedMB to one block-size past the highest found,
 * so it points to the next free offset.
 *
 * Must be called during VHDX open, after BAT and metadata are parsed.
 */
VOID
RosvVhdxScanBatForHighestOffset(
    _Inout_ PROSV_VHDX_STATE State)
{
    PULONG64 BatArray;
    ULONG Index;
    ULONG64 MaxOffsetMB = 0;
    ULONG64 EntryVal;
    ULONG EntryState;
    ULONG64 OffsetMB;
    ULONG BlockSizeMB;
    BOOLEAN Found = FALSE;

    if (!State->BatBase || State->BatEntryCount == 0)
    {
        ROSV_ERR("VHDX: ScanBat called with no BAT (base=%p count=%u)",
                 State->BatBase, State->BatEntryCount);
        State->HighestAllocatedMB = 0;
        return;
    }

    BatArray = (PULONG64)State->BatBase;
    BlockSizeMB = State->BlockSize / VHDX_MB_ALIGNMENT;

    ROSV_TRACE("VHDX: scanning %u BAT entries for highest allocated offset (BlockSizeMB=%u)",
               State->BatEntryCount, BlockSizeMB);

    for (Index = 0; Index < State->BatEntryCount; Index++)
    {
        EntryVal = BatArray[Index];
        EntryState = VHDX_BAT_STATE(EntryVal);

        if (EntryState == VHDX_PAYLOAD_BLOCK_FULLY_PRESENT ||
            EntryState == VHDX_PAYLOAD_BLOCK_PARTIALLY_PRESENT ||
            EntryState == VHDX_SB_BLOCK_PRESENT)
        {
            OffsetMB = VHDX_BAT_FILE_OFFSET_MB(EntryVal);
            if (OffsetMB > MaxOffsetMB)
            {
                MaxOffsetMB = OffsetMB;
                Found = TRUE;
            }
        }
    }

    if (Found)
    {
        /* Next free offset is one block past the highest found */
        State->HighestAllocatedMB = MaxOffsetMB + BlockSizeMB;
        ROSV_TRACE("VHDX: highest allocated offset = %llu MB, next free = %llu MB",
                   MaxOffsetMB, State->HighestAllocatedMB);
    }
    else
    {
        /*
         * No allocated blocks found. Start allocating after the header area.
         * Use a conservative starting offset: round up the BAT region end to
         * the next MB boundary, or use a minimum of 1 MB past all metadata.
         * For simplicity, start at the end of the BAT+metadata regions.
         */
        ULONG64 BatEndMB = (State->BatOffset + State->BatLength + VHDX_MB_ALIGNMENT - 1) / VHDX_MB_ALIGNMENT;
        ULONG64 MetaEndMB = (State->MetadataOffset + State->MetadataLength + VHDX_MB_ALIGNMENT - 1) / VHDX_MB_ALIGNMENT;
        State->HighestAllocatedMB = (BatEndMB > MetaEndMB) ? BatEndMB : MetaEndMB;

        /* Also account for the log region */
        if (State->LogOffset > 0 && State->LogLength > 0)
        {
            ULONG64 LogEndMB = (State->LogOffset + State->LogLength + VHDX_MB_ALIGNMENT - 1) / VHDX_MB_ALIGNMENT;
            if (LogEndMB > State->HighestAllocatedMB)
            {
                State->HighestAllocatedMB = LogEndMB;
            }
        }

        ROSV_TRACE("VHDX: no allocated blocks found, starting allocations at %llu MB",
                   State->HighestAllocatedMB);
    }
}

/* ========================================================================== */
/* BAT entry update                                                            */
/* ========================================================================== */

/**
 * Update a single BAT entry in the memory-mapped file.
 *
 * @param State     Parsed VHDX state with valid BatBase.
 * @param BatIndex  Index into the BAT array (includes interleaved SB entries).
 * @param NewEntry  The new 64-bit BAT entry value (state + FileOffsetMB).
 *
 * @return STATUS_SUCCESS or STATUS_INVALID_PARAMETER if index is out of range.
 */
NTSTATUS
RosvVhdxUpdateBatEntry(
    _Inout_ PROSV_VHDX_STATE State,
    _In_ ULONG BatIndex,
    _In_ ULONG64 NewEntry)
{
    PULONG64 BatArray;

    if (BatIndex >= State->BatEntryCount)
    {
        ROSV_ERR("VHDX: UpdateBatEntry index %u out of range (max %u)",
                 BatIndex, State->BatEntryCount);
        return STATUS_INVALID_PARAMETER;
    }

    if (!State->BatBase)
    {
        ROSV_ERR("VHDX: UpdateBatEntry called with NULL BatBase");
        return STATUS_INVALID_PARAMETER;
    }

    BatArray = (PULONG64)State->BatBase;
    BatArray[BatIndex] = NewEntry;

    /* Ensure the write is visible before any subsequent reads */
    KeMemoryBarrier();

    ROSV_DEBUG("VHDX: BAT[%u] = 0x%016llX (state=%u offset=%llu MB)",
               BatIndex,
               NewEntry,
               VHDX_BAT_STATE(NewEntry),
               VHDX_BAT_FILE_OFFSET_MB(NewEntry));

    return STATUS_SUCCESS;
}

/* ========================================================================== */
/* Block allocation                                                            */
/* ========================================================================== */

/**
 * Allocate a new payload block for the given block index.
 *
 * Finds the next free 1MB-aligned file offset, zero-fills the block region,
 * updates the BAT entry to FULLY_PRESENT, and returns the offset.
 *
 * @param State         Parsed VHDX state (mutable for allocation tracking).
 * @param BlockIndex    Logical block index (0-based, not BAT index).
 * @param FileOffsetMB  Receives the allocated file offset in MB units.
 *
 * @return STATUS_SUCCESS, STATUS_DISK_FULL if file is too small, or error.
 */
NTSTATUS
RosvVhdxAllocateBlock(
    _Inout_ PROSV_VHDX_STATE State,
    _In_ ULONG BlockIndex,
    _Out_ PULONG64 FileOffsetMB)
{
    ULONG64 NewOffsetMB;
    ULONG64 NewOffsetBytes;
    ULONG BatIndex;
    ULONG64 BatEntry;
    NTSTATUS Status;
    PVOID BlockRegion;

    *FileOffsetMB = 0;

    /* Validate block index */
    if (BlockIndex >= State->DataBlocksCount)
    {
        ROSV_ERR("VHDX: AllocateBlock index %u out of range (max %u)",
                 BlockIndex, State->DataBlocksCount);
        return STATUS_INVALID_PARAMETER;
    }

    /* Compute the next free 1MB-aligned file offset */
    NewOffsetMB = State->HighestAllocatedMB;
    NewOffsetBytes = NewOffsetMB * VHDX_MB_ALIGNMENT;

    /* Validate that the block fits within the file */
    if (NewOffsetBytes + State->BlockSize > State->FileSize)
    {
        ROSV_ERR("VHDX: AllocateBlock %u failed - file too small "
                 "(need offset %llu + %u bytes, file size %llu bytes)",
                 BlockIndex, NewOffsetBytes, State->BlockSize, State->FileSize);
        return STATUS_DISK_FULL;
    }

    /* Zero-fill the new block region */
    BlockRegion = (PUCHAR)State->FileBase + NewOffsetBytes;
    RtlZeroMemory(BlockRegion, State->BlockSize);

    /* Compute the BAT index, accounting for interleaved sector bitmap entries */
    BatIndex = BlockIndex + (BlockIndex / State->ChunkRatio);

    /* Build and write the BAT entry */
    BatEntry = VHDX_BAT_MAKE_ENTRY(VHDX_PAYLOAD_BLOCK_FULLY_PRESENT, NewOffsetMB);

    Status = RosvVhdxUpdateBatEntry(State, BatIndex, BatEntry);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("VHDX: AllocateBlock %u failed to update BAT[%u]: 0x%08X",
                 BlockIndex, BatIndex, Status);
        return Status;
    }

    /* Update allocation tracking */
    State->HighestAllocatedMB = NewOffsetMB + (State->BlockSize / VHDX_MB_ALIGNMENT);
    State->BlockAllocations++;

    *FileOffsetMB = NewOffsetMB;

    ROSV_TRACE("VHDX: allocated block %u at file offset %llu MB (BAT[%u], total allocs=%llu)",
               BlockIndex, NewOffsetMB, BatIndex, State->BlockAllocations);

    return STATUS_SUCCESS;
}

