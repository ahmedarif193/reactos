/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS volume formatter internals
 */

#ifndef _NTFSFORMATINT_H_
#define _NTFSFORMATINT_H_

/*
 * The formatter builds for user/native mode only: it is consumed by the fmifs
 * provider and by setuplib, never by the kernel driver. That lets it use the
 * ndk headers directly and stay clear of ntfslib_new_internal.h, which pulls
 * in kernel-only ntifs.h.
 */
#ifdef NTFSLIB_PORTABLE

/* Host builds get the types from ntfsenv.h via ntfsformat.h. */
#include <string.h>
#include <wctype.h>

#include "ntfsformat.h"

#define RtlZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#define RtlCopyMemory(Destination, Source, Length) \
    memcpy((Destination), (Source), (Length))
#define RtlFillMemory(Destination, Length, Fill) \
    memset((Destination), (Fill), (Length))

#define C_ASSERT(Expression) static_assert(Expression, #Expression)

#ifndef STATUS_CANCELLED
#define STATUS_CANCELLED ((NTSTATUS)0xC0000120)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001)
#endif
#ifndef NULL
#define NULL 0
#endif

static inline WCHAR
RtlUpcaseUnicodeChar(WCHAR Character)
{
    return (WCHAR)towupper((wint_t)(UINT16)Character);
}

#else /* NTFSLIB_PORTABLE */

#ifndef NTOS_MODE_USER
#define NTOS_MODE_USER
#endif

#include <ndk/umtypes.h>
#include <ndk/rtlfuncs.h>

#include "ntfsformat.h"

#endif /* NTFSLIB_PORTABLE */

/* ntifs.h's name for the same limit; not visible in user mode. */
#define NTFS_FORMAT_MAX_LABEL_CHARS 32

/* $UpCase covers the whole UTF-16 code unit range. */
#define NTFS_UPCASE_LENGTH 65536
#define NTFS_UPCASE_SIZE (NTFS_UPCASE_LENGTH * sizeof(WCHAR))

/* 15 defined attribute types plus a zeroed terminator entry. */
#define NTFS_ATTRDEF_ENTRIES 16
#define NTFS_ATTRDEF_SIZE (NTFS_ATTRDEF_ENTRIES * sizeof(AttrDefEntry))

/* $Boot always describes the first 8 KB of the volume. */
#define NTFS_BOOT_AREA_SIZE 8192

/* $LogFile is written in 4 KB pages regardless of the cluster size. */
#define NTFS_LOG_PAGE_SIZE 4096

/* An MFT reference packs the record number with its sequence number. */
#define NTFS_MK_FILE_REFERENCE(Record, Sequence) \
    ((ULONGLONG)(Record) | ((ULONGLONG)(Sequence) << 48))
#define NTFS_ROOT_FILE_REFERENCE NTFS_MK_FILE_REFERENCE(_Root, _Root)

/*
 * The volume layout. Everything except $MFTMirr is packed contiguously from
 * the start of the volume; $MFTMirr goes near the middle so that losing the
 * head of the disk does not take the mirror with it.
 */
typedef struct FormatContext
{
    const NtfsFormatParameters* Params;

    ULONG BytesPerSector;
    ULONG SectorsPerCluster;
    ULONG ClusterSize;
    ULONG MftRecordSize;
    ULONG IndexRecordSize;

    ULONGLONG UsableSectors;    /* TotalSectors - 1 (backup boot sector) */
    ULONGLONG TotalClusters;
    ULONGLONG SerialNumber;
    ULONGLONG CurrentTime;

    ULONGLONG BootLcn,      BootClusters;
    ULONGLONG MftLcn,       MftClusters;
    ULONGLONG MftBitmapLcn, MftBitmapClusters;
    ULONGLONG LogFileLcn,   LogFileClusters;
    ULONGLONG BitmapLcn,    BitmapClusters;
    ULONGLONG UpCaseLcn,    UpCaseClusters;
    ULONGLONG AttrDefLcn,   AttrDefClusters;
    ULONGLONG RootIndexLcn, RootIndexClusters;
    ULONGLONG MftMirrLcn,   MftMirrClusters;

    /* First cluster past the contiguous head region. */
    ULONGLONG HeadEndLcn;

    ULONG MftRecordCount;       /* records with a valid FILE header */
    ULONGLONG MftDataSize;
    ULONGLONG MftBitmapDataSize;
    ULONGLONG LogFileSize;
    ULONGLONG BitmapDataSize;

    PUCHAR RecordBuffer;        /* one MFT record */
    PUCHAR TransferBuffer;      /* bulk fill buffer */
    ULONG TransferSize;

    /* Set while building a record by the FormatAdd* helpers. */
    ULONG RecordOffset;
    USHORT NextAttributeId;
} FormatContext, *PFormatContext;

/* format.cpp */
NTSTATUS
FormatWriteAt(
    _In_ PFormatContext Ctx,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_ const void* Buffer);

NTSTATUS
FormatWriteCluster(
    _In_ PFormatContext Ctx,
    _In_ ULONGLONG Lcn,
    _In_ ULONG Length,
    _In_ const void* Buffer);

NTSTATUS
FormatFillClusters(
    _In_ PFormatContext Ctx,
    _In_ ULONGLONG Lcn,
    _In_ ULONGLONG ClusterCount,
    _In_ UCHAR Value);

ULONG
FormatEncodeDataRuns(
    _Out_ PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONGLONG StartLcn,
    _In_ ULONGLONG ClusterCount,
    _In_ BOOLEAN Sparse);

/* record.cpp */
void
FormatBeginRecord(
    _In_ PFormatContext Ctx,
    _In_ ULONG RecordNumber,
    _In_ USHORT SequenceNumber,
    _In_ USHORT Flags);

PAttribute
FormatAddResident(
    _In_ PFormatContext Ctx,
    _In_ ULONG Type,
    _In_opt_ PCWSTR Name,
    _In_ const void* Value,
    _In_ ULONG ValueLength,
    _In_ UCHAR IndexedFlag);

PAttribute
FormatAddNonResident(
    _In_ PFormatContext Ctx,
    _In_ ULONG Type,
    _In_opt_ PCWSTR Name,
    _In_ ULONGLONG StartLcn,
    _In_ ULONGLONG ClusterCount,
    _In_ ULONGLONG DataSize,
    _In_ ULONGLONG ValidDataSize,
    _In_ USHORT AttributeFlags,
    _In_ BOOLEAN Sparse);

NTSTATUS
FormatEndRecord(
    _In_ PFormatContext Ctx);

void
FormatFillStandardInformation(
    _In_ PFormatContext Ctx,
    _Out_ PStandardInformationEx Information,
    _In_ ULONG FilePermissions);

NTSTATUS
FormatAddFileName(
    _In_ PFormatContext Ctx,
    _In_ ULONGLONG ParentReference,
    _In_ PCWSTR Name,
    _In_ ULONG Flags,
    _In_ UCHAR NameType,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize);

NTSTATUS
FormatAddEmptyDirectoryIndex(
    _In_ PFormatContext Ctx,
    _In_ ULONG IndexedAttributeType,
    _In_ ULONG CollationRule,
    _In_opt_ PCWSTR IndexName);

/* Fills a $FILE_NAME value into Buffer and returns its length. */
ULONG
FormatFillFileName(
    _In_ PFormatContext Ctx,
    _Out_ PUCHAR Buffer,
    _In_ ULONGLONG ParentReference,
    _In_ PCWSTR Name,
    _In_ ULONG Flags,
    _In_ UCHAR NameType,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize);

/*
 * $INDEX_ROOT for a directory whose entries live in $INDEX_ALLOCATION: it
 * holds a single end entry that descends to VCN 0.
 */
NTSTATUS
FormatAddIndexRootNode(
    _In_ PFormatContext Ctx,
    _In_ ULONG IndexedAttributeType,
    _In_ ULONG CollationRule,
    _In_ PCWSTR IndexName);

/* Builds one leaf index entry describing a file name. */
ULONG
FormatBuildFileNameIndexEntry(
    _In_ PFormatContext Ctx,
    _Out_ PUCHAR Buffer,
    _In_ ULONGLONG FileReference,
    _In_ PCWSTR Name,
    _In_ ULONG Flags,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize);

/* Stamps the update sequence array over any FILE/INDX record. */
void
FormatCommitFixupBuffer(
    _In_ PFormatContext Ctx,
    _Inout_ PUCHAR Buffer,
    _In_ ULONG Size);

/* tables.cpp */
void
FormatBuildUpCaseTable(
    _Out_ PWCHAR Table);

void
FormatBuildAttrDefTable(
    _Out_ PAttrDefEntry Table);

ULONG
FormatBuildDefaultSecurityDescriptor(
    _Out_ PUCHAR Buffer,
    _In_ ULONG BufferLength);

/* sysfiles.cpp */
NTSTATUS
FormatWriteMetadata(
    _In_ PFormatContext Ctx);

/* bootsect.cpp */
NTSTATUS
FormatWriteBootSectors(
    _In_ PFormatContext Ctx);

#endif /* _NTFSFORMATINT_H_ */
