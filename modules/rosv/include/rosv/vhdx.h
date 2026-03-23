/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VHDX (Virtual Hard Disk v2) format structures and constants
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements structures from [MS-VHDX] revision 8.0 (April 2024).
 * All on-disk structures use packed layout and little-endian byte order.
 */

#pragma once

/*
 * When VHDX_USER_MODE is defined (e.g. by vhdxtool.c), skip the kernel-mode
 * rosv.h include — the caller supplies windows.h and minimal NT type stubs.
 */
#ifndef VHDX_USER_MODE
#include <rosv/rosv.h>
#endif

/* Forward declarations */
typedef struct _ROSV_VM ROSV_VM, *PROSV_VM;

/* ========================================================================== */
/* Signatures                                                                  */
/* ========================================================================== */

/* "vhdxfile" in little-endian (bytes: 76 68 64 78 66 69 6C 65) */
#define VHDX_FILE_SIGNATURE         0x656C696678646876ULL

/* "head" in little-endian */
#define VHDX_HEADER_SIGNATURE       0x64616568UL

/* "regi" in little-endian */
#define VHDX_REGION_SIGNATURE       0x69676572UL

/* "metadata" in little-endian (8 bytes) */
#define VHDX_METADATA_SIGNATURE     0x617461646174656DULL

/* Log signatures */
#define VHDX_LOG_ENTRY_SIGNATURE    0x65676F6CUL   /* "loge" */
#define VHDX_LOG_ZERO_SIGNATURE     0x6F72657AUL   /* "zero" */
#define VHDX_LOG_DESC_SIGNATURE     0x63736564UL   /* "desc" */
#define VHDX_LOG_DATA_SIGNATURE     0x61746164UL   /* "data" */

/* ========================================================================== */
/* Format constants                                                            */
/* ========================================================================== */

#define VHDX_HEADER_SECTION_SIZE    (1 * 1024 * 1024)   /* 1 MB */
#define VHDX_FILE_ID_OFFSET         0x00000000ULL
#define VHDX_FILE_ID_SIZE           0x00010000UL         /* 64 KB */
#define VHDX_HEADER1_OFFSET         0x00010000ULL        /* 64 KB */
#define VHDX_HEADER2_OFFSET         0x00020000ULL        /* 128 KB */
#define VHDX_HEADER_SIZE            0x00010000UL         /* 64 KB each */
#define VHDX_HEADER_STRUCT_SIZE     4096                 /* 4 KB used */
#define VHDX_REGION_TABLE1_OFFSET   0x00030000ULL        /* 192 KB */
#define VHDX_REGION_TABLE2_OFFSET   0x00040000ULL        /* 256 KB */
#define VHDX_REGION_TABLE_SIZE      0x00010000UL         /* 64 KB each */

#define VHDX_FORMAT_VERSION         1       /* Must be 1 */
#define VHDX_LOG_VERSION            0       /* Must be 0 */

#define VHDX_MAX_REGION_ENTRIES     2047
#define VHDX_MAX_METADATA_ENTRIES   2047

/* Block size range (File Parameters metadata) */
#define VHDX_BLOCK_SIZE_MIN         (1  * 1024 * 1024)   /* 1 MB */
#define VHDX_BLOCK_SIZE_MAX         (256 * 1024 * 1024)   /* 256 MB */

/* Log minimum size */
#define VHDX_LOG_MIN_SIZE           (1 * 1024 * 1024)    /* 1 MB */

/* Maximum diff chain depth (Hyper-V limit) */
#define VHDX_MAX_PARENT_CHAIN       250

/* MB alignment for all regions */
#define VHDX_MB_ALIGNMENT           (1 * 1024 * 1024)

/* ========================================================================== */
/* GUIDs (stored as GUID structures)                                           */
/* ========================================================================== */

/*
 * Region type GUIDs
 * BAT:      {2dc27766-f623-4200-9d64-115e9bfd4a08}
 * Metadata: {8b7ca206-4790-4b9a-b8fe-575f050f886e}
 */
#define VHDX_BAT_REGION_GUID \
    { 0x2dc27766, 0xf623, 0x4200, { 0x9d, 0x64, 0x11, 0x5e, 0x9b, 0xfd, 0x4a, 0x08 } }

#define VHDX_METADATA_REGION_GUID \
    { 0x8b7ca206, 0x4790, 0x4b9a, { 0xb8, 0xfe, 0x57, 0x5f, 0x05, 0x0f, 0x88, 0x6e } }

/*
 * Metadata item GUIDs
 */

/* File Parameters: {caa16737-fa36-4d43-b3b6-33f0aa44e76b} */
#define VHDX_META_FILE_PARAMETERS_GUID \
    { 0xcaa16737, 0xfa36, 0x4d43, { 0xb3, 0xb6, 0x33, 0xf0, 0xaa, 0x44, 0xe7, 0x6b } }

/* Virtual Disk Size: {2fa54224-cd1b-4876-b211-5dbed83bf4b8} */
#define VHDX_META_VIRTUAL_DISK_SIZE_GUID \
    { 0x2fa54224, 0xcd1b, 0x4876, { 0xb2, 0x11, 0x5d, 0xbe, 0xd8, 0x3b, 0xf4, 0xb8 } }

/* Logical Sector Size: {8141bf1d-a96f-4709-ba47-f233a8faab5f} */
#define VHDX_META_LOGICAL_SECTOR_SIZE_GUID \
    { 0x8141bf1d, 0xa96f, 0x4709, { 0xba, 0x47, 0xf2, 0x33, 0xa8, 0xfa, 0xab, 0x5f } }

/* Physical Sector Size: {cda348c7-445d-4471-9cc9-e9885251c556} */
#define VHDX_META_PHYSICAL_SECTOR_SIZE_GUID \
    { 0xcda348c7, 0x445d, 0x4471, { 0x9c, 0xc9, 0xe9, 0x88, 0x52, 0x51, 0xc5, 0x56 } }

/* Virtual Disk ID: {beca12ab-b2e6-4523-93ef-c309e000c746} */
#define VHDX_META_VIRTUAL_DISK_ID_GUID \
    { 0xbeca12ab, 0xb2e6, 0x4523, { 0x93, 0xef, 0xc3, 0x09, 0xe0, 0x00, 0xc7, 0x46 } }

/* Parent Locator: {a8d35f2d-b30b-454d-abf7-d3d84834ab0c} */
#define VHDX_META_PARENT_LOCATOR_GUID \
    { 0xa8d35f2d, 0xb30b, 0x454d, { 0xab, 0xf7, 0xd3, 0xd8, 0x48, 0x34, 0xab, 0x0c } }

/* Parent Locator Type: {b04aefb7-d19e-4a81-b789-25b8e9445913} */
#define VHDX_PARENT_LOCATOR_TYPE_GUID \
    { 0xb04aefb7, 0xd19e, 0x4a81, { 0xb7, 0x89, 0x25, 0xb8, 0xe9, 0x44, 0x59, 0x13 } }

/* ========================================================================== */
/* BAT entry macros                                                            */
/* ========================================================================== */

/* Extract 3-bit state from BAT entry (bits 0-2) */
#define VHDX_BAT_STATE(Entry)           ((ULONG)((Entry) & 7ULL))

/* Extract FileOffsetMB from BAT entry (bits 20-63, 44 bits) */
#define VHDX_BAT_FILE_OFFSET_MB(Entry)  ((ULONG64)((Entry) >> 20))

/* Convert BAT entry to byte offset in file */
#define VHDX_BAT_OFFSET_BYTES(Entry)    (((ULONG64)((Entry) >> 20)) << 20)

/* Construct a BAT entry from state and MB offset */
#define VHDX_BAT_MAKE_ENTRY(State, OffsetMB) \
    (((ULONG64)(State) & 7ULL) | ((ULONG64)(OffsetMB) << 20))

/* ========================================================================== */
/* Payload block states (BAT entry bits 0-2)                                   */
/* ========================================================================== */

#define VHDX_PAYLOAD_BLOCK_NOT_PRESENT          0
#define VHDX_PAYLOAD_BLOCK_UNDEFINED            1
#define VHDX_PAYLOAD_BLOCK_ZERO                 2
#define VHDX_PAYLOAD_BLOCK_UNMAPPED             3
/* Values 4 and 5 are reserved */
#define VHDX_PAYLOAD_BLOCK_FULLY_PRESENT        6
#define VHDX_PAYLOAD_BLOCK_PARTIALLY_PRESENT    7

/* Sector bitmap block states */
#define VHDX_SB_BLOCK_NOT_PRESENT               0
#define VHDX_SB_BLOCK_PRESENT                   6

/* ========================================================================== */
/* File Parameters flags                                                       */
/* ========================================================================== */

#define VHDX_FILE_PARAMS_LEAVE_BLOCKS_ALLOCATED 0x00000001  /* Fixed disk */
#define VHDX_FILE_PARAMS_HAS_PARENT             0x00000002  /* Differencing disk */

/* ========================================================================== */
/* Metadata entry flags                                                        */
/* ========================================================================== */

#define VHDX_METADATA_FLAG_IS_USER              0x00000001
#define VHDX_METADATA_FLAG_IS_VIRTUAL_DISK      0x00000002
#define VHDX_METADATA_FLAG_IS_REQUIRED          0x00000004

/* ========================================================================== */
/* On-disk structures (packed, little-endian)                                   */
/* ========================================================================== */

#include <pshpack1.h>

/* --- File Type Identifier (offset 0, 64KB) --- */

typedef struct _VHDX_FILE_IDENTIFIER {
    ULONG64 Signature;                  /* Must be VHDX_FILE_SIGNATURE */
    WCHAR   Creator[256];               /* UTF-16LE creator string (512 bytes) */
    /* Remaining bytes to 64KB are reserved zeros */
} VHDX_FILE_IDENTIFIER, *PVHDX_FILE_IDENTIFIER;

C_ASSERT(sizeof(VHDX_FILE_IDENTIFIER) == 520);

/* --- Header (4KB structure at 64KB or 128KB) --- */

typedef struct _VHDX_HEADER {
    ULONG   Signature;                  /* Must be VHDX_HEADER_SIGNATURE */
    ULONG   Checksum;                   /* CRC-32C (zeroed for computation) */
    ULONG64 SequenceNumber;             /* Higher = more recent */
    GUID    FileWriteGuid;              /* Changes on each open-for-write */
    GUID    DataWriteGuid;              /* Changes when virtual disk data changes */
    GUID    LogGuid;                    /* GUID_NULL = log clean */
    USHORT  LogVersion;                 /* Must be 0 */
    USHORT  Version;                    /* Must be 1 */
    ULONG   LogLength;                  /* In bytes, multiple of 1MB */
    ULONG64 LogOffset;                  /* In bytes, multiple of 1MB, >= 1MB */
    UCHAR   Reserved[4016];            /* Must be zero */
} VHDX_HEADER, *PVHDX_HEADER;

C_ASSERT(sizeof(VHDX_HEADER) == 4096);

/* --- Region Table Header (16 bytes) --- */

typedef struct _VHDX_REGION_TABLE_HEADER {
    ULONG   Signature;                  /* Must be VHDX_REGION_SIGNATURE */
    ULONG   Checksum;                   /* CRC-32C */
    ULONG   EntryCount;                 /* Max 2047 */
    ULONG   Reserved;
} VHDX_REGION_TABLE_HEADER, *PVHDX_REGION_TABLE_HEADER;

C_ASSERT(sizeof(VHDX_REGION_TABLE_HEADER) == 16);

/* --- Region Table Entry (32 bytes) --- */

typedef struct _VHDX_REGION_TABLE_ENTRY {
    GUID    Guid;                       /* Region type identifier */
    ULONG64 FileOffset;                 /* >= 1MB, 1MB-aligned */
    ULONG   Length;                     /* Multiple of 1MB */
    ULONG   Required;                   /* 1 = must understand to open */
} VHDX_REGION_TABLE_ENTRY, *PVHDX_REGION_TABLE_ENTRY;

C_ASSERT(sizeof(VHDX_REGION_TABLE_ENTRY) == 32);

/* --- Metadata Table Header (32 bytes) --- */

typedef struct _VHDX_METADATA_TABLE_HEADER {
    ULONG64 Signature;                  /* Must be VHDX_METADATA_SIGNATURE */
    USHORT  Reserved;
    USHORT  EntryCount;                 /* Max 2047 */
    UCHAR   Reserved2[20];
} VHDX_METADATA_TABLE_HEADER, *PVHDX_METADATA_TABLE_HEADER;

C_ASSERT(sizeof(VHDX_METADATA_TABLE_HEADER) == 32);

/* --- Metadata Table Entry (32 bytes) --- */

typedef struct _VHDX_METADATA_TABLE_ENTRY {
    GUID    ItemId;                     /* Metadata item GUID */
    ULONG   Offset;                     /* From metadata region start, > 64KB */
    ULONG   Length;                     /* Item size in bytes */
    ULONG   Flags;                      /* VHDX_METADATA_FLAG_* */
    ULONG   Reserved;
} VHDX_METADATA_TABLE_ENTRY, *PVHDX_METADATA_TABLE_ENTRY;

C_ASSERT(sizeof(VHDX_METADATA_TABLE_ENTRY) == 32);

/* --- File Parameters metadata item (8 bytes) --- */

typedef struct _VHDX_FILE_PARAMETERS {
    ULONG   BlockSize;                  /* Power of 2, 1MB-256MB */
    ULONG   Flags;                      /* VHDX_FILE_PARAMS_* */
} VHDX_FILE_PARAMETERS, *PVHDX_FILE_PARAMETERS;

C_ASSERT(sizeof(VHDX_FILE_PARAMETERS) == 8);

/* --- Parent Locator Header (20 bytes) --- */

typedef struct _VHDX_PARENT_LOCATOR_HEADER {
    GUID    LocatorType;                /* Must be VHDX_PARENT_LOCATOR_TYPE_GUID */
    USHORT  Reserved;
    USHORT  KeyValueCount;
} VHDX_PARENT_LOCATOR_HEADER, *PVHDX_PARENT_LOCATOR_HEADER;

C_ASSERT(sizeof(VHDX_PARENT_LOCATOR_HEADER) == 20);

/* --- Parent Locator Entry (12 bytes) --- */

typedef struct _VHDX_PARENT_LOCATOR_ENTRY {
    ULONG   KeyOffset;                  /* From parent locator header start */
    ULONG   ValueOffset;                /* From parent locator header start */
    USHORT  KeyLength;                  /* In bytes (UTF-16LE) */
    USHORT  ValueLength;                /* In bytes (UTF-16LE) */
} VHDX_PARENT_LOCATOR_ENTRY, *PVHDX_PARENT_LOCATOR_ENTRY;

C_ASSERT(sizeof(VHDX_PARENT_LOCATOR_ENTRY) == 12);

/* --- Log Entry Header (64 bytes) --- */

typedef struct _VHDX_LOG_ENTRY_HEADER {
    ULONG   Signature;                  /* Must be VHDX_LOG_ENTRY_SIGNATURE */
    ULONG   Checksum;                   /* CRC-32C */
    ULONG   EntryLength;               /* Total including descriptors + data sectors */
    ULONG   Tail;                       /* Byte offset of oldest active entry in log */
    ULONG64 SequenceNumber;
    ULONG   DescriptorCount;
    ULONG   Reserved;
    GUID    LogGuid;                    /* Must match header.LogGuid */
    ULONG64 FlushedFileOffset;
    ULONG64 LastFileOffset;
} VHDX_LOG_ENTRY_HEADER, *PVHDX_LOG_ENTRY_HEADER;

C_ASSERT(sizeof(VHDX_LOG_ENTRY_HEADER) == 64);

/* --- Log Descriptor (32 bytes) --- */

typedef struct _VHDX_LOG_DESCRIPTOR {
    ULONG   Signature;                  /* "desc" or "zero" */
    ULONG   TrailingBytes;
    ULONG64 LeadingBytes;
    ULONG64 FileOffset;                /* Target location in VHDX file */
    ULONG64 SequenceNumber;
} VHDX_LOG_DESCRIPTOR, *PVHDX_LOG_DESCRIPTOR;

C_ASSERT(sizeof(VHDX_LOG_DESCRIPTOR) == 32);

/* --- Log Data Sector (4096 bytes) --- */

typedef struct _VHDX_LOG_DATA_SECTOR {
    ULONG   Signature;                  /* Must be VHDX_LOG_DATA_SIGNATURE */
    ULONG   SequenceHigh;              /* Upper 32 bits of sequence number */
    UCHAR   Data[4084];               /* Payload data */
    ULONG   SequenceLow;              /* Lower 32 bits of sequence number */
} VHDX_LOG_DATA_SECTOR, *PVHDX_LOG_DATA_SECTOR;

C_ASSERT(sizeof(VHDX_LOG_DATA_SECTOR) == 4096);

#include <poppack.h>

/* ========================================================================== */
/* Disk backend type                                                           */
/* ========================================================================== */

typedef enum _ROSV_DISK_BACKEND_TYPE {
    ROSV_DISK_BACKEND_RAW  = 0,         /* Flat raw disk image */
    ROSV_DISK_BACKEND_VHDX = 1          /* VHDX format */
} ROSV_DISK_BACKEND_TYPE;

/* ========================================================================== */
/* Runtime VHDX state (parsed from file)                                       */
/* ========================================================================== */

typedef struct _ROSV_VHDX_STATE {
    /* File mapping */
    PVOID       FileBase;               /* HVA of memory-mapped VHDX file */
    ULONG64     FileSize;               /* Total file size in bytes */

    /* Active header (the one with higher SequenceNumber) */
    ULONG       ActiveHeaderIndex;      /* 0 = Header1, 1 = Header2 */
    ULONG64     SequenceNumber;
    GUID        FileWriteGuid;
    GUID        DataWriteGuid;
    GUID        LogGuid;

    /* Log */
    ULONG64     LogOffset;              /* Byte offset in file */
    ULONG       LogLength;              /* Size in bytes */
    BOOLEAN     NeedsLogReplay;         /* TRUE if LogGuid != GUID_NULL */

    /* BAT region */
    PVOID       BatBase;                /* Pointer to BAT array start */
    ULONG64     BatOffset;              /* BAT region file offset */
    ULONG       BatLength;              /* BAT region length */
    ULONG       BatEntryCount;          /* Total entries (payload + SB interleaved) */

    /* Metadata region */
    PVOID       MetadataBase;           /* Pointer to metadata region start */
    ULONG64     MetadataOffset;         /* Metadata region file offset */
    ULONG       MetadataLength;

    /* Parsed metadata values */
    ULONG64     VirtualDiskSize;        /* Virtual disk size in bytes */
    ULONG       BlockSize;              /* Payload block size (1MB-256MB) */
    ULONG       LogicalSectorSize;      /* 512 or 4096 */
    ULONG       PhysicalSectorSize;     /* 512 or 4096 */
    GUID        VirtualDiskId;
    ULONG       DataBlocksCount;        /* ceil(VirtualDiskSize / BlockSize) */
    ULONG       ChunkRatio;             /* (2^23 * LogicalSectorSize) / BlockSize */

    /* Disk type flags */
    BOOLEAN     IsFixed;                /* LeaveBlocksAllocated */
    BOOLEAN     HasParent;              /* Differencing disk */

    /* Parent chain (differencing disks) */
    struct _ROSV_VHDX_STATE *Parent;    /* Parent VHDX state (NULL if no parent) */
    ULONG       ChainDepth;             /* Depth in parent chain */

    /* Dynamic allocation tracking */
    ULONG64     HighestAllocatedMB;     /* Highest FileOffsetMB in BAT + 1 */

    /* Statistics */
    ULONG64     BatLookups;
    ULONG64     BatMisses;              /* NOT_PRESENT / ZERO returns */
    ULONG64     BlockAllocations;
    ULONG64     DemandPagedReads;       /* Total demand-paged read batches */
    ULONG64     DemandPagedErrors;      /* Failed demand-paged reads */
} ROSV_VHDX_STATE, *PROSV_VHDX_STATE;

/* ========================================================================== */
/* Function prototypes - vhdx_crc32c.c                                         */
/* ========================================================================== */

ULONG
RosvVhdxCrc32c(
    _In_reads_bytes_(Length) PVOID Data,
    _In_ ULONG Length);

BOOLEAN
RosvVhdxVerifyChecksum(
    _In_reads_bytes_(StructureSize) PVOID Structure,
    _In_ ULONG StructureSize,
    _In_ ULONG ChecksumOffset);

/* ========================================================================== */
/* Function prototypes - vhdx_parser.c                                         */
/* ========================================================================== */

NTSTATUS
RosvVhdxOpen(
    _Out_ PROSV_VHDX_STATE State,
    _In_ PVOID FileBase,
    _In_ ULONG64 FileSize);

VOID
RosvVhdxClose(
    _Inout_ PROSV_VHDX_STATE State);

BOOLEAN
RosvVhdxIsVhdx(
    _In_ PVOID FileBase,
    _In_ ULONG64 FileSize);

/* ========================================================================== */
/* Function prototypes - vhdx_bat.c                                            */
/* ========================================================================== */

NTSTATUS
RosvVhdxReadSectors(
    _In_ PROSV_VHDX_STATE State,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _Out_writes_bytes_(SectorCount * State->LogicalSectorSize) PVOID Buffer);

NTSTATUS
RosvVhdxWriteSectors(
    _Inout_ PROSV_VHDX_STATE State,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _In_reads_bytes_(SectorCount * State->LogicalSectorSize) PVOID Buffer);

NTSTATUS
RosvVhdxTranslateSector(
    _In_ PROSV_VHDX_STATE State,
    _In_ ULONG64 VirtualSector,
    _Out_ PULONG64 FileOffset,
    _Out_ PULONG BlockState);

/* ========================================================================== */
/* Function prototypes - vhdx_bat.c (demand-paged I/O)                         */
/* ========================================================================== */

NTSTATUS
RosvVhdxReadSectorsDemandPaged(
    _In_ PROSV_VHDX_STATE State,
    _In_ HANDLE FileHandle,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _Out_writes_bytes_(SectorCount * State->LogicalSectorSize) PVOID Buffer);

/* ========================================================================== */
/* Function prototypes - vhdx_log.c                                            */
/* ========================================================================== */

NTSTATUS
RosvVhdxLogReplay(
    _Inout_ PROSV_VHDX_STATE State);

BOOLEAN
RosvVhdxLogIsClean(
    _In_ PROSV_VHDX_STATE State);

/* ========================================================================== */
/* Function prototypes - vhdx_alloc.c                                          */
/* ========================================================================== */

NTSTATUS
RosvVhdxAllocateBlock(
    _Inout_ PROSV_VHDX_STATE State,
    _In_ ULONG BlockIndex,
    _Out_ PULONG64 FileOffsetMB);

NTSTATUS
RosvVhdxUpdateBatEntry(
    _Inout_ PROSV_VHDX_STATE State,
    _In_ ULONG BatIndex,
    _In_ ULONG64 NewEntry);

VOID
RosvVhdxScanBatForHighestOffset(
    _Inout_ PROSV_VHDX_STATE State);

/* ========================================================================== */
/* Function prototypes - vhdx_diff.c                                           */
/* ========================================================================== */

NTSTATUS
RosvVhdxOpenDiffChain(
    _Inout_ PROSV_VHDX_STATE Child);

NTSTATUS
RosvVhdxReadSectorsDiff(
    _In_ PROSV_VHDX_STATE State,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _Out_ PVOID Buffer);

BOOLEAN
RosvVhdxIsSectorPresent(
    _In_ PROSV_VHDX_STATE State,
    _In_ ULONG BlockIndex,
    _In_ ULONG SectorWithinBlock);

/* ========================================================================== */
/* GUID comparison helper                                                      */
/* ========================================================================== */

#ifndef RosvIsEqualGuid
#define RosvIsEqualGuid(a, b) \
    (RtlCompareMemory((a), (b), sizeof(GUID)) == sizeof(GUID))
#endif

#ifndef RosvIsNullGuid
static __inline BOOLEAN RosvIsNullGuid(_In_ const GUID *g)
{
    static const GUID NullGuid = { 0 };
    return RosvIsEqualGuid(g, &NullGuid);
}
#endif
