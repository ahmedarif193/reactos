/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS volume formatter public interface
 */

#ifndef _NTFSFORMAT_H_
#define _NTFSFORMAT_H_

#include "ntfslib_new.h"

/*
 * The formatter is deliberately independent of the ntfslib environment layer
 * (env/fl, env/km, env/um): it builds on-disk structures in caller-provided
 * memory and hands them to caller-provided callbacks. That keeps it usable
 * from a native-mode module such as setuplib, which only has ntdll available,
 * without dragging in the Volume/FileRecord machinery or its env symbols.
 */

typedef NTSTATUS
(*PNTFS_FORMAT_WRITE)(
    _In_ void* Context,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_ const void* Buffer);

typedef void*
(*PNTFS_FORMAT_ALLOCATE)(
    _In_ void* Context,
    _In_ ULONG Length);

typedef void
(*PNTFS_FORMAT_FREE)(
    _In_ void* Context,
    _In_ void* Buffer);

/* Returning FALSE aborts the format with STATUS_CANCELLED. */
typedef BOOLEAN
(*PNTFS_FORMAT_PROGRESS)(
    _In_ void* Context,
    _In_ ULONG PercentComplete);

/* Smallest volume the layout below fits in, in bytes. */
#define NTFS_FORMAT_MINIMUM_VOLUME_SIZE (2 * 1024 * 1024)

#define NTFS_FORMAT_DEFAULT_MFT_RECORD_SIZE   1024
#define NTFS_FORMAT_DEFAULT_INDEX_RECORD_SIZE 4096

/*
 * Records marked in use in the $MFT bitmap. NTFS reserves everything up to
 * NTFS_LAST_RESERVED_FILE_RECORD for metadata and hides it from directory
 * enumeration, so those slots must never be handed out to user files.
 */
#define NTFS_FORMAT_RESERVED_RECORDS (NTFS_LAST_RESERVED_FILE_RECORD + 1)

/* Records the formatter lays down a valid FILE header for. */
#define NTFS_FORMAT_INITIAL_RECORDS 64

typedef struct NtfsFormatParameters
{
    /* Geometry. TotalSectors covers the whole partition; the last sector is
     * reserved for the backup boot sector, as NTFS requires. */
    ULONGLONG TotalSectors;
    ULONG BytesPerSector;
    ULONG SectorsPerCluster;        /* 0: derive from the volume size */
    ULONG MftRecordSize;            /* 0: NTFS_FORMAT_DEFAULT_MFT_RECORD_SIZE */
    ULONG IndexRecordSize;          /* 0: NTFS_FORMAT_DEFAULT_INDEX_RECORD_SIZE */

    /* BPB fields that describe the containing disk. */
    UCHAR MediaDescriptor;          /* 0: 0xF8 (fixed disk) */
    USHORT SectorsPerTrack;
    USHORT NumberOfHeads;
    ULONG HiddenSectors;

    ULONGLONG SerialNumber;         /* 0: derived from CurrentTime */
    PCWSTR VolumeLabel;             /* may be NULL */

    /* NT timestamp stamped into every record the format creates. Supplied by
     * the caller so that the formatter needs no clock of its own. */
    ULONGLONG CurrentTime;

    /*
     * FALSE additionally zeroes every unallocated cluster, which is what a
     * non-quick format means. The metadata written is identical either way.
     */
    BOOLEAN QuickFormat;

    void* IoContext;
    PNTFS_FORMAT_WRITE Write;
    PNTFS_FORMAT_ALLOCATE Allocate;
    PNTFS_FORMAT_FREE Free;
    PNTFS_FORMAT_PROGRESS Progress; /* optional */
} NtfsFormatParameters, *PNtfsFormatParameters;

/*
 * Lays down a fresh NTFS 3.1 volume. On success the volume carries a valid
 * BPB, both boot sectors, MFT records 0-23 and an empty root directory.
 *
 * The bootstrap area of $Boot is left zeroed: on ReactOS the boot code is
 * installed separately from loader\ntfs.bin (see InstallNtfsBootCode), which
 * preserves the BPB this function writes.
 */
EXTERN_C
NTSTATUS
NtfsVolumeFormat(
    _In_ const NtfsFormatParameters* Parameters);

#endif /* _NTFSFORMAT_H_ */
