/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfspch.h"

/* FUNCTIONS ****************************************************************/

/*
 * The cache manager acquires the file only to keep its size stable while it
 * flushes or reads ahead; the resulting paging I/O serializes separately on
 * PagingIoResource, so shared access is what these callbacks need.
 */
BOOLEAN
NTAPI
NtfsAcqLazyWrite(PVOID Context,
                 BOOLEAN Wait)
{
    PFileContextBlock FileCB = (PFileContextBlock)Context;

    FsRtlEnterFileSystem();
    if (!ExAcquireResourceSharedLite(&FileCB->MainResource, Wait))
    {
        FsRtlExitFileSystem();
        return FALSE;
    }

    /* Mark the recursive writes that follow as cache-manager initiated. */
    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    return TRUE;
}

VOID
NTAPI
NtfsRelLazyWrite(PVOID Context)
{
    PFileContextBlock FileCB = (PFileContextBlock)Context;

    IoSetTopLevelIrp(NULL);
    ExReleaseResourceLite(&FileCB->MainResource);
    FsRtlExitFileSystem();
}

BOOLEAN
NTAPI
NtfsAcqReadAhead(PVOID Context,
                 BOOLEAN Wait)
{
    PFileContextBlock FileCB = (PFileContextBlock)Context;

    FsRtlEnterFileSystem();
    if (!ExAcquireResourceSharedLite(&FileCB->MainResource, Wait))
    {
        FsRtlExitFileSystem();
        return FALSE;
    }

    return TRUE;
}

VOID
NTAPI
NtfsRelReadAhead(PVOID Context)
{
    PFileContextBlock FileCB = (PFileContextBlock)Context;

    ExReleaseResourceLite(&FileCB->MainResource);
    FsRtlExitFileSystem();
}

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
    /* Deny FastIo */
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(FileOffset);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(LockKey);
    UNREFERENCED_PARAMETER(CheckForReadOperation);
    UNREFERENCED_PARAMETER(IoStatus);
    UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _Out_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(FileOffset);
    DBG_UNREFERENCED_PARAMETER(Length);
    DBG_UNREFERENCED_PARAMETER(Wait);
    DBG_UNREFERENCED_PARAMETER(LockKey);
    DBG_UNREFERENCED_PARAMETER(Buffer);
    DBG_UNREFERENCED_PARAMETER(IoStatus);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _In_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(FileOffset);
    DBG_UNREFERENCED_PARAMETER(Length);
    DBG_UNREFERENCED_PARAMETER(Wait);
    DBG_UNREFERENCED_PARAMETER(LockKey);
    DBG_UNREFERENCED_PARAMETER(Buffer);
    DBG_UNREFERENCED_PARAMETER(IoStatus);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoQueryBasicInfo(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _Out_ PFILE_BASIC_INFORMATION Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(Wait);
    DBG_UNREFERENCED_PARAMETER(Buffer);
    DBG_UNREFERENCED_PARAMETER(IoStatus);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoQueryStandardInfo(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _Out_ PFILE_STANDARD_INFORMATION Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(Wait);
    DBG_UNREFERENCED_PARAMETER(Buffer);
    DBG_UNREFERENCED_PARAMETER(IoStatus);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoQueryNetworkOpenInfo(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _Out_ PFILE_NETWORK_OPEN_INFORMATION Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(Wait);
    DBG_UNREFERENCED_PARAMETER(Buffer);
    DBG_UNREFERENCED_PARAMETER(IoStatus);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoQueryDirectory(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ BOOLEAN ReturnSingleEntry,
    _In_opt_ PUNICODE_STRING FileName,
    _In_ BOOLEAN RestartScan,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(Wait);
    DBG_UNREFERENCED_PARAMETER(Buffer);
    DBG_UNREFERENCED_PARAMETER(Length);
    DBG_UNREFERENCED_PARAMETER(FileInformationClass);
    DBG_UNREFERENCED_PARAMETER(ReturnSingleEntry);
    DBG_UNREFERENCED_PARAMETER(FileName);
    DBG_UNREFERENCED_PARAMETER(RestartScan);
    DBG_UNREFERENCED_PARAMETER(IoStatus);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoAcquireFile(
    _In_ PFILE_OBJECT FileObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoReleaseFile(
    _In_ PFILE_OBJECT FileObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoDetachDevice(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice)
{
    DBG_UNREFERENCED_PARAMETER(SourceDevice);
    DBG_UNREFERENCED_PARAMETER(TargetDevice);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoQueryOpen(
    _In_ PIRP Irp,
    _Out_ PFILE_NETWORK_OPEN_INFORMATION NetworkInformation,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(Irp);
    DBG_UNREFERENCED_PARAMETER(NetworkInformation);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoPrepareMdlWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _Out_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(FileOffset);
    DBG_UNREFERENCED_PARAMETER(Length);
    DBG_UNREFERENCED_PARAMETER(LockKey);
    DBG_UNREFERENCED_PARAMETER(MdlChain);
    DBG_UNREFERENCED_PARAMETER(IoStatus);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoMdlWriteComplete(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PMDL MdlChain,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(FileOffset);
    DBG_UNREFERENCED_PARAMETER(MdlChain);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoReadCompressed(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _Out_ PVOID Buffer,
    _Out_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(FileOffset);
    DBG_UNREFERENCED_PARAMETER(Length);
    DBG_UNREFERENCED_PARAMETER(LockKey);
    DBG_UNREFERENCED_PARAMETER(Buffer);
    DBG_UNREFERENCED_PARAMETER(MdlChain);
    DBG_UNREFERENCED_PARAMETER(IoStatus);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoWriteCompressed(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _In_ PVOID Buffer,
    _Out_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(FileOffset);
    DBG_UNREFERENCED_PARAMETER(Length);
    DBG_UNREFERENCED_PARAMETER(LockKey);
    DBG_UNREFERENCED_PARAMETER(Buffer);
    DBG_UNREFERENCED_PARAMETER(MdlChain);
    DBG_UNREFERENCED_PARAMETER(IoStatus);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoMdlRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _Out_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(FileOffset);
    DBG_UNREFERENCED_PARAMETER(Length);
    DBG_UNREFERENCED_PARAMETER(LockKey);
    DBG_UNREFERENCED_PARAMETER(MdlChain);
    DBG_UNREFERENCED_PARAMETER(IoStatus);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoMdlReadComplete(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL MdlChain,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DBG_UNREFERENCED_PARAMETER(FileObject);
    DBG_UNREFERENCED_PARAMETER(MdlChain);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
NtfsFastIoAcquireFileForNtCreateSection(
    _In_ PFILE_OBJECT FileObject)
{
    PFileContextBlock FileCB;

    if (!FileObject)
        return FALSE;

    FileCB = (PFileContextBlock)FileObject->FsContext;
    if (!FileCB)
        return FALSE;

    // Acquire the main resource exclusively
    KeEnterCriticalRegion();
    return ExAcquireResourceExclusiveLite(&FileCB->MainResource, TRUE);
}

BOOLEAN
NTAPI
NtfsFastIoReleaseFileForNtCreateSection(
    _In_ PFILE_OBJECT FileObject)
{
    PFileContextBlock FileCB;

    if (!FileObject)
        return FALSE;

    FileCB = (PFileContextBlock)FileObject->FsContext;
    if (!FileCB)
        return FALSE;

    // Release the main resource
    ExReleaseResourceLite(&FileCB->MainResource);
    KeLeaveCriticalRegion();
    return TRUE;
}

/* EOF */