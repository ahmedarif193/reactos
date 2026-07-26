/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new file close APIs
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */


#include "ntfspch.h"

/* GLOBALS *****************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtfsFsdClose)
#endif

/* FUNCTIONS ****************************************************************/

_Function_class_(IRP_MJ_CLOSE)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdClose(_In_ PDEVICE_OBJECT VolumeDeviceObject,
             _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * All instances of a file object have been closed.
     * Do any processing required and complete the IRP.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-close
     */

    if (VolumeDeviceObject == NtfsDiskFileSystemDeviceObject)
    {
        /* DeviceObject represents FileSystem */
        Irp->IoStatus.Information = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    /* Perform final teardown of the file object's context. */
    {
        PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
        PFileContextBlock FileCB = (PFileContextBlock)IrpSp->FileObject->FsContext;
        if (FileCB)
        {
            // Cleanup normally tore the private cache map down already; a
            // file object closed without one still needs this.
            if (IrpSp->FileObject->PrivateCacheMap)
            {
                CcUninitializeCacheMap(IrpSp->FileObject, NULL, NULL);
            }

            /* Resources are kept alive with the block; see the reuse path
             * in create. They are only torn down when it is really freed. */

            if (FileCB->FileDir)
            {
                PVolumeContextBlock Vol =
                    (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
                PNtfsDirectory Doomed = FileCB->FileDir;

                ExAcquireFastMutex(&Vol->DirCacheMutex);
                if (FileCB->FileDirBorrowed)
                {
                    Vol->CachedDirBusy = FALSE;
                    Doomed = NULL;
                }
                else if ((!Vol->CachedDir ||
                          (!Vol->CachedDirBusy &&
                           Vol->CachedDirGeneration != Vol->DirGeneration)) &&
                         FileCB->FileName.Length != 0 &&
                         FileCB->FileName.Length <= sizeof(Vol->CachedDirPath))
                {
                    PNtfsDirectory Evicted = Vol->CachedDir;

                    Vol->CachedDir = FileCB->FileDir;
                    Vol->CachedDirBusy = FALSE;
                    Vol->CachedDirGeneration = Vol->DirGeneration;
                    Vol->CachedDirPathLength =
                        (USHORT)(FileCB->FileName.Length / sizeof(WCHAR));
                    RtlCopyMemory(Vol->CachedDirPath,
                                  FileCB->FileName.Buffer,
                                  FileCB->FileName.Length);
                    Doomed = Evicted;
                }
                ExReleaseFastMutex(&Vol->DirCacheMutex);

                if (Doomed)
                    NtfsDirectoryDestroy(Doomed);
            }

            /* A cached record outlives this handle and is freed by the cache. */
            if (FileCB->CachedRecord)
            {
                NtfsReleaseCachedRecord(
                    (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension,
                    FileCB->CachedRecord);
            }
            else if (FileCB->FileRec)
            {
                NtfsFileRecordDestroy(FileCB->FileRec);
            }

            if (FileCB->RequestedStream)
                ExFreePool(FileCB->RequestedStream);

            if (FileCB->StreamCB)
            {
                NtfsDereferenceStreamContext(
                    (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension,
                    FileCB->StreamCB);
            }

            if (FileCB->FileName.Buffer)
                ExFreePool(FileCB->FileName.Buffer);

            /* Keep the block, with its resources, for the next open. */
            {
                PVolumeContextBlock Vol =
                    (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
                BOOLEAN Kept = FALSE;

                ExAcquireFastMutex(&Vol->IdleFcbMutex);
                if (Vol->IdleFcbCount < NTFS_MAX_IDLE_FCBS)
                {
                    InsertHeadList(&Vol->IdleFcbList, &FileCB->IdleLink);
                    Vol->IdleFcbCount++;
                    Kept = TRUE;
                }
                ExReleaseFastMutex(&Vol->IdleFcbMutex);

                if (!Kept)
                {
                    ExDeleteResourceLite(&FileCB->MainResource);
                    ExDeleteResourceLite(&FileCB->PagingIoResource);
                    ExFreePool(FileCB);
                }
            }
            IrpSp->FileObject->FsContext = NULL;
        }
    }

    Irp->IoStatus.Information = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}
