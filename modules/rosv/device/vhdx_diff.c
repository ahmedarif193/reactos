/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VHDX differencing disk support
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements differencing (parent-child) VHDX disk chains.
 *
 * A differencing VHDX stores only the sectors that have been modified relative
 * to a parent image. On read, sectors not present in the child are resolved by
 * walking up the parent chain. Sector presence within a partially-present
 * block is determined by sector bitmap entries interleaved in the BAT.
 *
 * For v1: Opening a differencing chain is not yet supported. The open function
 * returns STATUS_NOT_SUPPORTED when a parent is detected. The read and bitmap
 * functions are fully implemented so that enabling differencing support later
 * only requires completing the open path.
 */

#include <rosv/rosv.h>
#include <rosv/vhdx.h>

/* ========================================================================== */
/* RosvVhdxOpenDiffChain                                                       */
/* ========================================================================== */

/**
 * Open the differencing disk parent chain for a child VHDX.
 *
 * Checks whether the child VHDX is a differencing disk (HasParent flag).
 * For v1, if a parent is detected, returns STATUS_NOT_SUPPORTED with a
 * diagnostic message. Non-differencing disks return STATUS_SUCCESS.
 *
 * @param Child  Parsed VHDX state for the child disk.
 * @return STATUS_SUCCESS if no parent (nothing to do),
 *         STATUS_NOT_SUPPORTED if HasParent is set.
 */
NTSTATUS
RosvVhdxOpenDiffChain(
    _Inout_ PROSV_VHDX_STATE Child)
{
    if (!Child)
    {
        ROSV_ERR("vhdx_diff: RosvVhdxOpenDiffChain called with NULL state");
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("vhdx_diff: checking differencing chain for disk "
               "(VirtualDiskSize=0x%llX, BlockSize=0x%X, HasParent=%u)",
               Child->VirtualDiskSize, Child->BlockSize, Child->HasParent);

    if (!Child->HasParent)
    {
        ROSV_TRACE("vhdx_diff: disk has no parent, differencing chain not needed");
        Child->Parent = NULL;
        Child->ChainDepth = 0;
        return STATUS_SUCCESS;
    }

    /*
     * v1: Differencing disks are not yet supported.
     * The parent locator metadata parsing, parent file opening, GUID linkage
     * validation, and recursive chain walking are not yet implemented.
     */
    ROSV_ERR("vhdx_diff: VHDX differencing disks not yet supported. "
             "Use a non-differencing VHDX.");
    return STATUS_NOT_SUPPORTED;
}

/* ========================================================================== */
/* RosvVhdxIsSectorPresent                                                     */
/* ========================================================================== */

/**
 * Check whether a specific sector within a partially-present block has data
 * in this VHDX file, using the sector bitmap.
 *
 * The sector bitmap for a chunk is stored in a sector bitmap BAT entry that
 * is interleaved among the payload BAT entries. The interleaving pattern:
 *
 *   chunk_index = BlockIndex / ChunkRatio
 *   sb_bat_index = chunk_index * (ChunkRatio + 1) + ChunkRatio
 *
 * Each bit in the sector bitmap corresponds to one logical sector within the
 * chunk. A set bit means the sector is present in this file; a clear bit
 * means the sector should be read from the parent.
 *
 * @param State            VHDX state for this file.
 * @param BlockIndex       Payload block index (0-based).
 * @param SectorWithinBlock  Sector offset within the block.
 * @return TRUE if the sector is present in this file, FALSE otherwise.
 */
BOOLEAN
RosvVhdxIsSectorPresent(
    _In_ PROSV_VHDX_STATE State,
    _In_ ULONG BlockIndex,
    _In_ ULONG SectorWithinBlock)
{
    ULONG ChunkIndex;
    ULONG SbBatIndex;
    ULONG64 SbBatEntry;
    ULONG SbState;
    ULONG64 BitmapFileOffset;
    PUCHAR BitmapBase;
    ULONG SectorBitIndex;
    ULONG ByteIndex;
    ULONG BitInByte;
    PULONG64 BatArray;

    if (!State || !State->BatBase)
    {
        ROSV_ERR("vhdx_diff: IsSectorPresent called with NULL state or BAT");
        return FALSE;
    }

    if (State->ChunkRatio == 0)
    {
        ROSV_ERR("vhdx_diff: ChunkRatio is 0, cannot compute sector bitmap index "
                 "(BlockIndex=%u)", BlockIndex);
        return FALSE;
    }

    /*
     * Compute the BAT index for the sector bitmap entry that covers this block.
     *
     * The BAT is laid out as groups of (ChunkRatio) payload entries followed by
     * one sector bitmap entry:
     *   [P0..P(CR-1), SB0, P(CR)..P(2*CR-1), SB1, ...]
     *
     * For a given BlockIndex, the containing chunk is BlockIndex / ChunkRatio.
     * The SB entry for that chunk is at index:
     *   chunk_index * (ChunkRatio + 1) + ChunkRatio
     */
    ChunkIndex = BlockIndex / State->ChunkRatio;
    SbBatIndex = ChunkIndex * (State->ChunkRatio + 1) + State->ChunkRatio;

    ROSV_DEBUG("vhdx_diff: IsSectorPresent BlockIndex=%u SectorWithinBlock=%u "
               "ChunkIndex=%u SbBatIndex=%u (BatEntryCount=%u)",
               BlockIndex, SectorWithinBlock, ChunkIndex, SbBatIndex,
               State->BatEntryCount);

    if (SbBatIndex >= State->BatEntryCount)
    {
        ROSV_ERR("vhdx_diff: SB BAT index %u out of range (BatEntryCount=%u, "
                 "BlockIndex=%u, ChunkRatio=%u)",
                 SbBatIndex, State->BatEntryCount, BlockIndex, State->ChunkRatio);
        return FALSE;
    }

    /* Read the sector bitmap BAT entry */
    BatArray = (PULONG64)State->BatBase;
    SbBatEntry = BatArray[SbBatIndex];
    SbState = VHDX_BAT_STATE(SbBatEntry);

    ROSV_DEBUG("vhdx_diff: SB BAT entry=0x%llX state=%u for chunk %u",
               SbBatEntry, SbState, ChunkIndex);

    if (SbState == VHDX_SB_BLOCK_NOT_PRESENT)
    {
        /*
         * No sector bitmap allocated for this chunk. Per spec, this means
         * no sectors in this chunk have been modified in the child -- all
         * reads should go to the parent.
         */
        ROSV_DEBUG("vhdx_diff: SB block not present for chunk %u, "
                   "sector not in this file", ChunkIndex);
        return FALSE;
    }

    if (SbState != VHDX_SB_BLOCK_PRESENT)
    {
        ROSV_ERR("vhdx_diff: unexpected SB block state %u for chunk %u "
                 "(expected NOT_PRESENT=0 or PRESENT=6)", SbState, ChunkIndex);
        return FALSE;
    }

    /* SB block is present -- read the bitmap from the file */
    BitmapFileOffset = VHDX_BAT_OFFSET_BYTES(SbBatEntry);

    if (BitmapFileOffset == 0 || BitmapFileOffset >= State->FileSize)
    {
        ROSV_ERR("vhdx_diff: SB bitmap file offset 0x%llX out of range "
                 "(FileSize=0x%llX) for chunk %u",
                 BitmapFileOffset, State->FileSize, ChunkIndex);
        return FALSE;
    }

    BitmapBase = (PUCHAR)State->FileBase + BitmapFileOffset;

    /*
     * Each bit in the bitmap corresponds to one logical sector.
     * SectorWithinBlock is the sector offset within the payload block.
     * Since sector bitmaps cover a full chunk (ChunkRatio blocks), the
     * actual bit index accounts for the block's position within the chunk.
     */
    SectorBitIndex = ((BlockIndex % State->ChunkRatio) *
                      (State->BlockSize / State->LogicalSectorSize)) +
                     SectorWithinBlock;

    ByteIndex = SectorBitIndex / 8;
    BitInByte = SectorBitIndex % 8;

    ROSV_DEBUG("vhdx_diff: checking bitmap at file offset 0x%llX, "
               "SectorBitIndex=%u (byte=%u, bit=%u)",
               BitmapFileOffset, SectorBitIndex, ByteIndex, BitInByte);

    /* Bounds check against the 1MB sector bitmap block */
    if (ByteIndex >= VHDX_MB_ALIGNMENT)
    {
        ROSV_ERR("vhdx_diff: bitmap byte index %u exceeds 1MB bitmap block size "
                 "(SectorBitIndex=%u)", ByteIndex, SectorBitIndex);
        return FALSE;
    }

    return (BitmapBase[ByteIndex] >> BitInByte) & 1;
}

/* ========================================================================== */
/* RosvVhdxReadSectorsDiff                                                     */
/* ========================================================================== */

/**
 * Read sectors from a differencing VHDX disk chain.
 *
 * For each sector requested, the function determines where the data lives:
 *   - FULLY_PRESENT: data is in this file, read directly.
 *   - PARTIALLY_PRESENT: check the sector bitmap to see if the sector is
 *     present in this file or needs to be read from the parent.
 *   - NOT_PRESENT, UNDEFINED, ZERO, UNMAPPED: the data is not in this file.
 *     Recurse to the parent. If no parent exists (end of chain), return zeros.
 *
 * @param State        VHDX state for the current file in the chain.
 * @param SectorNumber Starting virtual sector number.
 * @param SectorCount  Number of sectors to read.
 * @param Buffer       Output buffer (must be SectorCount * LogicalSectorSize bytes).
 * @return STATUS_SUCCESS or an error status.
 */
NTSTATUS
RosvVhdxReadSectorsDiff(
    _In_ PROSV_VHDX_STATE State,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _Out_ PVOID Buffer)
{
    NTSTATUS Status;
    ULONG i;
    ULONG SectorSize;
    PUCHAR OutputPtr;
    ULONG64 CurrentSector;
    ULONG64 FileOffset;
    ULONG BlockState;
    ULONG BlockIndex;
    ULONG SectorWithinBlock;
    ULONG SectorsPerBlock;
    PUCHAR SrcPtr;

    if (!State)
    {
        ROSV_ERR("vhdx_diff: ReadSectorsDiff called with NULL state");
        return STATUS_INVALID_PARAMETER;
    }

    if (!Buffer)
    {
        ROSV_ERR("vhdx_diff: ReadSectorsDiff called with NULL buffer");
        return STATUS_INVALID_PARAMETER;
    }

    if (SectorCount == 0)
    {
        ROSV_TRACE("vhdx_diff: ReadSectorsDiff called with SectorCount=0, nothing to do");
        return STATUS_SUCCESS;
    }

    SectorSize = State->LogicalSectorSize;
    if (SectorSize == 0)
    {
        ROSV_ERR("vhdx_diff: LogicalSectorSize is 0");
        return STATUS_INTERNAL_ERROR;
    }

    SectorsPerBlock = State->BlockSize / SectorSize;
    if (SectorsPerBlock == 0)
    {
        ROSV_ERR("vhdx_diff: SectorsPerBlock is 0 (BlockSize=0x%X, SectorSize=%u)",
                 State->BlockSize, SectorSize);
        return STATUS_INTERNAL_ERROR;
    }

    OutputPtr = (PUCHAR)Buffer;

    ROSV_DEBUG("vhdx_diff: ReadSectorsDiff start sector=%llu count=%u "
               "(ChainDepth=%u, HasParent=%u, Parent=%p)",
               SectorNumber, SectorCount, State->ChainDepth,
               State->HasParent, State->Parent);

    for (i = 0; i < SectorCount; i++)
    {
        CurrentSector = SectorNumber + i;

        /* Translate the virtual sector to a file offset and block state */
        Status = RosvVhdxTranslateSector(State, CurrentSector, &FileOffset, &BlockState);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("vhdx_diff: TranslateSector failed for sector %llu, status=0x%08X",
                     CurrentSector, Status);
            return Status;
        }

        /* Compute block index and offset within block for bitmap lookups */
        BlockIndex = (ULONG)((CurrentSector * SectorSize) / State->BlockSize);
        SectorWithinBlock = (ULONG)(CurrentSector - ((ULONG64)BlockIndex * SectorsPerBlock));

        switch (BlockState)
        {
        case VHDX_PAYLOAD_BLOCK_FULLY_PRESENT:
            /*
             * The entire block is present in this file.
             * Read the sector directly from the file at the translated offset.
             */
            if (FileOffset == 0 || FileOffset + SectorSize > State->FileSize)
            {
                ROSV_ERR("vhdx_diff: FULLY_PRESENT sector %llu has invalid file offset "
                         "0x%llX (FileSize=0x%llX)", CurrentSector, FileOffset, State->FileSize);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            SrcPtr = (PUCHAR)State->FileBase + FileOffset;
            RtlCopyMemory(OutputPtr, SrcPtr, SectorSize);

            ROSV_DEBUG("vhdx_diff: sector %llu FULLY_PRESENT at file offset 0x%llX",
                       CurrentSector, FileOffset);
            break;

        case VHDX_PAYLOAD_BLOCK_PARTIALLY_PRESENT:
            /*
             * The block is partially present -- some sectors are in this file,
             * others must come from the parent. Check the sector bitmap.
             */
            if (RosvVhdxIsSectorPresent(State, BlockIndex, SectorWithinBlock))
            {
                /* Sector is present in this file, read it */
                if (FileOffset == 0 || FileOffset + SectorSize > State->FileSize)
                {
                    ROSV_ERR("vhdx_diff: PARTIALLY_PRESENT sector %llu (present in bitmap) "
                             "has invalid file offset 0x%llX (FileSize=0x%llX)",
                             CurrentSector, FileOffset, State->FileSize);
                    return STATUS_FILE_CORRUPT_ERROR;
                }

                SrcPtr = (PUCHAR)State->FileBase + FileOffset;
                RtlCopyMemory(OutputPtr, SrcPtr, SectorSize);

                ROSV_DEBUG("vhdx_diff: sector %llu PARTIALLY_PRESENT, bitmap=1, "
                           "reading from file offset 0x%llX", CurrentSector, FileOffset);
            }
            else
            {
                /* Sector not in this file -- fall through to parent resolution */
                ROSV_DEBUG("vhdx_diff: sector %llu PARTIALLY_PRESENT, bitmap=0, "
                           "deferring to parent", CurrentSector);
                goto resolve_from_parent;
            }
            break;

        case VHDX_PAYLOAD_BLOCK_NOT_PRESENT:
        case VHDX_PAYLOAD_BLOCK_UNDEFINED:
        case VHDX_PAYLOAD_BLOCK_ZERO:
        case VHDX_PAYLOAD_BLOCK_UNMAPPED:
            /*
             * The block is not present in this file. For differencing disks,
             * NOT_PRESENT and UNDEFINED mean "check parent". ZERO and UNMAPPED
             * mean "return zeros" regardless of parent, per the spec.
             */
            if (BlockState == VHDX_PAYLOAD_BLOCK_ZERO ||
                BlockState == VHDX_PAYLOAD_BLOCK_UNMAPPED)
            {
                ROSV_DEBUG("vhdx_diff: sector %llu state=%u (ZERO/UNMAPPED), returning zeros",
                           CurrentSector, BlockState);
                RtlZeroMemory(OutputPtr, SectorSize);
                break;
            }

            ROSV_DEBUG("vhdx_diff: sector %llu state=%u (NOT_PRESENT/UNDEFINED), "
                       "deferring to parent", CurrentSector, BlockState);

resolve_from_parent:
            if (State->Parent != NULL)
            {
                /*
                 * Recurse into the parent. The parent's ReadSectorsDiff
                 * will apply the same logic, walking further up the chain
                 * if needed.
                 */
                ROSV_DEBUG("vhdx_diff: resolving sector %llu from parent (depth=%u)",
                           CurrentSector, State->ChainDepth);

                Status = RosvVhdxReadSectorsDiff(State->Parent, CurrentSector, 1, OutputPtr);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("vhdx_diff: parent read failed for sector %llu, status=0x%08X",
                             CurrentSector, Status);
                    return Status;
                }
            }
            else
            {
                /*
                 * End of the chain -- no parent. Return zeros.
                 * This is the correct behavior: unallocated sectors at the
                 * root of a differencing chain read as zero.
                 */
                ROSV_DEBUG("vhdx_diff: sector %llu has no parent (end of chain), "
                           "returning zeros", CurrentSector);
                RtlZeroMemory(OutputPtr, SectorSize);
            }
            break;

        default:
            ROSV_ERR("vhdx_diff: unexpected block state %u for sector %llu",
                     BlockState, CurrentSector);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        OutputPtr += SectorSize;
    }

    ROSV_DEBUG("vhdx_diff: ReadSectorsDiff completed %u sectors starting at %llu",
               SectorCount, SectorNumber);

    return STATUS_SUCCESS;
}
