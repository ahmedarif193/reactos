/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VHDX BAT (Block Allocation Table) sector translation engine
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Translates virtual sector addresses to file byte offsets by looking up
 * the BAT array.  Handles payload block states (FULLY_PRESENT, ZERO,
 * UNMAPPED, NOT_PRESENT, PARTIALLY_PRESENT) and routes differencing-disk
 * reads to the parent chain.
 *
 * Reference: [MS-VHDX] revision 8.0, Section 4 (BAT) and Section 8
 *            (Sector Translation Algorithm).
 */

#include <rosv/rosv.h>
#include <rosv/vhdx.h>

NTSYSAPI
NTSTATUS
NTAPI
ZwWaitForSingleObject(
    _In_ HANDLE Handle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);

#define ROSV_VHDX_PENDING_READ_TIMEOUT_SECONDS 20ULL
#define ROSV_VHDX_MAX_DEMAND_READ_BYTES (64UL * 1024UL)

/* ROSV_VHDX_READAHEAD_BYTES and ROSV_VHDX_READAHEAD_SECTORS are in vhdx.h */

FORCEINLINE
VOID
RosvVhdxInvalidateReadahead(
    _Inout_ PROSV_VHDX_STATE State)
{
    if (State == NULL)
        return;

    State->ReadaheadStartSector = 0;
    State->ReadaheadSectorCount = 0;
}

static
BOOLEAN
RosvIsMappedRangeResident(
    _In_ PUCHAR Base,
    _In_ SIZE_T Length)
{
    SIZE_T Offset;

    if (Base == NULL)
        return FALSE;
    if (Length == 0)
        return TRUE;

    for (Offset = 0; Offset < Length; Offset += PAGE_SIZE)
    {
        if (!MmIsAddressValid(Base + Offset))
            return FALSE;
    }

    return MmIsAddressValid(Base + Length - 1);
}

/* ========================================================================== */
/* RosvVhdxTranslateSector                                                     */
/* ========================================================================== */

/**
 * Translate a virtual sector number to a file byte offset via the BAT.
 *
 * @param State          Parsed VHDX state (must have valid BatBase).
 * @param VirtualSector  Sector number in the virtual disk address space.
 * @param FileOffset     Receives the byte offset in the VHDX file where
 *                       the sector data resides (only meaningful when
 *                       *BlockState == FULLY_PRESENT or PARTIALLY_PRESENT).
 * @param BlockState     Receives the 3-bit payload block state from the
 *                       BAT entry (VHDX_PAYLOAD_BLOCK_*).
 *
 * @return STATUS_SUCCESS or STATUS_INVALID_PARAMETER.
 */
NTSTATUS
RosvVhdxTranslateSector(
    _In_ PROSV_VHDX_STATE State,
    _In_ ULONG64 VirtualSector,
    _Out_ PULONG64 FileOffset,
    _Out_ PULONG BlockState)
{
    ULONG64 ByteOffset;
    ULONG   BlockIndex;
    ULONG64 OffsetInBlock;
    ULONG   BatIndex;
    ULONG64 BatEntry;
    ULONG   EntryState;

    *FileOffset = 0;
    *BlockState = VHDX_PAYLOAD_BLOCK_NOT_PRESENT;

    /* Validate inputs */
    if (!State || !State->BatBase)
    {
        ROSV_ERR("vhdx_bat: TranslateSector called with NULL state or BAT");
        return STATUS_INVALID_PARAMETER;
    }

    /* Step 1: Virtual sector to byte offset */
    ByteOffset = VirtualSector * (ULONG64)State->LogicalSectorSize;

    /* Bounds check */
    if (ByteOffset >= State->VirtualDiskSize)
    {
        ROSV_ERR("vhdx_bat: sector %llu (byte offset 0x%llX) exceeds virtual disk size 0x%llX",
                 VirtualSector, ByteOffset, State->VirtualDiskSize);
        return STATUS_INVALID_PARAMETER;
    }

    /* Step 2: Block index and offset within block */
    {
        ULONG64 BlockIndex64 = ByteOffset / State->BlockSize;
        if (BlockIndex64 > 0xFFFFFFFF)
        {
            ROSV_ERR("VHDX: BlockIndex overflow: offset 0x%llX / blocksize 0x%X = 0x%llX",
                     ByteOffset, State->BlockSize, BlockIndex64);
            return STATUS_INTEGER_OVERFLOW;
        }
        BlockIndex = (ULONG)BlockIndex64;
    }
    OffsetInBlock = ByteOffset % (ULONG64)State->BlockSize;

    /* Step 3: BAT array index, accounting for interleaved SB entries */
    if (State->ChunkRatio == 0)
    {
        ROSV_ERR("vhdx_bat: invalid ChunkRatio=0 (virtual_sector=%llu)", VirtualSector);
        return STATUS_DATA_ERROR;
    }
    BatIndex = BlockIndex + (BlockIndex / State->ChunkRatio);

    /* Bounds check on BAT array */
    if (BatIndex >= State->BatEntryCount)
    {
        ROSV_ERR("vhdx_bat: BAT index %u exceeds entry count %u (block_idx=%u, chunk_ratio=%u)",
                 BatIndex, State->BatEntryCount, BlockIndex, State->ChunkRatio);
        return STATUS_INVALID_PARAMETER;
    }

    /* Step 4: Read the 64-bit BAT entry */
    BatEntry = ((PULONG64)State->BatBase)[BatIndex];

    /* Step 5: Extract state */
    EntryState = VHDX_BAT_STATE(BatEntry);
    *BlockState = EntryState;

    /* Update stats */
    State->BatLookups++;

    /* Step 6: Compute file offset based on state */
    switch (EntryState)
    {
        case VHDX_PAYLOAD_BLOCK_FULLY_PRESENT:
        {
            ULONG64 BlockFileOffset = VHDX_BAT_OFFSET_BYTES(BatEntry);
            *FileOffset = BlockFileOffset + OffsetInBlock;
            break;
        }

        case VHDX_PAYLOAD_BLOCK_PARTIALLY_PRESENT:
        {
            /* For differencing disks: the block exists but individual sectors
             * may or may not be present (checked via sector bitmap).
             * Return the file offset; caller must consult the sector bitmap. */
            ULONG64 BlockFileOffset = VHDX_BAT_OFFSET_BYTES(BatEntry);
            *FileOffset = BlockFileOffset + OffsetInBlock;
            break;
        }

        case VHDX_PAYLOAD_BLOCK_ZERO:
        case VHDX_PAYLOAD_BLOCK_UNMAPPED:
            *FileOffset = 0;
            State->BatMisses++;
            break;

        case VHDX_PAYLOAD_BLOCK_NOT_PRESENT:
        case VHDX_PAYLOAD_BLOCK_UNDEFINED:
            *FileOffset = 0;
            State->BatMisses++;
            break;

        default:
            /* Reserved states 4 and 5 */
            *FileOffset = 0;
            State->BatMisses++;
            ROSV_ERR("vhdx_bat: sector %llu -> block %u, bat[%u]=0x%llX has reserved state %u",
                     VirtualSector, BlockIndex, BatIndex, BatEntry, EntryState);
            break;
    }

    return STATUS_SUCCESS;
}

/* ========================================================================== */
/* RosvVhdxReadSectors                                                         */
/* ========================================================================== */

/**
 * Read one or more contiguous virtual sectors from a VHDX.
 *
 * Translates each sector (or contiguous range within the same block) through
 * the BAT and either copies data from the mapped file, returns zeros, or
 * delegates to the differencing-disk parent chain.
 *
 * @param State        Parsed VHDX state.
 * @param SectorNumber First virtual sector to read.
 * @param SectorCount  Number of sectors to read.
 * @param Buffer       Output buffer (must be at least SectorCount * LogicalSectorSize).
 *
 * @return STATUS_SUCCESS or error.
 */
NTSTATUS
RosvVhdxReadSectors(
    _In_ PROSV_VHDX_STATE State,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _Out_writes_bytes_(SectorCount * State->LogicalSectorSize) PVOID Buffer)
{
    static ULONG ReadTraceCount;
    NTSTATUS Status;
    ULONG64 CurrentSector;
    ULONG SectorsRemaining;
    PUCHAR OutputPtr;
    ULONG SectorSize;
    ULONG LoopIterations;

    if (ReadTraceCount < 64)
    {
        ROSV_TRACE("vhdx_bat: ReadSectors begin sector=%llu count=%u lss=%u block=0x%X",
                   SectorNumber,
                   SectorCount,
                   State ? State->LogicalSectorSize : 0,
                   State ? State->BlockSize : 0);
        ReadTraceCount++;
    }

    if (!State || !Buffer)
    {
        ROSV_ERR("vhdx_bat: ReadSectors called with NULL state or buffer");
        return STATUS_INVALID_PARAMETER;
    }

    if (State->FileBase == NULL)
    {
        /*
         * This path requires a mapped VHDX payload view.
         * Demand-paged mode must use RosvVhdxReadSectorsDemandPaged instead.
         */
        ROSV_ERR("vhdx_bat: ReadSectors requires mapped FileBase, but FileBase is NULL");
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (SectorCount == 0)
    {
        ROSV_DEBUG("vhdx_bat: ReadSectors called with SectorCount=0, nothing to do");
        return STATUS_SUCCESS;
    }

    SectorSize = State->LogicalSectorSize;

    /* Bounds check: ensure the entire read range fits within the virtual disk */
    {
        ULONG64 EndByte = (SectorNumber + SectorCount) * (ULONG64)SectorSize;
        if (EndByte > State->VirtualDiskSize ||
            (SectorNumber + SectorCount) < SectorNumber) /* overflow check */
        {
            ROSV_ERR("vhdx_bat: ReadSectors range [%llu, %llu) exceeds virtual disk "
                     "(size=0x%llX, end_byte=0x%llX)",
                     SectorNumber, SectorNumber + SectorCount,
                     State->VirtualDiskSize, EndByte);
            return STATUS_INVALID_PARAMETER;
        }
    }

    CurrentSector = SectorNumber;
    SectorsRemaining = SectorCount;
    OutputPtr = (PUCHAR)Buffer;
    LoopIterations = 0;

    while (SectorsRemaining > 0)
    {
        ULONG64 FileOffset;
        ULONG   EntryState;
        ULONG   SectorsInThisBatch;
        ULONG64 ByteOffset;
        ULONG   BlockIndex;
        ULONG64 OffsetInBlock;
        ULONG   SectorsToBlockEnd;

        /* Translate the current sector */
        Status = RosvVhdxTranslateSector(State, CurrentSector, &FileOffset, &EntryState);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("vhdx_bat: ReadSectors - TranslateSector failed for sector %llu, status=0x%08X",
                     CurrentSector, Status);
            return Status;
        }

        /* Compute how many sectors remain in the current block, so we can
         * batch contiguous sectors that share the same BAT entry. */
        ByteOffset = CurrentSector * (ULONG64)SectorSize;
        BlockIndex = (ULONG)(ByteOffset / State->BlockSize);
        OffsetInBlock = ByteOffset % (ULONG64)State->BlockSize;
        SectorsToBlockEnd = (ULONG)((State->BlockSize - OffsetInBlock) / SectorSize);

        SectorsInThisBatch = SectorsRemaining;
        if (SectorsInThisBatch > SectorsToBlockEnd)
            SectorsInThisBatch = SectorsToBlockEnd;

        if (SectorsInThisBatch == 0)
        {
            ROSV_ERR("vhdx_bat: ReadSectors no-progress batch "
                     "(sector=%llu remaining=%u block=%u offset_in_block=0x%llX block_size=0x%X lss=%u)",
                     CurrentSector,
                     SectorsRemaining,
                     BlockIndex,
                     OffsetInBlock,
                     State->BlockSize,
                     SectorSize);
            return STATUS_DATA_ERROR;
        }

        LoopIterations++;
        if (LoopIterations > (SectorCount + 8))
        {
            ROSV_ERR("vhdx_bat: ReadSectors loop exceeded expected iterations "
                     "(start=%llu count=%u current=%llu remaining=%u iterations=%u)",
                     SectorNumber,
                     SectorCount,
                     CurrentSector,
                     SectorsRemaining,
                     LoopIterations);
            return STATUS_DATA_ERROR;
        }

        switch (EntryState)
        {
            case VHDX_PAYLOAD_BLOCK_FULLY_PRESENT:
            {
                ULONG64 CopySize = (ULONG64)SectorsInThisBatch * SectorSize;
                ROSV_ASSERT(KeGetCurrentIrql() <= APC_LEVEL,
                            "mapped VHDX payload copy requires IRQL <= APC_LEVEL");

                /* Validate file offset is within the mapped file */
                if (FileOffset + CopySize > State->FileSize)
                {
                    ROSV_ERR("vhdx_bat: ReadSectors - file offset 0x%llX + 0x%llX exceeds "
                             "file size 0x%llX (sector %llu, block %u)",
                             FileOffset, CopySize, State->FileSize, CurrentSector, BlockIndex);
                    return STATUS_DATA_ERROR;
                }

                RtlCopyMemory(OutputPtr,
                              (PUCHAR)State->FileBase + FileOffset,
                              (SIZE_T)CopySize);
                break;
            }

            case VHDX_PAYLOAD_BLOCK_ZERO:
            case VHDX_PAYLOAD_BLOCK_UNMAPPED:
                /* Return zeros */
                RtlZeroMemory(OutputPtr, (SIZE_T)SectorsInThisBatch * SectorSize);
                break;

            case VHDX_PAYLOAD_BLOCK_NOT_PRESENT:
            case VHDX_PAYLOAD_BLOCK_UNDEFINED:
                if (State->HasParent && State->Parent)
                {
                    /* Differencing disk: read from parent chain */
                    Status = RosvVhdxReadSectorsDiff(State, CurrentSector,
                                                     SectorsInThisBatch, OutputPtr);
                    if (!NT_SUCCESS(Status))
                    {
                        ROSV_ERR("vhdx_bat: ReadSectors - diff read failed for sector %llu "
                                 "count %u, status=0x%08X",
                                 CurrentSector, SectorsInThisBatch, Status);
                        return Status;
                    }
                }
                else
                {
                    /* Non-differencing or no parent: return zeros */
                    RtlZeroMemory(OutputPtr, (SIZE_T)SectorsInThisBatch * SectorSize);
                }
                break;

            case VHDX_PAYLOAD_BLOCK_PARTIALLY_PRESENT:
                if (State->HasParent && State->Parent)
                {
                    /* Differencing disk with per-sector bitmap.
                     * Must check bitmap for each sector individually. */
                    ULONG i;
                    for (i = 0; i < SectorsInThisBatch; i++)
                    {
                        ULONG64 SectorByteOff = (CurrentSector + i) * (ULONG64)SectorSize;
                        ULONG   SectorBlockIdx = (ULONG)(SectorByteOff / State->BlockSize);
                        ULONG   SectorWithinBlock = (ULONG)(
                            (SectorByteOff % (ULONG64)State->BlockSize) / SectorSize);

                        if (RosvVhdxIsSectorPresent(State, SectorBlockIdx, SectorWithinBlock))
                        {
                            /* Sector is present in this child disk */
                            ULONG64 SectorFileOffset;
                            ULONG   Unused;

                            Status = RosvVhdxTranslateSector(State, CurrentSector + i,
                                                             &SectorFileOffset, &Unused);
                            if (!NT_SUCCESS(Status))
                            {
                                ROSV_ERR("vhdx_bat: ReadSectors - re-translate failed "
                                         "for sector %llu, status=0x%08X",
                                         CurrentSector + i, Status);
                                return Status;
                            }

                            if (SectorFileOffset + SectorSize > State->FileSize)
                            {
                                ROSV_ERR("vhdx_bat: ReadSectors - partial sector file offset "
                                         "0x%llX exceeds file size 0x%llX",
                                         SectorFileOffset, State->FileSize);
                                return STATUS_DATA_ERROR;
                            }

                            RtlCopyMemory(OutputPtr + (SIZE_T)i * SectorSize,
                                          (PUCHAR)State->FileBase + SectorFileOffset,
                                          SectorSize);
                        }
                        else
                        {
                            /* Sector not present in child: read from parent */
                            Status = RosvVhdxReadSectorsDiff(State, CurrentSector + i,
                                                             1, OutputPtr + (SIZE_T)i * SectorSize);
                            if (!NT_SUCCESS(Status))
                            {
                                ROSV_ERR("vhdx_bat: ReadSectors - diff single sector read "
                                         "failed for sector %llu, status=0x%08X",
                                         CurrentSector + i, Status);
                                return Status;
                            }
                        }
                    }
                }
                else
                {
                    /* PARTIALLY_PRESENT on a non-differencing disk means corrupt BAT metadata */
                    ULONG BatIdx = BlockIndex + (BlockIndex / State->ChunkRatio);
                    ULONG64 BatEntry = (BatIdx < State->BatEntryCount)
                                       ? ((PULONG64)State->BatBase)[BatIdx] : 0;
                    ROSV_ERR("vhdx_bat: ReadSectors - PARTIALLY_PRESENT on non-differencing disk "
                             "is CORRUPT VHDX metadata (block=%u bat[%u]=0x%llX sector=%llu)",
                             BlockIndex, BatIdx, BatEntry, CurrentSector);
                    return STATUS_DATA_ERROR;
                }
                break;

            default:
                /* Reserved states: treat as zeros with a warning */
                ROSV_ERR("vhdx_bat: ReadSectors - reserved block state %u at sector %llu, "
                         "returning zeros", EntryState, CurrentSector);
                RtlZeroMemory(OutputPtr, (SIZE_T)SectorsInThisBatch * SectorSize);
                break;
        }

        OutputPtr += (SIZE_T)SectorsInThisBatch * SectorSize;
        CurrentSector += SectorsInThisBatch;
        SectorsRemaining -= SectorsInThisBatch;
    }

    if (ReadTraceCount < 64)
    {
        ROSV_TRACE("vhdx_bat: ReadSectors end sector=%llu count=%u",
                   SectorNumber,
                   SectorCount);
    }

    return STATUS_SUCCESS;
}

/* ========================================================================== */
/* RosvVhdxReadSectorsDemandPaged                                              */
/* ========================================================================== */

/**
 * Read one or more contiguous virtual sectors from a VHDX using demand-paged
 * I/O (ZwReadFile) instead of memory-mapped access.
 *
 * The BAT and metadata are still accessed from memory (they were parsed into
 * State at open time). Only the payload data blocks are read from the file
 * handle on demand, avoiding the need to memory-map the entire VHDX file.
 *
 * IMPORTANT: This function must be called at PASSIVE_LEVEL because ZwReadFile
 * requires it.
 *
 * @param State        Parsed VHDX state (BAT must be valid in memory).
 * @param FileHandle   Open NT file handle for the VHDX file (GENERIC_READ).
 * @param SectorNumber First virtual sector to read.
 * @param SectorCount  Number of sectors to read.
 * @param Buffer       Output buffer (must be at least SectorCount * LogicalSectorSize).
 *
 * @return STATUS_SUCCESS or error.
 */
static
NTSTATUS
RosvVhdxReadSectorsDemandPagedDirect(
    _In_ PROSV_VHDX_STATE State,
    _In_ HANDLE FileHandle,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _Out_writes_bytes_(SectorCount * State->LogicalSectorSize) PVOID Buffer);

static
NTSTATUS
RosvVhdxReadSectorsDemandPagedDirect(
    _In_ PROSV_VHDX_STATE State,
    _In_ HANDLE FileHandle,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _Out_writes_bytes_(SectorCount * State->LogicalSectorSize) PVOID Buffer)
{
    NTSTATUS Status;
    ULONG64 CurrentSector;
    ULONG SectorsRemaining;
    PUCHAR OutputPtr;
    ULONG SectorSize;

    if (!State || !Buffer || !FileHandle)
    {
        ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged called with NULL state/buffer/handle");
        return STATUS_INVALID_PARAMETER;
    }

    if (!State->BatBase)
    {
        ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged - BAT not loaded in memory");
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (SectorCount == 0)
    {
        ROSV_DEBUG("vhdx_bat: ReadSectorsDemandPaged called with SectorCount=0");
        return STATUS_SUCCESS;
    }

    SectorSize = State->LogicalSectorSize;

    /* Bounds check */
    {
        ULONG64 EndByte = (SectorNumber + SectorCount) * (ULONG64)SectorSize;
        if (EndByte > State->VirtualDiskSize ||
            (SectorNumber + SectorCount) < SectorNumber)
        {
            ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged range [%llu, %llu) exceeds virtual disk "
                     "(size=0x%llX, end_byte=0x%llX)",
                     SectorNumber, SectorNumber + SectorCount,
                     State->VirtualDiskSize, EndByte);
            return STATUS_INVALID_PARAMETER;
        }
    }

    CurrentSector = SectorNumber;
    SectorsRemaining = SectorCount;
    OutputPtr = (PUCHAR)Buffer;

    while (SectorsRemaining > 0)
    {
        ULONG64 FileOffset;
        ULONG   EntryState;
        ULONG   SectorsInThisBatch;
        ULONG64 ByteOffset;
        ULONG   BlockIndex;
        ULONG64 OffsetInBlock;
        ULONG   SectorsToBlockEnd;
        ULONG   MaxSectorsPerRead;

        /* Translate the current sector via BAT (BAT is in memory) */
        Status = RosvVhdxTranslateSector(State, CurrentSector, &FileOffset, &EntryState);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged - TranslateSector failed for sector %llu, "
                     "status=0x%08X", CurrentSector, Status);
            return Status;
        }

        /* Compute batch size (sectors remaining in this block) */
        ByteOffset = CurrentSector * (ULONG64)SectorSize;
        BlockIndex = (ULONG)(ByteOffset / State->BlockSize);
        OffsetInBlock = ByteOffset % (ULONG64)State->BlockSize;
        SectorsToBlockEnd = (ULONG)((State->BlockSize - OffsetInBlock) / SectorSize);

        SectorsInThisBatch = SectorsRemaining;
        if (SectorsInThisBatch > SectorsToBlockEnd)
            SectorsInThisBatch = SectorsToBlockEnd;

        /* Keep each demand-paged payload read bounded to one page. */
        MaxSectorsPerRead = ROSV_VHDX_MAX_DEMAND_READ_BYTES / SectorSize;
        if (MaxSectorsPerRead == 0)
            MaxSectorsPerRead = 1;
        if (SectorsInThisBatch > MaxSectorsPerRead)
            SectorsInThisBatch = MaxSectorsPerRead;

        /* Track demand-paged read stats */
        State->DemandPagedReads++;

        switch (EntryState)
        {
            case VHDX_PAYLOAD_BLOCK_FULLY_PRESENT:
            {
                ULONG64 ReadSize = (ULONG64)SectorsInThisBatch * SectorSize;
                IO_STATUS_BLOCK IoStatusBlock;
                LARGE_INTEGER ReadOffset;
                PUCHAR IoBuffer;
                PVOID IoBufferAlloc;
                ULONG BatIdx = BlockIndex + (BlockIndex / State->ChunkRatio);
                ULONG64 BatEntry = (BatIdx < State->BatEntryCount)
                                   ? ((PULONG64)State->BatBase)[BatIdx] : 0;

                /*
                 * Readahead cache check: if the requested sectors fall entirely
                 * within the cached readahead window, copy from cache and skip
                 * all I/O.  This dramatically reduces per-sector round-trips
                 * for sequential workloads (ext4 journal replay, inode tables).
                 */
                if (State->ReadaheadBuffer != NULL &&
                    CurrentSector >= State->ReadaheadStartSector &&
                    (CurrentSector + SectorsInThisBatch) <=
                        (State->ReadaheadStartSector + State->ReadaheadSectorCount))
                {
                    ULONG64 CacheOffset =
                        (CurrentSector - State->ReadaheadStartSector) * SectorSize;
                    RtlCopyMemory(OutputPtr,
                                  (PUCHAR)State->ReadaheadBuffer + CacheOffset,
                                  (SIZE_T)ReadSize);
                    State->ReadaheadHits++;
                    break;
                }

                /*
                 * Prefer mapped payload copy when FileBase is retained.
                 * The demand worker thread runs outside VM-exit context, so this
                 * avoids blocking filesystem ZwReadFile calls in the hot path.
                 */
                if (State->FileBase != NULL)
                {
                    PUCHAR MappedPtr;

                    if (FileOffset + ReadSize > State->FileSize)
                    {
                        State->DemandPagedErrors++;
                        ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged mapped copy out of range "
                                 "offset=0x%llX size=%llu file_size=0x%llX",
                                 FileOffset,
                                 ReadSize,
                                 State->FileSize);
                        return STATUS_DATA_ERROR;
                    }

                    MappedPtr = (PUCHAR)State->FileBase + FileOffset;
                    if (RosvIsMappedRangeResident(MappedPtr, (SIZE_T)ReadSize))
                    {
                        RtlCopyMemory(OutputPtr,
                                      MappedPtr,
                                      (SIZE_T)ReadSize);
                        break;
                    }
                }

                if (ReadSize > MAXULONG)
                {
                    State->DemandPagedErrors++;
                    ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged - read size too large: %llu",
                             ReadSize);
                    return STATUS_INVALID_PARAMETER;
                }

                /*
                 * Demand handles are opened with FILE_NO_INTERMEDIATE_BUFFERING.
                 * Ensure the I/O buffer is sector-aligned; copy back into the
                 * caller buffer when alignment requires a temporary bounce.
                 */
                IoBuffer = OutputPtr;
                IoBufferAlloc = NULL;
                if (((ULONG_PTR)IoBuffer & ((ULONG_PTR)SectorSize - 1)) != 0)
                {
                    SIZE_T RawSize;
                    ULONG_PTR RawAddress;
                    ULONG_PTR AlignedAddress;

                    RawSize = (SIZE_T)ReadSize + (SIZE_T)SectorSize - 1;
                    IoBufferAlloc = ExAllocatePoolWithTag(NonPagedPool, RawSize, ROSV_DRIVER_TAG);
                    if (IoBufferAlloc == NULL)
                    {
                        State->DemandPagedErrors++;
                        ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged - failed to allocate aligned bounce buffer (%zu bytes)",
                                 RawSize);
                        return STATUS_INSUFFICIENT_RESOURCES;
                    }

                    RawAddress = (ULONG_PTR)IoBufferAlloc;
                    AlignedAddress = (RawAddress + ((ULONG_PTR)SectorSize - 1)) &
                                     ~((ULONG_PTR)SectorSize - 1);
                    IoBuffer = (PUCHAR)AlignedAddress;
                }

                ReadOffset.QuadPart = (LONGLONG)FileOffset;

                IoStatusBlock.Status = STATUS_PENDING;
                IoStatusBlock.Information = 0;
                Status = ZwReadFile(FileHandle, NULL, NULL, NULL,
                                    &IoStatusBlock, IoBuffer, (ULONG)ReadSize,
                                    &ReadOffset, NULL);
                if (Status == STATUS_PENDING)
                {
                    LARGE_INTEGER Timeout;
                    NTSTATUS WaitStatus;

                    Timeout.QuadPart =
                        -((LONGLONG)ROSV_VHDX_PENDING_READ_TIMEOUT_SECONDS * 10 * 1000 * 1000);
                    WaitStatus = ZwWaitForSingleObject(FileHandle, FALSE, &Timeout);
                    if (WaitStatus == STATUS_TIMEOUT)
                    {
                        Status = STATUS_IO_TIMEOUT;
                        ROSV_ERR("vhdx_bat: demand-read timeout sector=%llu file_off=0x%llX size=%llu",
                                 CurrentSector,
                                 FileOffset,
                                 ReadSize);
                    }
                    else if (!NT_SUCCESS(WaitStatus))
                    {
                        Status = WaitStatus;
                    }
                    else
                    {
                        Status = IoStatusBlock.Status;
                    }
                }
                if (!NT_SUCCESS(Status))
                {
                    if (IoBufferAlloc != NULL)
                        ExFreePoolWithTag(IoBufferAlloc, ROSV_DRIVER_TAG);
                    State->DemandPagedErrors++;
                    ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged - ZwReadFile FAILED: "
                             "offset=0x%llX size=%llu status=0x%08X "
                             "block=%u bat[%u]=0x%llX sector=%llu",
                             FileOffset, ReadSize, Status,
                             BlockIndex, BatIdx, BatEntry, CurrentSector);
                    return Status;
                }

                if (IoStatusBlock.Information < ReadSize)
                {
                    if (IoBufferAlloc != NULL)
                        ExFreePoolWithTag(IoBufferAlloc, ROSV_DRIVER_TAG);
                    State->DemandPagedErrors++;
                    ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged - SHORT READ: "
                             "offset=0x%llX got %llu of %llu bytes, "
                             "block=%u bat[%u]=0x%llX sector=%llu - treating as I/O error",
                             FileOffset, (ULONG64)IoStatusBlock.Information, ReadSize,
                             BlockIndex, BatIdx, BatEntry, CurrentSector);
                    return STATUS_DATA_ERROR;
                }

                if (IoBufferAlloc != NULL)
                {
                    RtlCopyMemory(OutputPtr, IoBuffer, (SIZE_T)ReadSize);
                    ExFreePoolWithTag(IoBufferAlloc, ROSV_DRIVER_TAG);
                }

                /*
                 * Populate readahead cache: after a successful ZwReadFile,
                 * issue a speculative read for the next ROSV_VHDX_READAHEAD_SECTORS
                 * within the same VHDX block.  This converts sequential
                 * per-sector round-trips into single bulk reads.
                 */
                if (State->ReadaheadBuffer != NULL &&
                    SectorsToBlockEnd > SectorsInThisBatch)
                {
                    ULONG64 AheadStart = CurrentSector + SectorsInThisBatch;
                    ULONG AheadCount = SectorsToBlockEnd - SectorsInThisBatch;
                    ULONG AheadMax = ROSV_VHDX_READAHEAD_SECTORS;
                    IO_STATUS_BLOCK AheadIoSb;
                    LARGE_INTEGER AheadOff;
                    ULONG64 AheadFileOff;
                    ULONG64 AheadBytes;

                    if (AheadCount > AheadMax)
                        AheadCount = AheadMax;
                    AheadBytes = (ULONG64)AheadCount * SectorSize;
                    AheadFileOff = FileOffset + ReadSize;

                    if (AheadBytes <= ROSV_VHDX_READAHEAD_BYTES &&
                        AheadFileOff + AheadBytes <= State->FileSize)
                    {
                        AheadOff.QuadPart = (LONGLONG)AheadFileOff;
                        AheadIoSb.Status = STATUS_PENDING;
                        AheadIoSb.Information = 0;
                        Status = ZwReadFile(FileHandle, NULL, NULL, NULL,
                                            &AheadIoSb,
                                            State->ReadaheadBuffer,
                                            (ULONG)AheadBytes,
                                            &AheadOff, NULL);
                        if (Status == STATUS_PENDING)
                        {
                            LARGE_INTEGER AheadTimeout;
                            AheadTimeout.QuadPart =
                                -((LONGLONG)ROSV_VHDX_PENDING_READ_TIMEOUT_SECONDS * 10 * 1000 * 1000);
                            Status = ZwWaitForSingleObject(FileHandle, FALSE, &AheadTimeout);
                            if (NT_SUCCESS(Status))
                                Status = AheadIoSb.Status;
                        }
                        if (NT_SUCCESS(Status) &&
                            AheadIoSb.Information >= AheadBytes)
                        {
                            State->ReadaheadStartSector = AheadStart;
                            State->ReadaheadSectorCount = AheadCount;
                            State->ReadaheadMisses++;
                        }
                        else
                        {
                            /* Readahead failed — invalidate cache, not fatal */
                            RosvVhdxInvalidateReadahead(State);
                        }
                        /* Reset Status for caller — readahead failure is non-fatal */
                        Status = STATUS_SUCCESS;
                    }
                }

                break;
            }

            case VHDX_PAYLOAD_BLOCK_ZERO:
            case VHDX_PAYLOAD_BLOCK_UNMAPPED:
                RtlZeroMemory(OutputPtr, (SIZE_T)SectorsInThisBatch * SectorSize);
                break;

            case VHDX_PAYLOAD_BLOCK_NOT_PRESENT:
            case VHDX_PAYLOAD_BLOCK_UNDEFINED:
                /* For non-differencing: return zeros. Differencing not supported in demand-paged mode yet. */
                RtlZeroMemory(OutputPtr, (SIZE_T)SectorsInThisBatch * SectorSize);
                break;

            case VHDX_PAYLOAD_BLOCK_PARTIALLY_PRESENT:
            {
                ULONG BatIdx = BlockIndex + (BlockIndex / State->ChunkRatio);
                ULONG64 BatEntry = (BatIdx < State->BatEntryCount)
                                   ? ((PULONG64)State->BatBase)[BatIdx] : 0;

                State->DemandPagedErrors++;
                if (State->HasParent && State->Parent)
                {
                    ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged - PARTIALLY_PRESENT with parent: "
                             "demand-paged mode does not support differencing disks "
                             "(block=%u bat[%u]=0x%llX sector=%llu)",
                             BlockIndex, BatIdx, BatEntry, CurrentSector);
                    return STATUS_NOT_SUPPORTED;
                }
                else
                {
                    ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged - PARTIALLY_PRESENT on "
                             "non-differencing disk is CORRUPT VHDX metadata "
                             "(block=%u bat[%u]=0x%llX sector=%llu)",
                             BlockIndex, BatIdx, BatEntry, CurrentSector);
                    return STATUS_DATA_ERROR;
                }
            }

            default:
            {
                ULONG BatIdx = BlockIndex + (BlockIndex / State->ChunkRatio);
                ULONG64 BatEntry = (BatIdx < State->BatEntryCount)
                                   ? ((PULONG64)State->BatBase)[BatIdx] : 0;

                State->DemandPagedErrors++;
                ROSV_ERR("vhdx_bat: ReadSectorsDemandPaged - reserved block state %u at "
                         "sector %llu block=%u bat[%u]=0x%llX, returning error",
                         EntryState, CurrentSector, BlockIndex, BatIdx, BatEntry);
                return STATUS_DATA_ERROR;
            }
        }

        OutputPtr += (SIZE_T)SectorsInThisBatch * SectorSize;
        CurrentSector += SectorsInThisBatch;
        SectorsRemaining -= SectorsInThisBatch;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
RosvVhdxReadSectorsDemandPaged(
    _In_ PROSV_VHDX_STATE State,
    _In_ HANDLE FileHandle,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _Out_writes_bytes_(SectorCount * State->LogicalSectorSize) PVOID Buffer)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        ROSV_ERR("vhdx_bat: demand-read wrapper requires PASSIVE_LEVEL (irql=%lu)",
                 (ULONG)KeGetCurrentIrql());
        return STATUS_INVALID_DEVICE_STATE;
    }
    return RosvVhdxReadSectorsDemandPagedDirect(State,
                                                FileHandle,
                                                SectorNumber,
                                                SectorCount,
                                                Buffer);
}

/* ========================================================================== */
/* RosvVhdxWriteSectors                                                        */
/* ========================================================================== */

/**
 * Write one or more contiguous virtual sectors to a VHDX.
 *
 * Translates each sector through the BAT.  For blocks that are FULLY_PRESENT,
 * writes directly.  For ZERO/UNMAPPED/NOT_PRESENT blocks, allocates a new
 * block first.  PARTIALLY_PRESENT writes are not supported in v1 (returns
 * STATUS_NOT_SUPPORTED).
 *
 * @param State        Parsed VHDX state.
 * @param SectorNumber First virtual sector to write.
 * @param SectorCount  Number of sectors to write.
 * @param Buffer       Input buffer containing the sector data.
 *
 * @return STATUS_SUCCESS or error.
 */
NTSTATUS
RosvVhdxWriteSectors(
    _Inout_ PROSV_VHDX_STATE State,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _In_reads_bytes_(SectorCount * State->LogicalSectorSize) PVOID Buffer)
{
    NTSTATUS Status;
    ULONG64 CurrentSector;
    ULONG SectorsRemaining;
    PUCHAR InputPtr;
    ULONG SectorSize;

    if (!State || !Buffer)
    {
        ROSV_ERR("vhdx_bat: WriteSectors called with NULL state or buffer");
        return STATUS_INVALID_PARAMETER;
    }

    if (SectorCount == 0)
    {
        ROSV_DEBUG("vhdx_bat: WriteSectors called with SectorCount=0, nothing to do");
        return STATUS_SUCCESS;
    }

    SectorSize = State->LogicalSectorSize;

    /* Bounds check */
    {
        ULONG64 EndByte = (SectorNumber + SectorCount) * (ULONG64)SectorSize;
        if (EndByte > State->VirtualDiskSize ||
            (SectorNumber + SectorCount) < SectorNumber)
        {
            ROSV_ERR("vhdx_bat: WriteSectors range [%llu, %llu) exceeds virtual disk "
                     "(size=0x%llX, end_byte=0x%llX)",
                     SectorNumber, SectorNumber + SectorCount,
                     State->VirtualDiskSize, EndByte);
            return STATUS_INVALID_PARAMETER;
        }
    }

    CurrentSector = SectorNumber;
    SectorsRemaining = SectorCount;
    InputPtr = (PUCHAR)Buffer;

    while (SectorsRemaining > 0)
    {
        ULONG64 FileOffset;
        ULONG   EntryState;
        ULONG   SectorsInThisBatch;
        ULONG64 ByteOffset;
        ULONG   BlockIndex;
        ULONG64 OffsetInBlock;
        ULONG   SectorsToBlockEnd;

        /* Translate the current sector */
        Status = RosvVhdxTranslateSector(State, CurrentSector, &FileOffset, &EntryState);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("vhdx_bat: WriteSectors - TranslateSector failed for sector %llu, "
                     "status=0x%08X", CurrentSector, Status);
            return Status;
        }

        /* Compute batch size (up to end of current block) */
        ByteOffset = CurrentSector * (ULONG64)SectorSize;
        BlockIndex = (ULONG)(ByteOffset / State->BlockSize);
        OffsetInBlock = ByteOffset % (ULONG64)State->BlockSize;
        SectorsToBlockEnd = (ULONG)((State->BlockSize - OffsetInBlock) / SectorSize);

        SectorsInThisBatch = SectorsRemaining;
        if (SectorsInThisBatch > SectorsToBlockEnd)
            SectorsInThisBatch = SectorsToBlockEnd;

        switch (EntryState)
        {
            case VHDX_PAYLOAD_BLOCK_FULLY_PRESENT:
            {
                ULONG64 CopySize = (ULONG64)SectorsInThisBatch * SectorSize;

                if (FileOffset + CopySize > State->FileSize)
                {
                    ROSV_ERR("vhdx_bat: WriteSectors - file offset 0x%llX + 0x%llX exceeds "
                             "file size 0x%llX (sector %llu, block %u)",
                             FileOffset, CopySize, State->FileSize, CurrentSector, BlockIndex);
                    return STATUS_DATA_ERROR;
                }

                RtlCopyMemory((PUCHAR)State->FileBase + FileOffset,
                              InputPtr,
                              (SIZE_T)CopySize);
                /* Guest writes must invalidate speculative read cache. */
                RosvVhdxInvalidateReadahead(State);

                ROSV_DEBUG("vhdx_bat: WriteSectors - wrote %u sectors at file offset 0x%llX "
                           "(sector %llu, block %u)",
                           SectorsInThisBatch, FileOffset, CurrentSector, BlockIndex);
                break;
            }

            case VHDX_PAYLOAD_BLOCK_ZERO:
            case VHDX_PAYLOAD_BLOCK_UNMAPPED:
            case VHDX_PAYLOAD_BLOCK_NOT_PRESENT:
            case VHDX_PAYLOAD_BLOCK_UNDEFINED:
            {
                /* Block not yet allocated: allocate it, update BAT, then write */
                ULONG64 NewOffsetMB;
                ULONG64 NewFileOffset;
                ULONG64 CopySize;
                ULONG   BatIndex;
                ULONG64 NewBatEntry;

                Status = RosvVhdxAllocateBlock(State, BlockIndex, &NewOffsetMB);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("vhdx_bat: WriteSectors - AllocateBlock failed for block %u, "
                             "status=0x%08X", BlockIndex, Status);
                    return Status;
                }

                /* Zero-fill the newly allocated block before writing partial data.
                 * This ensures sectors not being written are zero (correct for
                 * previously-ZERO/UNMAPPED blocks). */
                NewFileOffset = NewOffsetMB << 20;
                if (NewFileOffset + State->BlockSize > State->FileSize)
                {
                    ROSV_ERR("vhdx_bat: WriteSectors - new block at 0x%llX (size 0x%X) "
                             "exceeds file size 0x%llX",
                             NewFileOffset, State->BlockSize, State->FileSize);
                    return STATUS_DATA_ERROR;
                }

                RtlZeroMemory((PUCHAR)State->FileBase + NewFileOffset, State->BlockSize);

                /* Update the BAT entry to FULLY_PRESENT */
                BatIndex = BlockIndex + (BlockIndex / State->ChunkRatio);
                NewBatEntry = VHDX_BAT_MAKE_ENTRY(VHDX_PAYLOAD_BLOCK_FULLY_PRESENT,
                                                   NewOffsetMB);
                Status = RosvVhdxUpdateBatEntry(State, BatIndex, NewBatEntry);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("vhdx_bat: WriteSectors - UpdateBatEntry failed for bat[%u], "
                             "status=0x%08X", BatIndex, Status);
                    return Status;
                }

                /* Now write the actual sector data */
                CopySize = (ULONG64)SectorsInThisBatch * SectorSize;
                RtlCopyMemory((PUCHAR)State->FileBase + NewFileOffset + OffsetInBlock,
                              InputPtr,
                              (SIZE_T)CopySize);
                /* Guest writes must invalidate speculative read cache. */
                RosvVhdxInvalidateReadahead(State);

                ROSV_DEBUG("vhdx_bat: WriteSectors - allocated block %u at MB offset %llu, "
                           "wrote %u sectors (sector %llu)",
                           BlockIndex, NewOffsetMB, SectorsInThisBatch, CurrentSector);
                break;
            }

            case VHDX_PAYLOAD_BLOCK_PARTIALLY_PRESENT:
                /* Differencing disk partial writes require sector bitmap updates.
                 * Not supported in v1. */
                ROSV_ERR("vhdx_bat: WriteSectors - PARTIALLY_PRESENT block %u write not "
                         "supported (sector %llu)", BlockIndex, CurrentSector);
                return STATUS_NOT_SUPPORTED;

            default:
                ROSV_ERR("vhdx_bat: WriteSectors - reserved block state %u at sector %llu",
                         EntryState, CurrentSector);
                return STATUS_DATA_ERROR;
        }

        InputPtr += (SIZE_T)SectorsInThisBatch * SectorSize;
        CurrentSector += SectorsInThisBatch;
        SectorsRemaining -= SectorsInThisBatch;
    }

    return STATUS_SUCCESS;
}
