/*
 * PROJECT:   Registry manipulation library
 * LICENSE:   GPL - See COPYING in the top level directory
 * COPYRIGHT: Copyright 2005 Filip Navara <navaraf@reactos.org>
 *            Copyright 2001 - 2005 Eric Kohl
 */

#pragma once

//
// Hive operations
//
#define HINIT_CREATE                    0
#define HINIT_MEMORY                    1
#define HINIT_FILE                      2
#define HINIT_MEMORY_INPLACE            3
#define HINIT_FLAT                      4
#define HINIT_MAPFILE                   5

//
// Hive flags
//
#define HIVE_VOLATILE                   1
#define HIVE_NOLAZYFLUSH                2
#define HIVE_HAS_BEEN_REPLACED          4
#define HIVE_HAS_BEEN_FREED             8
#define HIVE_UNKNOWN                    0x10
#define HIVE_IS_UNLOADING               0x20

//
// Hive types
//
#define HFILE_TYPE_PRIMARY              0
#define HFILE_TYPE_LOG                  1
#define HFILE_TYPE_EXTERNAL             2
#define HFILE_TYPE_ALTERNATE            3 // Technically a HFILE_TYPE_PRIMARY but for mirror backup hives. ONLY USED for the SYSTEM hive!
#define HFILE_TYPE_MAX                  4

//
// Hive sizes
//
#define HBLOCK_SIZE                     0x1000
#define HSECTOR_SIZE                    0x200
#define HSECTOR_COUNT                   8

#define HV_LOG_HEADER_SIZE              FIELD_OFFSET(HBASE_BLOCK, Reserved2)

//
// Clean Block identifier
//
#define HV_CLEAN_BLOCK 0U

//
// Hive Log identifiers
//
#define HV_LOG_DIRTY_BLOCK 0xFF
#define HV_LOG_DIRTY_SIGNATURE 0x54524944 // "DIRT"

//
// Hive structure identifiers
//
#define HV_HHIVE_SIGNATURE              0xbee0bee0
#define HV_HBLOCK_SIGNATURE             0x66676572  // "regf"
#define HV_HBIN_SIGNATURE               0x6e696268  // "hbin"

//
// Hive versions
//
#define HSYS_MAJOR                      1
#define HSYS_MINOR                      3
#define HSYS_WHISTLER_BETA1             4
#define HSYS_WHISTLER                   5
#define HSYS_MINOR_SUPPORTED            HSYS_WHISTLER

//
// Hive formats
//
#define HBASE_FORMAT_MEMORY             1

//
// Hive storage
//
#define HTYPE_COUNT                     2

//
// Hive boot types
//
#define HBOOT_TYPE_REGULAR         0
#define HBOOT_TYPE_SELF_HEAL       4

//
// Hive boot recover types
//
#define HBOOT_NO_BOOT_RECOVER                   0
#define HBOOT_BOOT_RECOVERED_BY_HIVE_LOG        1
#define HBOOT_BOOT_RECOVERED_BY_ALTERNATE_HIVE  2

/**
 * @name HCELL_INDEX
 *
 * A handle to cell index. The highest bit specifies the cell storage and
 * the other bits specify index into the hive file. The value HCELL_NULL
 * (-1) is reserved for marking invalid cells.
 */
typedef ULONG HCELL_INDEX, *PHCELL_INDEX;

//
// Cell Magic Values
//
#define HCELL_NIL                       MAXULONG
#define HCELL_CACHED                    1

#define HCELL_TYPE_MASK                 0x80000000
#define HCELL_BLOCK_MASK                0x7ffff000
#define HCELL_OFFSET_MASK               0x00000fff
#define HCELL_TYPE_SHIFT                31
#define HCELL_BLOCK_SHIFT               12
#define HCELL_OFFSET_SHIFT              0

#define HvGetCellType(Cell)             \
    ((ULONG)(((Cell) & HCELL_TYPE_MASK) >> HCELL_TYPE_SHIFT))
#define HvGetCellBlock(Cell)            \
    ((ULONG)(((Cell) & HCELL_BLOCK_MASK) >> HCELL_BLOCK_SHIFT))

typedef enum
{
    Stable   = 0,
    Volatile = 1
} HSTORAGE_TYPE;

#include <pshpack1.h>

/**
 * @name HBASE_BLOCK
 *
 * On-disk header for registry hive file.
 */

#define HIVE_FILENAME_MAXLEN            31

typedef struct _HBASE_BLOCK
{
    /* Hive base block identifier "regf" (0x66676572) */
    ULONG Signature;

    /* Update counters */
    ULONG Sequence1;
    ULONG Sequence2;

    /* When this hive file was last modified */
    LARGE_INTEGER TimeStamp;

    /* Registry format major version (1) */
    ULONG Major;

    /* Registry format minor version (3)
       Version 3 added fast indexes, version 5 has large value optimizations */
    ULONG Minor;

    /* Registry file type (0 - Primary, 1 - Log) */
    ULONG Type;

    /* Registry format (1 is the only defined value so far) */
    ULONG Format;

    /* Offset into file from the byte after the end of the base block.
       If the hive is volatile, this is the actual pointer to the CM_KEY_NODE */
    HCELL_INDEX RootCell;

    /* Size in bytes of the full hive, minus the header, multiple of the block size (4KB) */
    ULONG Length;

    /* (1?) */
    ULONG Cluster;

    /* Last 31 UNICODE characters, plus terminating NULL character,
       of the full name of the hive file */
    WCHAR FileName[HIVE_FILENAME_MAXLEN + 1];

    ULONG Reserved1[99];

    /* Checksum of first 0x200 bytes */
    ULONG CheckSum;

    ULONG Reserved2[0x37E];
    ULONG BootType;
    ULONG BootRecover;
} HBASE_BLOCK, *PHBASE_BLOCK;

C_ASSERT(sizeof(HBASE_BLOCK) == HBLOCK_SIZE);

typedef struct _HBIN
{
    /* Hive bin identifier "hbin" (0x6E696268) */
    ULONG Signature;

    /* Block offset of this bin */
    HCELL_INDEX FileOffset;

    /* Size in bytes of this bin, multiple of the block size (4KB) */
    ULONG Size;

    ULONG Reserved1[2];

    /* When this bin was last modified */
    LARGE_INTEGER TimeStamp;

    /* Unused (In-memory only) */
    ULONG Spare;
} HBIN, *PHBIN;

typedef struct _HCELL
{
    /* <0 if used, >0 if free */
    LONG Size;
} HCELL, *PHCELL;

#include <poppack.h>

struct _HHIVE;

typedef struct _CELL_DATA*
(CMAPI *PGET_CELL_ROUTINE)(
    struct _HHIVE *Hive,
    HCELL_INDEX Cell
);

typedef VOID
(CMAPI *PRELEASE_CELL_ROUTINE)(
    struct _HHIVE *Hive,
    HCELL_INDEX Cell
);

typedef PVOID
(CMAPI *PALLOCATE_ROUTINE)(
    SIZE_T Size,
    BOOLEAN Paged,
    ULONG Tag
);

typedef VOID
(CMAPI *PFREE_ROUTINE)(
    PVOID Ptr,
    ULONG Quota
);

typedef BOOLEAN
(CMAPI *PFILE_READ_ROUTINE)(
    struct _HHIVE *RegistryHive,
    ULONG FileType,
    PULONG FileOffset,
    PVOID Buffer,
    SIZE_T BufferLength
);

typedef BOOLEAN
(CMAPI *PFILE_WRITE_ROUTINE)(
    struct _HHIVE *RegistryHive,
    ULONG FileType,
    PULONG FileOffset,
    PVOID Buffer,
    SIZE_T BufferLength
);

typedef BOOLEAN
(CMAPI *PFILE_SET_SIZE_ROUTINE)(
    struct _HHIVE *RegistryHive,
    ULONG FileType,
    ULONG FileSize,
    ULONG OldfileSize
);

typedef BOOLEAN
(CMAPI *PFILE_FLUSH_ROUTINE)(
    struct _HHIVE *RegistryHive,
    ULONG FileType,
    PLARGE_INTEGER FileOffset,
    ULONG Length
);

#define MAP_ENTRY_NEW_ALLOC 0x1
#define MAP_ENTRY_DUMMY 0x8

typedef struct _HMAP_ENTRY
{
    ULONG_PTR BlockOffset;
    ULONG_PTR PermanentBinAddress;
    ULONG MemAlloc;
} HMAP_ENTRY, *PHMAP_ENTRY;

typedef struct _HMAP_TABLE
{
    HMAP_ENTRY Table[512];
} HMAP_TABLE, *PHMAP_TABLE;

typedef struct _HMAP_DIRECTORY
{
    PHMAP_TABLE Directory[1024];
} HMAP_DIRECTORY, *PHMAP_DIRECTORY;

#if defined(_M_ARM64)
C_ASSERT(sizeof(HMAP_ENTRY) == 24);
C_ASSERT(FIELD_OFFSET(HMAP_ENTRY, BlockOffset) == 0);
C_ASSERT(FIELD_OFFSET(HMAP_ENTRY, PermanentBinAddress) == 8);
C_ASSERT(FIELD_OFFSET(HMAP_ENTRY, MemAlloc) == 16);
C_ASSERT(sizeof(HMAP_TABLE) == 12288);
C_ASSERT(sizeof(HMAP_DIRECTORY) == 8192);
#endif

typedef struct _FREE_DISPLAY
{
    ULONG RealVectorSize;
    ULONG Hint;
    RTL_BITMAP Display;
} FREE_DISPLAY, *PFREE_DISPLAY;

typedef struct _DUAL
{
    ULONG Length;
    PHMAP_DIRECTORY Map;
    PHMAP_TABLE SmallDir;
    ULONG Guard;
    FREE_DISPLAY FreeDisplay[24];
    LIST_ENTRY FreeBins;
    ULONG FreeSummary;
} DUAL, *PDUAL;

#if defined(_M_ARM64)
C_ASSERT(sizeof(FREE_DISPLAY) == 24);
C_ASSERT(FIELD_OFFSET(FREE_DISPLAY, RealVectorSize) == 0);
C_ASSERT(FIELD_OFFSET(FREE_DISPLAY, Hint) == 4);
C_ASSERT(FIELD_OFFSET(FREE_DISPLAY, Display) == 8);
C_ASSERT(sizeof(DUAL) == 632);
C_ASSERT(FIELD_OFFSET(DUAL, Length) == 0);
C_ASSERT(FIELD_OFFSET(DUAL, Map) == 8);
C_ASSERT(FIELD_OFFSET(DUAL, SmallDir) == 16);
C_ASSERT(FIELD_OFFSET(DUAL, Guard) == 24);
C_ASSERT(FIELD_OFFSET(DUAL, FreeDisplay) == 32);
C_ASSERT(FIELD_OFFSET(DUAL, FreeBins) == 608);
C_ASSERT(FIELD_OFFSET(DUAL, FreeSummary) == 624);
#endif

typedef struct _CMSI_RW_LOCK
{
    PVOID Reserved;
} CMSI_RW_LOCK, *PCMSI_RW_LOCK;

typedef struct _HVP_VIEW_MAP
{
    PVOID SectionReference;
    LONGLONG StorageEndFileOffset;
    LONGLONG SectionEndFileOffset;
    struct _CMSI_PROCESS_TUPLE *ProcessTuple;
    ULONG Flags;
    RTL_RB_TREE ViewTree;
} HVP_VIEW_MAP, *PHVP_VIEW_MAP;

typedef struct _HHIVE
{
    ULONG Signature;
    PGET_CELL_ROUTINE GetCellRoutine;
    PRELEASE_CELL_ROUTINE ReleaseCellRoutine;
    PALLOCATE_ROUTINE Allocate;
    PFREE_ROUTINE Free;
    PFILE_WRITE_ROUTINE FileWrite;
    PFILE_READ_ROUTINE FileRead;
    PVOID HiveLoadFailure;
    PHBASE_BLOCK BaseBlock;
    CMSI_RW_LOCK FlusherLock;
    CMSI_RW_LOCK WriterLock;
    RTL_BITMAP DirtyVector;
    ULONG DirtyCount;
    ULONG DirtyAlloc;
    RTL_BITMAP UnreconciledVector;
    ULONG UnreconciledCount;
    ULONG BaseBlockAlloc;
    ULONG Cluster;
    UCHAR Flat : 1;
    UCHAR ReadOnly : 1;
    UCHAR Reserved : 6;
    UCHAR DirtyFlag;
    ULONG HvBinHeadersUse;
    ULONG HvFreeCellsUse;
    ULONG HvUsedCellsUse;
    ULONG CmUsedCellsUse;
    ULONG HiveFlags;
    ULONG FlusherFlags;
    ULONG CurrentLog;
    ULONG CurrentLogSequence;
    ULONG CurrentLogMinimumSequence;
    ULONG CurrentLogOffset;
    ULONG MinimumLogSequence;
    ULONG LogFileSizeCap;
    UCHAR LogDataPresent[2];
    BOOLEAN PrimaryFileValid;
    BOOLEAN BaseBlockDirty;
    LARGE_INTEGER LastLogSwapTime;
    union
    {
        struct
        {
            USHORT FirstLogFile : 3;
            USHORT SecondLogFile : 3;
            USHORT HeaderRecovered : 1;
            USHORT LegacyRecoveryIndicated : 1;
            USHORT RecoveryInformationReserved : 8;
        };
        USHORT RecoveryInformation;
    };
    UCHAR LogEntriesRecovered[2];
    ULONG RefreshCount;
    ULONG StorageTypeCount;
    ULONG Version;
    HVP_VIEW_MAP ViewMap;
    DUAL Storage[HTYPE_COUNT];
} HHIVE, *PHHIVE;

#if defined(_M_ARM64)
C_ASSERT(sizeof(CMSI_RW_LOCK) == 8);
C_ASSERT(FIELD_OFFSET(CMSI_RW_LOCK, Reserved) == 0);
C_ASSERT(sizeof(RTL_RB_TREE) == 16);
C_ASSERT(FIELD_OFFSET(RTL_RB_TREE, Root) == 0);
C_ASSERT(FIELD_OFFSET(RTL_RB_TREE, Min) == 8);
C_ASSERT(sizeof(HVP_VIEW_MAP) == 56);
C_ASSERT(FIELD_OFFSET(HVP_VIEW_MAP, SectionReference) == 0);
C_ASSERT(FIELD_OFFSET(HVP_VIEW_MAP, StorageEndFileOffset) == 8);
C_ASSERT(FIELD_OFFSET(HVP_VIEW_MAP, SectionEndFileOffset) == 16);
C_ASSERT(FIELD_OFFSET(HVP_VIEW_MAP, ProcessTuple) == 24);
C_ASSERT(FIELD_OFFSET(HVP_VIEW_MAP, Flags) == 32);
C_ASSERT(FIELD_OFFSET(HVP_VIEW_MAP, ViewTree) == 40);
C_ASSERT(sizeof(HHIVE) == 1544);
C_ASSERT(FIELD_OFFSET(HHIVE, BaseBlock) == 64);
C_ASSERT(FIELD_OFFSET(HHIVE, FlusherLock) == 72);
C_ASSERT(FIELD_OFFSET(HHIVE, WriterLock) == 80);
C_ASSERT(FIELD_OFFSET(HHIVE, DirtyVector) == 88);
C_ASSERT(FIELD_OFFSET(HHIVE, UnreconciledVector) == 112);
C_ASSERT(FIELD_OFFSET(HHIVE, HiveFlags) == 160);
C_ASSERT(FIELD_OFFSET(HHIVE, ViewMap) == 224);
C_ASSERT(FIELD_OFFSET(HHIVE, Storage) == 280);
#endif

#define IsFreeCell(Cell)    ((Cell)->Size >= 0)
#define IsUsedCell(Cell)    ((Cell)->Size <  0)
