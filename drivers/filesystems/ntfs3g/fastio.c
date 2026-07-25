/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G section synchronization
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

BOOLEAN
NTAPI
NtfsFastIoCheckIfPossible(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _In_ BOOLEAN CheckForReadOperation,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PFileContextBlock File = FileObject->FsContext;
    PHandleContextBlock Handle = FileObject->FsContext2;
    LARGE_INTEGER LargeLength;

    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(IoStatus);
    UNREFERENCED_PARAMETER(DeviceObject);

    if (!File || !Handle || Handle->CleanupComplete ||
        File->IsVolume ||
        (File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY) ||
        FileOffset->QuadPart < 0 ||
        Length > MAXLONGLONG - FileOffset->QuadPart)
        return FALSE;

    /*
     * Cached writes are intentionally not exposed until the IRP write path
     * grows the backing file and publishes coherent cache-manager sizes.
     */
    if (!CheckForReadOperation)
        return FALSE;
    if (!(Handle->DesiredAccess & (FILE_READ_DATA | FILE_EXECUTE)))
        return FALSE;

    LargeLength.QuadPart = Length;
    return FsRtlFastCheckLockForRead(&File->FileLock,
                                     FileOffset,
                                     &LargeLength,
                                     LockKey,
                                     FileObject,
                                     PsGetCurrentProcess());
}

BOOLEAN
NTAPI
NtfsFastIoWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _In_reads_bytes_(Length) PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(FileOffset);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(LockKey);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(IoStatus);
    UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsAcquireForLazyWrite(_In_ PVOID Context,
                        _In_ BOOLEAN Wait)
{
    PFileContextBlock File = Context;

    if (!ExAcquireResourceExclusiveLite(&File->MainResource, Wait))
        return FALSE;
    ASSERT(IoGetTopLevelIrp() == NULL);
    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    return TRUE;
}

VOID
NTAPI
NtfsReleaseFromLazyWrite(_In_ PVOID Context)
{
    PFileContextBlock File = Context;

    ASSERT(IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    IoSetTopLevelIrp(NULL);
    ExReleaseResourceLite(&File->MainResource);
}

BOOLEAN
NTAPI
NtfsAcquireForReadAhead(_In_ PVOID Context,
                        _In_ BOOLEAN Wait)
{
    PFileContextBlock File = Context;

    if (!ExAcquireResourceSharedLite(&File->MainResource, Wait))
        return FALSE;
    ASSERT(IoGetTopLevelIrp() == NULL);
    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    return TRUE;
}

VOID
NTAPI
NtfsReleaseFromReadAhead(_In_ PVOID Context)
{
    PFileContextBlock File = Context;

    ASSERT(IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    IoSetTopLevelIrp(NULL);
    ExReleaseResourceLite(&File->MainResource);
}

VOID
NTAPI
NtfsFastIoAcquireFileForNtCreateSection(_In_ PFILE_OBJECT FileObject)
{
    PFileContextBlock File = FileObject->FsContext;

    if (File) {
        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
    }
}

VOID
NTAPI
NtfsFastIoReleaseFileForNtCreateSection(_In_ PFILE_OBJECT FileObject)
{
    PFileContextBlock File = FileObject->FsContext;

    if (File) {
        ExReleaseResourceLite(&File->MainResource);
        KeLeaveCriticalRegion();
    }
}
