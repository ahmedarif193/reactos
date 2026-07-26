/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new entry point
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2024 Carl J. Bialorucki <carl.bialorucki@reactos.org>
 */

#include "ntfspch.h"

/* GLOBALS *****************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, NtfsUnload)
#pragma alloc_text(PAGE, NtfsFsdCleanup)
#pragma alloc_text(PAGE, NtfsFsdLockControl)
#pragma alloc_text(PAGE, NtfsFsdDeviceControl)
#pragma alloc_text(PAGE, NtfsFsdShutdown)
#endif

PDEVICE_OBJECT NtfsDiskFileSystemDeviceObject;

#define TAG_IRP_CTXT 'iftN'
#define TAG_ATT_CTXT 'aftN'
#define TAG_FILE_REC 'rftN'
#define TAG_FCB 'FftN'

CACHE_MANAGER_CALLBACKS CacheMgrCallbacks;
FAST_IO_DISPATCH FastIoDispatch;
NPAGED_LOOKASIDE_LIST IrpContextLookasideList;
NPAGED_LOOKASIDE_LIST FcbLookasideList;
NPAGED_LOOKASIDE_LIST AttrCtxtLookasideList;
PDRIVER_OBJECT NtfsDriverObject;
/* FUNCTIONS ****************************************************************/
NTSTATUS
NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
            _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;
    UNICODE_STRING UnicodeString;
    NtfsDriverObject = DriverObject;
    UNREFERENCED_PARAMETER(RegistryPath);
    RtlInitUnicodeString(&UnicodeString, L"\\Ntfs");
    Status = IoCreateDevice(DriverObject,
                            0,
                            &UnicodeString,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &NtfsDiskFileSystemDeviceObject);
    if (!NT_SUCCESS( Status )) {
        DPRINT("NtfsDriverEntry: Failed with Status %X\n", Status);
        return Status;
    }
    DriverObject->DriverUnload = NtfsUnload;

    DriverObject->MajorFunction[IRP_MJ_CREATE]                   = NtfsFsdCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]                    = NtfsFsdClose;
    DriverObject->MajorFunction[IRP_MJ_READ]                     = NtfsFsdRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE]                    = NtfsFsdWrite;
    DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION]        = NtfsFsdQueryInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION]          = NtfsFsdSetInformation;
    DriverObject->MajorFunction[IRP_MJ_QUERY_EA]                 = NtfsFsdQueryEa;
    DriverObject->MajorFunction[IRP_MJ_SET_EA]                   = NtfsFsdSetEa;
    DriverObject->MajorFunction[IRP_MJ_QUERY_SECURITY]           = NtfsFsdQuerySecurity;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS]            = NtfsFsdFlushBuffers;
    DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = NtfsFsdQueryVolumeInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION]   = NtfsFsdSetVolumeInformation;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]                  = NtfsFsdCleanup;
    DriverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL]        = NtfsFsdDirectoryControl;
    DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL]      = NtfsFsdFileSystemControl;
    DriverObject->MajorFunction[IRP_MJ_LOCK_CONTROL]             = NtfsFsdLockControl;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]           = NtfsFsdDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN]                 = NtfsFsdShutdown;
    //DriverObject->MajorFunction[IRP_MJ_PNP]                      = NtfsFsdPnp;

    // Do not set DO_DIRECT_IO/DO_BUFFERED_IO flags on the FS control device
    // The I/O manager and Cache Manager will decide buffering for file I/O

    // Initialize FastIo dispatch table
    RtlZeroMemory(&FastIoDispatch, sizeof(FAST_IO_DISPATCH));
    FastIoDispatch.SizeOfFastIoDispatch = sizeof(FAST_IO_DISPATCH);
    FastIoDispatch.FastIoCheckIfPossible = NtfsFastIoCheckIfPossible;
    // Defer to CopyRead/CopyWrite if we later support it, for now return FALSE
    FastIoDispatch.FastIoRead = NtfsFastIoRead;
    FastIoDispatch.FastIoWrite = NtfsFastIoWrite;
    FastIoDispatch.FastIoQueryBasicInfo = NtfsFastIoQueryBasicInfo;
    FastIoDispatch.FastIoQueryStandardInfo = NtfsFastIoQueryStandardInfo;
    FastIoDispatch.FastIoQueryNetworkOpenInfo = NtfsFastIoQueryNetworkOpenInfo;
    FastIoDispatch.AcquireFileForNtCreateSection = (PFAST_IO_ACQUIRE_FILE)NtfsFastIoAcquireFileForNtCreateSection;
    FastIoDispatch.ReleaseFileForNtCreateSection = (PFAST_IO_RELEASE_FILE)NtfsFastIoReleaseFileForNtCreateSection;
    FastIoDispatch.FastIoDetachDevice = (PFAST_IO_DETACH_DEVICE)NtfsFastIoDetachDevice;
    FastIoDispatch.FastIoQueryOpen = NtfsFastIoQueryOpen;

    // Register Fast I/O dispatch at driver level so all created FS device objects inherit it
    DriverObject->FastIoDispatch = &FastIoDispatch;

    // Get global driver settings from registry
    GetGlobalSettingsFromRegistry();

    // Register file system
    IoRegisterFileSystem(NtfsDiskFileSystemDeviceObject);
    ObReferenceObject(NtfsDiskFileSystemDeviceObject);

    return STATUS_SUCCESS;
}

_Function_class_(IRP_MJ_LOCK_CONTROL)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdLockControl(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                   _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Handles lock and unlock requests.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-lock-control
     */
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFileContextBlock FileCB = IrpSp->FileObject
        ? (PFileContextBlock)IrpSp->FileObject->FsContext
        : NULL;

    if (!FileCB || !FileCB->StreamCB)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    /* The lock package owns the request from here, including completing it. */
    return FsRtlProcessFileLock(&FileCB->StreamCB->FileLock, Irp, NULL);
}

_Function_class_(IRP_MJ_DEVICE_CONTROL)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdDeviceControl(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                     _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Determine if volume is open.
     * If it is, pass the IRP to the appropriate storage driver.
     * If not, fail the IRP.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-device-control
     */

    // Shamelessly ripped from the old driver.

    PVolumeContextBlock DeviceExt;

    DeviceExt = (PVolumeContextBlock)(VolumeDeviceObject->DeviceExtension);
    IoSkipCurrentIrpStackLocation(Irp);

    /* Lower driver will complete - we don't have to */
    // IrpContext->Flags &= ~IRPCONTEXT_COMPLETE;

    return IoCallDriver(DeviceExt->StorageDevice, Irp);
}

_Function_class_(IRP_MJ_SHUTDOWN)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdShutdown (_In_ PDEVICE_OBJECT VolumeDeviceObject,
                 _Inout_ PIRP Irp)
{
    /* Last chance to commit metadata the library is still holding. */
    NtfsDiskFlushKm();

    /* Overview:
     * Occurs when the system is being shutdown.
     * Do any cleanup needed and return STATUS_SUCCESS.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-shutdown
     */
    UNREFERENCED_PARAMETER(VolumeDeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    return STATUS_SUCCESS;
}

_Function_class_(DRIVER_UNLOAD)
VOID
NTAPI
NtfsUnload(_In_ _Unreferenced_parameter_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    ObDereferenceObject(NtfsDiskFileSystemDeviceObject);
}

_Function_class_(IRP_MJ_CLEANUP)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdCleanup(_In_ PDEVICE_OBJECT VolumeDeviceObject,
               _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * If the device object is the control device, complete the IRP.
     * Otherwise, perform any cleanup as needed.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-cleanup
     */
    PIO_STACK_LOCATION IrpSp;
    PFileContextBlock FileCB;

    if (VolumeDeviceObject == NtfsDiskFileSystemDeviceObject)
    {
        // DeviceObject represents FileSystem
        Irp->IoStatus.Information = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileCB = (PFileContextBlock)IrpSp->FileObject->FsContext;

    // Do not free the FCB/stream structures here. Cleanup is called when the
    // last handle is closed, but the file object may still be referenced by
    // the cache/section. The actual deallocation is done on IRP_MJ_CLOSE.
    if (FileCB)
    {
        PVolumeContextBlock VolCB =
            (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;

        if (FileCB->FileDir && VolCB->NotifySync)
        {
            KeEnterCriticalRegion();
            FsRtlNotifyCleanup(VolCB->NotifySync,
                               &VolCB->NotifyList,
                               FileCB);
            KeLeaveCriticalRegion();
        }
        if (FileCB->StreamCB)
        {
            // Byte-range locks belong to the handle, so they end with it.
            FsRtlFastUnlockAll(&FileCB->StreamCB->FileLock,
                               IrpSp->FileObject,
                               IoGetRequestorProcess(Irp),
                               NULL);
        }

        /* The handle is going away, so a requested delete happens now. */
        if ((FileCB->DeletePending ||
             (FileCB->CreateOptions & FILE_DELETE_ON_CLOSE)) &&
            VolCB->DiskVolume &&
            !NtfsVolumeIsReadOnly(VolCB->DiskVolume) &&
            FileCB->FileName.Length != 0)
        {
            BOOLEAN IsDirectory =
                !!(NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY);
            NTSTATUS DeleteStatus;

            /*
             * Cached pages of a file that is about to stop existing must go
             * before it does, or the cache manager keeps trying to write them
             * back to a record that has been freed.
             */
            if (IrpSp->FileObject->SectionObjectPointer)
            {
                IrpSp->FileObject->SectionObjectPointer->ImageSectionObject = NULL;
                if (IrpSp->FileObject->PrivateCacheMap)
                {
                    LARGE_INTEGER Empty = { { 0, 0 } };

                    CcUninitializeCacheMap(IrpSp->FileObject, &Empty, NULL);
                }
                /* TRUE also tears down the shared map, which outlives the
                 * private one and is what keeps retrying the write-back. */
                CcPurgeCacheSection(IrpSp->FileObject->SectionObjectPointer,
                                    NULL, 0, TRUE);
            }

            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&VolCB->MetadataResource, TRUE);
            DeleteStatus = NtfsMasterFileTableDeleteFile(
                NtfsVolumeGetMft(VolCB->DiskVolume),
                FileCB->FileName.Buffer,
                FileCB->FileName.Length / sizeof(WCHAR),
                IsDirectory);
            ExReleaseResourceLite(&VolCB->MetadataResource);
            KeLeaveCriticalRegion();

            /* On success the library has already released the record set. */
            NtfsEvictCachedRecord(VolCB,
                                  FileCB->FileName.Buffer,
                                  (USHORT)(FileCB->FileName.Length / sizeof(WCHAR)),
                                  NT_SUCCESS(DeleteStatus));
            if (NT_SUCCESS(DeleteStatus))
                FileCB->FileRec = NULL;

            if (!NT_SUCCESS(DeleteStatus))
                DPRINT1("NtfsFsdCleanup: delete failed 0x%08lx\n", DeleteStatus);
        }
    }

    // TODO: How do we determine when the volume needs to get cleaned up?

    Irp->IoStatus.Information = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}
