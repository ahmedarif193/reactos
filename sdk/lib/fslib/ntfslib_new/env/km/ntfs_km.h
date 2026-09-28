#ifndef _NTFS_KM_H_
#define _NTFS_KM_H_

/* For the AttributeType enum; C++ forbids forward-declaring an enum
 * without a fixed underlying type. */
#include <ntfslib_new.h>

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS
NtfsDiskInitializeKm(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorBytes);

NTSTATUS
NtfsDiskPrepareMountKm(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorBytes);

/* Writes out metadata held back by the block cache. */
NTSTATUS
NtfsDiskFlushKm(VOID);

/* Flush one mounted volume without submitting writes to previous devices. */
NTSTATUS
NtfsDiskFlushVolumeKm(_In_ PDEVICE_OBJECT DeviceObject);

/* Make writes held in the block cache visible to direct storage reads. */
NTSTATUS
NtfsDiskFlushRangeKm(_In_ PDEVICE_OBJECT DeviceObject,
                    _In_ ULONGLONG Offset,
                    _In_ ULONG Length);

/* Declared here rather than in the public header because
 * FILE_BOTH_DIR_INFORMATION is a kernel-mode type.
 */
NTSTATUS
NtfsDirectoryGetFileBothDirInfo(
    _In_    PNtfsDirectory Dir,
    _In_    BOOLEAN ReturnSingleEntry,
    _In_    BOOLEAN RestartScan,
    _In_    PUNICODE_STRING FileNameFilter,
    _In_    FILE_INFORMATION_CLASS InformationClass,
    _Inout_ PVOID Buffer,
    _Inout_ PULONG BufferLength);

NTSTATUS
NtfsDirectoryStoreInfo(
    _In_ FILE_INFORMATION_CLASS InformationClass,
    _Inout_ PFILE_ID_BOTH_DIR_INFORMATION Info,
    _In_reads_bytes_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _Out_writes_bytes_(*BufferLength) PVOID Buffer,
    _Inout_ PULONG BufferLength,
    _Out_ PULONG EntrySize);

NTSTATUS
NtfsDirectoryLoadForEnumeration(
    _In_ PNtfsDirectory Dir,
    _In_ PNtfsFileRecord File);

#ifdef __cplusplus
}
#endif

#endif /* _NTFS_KM_H_ */
