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

/* Declared here rather than in the public header because
 * FILE_BOTH_DIR_INFORMATION is a kernel-mode type.
 */
NTSTATUS
NtfsDirectoryGetFileBothDirInfo(
    _In_    PNtfsDirectory Dir,
    _In_    BOOLEAN ReturnSingleEntry,
    _In_    BOOLEAN RestartScan,
    _In_    PUNICODE_STRING FileNameFilter,
    _Inout_ PFILE_BOTH_DIR_INFORMATION Buffer,
    _Inout_ PULONG BufferLength);

NTSTATUS
NtfsDirectoryLoadForEnumeration(
    _In_ PNtfsDirectory Dir,
    _In_ PNtfsFileRecord File);

#ifdef __cplusplus
}
#endif

#endif /* _NTFS_KM_H_ */
