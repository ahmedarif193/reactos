/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VHDX file open, validation, and metadata parsing
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements RosvVhdxOpen, RosvVhdxClose, and RosvVhdxIsVhdx.
 * All operations work on a memory-mapped VHDX file (base pointer + offsets).
 * Validates signatures, checksums (CRC-32C), and parses the full metadata chain.
 */

#include <rosv/rosv.h>
#include <rosv/vhdx.h>

/* ========================================================================== */
/* Helpers                                                                      */
/* ========================================================================== */

/* Safely compute pointer within mapped file, returns NULL if out of bounds */
static __inline PVOID
VhdxFilePtr(
    _In_ PVOID FileBase,
    _In_ ULONG64 FileSize,
    _In_ ULONG64 Offset,
    _In_ ULONG Length)
{
    if (Offset + Length > FileSize || Offset + Length < Offset)
        return NULL;
    return (PUCHAR)FileBase + Offset;
}

/* ========================================================================== */
/* Header parsing                                                               */
/* ========================================================================== */

/*
 * Validate and parse a single VHDX header at the given file offset.
 * Returns STATUS_SUCCESS if the header is valid, error otherwise.
 * On success, *SequenceNumber is set.
 */
static NTSTATUS
VhdxValidateHeader(
    _In_ PVOID FileBase,
    _In_ ULONG64 FileSize,
    _In_ ULONG64 HeaderOffset,
    _In_ ULONG HeaderIndex,
    _Out_ PVHDX_HEADER *OutHeader,
    _Out_ PULONG64 SequenceNumber)
{
    PVHDX_HEADER Header;

    *OutHeader = NULL;
    *SequenceNumber = 0;

    Header = (PVHDX_HEADER)VhdxFilePtr(FileBase, FileSize,
                                        HeaderOffset, VHDX_HEADER_STRUCT_SIZE);
    if (!Header)
    {
        ROSV_ERR("VhdxValidateHeader: Header%u at offset 0x%llX extends past file end (size 0x%llX)",
                 HeaderIndex + 1, HeaderOffset, FileSize);
        return STATUS_INVALID_PARAMETER;
    }

    /* Check signature */
    if (Header->Signature != VHDX_HEADER_SIGNATURE)
    {
        ROSV_ERR("VhdxValidateHeader: Header%u bad signature 0x%08X (expected 0x%08X)",
                 HeaderIndex + 1, Header->Signature, VHDX_HEADER_SIGNATURE);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Check version */
    if (Header->Version != VHDX_FORMAT_VERSION)
    {
        ROSV_ERR("VhdxValidateHeader: Header%u unsupported version %u (expected %u)",
                 HeaderIndex + 1, Header->Version, VHDX_FORMAT_VERSION);
        return STATUS_REVISION_MISMATCH;
    }

    /* Verify CRC-32C (checksum at offset 4 within the 4KB header) */
    if (!RosvVhdxVerifyChecksum(Header, VHDX_HEADER_STRUCT_SIZE,
                                 FIELD_OFFSET(VHDX_HEADER, Checksum)))
    {
        ROSV_ERR("VhdxValidateHeader: Header%u CRC-32C checksum mismatch",
                 HeaderIndex + 1);
        return STATUS_CRC_ERROR;
    }

    ROSV_TRACE("VhdxValidateHeader: Header%u valid, SequenceNumber=%llu",
               HeaderIndex + 1, Header->SequenceNumber);

    *OutHeader = Header;
    *SequenceNumber = Header->SequenceNumber;
    return STATUS_SUCCESS;
}

/* ========================================================================== */
/* Region table parsing                                                         */
/* ========================================================================== */

/*
 * Find a region entry by GUID in the region table.
 * Returns NULL if not found.
 */
static PVHDX_REGION_TABLE_ENTRY
VhdxFindRegionEntry(
    _In_ PVHDX_REGION_TABLE_HEADER RegionTable,
    _In_ const GUID *TargetGuid)
{
    PVHDX_REGION_TABLE_ENTRY Entries;
    ULONG i;

    Entries = (PVHDX_REGION_TABLE_ENTRY)(RegionTable + 1);
    for (i = 0; i < RegionTable->EntryCount; i++)
    {
        if (RosvIsEqualGuid(&Entries[i].Guid, TargetGuid))
            return &Entries[i];
    }
    return NULL;
}

/*
 * Validate a region table at the given offset.
 * The entire 64KB region is checksummed.
 */
static NTSTATUS
VhdxValidateRegionTable(
    _In_ PVOID FileBase,
    _In_ ULONG64 FileSize,
    _In_ ULONG64 TableOffset,
    _In_ ULONG TableIndex,
    _Out_ PVHDX_REGION_TABLE_HEADER *OutTable)
{
    PVHDX_REGION_TABLE_HEADER Table;

    *OutTable = NULL;

    Table = (PVHDX_REGION_TABLE_HEADER)VhdxFilePtr(FileBase, FileSize,
                                                     TableOffset,
                                                     VHDX_REGION_TABLE_SIZE);
    if (!Table)
    {
        ROSV_ERR("VhdxValidateRegionTable: Table%u at 0x%llX extends past file (size 0x%llX)",
                 TableIndex + 1, TableOffset, FileSize);
        return STATUS_INVALID_PARAMETER;
    }

    if (Table->Signature != VHDX_REGION_SIGNATURE)
    {
        ROSV_ERR("VhdxValidateRegionTable: Table%u bad signature 0x%08X (expected 0x%08X)",
                 TableIndex + 1, Table->Signature, VHDX_REGION_SIGNATURE);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    if (Table->EntryCount > VHDX_MAX_REGION_ENTRIES)
    {
        ROSV_ERR("VhdxValidateRegionTable: Table%u EntryCount %u exceeds max %u",
                 TableIndex + 1, Table->EntryCount, VHDX_MAX_REGION_ENTRIES);
        return STATUS_INVALID_PARAMETER;
    }

    /* CRC-32C over the full 64KB region table */
    if (!RosvVhdxVerifyChecksum(Table, VHDX_REGION_TABLE_SIZE,
                                 FIELD_OFFSET(VHDX_REGION_TABLE_HEADER, Checksum)))
    {
        ROSV_ERR("VhdxValidateRegionTable: Table%u CRC-32C checksum mismatch",
                 TableIndex + 1);
        return STATUS_CRC_ERROR;
    }

    ROSV_TRACE("VhdxValidateRegionTable: Table%u valid, %u entries",
               TableIndex + 1, Table->EntryCount);

    *OutTable = Table;
    return STATUS_SUCCESS;
}

/* ========================================================================== */
/* Metadata parsing                                                             */
/* ========================================================================== */

/*
 * Find a metadata entry by GUID in the metadata table.
 * Returns NULL if not found.
 */
static PVHDX_METADATA_TABLE_ENTRY
VhdxFindMetadataEntry(
    _In_ PVHDX_METADATA_TABLE_HEADER MetaTable,
    _In_ const GUID *TargetGuid)
{
    PVHDX_METADATA_TABLE_ENTRY Entries;
    ULONG i;

    Entries = (PVHDX_METADATA_TABLE_ENTRY)(MetaTable + 1);
    for (i = 0; i < MetaTable->EntryCount; i++)
    {
        if (RosvIsEqualGuid(&Entries[i].ItemId, TargetGuid))
            return &Entries[i];
    }
    return NULL;
}

/*
 * Parse all required metadata items from the metadata region.
 * MetadataBase points to the start of the metadata region in memory.
 */
static NTSTATUS
VhdxParseMetadata(
    _In_ PVOID MetadataBase,
    _In_ ULONG MetadataLength,
    _Inout_ PROSV_VHDX_STATE State)
{
    PVHDX_METADATA_TABLE_HEADER MetaTable;
    PVHDX_METADATA_TABLE_ENTRY Entry;
    PVOID ItemData;

    static const GUID FileParamsGuid = VHDX_META_FILE_PARAMETERS_GUID;
    static const GUID VirtualDiskSizeGuid = VHDX_META_VIRTUAL_DISK_SIZE_GUID;
    static const GUID LogicalSectorSizeGuid = VHDX_META_LOGICAL_SECTOR_SIZE_GUID;
    static const GUID PhysicalSectorSizeGuid = VHDX_META_PHYSICAL_SECTOR_SIZE_GUID;
    static const GUID VirtualDiskIdGuid = VHDX_META_VIRTUAL_DISK_ID_GUID;

    /* The metadata table header is at the start of the metadata region */
    if (MetadataLength < sizeof(VHDX_METADATA_TABLE_HEADER))
    {
        ROSV_ERR("VhdxParseMetadata: Metadata region too small (%u bytes)", MetadataLength);
        return STATUS_INVALID_PARAMETER;
    }

    MetaTable = (PVHDX_METADATA_TABLE_HEADER)MetadataBase;

    if (MetaTable->Signature != VHDX_METADATA_SIGNATURE)
    {
        ROSV_ERR("VhdxParseMetadata: Bad metadata signature 0x%016llX (expected 0x%016llX)",
                 MetaTable->Signature, VHDX_METADATA_SIGNATURE);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    if (MetaTable->EntryCount > VHDX_MAX_METADATA_ENTRIES)
    {
        ROSV_ERR("VhdxParseMetadata: EntryCount %u exceeds max %u",
                 MetaTable->EntryCount, VHDX_MAX_METADATA_ENTRIES);
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("VhdxParseMetadata: %u metadata entries", MetaTable->EntryCount);

    /* --- File Parameters (required) --- */
    Entry = VhdxFindMetadataEntry(MetaTable, &FileParamsGuid);
    if (!Entry)
    {
        ROSV_ERR("VhdxParseMetadata: File Parameters metadata item not found");
        return STATUS_NOT_FOUND;
    }
    if (Entry->Length < sizeof(VHDX_FILE_PARAMETERS))
    {
        ROSV_ERR("VhdxParseMetadata: File Parameters item too small (%u bytes)", Entry->Length);
        return STATUS_INVALID_PARAMETER;
    }
    if (Entry->Offset > MetadataLength || Entry->Length > MetadataLength - Entry->Offset)
    {
        ROSV_ERR("VhdxParseMetadata: File Parameters item at offset %u+%u exceeds region %u",
                 Entry->Offset, Entry->Length, MetadataLength);
        return STATUS_INVALID_PARAMETER;
    }
    ItemData = (PUCHAR)MetadataBase + Entry->Offset;
    {
        PVHDX_FILE_PARAMETERS FileParams = (PVHDX_FILE_PARAMETERS)ItemData;
        State->BlockSize = FileParams->BlockSize;
        State->IsFixed = (FileParams->Flags & VHDX_FILE_PARAMS_LEAVE_BLOCKS_ALLOCATED) ? TRUE : FALSE;
        State->HasParent = (FileParams->Flags & VHDX_FILE_PARAMS_HAS_PARENT) ? TRUE : FALSE;

        ROSV_TRACE("VhdxParseMetadata: BlockSize=%u IsFixed=%u HasParent=%u",
                   State->BlockSize, State->IsFixed, State->HasParent);

        /* Validate block size range */
        if (State->BlockSize < VHDX_BLOCK_SIZE_MIN || State->BlockSize > VHDX_BLOCK_SIZE_MAX)
        {
            ROSV_ERR("VhdxParseMetadata: BlockSize %u out of range [%u, %u]",
                     State->BlockSize, VHDX_BLOCK_SIZE_MIN, VHDX_BLOCK_SIZE_MAX);
            return STATUS_INVALID_PARAMETER;
        }
        /* Block size must be a power of 2 */
        if ((State->BlockSize & (State->BlockSize - 1)) != 0)
        {
            ROSV_ERR("VhdxParseMetadata: BlockSize %u is not a power of 2", State->BlockSize);
            return STATUS_INVALID_PARAMETER;
        }
    }

    /* --- Virtual Disk Size (required) --- */
    Entry = VhdxFindMetadataEntry(MetaTable, &VirtualDiskSizeGuid);
    if (!Entry)
    {
        ROSV_ERR("VhdxParseMetadata: Virtual Disk Size metadata item not found");
        return STATUS_NOT_FOUND;
    }
    if (Entry->Length < sizeof(ULONG64))
    {
        ROSV_ERR("VhdxParseMetadata: Virtual Disk Size item too small (%u bytes)", Entry->Length);
        return STATUS_INVALID_PARAMETER;
    }
    if (Entry->Offset > MetadataLength || Entry->Length > MetadataLength - Entry->Offset)
    {
        ROSV_ERR("VhdxParseMetadata: Virtual Disk Size at offset %u+%u exceeds region %u",
                 Entry->Offset, Entry->Length, MetadataLength);
        return STATUS_INVALID_PARAMETER;
    }
    State->VirtualDiskSize = *(PULONG64)((PUCHAR)MetadataBase + Entry->Offset);
    ROSV_TRACE("VhdxParseMetadata: VirtualDiskSize=%llu bytes (%llu MB)",
               State->VirtualDiskSize, State->VirtualDiskSize / (1024 * 1024));

    /* --- Logical Sector Size (required) --- */
    Entry = VhdxFindMetadataEntry(MetaTable, &LogicalSectorSizeGuid);
    if (!Entry)
    {
        ROSV_ERR("VhdxParseMetadata: Logical Sector Size metadata item not found");
        return STATUS_NOT_FOUND;
    }
    if (Entry->Length < sizeof(ULONG))
    {
        ROSV_ERR("VhdxParseMetadata: Logical Sector Size item too small (%u bytes)", Entry->Length);
        return STATUS_INVALID_PARAMETER;
    }
    if (Entry->Offset > MetadataLength || Entry->Length > MetadataLength - Entry->Offset)
    {
        ROSV_ERR("VhdxParseMetadata: Logical Sector Size at offset %u+%u exceeds region %u",
                 Entry->Offset, Entry->Length, MetadataLength);
        return STATUS_INVALID_PARAMETER;
    }
    State->LogicalSectorSize = *(PULONG)((PUCHAR)MetadataBase + Entry->Offset);
    ROSV_TRACE("VhdxParseMetadata: LogicalSectorSize=%u", State->LogicalSectorSize);
    if (State->LogicalSectorSize != 512 && State->LogicalSectorSize != 4096)
    {
        ROSV_ERR("VhdxParseMetadata: Unsupported LogicalSectorSize %u (must be 512 or 4096)",
                 State->LogicalSectorSize);
        return STATUS_INVALID_PARAMETER;
    }

    /* --- Physical Sector Size (required) --- */
    Entry = VhdxFindMetadataEntry(MetaTable, &PhysicalSectorSizeGuid);
    if (!Entry)
    {
        ROSV_ERR("VhdxParseMetadata: Physical Sector Size metadata item not found");
        return STATUS_NOT_FOUND;
    }
    if (Entry->Length < sizeof(ULONG))
    {
        ROSV_ERR("VhdxParseMetadata: Physical Sector Size item too small (%u bytes)", Entry->Length);
        return STATUS_INVALID_PARAMETER;
    }
    if (Entry->Offset > MetadataLength || Entry->Length > MetadataLength - Entry->Offset)
    {
        ROSV_ERR("VhdxParseMetadata: Physical Sector Size at offset %u+%u exceeds region %u",
                 Entry->Offset, Entry->Length, MetadataLength);
        return STATUS_INVALID_PARAMETER;
    }
    State->PhysicalSectorSize = *(PULONG)((PUCHAR)MetadataBase + Entry->Offset);
    ROSV_TRACE("VhdxParseMetadata: PhysicalSectorSize=%u", State->PhysicalSectorSize);
    if (State->PhysicalSectorSize != 512 && State->PhysicalSectorSize != 4096)
    {
        ROSV_ERR("VhdxParseMetadata: Unsupported PhysicalSectorSize %u (must be 512 or 4096)",
                 State->PhysicalSectorSize);
        return STATUS_INVALID_PARAMETER;
    }

    /* --- Virtual Disk ID (required) --- */
    Entry = VhdxFindMetadataEntry(MetaTable, &VirtualDiskIdGuid);
    if (!Entry)
    {
        ROSV_ERR("VhdxParseMetadata: Virtual Disk ID metadata item not found");
        return STATUS_NOT_FOUND;
    }
    if (Entry->Length < sizeof(GUID))
    {
        ROSV_ERR("VhdxParseMetadata: Virtual Disk ID item too small (%u bytes)", Entry->Length);
        return STATUS_INVALID_PARAMETER;
    }
    if (Entry->Offset > MetadataLength || Entry->Length > MetadataLength - Entry->Offset)
    {
        ROSV_ERR("VhdxParseMetadata: Virtual Disk ID at offset %u+%u exceeds region %u",
                 Entry->Offset, Entry->Length, MetadataLength);
        return STATUS_INVALID_PARAMETER;
    }
    State->VirtualDiskId = *(GUID *)((PUCHAR)MetadataBase + Entry->Offset);
    ROSV_TRACE("VhdxParseMetadata: VirtualDiskId={%08X-%04X-%04X-...}",
               State->VirtualDiskId.Data1, State->VirtualDiskId.Data2,
               State->VirtualDiskId.Data3);

    return STATUS_SUCCESS;
}

/* ========================================================================== */
/* RosvVhdxIsVhdx - Quick format detection                                     */
/* ========================================================================== */

BOOLEAN
RosvVhdxIsVhdx(
    _In_ PVOID FileBase,
    _In_ ULONG64 FileSize)
{
    PVHDX_FILE_IDENTIFIER FileId;

    /* Minimum size: at least 1MB (file id + two headers + two region tables) */
    if (FileSize < VHDX_HEADER_SECTION_SIZE)
    {
        ROSV_DEBUG("VHDX: signature check failed (file too small: %llu bytes)", FileSize);
        return FALSE;
    }

    FileId = (PVHDX_FILE_IDENTIFIER)FileBase;
    ROSV_DEBUG("VHDX: signature check %s (0x%llX)",
               FileId->Signature == VHDX_FILE_SIGNATURE ? "passed" : "failed",
               (ULONG64)FileId->Signature);

    return (FileId->Signature == VHDX_FILE_SIGNATURE);
}

/* ========================================================================== */
/* RosvVhdxOpen - Full open and validation                                      */
/* ========================================================================== */

NTSTATUS
RosvVhdxOpen(
    _Out_ PROSV_VHDX_STATE State,
    _In_ PVOID FileBase,
    _In_ ULONG64 FileSize)
{
    NTSTATUS Status;
    PVHDX_HEADER Header1 = NULL, Header2 = NULL, ActiveHeader;
    ULONG64 Seq1 = 0, Seq2 = 0;
    NTSTATUS Status1, Status2;
    PVHDX_REGION_TABLE_HEADER RegionTable1 = NULL, RegionTable2 = NULL;
    PVHDX_REGION_TABLE_HEADER ActiveRegionTable;
    PVHDX_REGION_TABLE_ENTRY BatEntry, MetaEntry;
    ULONG64 DataBlocksCount, ChunkRatio, TotalBatEntries;

    static const GUID BatGuid = VHDX_BAT_REGION_GUID;
    static const GUID MetaGuid = VHDX_METADATA_REGION_GUID;

    RtlZeroMemory(State, sizeof(*State));

    /* ---- Step 1: Validate file identifier ---- */
    if (!RosvVhdxIsVhdx(FileBase, FileSize))
    {
        ROSV_ERR("VhdxOpen: Not a valid VHDX file");
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    State->FileBase = FileBase;
    State->FileSize = FileSize;

    /* ---- Step 2: Parse and validate both headers ---- */
    Status1 = VhdxValidateHeader(FileBase, FileSize, VHDX_HEADER1_OFFSET, 0, &Header1, &Seq1);
    Status2 = VhdxValidateHeader(FileBase, FileSize, VHDX_HEADER2_OFFSET, 1, &Header2, &Seq2);

    if (!NT_SUCCESS(Status1) && !NT_SUCCESS(Status2))
    {
        ROSV_ERR("VhdxOpen: Both headers invalid (Header1=0x%08X, Header2=0x%08X)",
                 Status1, Status2);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Pick header with higher sequence number; if one is invalid, use the other */
    if (NT_SUCCESS(Status1) && NT_SUCCESS(Status2))
    {
        if (Seq1 >= Seq2)
        {
            State->ActiveHeaderIndex = 0;
            ActiveHeader = Header1;
        }
        else
        {
            State->ActiveHeaderIndex = 1;
            ActiveHeader = Header2;
        }
    }
    else if (NT_SUCCESS(Status1))
    {
        State->ActiveHeaderIndex = 0;
        ActiveHeader = Header1;
    }
    else
    {
        State->ActiveHeaderIndex = 1;
        ActiveHeader = Header2;
    }

    State->SequenceNumber = ActiveHeader->SequenceNumber;
    State->FileWriteGuid = ActiveHeader->FileWriteGuid;
    State->DataWriteGuid = ActiveHeader->DataWriteGuid;
    State->LogGuid = ActiveHeader->LogGuid;
    State->LogOffset = ActiveHeader->LogOffset;
    State->LogLength = ActiveHeader->LogLength;

    ROSV_TRACE("VHDX: using header %u (seq=%llu)",
               State->ActiveHeaderIndex + 1, State->SequenceNumber);

    /* Check if log replay is needed */
    State->NeedsLogReplay = !RosvIsNullGuid(&State->LogGuid);
    if (State->NeedsLogReplay)
    {
        ROSV_WARN("VHDX: log is dirty, replay may be needed");
    }

    /* ---- Step 3: Parse and validate region tables ---- */
    Status1 = VhdxValidateRegionTable(FileBase, FileSize, VHDX_REGION_TABLE1_OFFSET, 0, &RegionTable1);
    Status2 = VhdxValidateRegionTable(FileBase, FileSize, VHDX_REGION_TABLE2_OFFSET, 1, &RegionTable2);

    if (!NT_SUCCESS(Status1) && !NT_SUCCESS(Status2))
    {
        ROSV_ERR("VhdxOpen: Both region tables invalid (Table1=0x%08X, Table2=0x%08X)",
                 Status1, Status2);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Use first valid region table (both should be identical per spec) */
    ActiveRegionTable = NT_SUCCESS(Status1) ? RegionTable1 : RegionTable2;

    /* ---- Step 4: Find BAT and Metadata regions ---- */
    BatEntry = VhdxFindRegionEntry(ActiveRegionTable, &BatGuid);
    if (!BatEntry)
    {
        ROSV_ERR("VhdxOpen: BAT region not found in region table");
        return STATUS_NOT_FOUND;
    }

    /* Validate BAT region is within file bounds */
    if (BatEntry->FileOffset + BatEntry->Length > FileSize)
    {
        ROSV_ERR("VhdxOpen: BAT region at 0x%llX+%u extends past file end 0x%llX",
                 BatEntry->FileOffset, BatEntry->Length, FileSize);
        return STATUS_INVALID_PARAMETER;
    }

    State->BatOffset = BatEntry->FileOffset;
    State->BatLength = BatEntry->Length;
    State->BatBase = (PUCHAR)FileBase + BatEntry->FileOffset;
    MetaEntry = VhdxFindRegionEntry(ActiveRegionTable, &MetaGuid);
    if (!MetaEntry)
    {
        ROSV_ERR("VhdxOpen: Metadata region not found in region table");
        return STATUS_NOT_FOUND;
    }

    /* Validate metadata region is within file bounds */
    if (MetaEntry->FileOffset + MetaEntry->Length > FileSize)
    {
        ROSV_ERR("VhdxOpen: Metadata region at 0x%llX+%u extends past file end 0x%llX",
                 MetaEntry->FileOffset, MetaEntry->Length, FileSize);
        return STATUS_INVALID_PARAMETER;
    }

    State->MetadataOffset = MetaEntry->FileOffset;
    State->MetadataLength = MetaEntry->Length;
    State->MetadataBase = (PUCHAR)FileBase + MetaEntry->FileOffset;

    ROSV_TRACE("VhdxOpen: BAT at 0x%llX (%u bytes), Metadata at 0x%llX (%u bytes)",
               State->BatOffset, State->BatLength,
               State->MetadataOffset, State->MetadataLength);

    /* ---- Step 5: Parse metadata items ---- */
    Status = VhdxParseMetadata(State->MetadataBase, State->MetadataLength, State);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("VhdxOpen: Failed to parse metadata (0x%08X)", Status);
        return Status;
    }

    /* ---- Step 6: Compute BAT geometry ---- */

    /* DataBlocksCount = ceil(VirtualDiskSize / BlockSize) */
    DataBlocksCount = (State->VirtualDiskSize + State->BlockSize - 1) / State->BlockSize;
    State->DataBlocksCount = (ULONG)DataBlocksCount;

    /* ChunkRatio = (2^23 * LogicalSectorSize) / BlockSize */
    ChunkRatio = ((ULONG64)1 << 23) * State->LogicalSectorSize / State->BlockSize;
    State->ChunkRatio = (ULONG)ChunkRatio;

    /*
     * For non-differencing disks:
     *   TotalBatEntries = DataBlocksCount + floor((DataBlocksCount - 1) / ChunkRatio)
     * The extra entries are interleaved sector bitmap blocks.
     * If DataBlocksCount == 0, there are no entries.
     */
    if (DataBlocksCount == 0)
    {
        TotalBatEntries = 0;
    }
    else if (!State->HasParent)
    {
        /* Dynamic or fixed: SB entries interleaved but not used (state must be 0) */
        TotalBatEntries = DataBlocksCount +
                          (ChunkRatio > 0 ? (DataBlocksCount - 1) / ChunkRatio : 0);
    }
    else
    {
        /* Differencing: has actual sector bitmap entries */
        TotalBatEntries = DataBlocksCount +
                          (ChunkRatio > 0 ? (DataBlocksCount - 1) / ChunkRatio : 0);
    }
    State->BatEntryCount = (ULONG)TotalBatEntries;

    ROSV_TRACE("VHDX: %u data blocks, chunk_ratio=%u, %u BAT entries",
               State->DataBlocksCount, State->ChunkRatio, State->BatEntryCount);

    /* Verify BAT region is large enough for all entries */
    if ((ULONG64)State->BatEntryCount * sizeof(ULONG64) > State->BatLength)
    {
        ROSV_ERR("VhdxOpen: BAT region too small: need %llu bytes for %u entries, have %u",
                 (ULONG64)State->BatEntryCount * sizeof(ULONG64),
                 State->BatEntryCount, State->BatLength);
        return STATUS_INVALID_PARAMETER;
    }

    /* Initialize statistics */
    State->BatLookups = 0;
    State->BatMisses = 0;
    State->BlockAllocations = 0;
    State->Parent = NULL;
    State->ChainDepth = 0;

    /* ---- Step 7: Scan BAT for highest allocated offset ---- */
    {
        PULONG64 BatArray = (PULONG64)State->BatBase;
        ULONG64 HighestMB = 0;
        ULONG Idx;

        for (Idx = 0; Idx < State->BatEntryCount; Idx++)
        {
            ULONG BatState = VHDX_BAT_STATE(BatArray[Idx]);

            if (BatState == VHDX_PAYLOAD_BLOCK_FULLY_PRESENT ||
                BatState == VHDX_PAYLOAD_BLOCK_PARTIALLY_PRESENT ||
                BatState == VHDX_SB_BLOCK_PRESENT)
            {
                ULONG64 OffsetMB = VHDX_BAT_FILE_OFFSET_MB(BatArray[Idx]);
                if (OffsetMB + 1 > HighestMB)
                    HighestMB = OffsetMB + 1;
            }
        }

        State->HighestAllocatedMB = HighestMB;
        ROSV_TRACE("VhdxOpen: Highest allocated offset=%llu MB (%u BAT entries scanned)",
                   HighestMB, State->BatEntryCount);
    }

    ROSV_TRACE("VhdxOpen: opened successfully: virtual_size=%llu MB, block_size=%u MB, %s, logical_sector=%u",
               State->VirtualDiskSize / (1024 * 1024),
               State->BlockSize / (1024 * 1024),
               State->IsFixed ? "fixed" : (State->HasParent ? "differencing" : "dynamic"),
               State->LogicalSectorSize);

    /* Allocate readahead cache for demand-paged reads (256KB).
     * Demand handles use FILE_NO_INTERMEDIATE_BUFFERING, so keep the cache
     * buffer aligned to the stricter of the logical and physical sector size. */
    {
        ULONG ReadaheadAlignment = State->LogicalSectorSize;
        SIZE_T RawSize;
        ULONG_PTR RawAddress;
        ULONG_PTR AlignedAddress;

        if (State->PhysicalSectorSize > ReadaheadAlignment)
            ReadaheadAlignment = State->PhysicalSectorSize;

        RawSize = (SIZE_T)ROSV_VHDX_READAHEAD_BYTES + (SIZE_T)ReadaheadAlignment - 1;
        State->ReadaheadBufferAlloc = ExAllocatePoolWithTag(
            NonPagedPool, RawSize, ROSV_DRIVER_TAG);
        State->ReadaheadBuffer = NULL;
        if (State->ReadaheadBufferAlloc != NULL)
        {
            RawAddress = (ULONG_PTR)State->ReadaheadBufferAlloc;
            AlignedAddress = (RawAddress + ((ULONG_PTR)ReadaheadAlignment - 1)) &
                             ~((ULONG_PTR)ReadaheadAlignment - 1);
            State->ReadaheadBuffer = (PVOID)AlignedAddress;
        }
    }
    State->ReadaheadSectorCount = 0;
    State->ReadaheadStartSector = 0;
    State->ReadaheadHits = 0;
    State->ReadaheadMisses = 0;
    if (State->ReadaheadBuffer == NULL)
    {
        ROSV_WARN("VhdxOpen: readahead cache allocation failed (256KB), demand reads will be uncached");
    }

    return STATUS_SUCCESS;
}

/* ========================================================================== */
/* RosvVhdxClose - Release VHDX state                                          */
/* ========================================================================== */

VOID
RosvVhdxClose(
    _Inout_ PROSV_VHDX_STATE State)
{
    ROSV_TRACE("VHDX: closing (lookups=%llu, misses=%llu, allocs=%llu)",
               State->BatLookups, State->BatMisses, State->BlockAllocations);

    /* Free readahead cache if allocated */
    if (State->ReadaheadBufferAlloc != NULL || State->ReadaheadBuffer != NULL)
    {
        ExFreePoolWithTag(State->ReadaheadBufferAlloc != NULL ?
                          State->ReadaheadBufferAlloc :
                          State->ReadaheadBuffer,
                          ROSV_DRIVER_TAG);
        State->ReadaheadBufferAlloc = NULL;
        State->ReadaheadBuffer = NULL;
    }

    if (State->ReadaheadHits > 0 || State->ReadaheadMisses > 0)
    {
        ROSV_TRACE("VHDX: readahead stats: hits=%llu misses=%llu",
                   State->ReadaheadHits, State->ReadaheadMisses);
    }

    /*
     * We don't own the file mapping (FileBase), so we don't free it.
     * Just zero the state to prevent use-after-close.
     */
    RtlZeroMemory(State, sizeof(*State));
}
