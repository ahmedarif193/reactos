/*
 * PROJECT:         ReactOS
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Windows crash dump header definitions
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define DUMP_HEADER32_SIZE PAGE_SIZE
#define DUMP_HEADER64_SIZE (PAGE_SIZE * 2)
#define DUMP_PHYSICAL_MEMORY_BLOCK_SIZE32 700
#define DUMP_PHYSICAL_MEMORY_BLOCK_SIZE64 704
#define DUMP_CONTEXT_RECORD_SIZE32 1200
#define DUMP_CONTEXT_RECORD_SIZE64 3000
#define DUMP_HEADER_COMMENT_SIZE 128
#define DUMP_RESERVED0_SIZE32 1760
#define DUMP_RESERVED2_SIZE32 16
#define DUMP_RESERVED3_SIZE32 56
#define DUMP_RESERVED0_SIZE64 4008

#define DUMP_SIGNATURE32 'EGAP'
#define DUMP_VALID_DUMP32 'PMUD'
#define DUMP_SIGNATURE64 'EGAP'
#define DUMP_VALID_DUMP64 '46UD'
#define DUMP_SUMMARY_SIGNATURE 'PMDS'
#define DUMP_SUMMARY_VALID 'PMUD'

typedef enum _DUMP_TYPES
{
    DUMP_TYPE_INVALID = -1,
    DUMP_TYPE_UNKNOWN,
    DUMP_TYPE_FULL,
    DUMP_TYPE_SUMMARY,
    DUMP_TYPE_HEADER,
    DUMP_TYPE_TRIAGE,
    DUMP_TYPE_BITMAP_FULL,
    DUMP_TYPE_BITMAP_KERNEL,
    DUMP_TYPE_AUTOMATIC
} DUMP_TYPES;

typedef struct _DUMP_PHYSICAL_MEMORY_RUN32
{
    ULONG BasePage;
    ULONG PageCount;
} DUMP_PHYSICAL_MEMORY_RUN32, *PDUMP_PHYSICAL_MEMORY_RUN32;

typedef struct _DUMP_PHYSICAL_MEMORY_DESCRIPTOR32
{
    ULONG NumberOfRuns;
    ULONG NumberOfPages;
    DUMP_PHYSICAL_MEMORY_RUN32 Run[1];
} DUMP_PHYSICAL_MEMORY_DESCRIPTOR32, *PDUMP_PHYSICAL_MEMORY_DESCRIPTOR32;

typedef struct _DUMP_PHYSICAL_MEMORY_RUN64
{
    ULONG64 BasePage;
    ULONG64 PageCount;
} DUMP_PHYSICAL_MEMORY_RUN64, *PDUMP_PHYSICAL_MEMORY_RUN64;

typedef struct _DUMP_PHYSICAL_MEMORY_DESCRIPTOR64
{
    ULONG NumberOfRuns;
    ULONG64 NumberOfPages;
    DUMP_PHYSICAL_MEMORY_RUN64 Run[1];
} DUMP_PHYSICAL_MEMORY_DESCRIPTOR64, *PDUMP_PHYSICAL_MEMORY_DESCRIPTOR64;

typedef union _DUMP_FILE_ATTRIBUTES
{
    struct
    {
        ULONG HiberCrash : 1;
        ULONG DumpDevicePowerOff : 1;
        ULONG InsufficientDumpfileSize : 1;
        ULONG KernelGeneratedTriageDump : 1;
        ULONG LiveDumpGeneratedDump : 1;
        ULONG DumpIsGeneratedOffline : 1;
        ULONG FilterDumpFile : 1;
        ULONG EarlyBootCrash : 1;
        ULONG EncryptedDumpData : 1;
        ULONG DecryptedDump : 1;
        ULONG ReservedFlags : 22;
    };
    ULONG Attributes;
} DUMP_FILE_ATTRIBUTES, *PDUMP_FILE_ATTRIBUTES;

typedef struct _DUMP_HEADER32
{
    ULONG Signature;
    ULONG ValidDump;
    ULONG MajorVersion;
    ULONG MinorVersion;
    ULONG DirectoryTableBase;
    ULONG PfnDataBase;
    ULONG PsLoadedModuleList;
    ULONG PsActiveProcessHead;
    ULONG MachineImageType;
    ULONG NumberProcessors;
    ULONG BugCheckCode;
    ULONG BugCheckParameter1;
    ULONG BugCheckParameter2;
    ULONG BugCheckParameter3;
    ULONG BugCheckParameter4;
    CHAR VersionUser[32];
    UCHAR PaeEnabled;
    UCHAR KdSecondaryVersion;
    UCHAR Spare3[2];
    ULONG KdDebuggerDataBlock;
    union
    {
        DUMP_PHYSICAL_MEMORY_DESCRIPTOR32 PhysicalMemoryBlock;
        UCHAR PhysicalMemoryBlockBuffer[DUMP_PHYSICAL_MEMORY_BLOCK_SIZE32];
    };
    UCHAR ContextRecord[DUMP_CONTEXT_RECORD_SIZE32];
    EXCEPTION_RECORD32 Exception;
    CHAR Comment[DUMP_HEADER_COMMENT_SIZE];
    DUMP_FILE_ATTRIBUTES Attributes;
    ULONG BootId;
    UCHAR Reserved0[DUMP_RESERVED0_SIZE32];
    ULONG DumpType;
    ULONG MiniDumpFields;
    ULONG SecondaryDataState;
    ULONG ProductType;
    ULONG SuiteMask;
    ULONG WriterStatus;
    LARGE_INTEGER RequiredDumpSpace;
    UCHAR Reserved2[DUMP_RESERVED2_SIZE32];
    LARGE_INTEGER SystemUpTime;
    LARGE_INTEGER SystemTime;
    UCHAR Reserved3[DUMP_RESERVED3_SIZE32];
} DUMP_HEADER32, *PDUMP_HEADER32;

typedef struct _DUMP_HEADER64
{
    ULONG Signature;
    ULONG ValidDump;
    ULONG MajorVersion;
    ULONG MinorVersion;
    ULONG64 DirectoryTableBase;
    ULONG64 PfnDataBase;
    ULONG64 PsLoadedModuleList;
    ULONG64 PsActiveProcessHead;
    ULONG MachineImageType;
    ULONG NumberProcessors;
    ULONG BugCheckCode;
    ULONG64 BugCheckParameter1;
    ULONG64 BugCheckParameter2;
    ULONG64 BugCheckParameter3;
    ULONG64 BugCheckParameter4;
    CHAR VersionUser[32];
    ULONG64 KdDebuggerDataBlock;
    union
    {
        DUMP_PHYSICAL_MEMORY_DESCRIPTOR64 PhysicalMemoryBlock;
        UCHAR PhysicalMemoryBlockBuffer[DUMP_PHYSICAL_MEMORY_BLOCK_SIZE64];
    };
    UCHAR ContextRecord[DUMP_CONTEXT_RECORD_SIZE64];
    EXCEPTION_RECORD64 Exception;
    ULONG DumpType;
    LARGE_INTEGER RequiredDumpSpace;
    LARGE_INTEGER SystemTime;
    CHAR Comment[DUMP_HEADER_COMMENT_SIZE];
    LARGE_INTEGER SystemUpTime;
    ULONG MiniDumpFields;
    ULONG SecondaryDataState;
    ULONG ProductType;
    ULONG SuiteMask;
    ULONG WriterStatus;
    UCHAR Unused1;
    UCHAR KdSecondaryVersion;
    UCHAR Unused[2];
    DUMP_FILE_ATTRIBUTES Attributes;
    ULONG BootId;
    UCHAR Reserved0[DUMP_RESERVED0_SIZE64];
} DUMP_HEADER64, *PDUMP_HEADER64;

C_ASSERT(sizeof(DUMP_HEADER32) == DUMP_HEADER32_SIZE);
C_ASSERT(sizeof(DUMP_HEADER64) == DUMP_HEADER64_SIZE);
C_ASSERT(FIELD_OFFSET(DUMP_HEADER64, KdDebuggerDataBlock) == 0x80);
C_ASSERT(FIELD_OFFSET(DUMP_HEADER64, PhysicalMemoryBlock) == 0x88);

/*
 * Bitmap (kernel/filtered) dump metadata, stored right after DUMP_HEADER64.
 * The bitmap has one bit per PFN from 0 to BitmapSize - 1; the pages whose
 * bits are set follow at file offset HeaderSize in ascending PFN order.
 * Layout verified byte-for-byte against a Windows 11 kernel MEMORY.DMP
 * (DumpType 6, "SDMP"/"DUMP" block, 64-bit fields, bits at +0x38).
 */
typedef struct _SUMMARY_DUMP64
{
    ULONG Signature;
    ULONG ValidDump;
    ULONG DumpOptions;
    ULONG Spare0;
    ULONG64 Spare1;
    ULONG64 Spare2;
    ULONG64 HeaderSize;
    ULONG64 Pages;
    ULONG64 BitmapSize;
    ULONG Buffer[ANYSIZE_ARRAY];
} SUMMARY_DUMP64, *PSUMMARY_DUMP64;

C_ASSERT(FIELD_OFFSET(SUMMARY_DUMP64, Buffer) == 0x38);
