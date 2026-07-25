/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file close handling
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

static VOID
NtfsUnlockProcessFileLocks(_Inout_ PFileContextBlock File,
                           _In_ PFILE_OBJECT FileObject,
                           _In_ PEPROCESS Process)
{
    PFILE_LOCK_INFO LockInfo;
    BOOLEAN Restart;
    ULONG Key;
    NTSTATUS Status;

    if (!File->FileLock.LockInformation)
        return;

    for (;;) {
        LockInfo = NULL;
        Restart = TRUE;
        while ((LockInfo = FsRtlGetNextFileLock(
                    &File->FileLock, Restart)) != NULL) {
            Restart = FALSE;
            if (LockInfo->ProcessId == Process)
                break;
        }
        if (!LockInfo)
            break;

        /*
         * ReactOS's FsRtlFastUnlockAll currently drops ranges belonging to
         * every process.  Releasing one owned key at a time preserves locks
         * held by other processes and also handles native callers that use
         * nonzero lock keys.
         */
        Key = LockInfo->Key;
        Status = FsRtlFastUnlockAllByKey(&File->FileLock,
                                         FileObject,
                                         Process,
                                         Key,
                                         NULL);
        if (!NT_SUCCESS(Status))
            break;
    }
}

VOID
NtfsReferenceFcb(_Inout_ PFileContextBlock File)
{
    ASSERT(File->ReferenceCount > 0);
    InterlockedIncrement(&File->ReferenceCount);
}

VOID
NtfsDereferenceFcb(_Inout_ PFileContextBlock File)
{
    PVolumeContextBlock Volume = File->Volume;
    int Result;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Volume->FcbListResource));
    ASSERT(File->ReferenceCount > 0);
    if (InterlockedDecrement(&File->ReferenceCount) != 0)
        return;

    ASSERT(File->OpenHandleCount == 0);
    ASSERT(File->SectionObjectPointers.SharedCacheMap == NULL);
    ASSERT(File->SectionObjectPointers.DataSectionObject == NULL);
    ASSERT(File->SectionObjectPointers.ImageSectionObject == NULL);
    if (File->IsVolume) {
        ASSERT(Volume->VolumeFcb == File);
        Volume->VolumeFcb = NULL;
    }
    RemoveEntryList(&File->ListEntry);
    if (File->File) {
        if (File->DeletePending && !File->DeleteCompleted) {
            Result = Ntfs3gRosDeleteFile(File->File);
            if (Result < 0) {
                DbgPrintEx(DPFLTR_NTFS_ID,
                           DPFLTR_ERROR_LEVEL,
                           "NTFS3G: failed to delete %wZ: %d\n",
                           &File->FileName,
                           -Result);
            }
        } else {
            Ntfs3gRosCloseFile(File->File);
        }
        File->File = NULL;
    }
    if (File->FileName.Buffer)
        ExFreePoolWithTag(File->FileName.Buffer, TAG_NTFS);
    FsRtlTeardownPerStreamContexts(&File->CommonFCBHeader);
    ExDeleteResourceLite(&File->MainResource);
    ExDeleteResourceLite(&File->PagingIoResource);
    FsRtlUninitializeFileLock(&File->FileLock);
    ExFreePoolWithTag(File, TAG_NTFS);
}

VOID
NtfsCleanupFileObject(_Inout_ PFILE_OBJECT FileObject,
                      _In_opt_ PEPROCESS Process)
{
    PFileContextBlock File = FileObject->FsContext;
    PHandleContextBlock Handle = FileObject->FsContext2;
    PVolumeContextBlock Volume;
    NTFS3G_ROS_FILE *DirectoryFile = NULL;
    NTFS3G_ROS_FILE *DeleteFile = NULL;
    UNICODE_STRING DirectoryPattern;
    IO_STATUS_BLOCK IoStatus;
    BOOLEAN PurgeFile = FALSE;
    BOOLEAN TrimFile = FALSE;
    BOOLEAN NotifyUnlock = FALSE;
    KIRQL VpbIrql;
    int DeleteResult;

    RtlZeroMemory(&DirectoryPattern, sizeof(DirectoryPattern));
    if (!File || !Handle || Handle->CleanupComplete)
        return;

    if (!(File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY) &&
        FileObject->PrivateCacheMap)
        CcUninitializeCacheMap(FileObject, NULL, NULL);

    Volume = File->Volume;
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Volume->FcbListResource, TRUE);
    if (Handle->CleanupComplete) {
        ExReleaseResourceLite(&Volume->FcbListResource);
        KeLeaveCriticalRegion();
        return;
    }

    if (Handle->ShareAccessSet) {
        IoRemoveShareAccess(FileObject, &File->ShareAccess);
        Handle->ShareAccessSet = FALSE;
    }
    if (File->OpenHandleCount)
        File->OpenHandleCount--;
    if (Volume->LockFileObject == FileObject) {
        Volume->LockFileObject = NULL;
        if (FileObject->Vpb) {
            IoAcquireVpbSpinLock(&VpbIrql);
            FileObject->Vpb->Flags &= ~VPB_LOCKED;
            if (!Volume->Dismounted)
                FileObject->Vpb->Flags &=
                    ~VPB_DIRECT_WRITES_ALLOWED;
            IoReleaseVpbSpinLock(VpbIrql);
        }
        NotifyUnlock = !Volume->Dismounted;
    }

    NtfsUnlockProcessFileLocks(
        File,
        FileObject,
        Process ? Process : PsGetCurrentProcess());

    ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
    DirectoryFile = Handle->DirectoryFile;
    Handle->DirectoryFile = NULL;
    DirectoryPattern = Handle->DirectoryPattern;
    RtlZeroMemory(&Handle->DirectoryPattern,
                  sizeof(Handle->DirectoryPattern));
    if (File->DeletePending &&
        File->OpenHandleCount == 0 &&
        !File->File &&
        DirectoryFile) {
        File->File = DirectoryFile;
        DirectoryFile = NULL;
    }
    Handle->CleanupComplete = TRUE;
    FileObject->Flags |= FO_CLEANUP_COMPLETE;
    if (File->DeletePending &&
        File->OpenHandleCount == 0 &&
        !File->DeleteCompleted &&
        File->File &&
        File->SectionObjectPointers.SharedCacheMap == NULL &&
        File->SectionObjectPointers.DataSectionObject == NULL &&
        File->SectionObjectPointers.ImageSectionObject == NULL) {
        DeleteFile = File->File;
        File->File = NULL;
        File->DeleteCompleted = TRUE;
    }
    PurgeFile = File->DeletePending &&
                File->OpenHandleCount == 0 &&
                !File->DeleteCompleted &&
                File->File != NULL &&
                (File->SectionObjectPointers.DataSectionObject ||
                 File->SectionObjectPointers.SharedCacheMap ||
                 File->SectionObjectPointers.ImageSectionObject);
    if (!PurgeFile) {
        TrimFile = File->OpenHandleCount == 0 &&
                   File->File != NULL &&
                   (File->SectionObjectPointers.DataSectionObject ||
                    File->SectionObjectPointers.SharedCacheMap ||
                    File->SectionObjectPointers.ImageSectionObject);
    }
    ExReleaseResourceLite(&File->MainResource);
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();

    if (PurgeFile) {
        IoStatus.Status = STATUS_SUCCESS;
        IoStatus.Information = 0;
        CcFlushCache(&File->SectionObjectPointers, NULL, 0, &IoStatus);
        if (NT_SUCCESS(IoStatus.Status)) {
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&File->PagingIoResource, TRUE);
            ExReleaseResourceLite(&File->PagingIoResource);
            KeLeaveCriticalRegion();
            CcPurgeCacheSection(&File->SectionObjectPointers,
                                NULL,
                                0,
                                FALSE);
        }

        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&Volume->FcbListResource, TRUE);
        ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
        if (NT_SUCCESS(IoStatus.Status) &&
            File->DeletePending &&
            File->OpenHandleCount == 0 &&
            !File->DeleteCompleted &&
            File->File &&
            File->SectionObjectPointers.SharedCacheMap == NULL &&
            File->SectionObjectPointers.DataSectionObject == NULL &&
            File->SectionObjectPointers.ImageSectionObject == NULL) {
            DeleteFile = File->File;
            File->File = NULL;
            File->DeleteCompleted = TRUE;
        }
        TrimFile = File->OpenHandleCount == 0 &&
                   File->File != NULL &&
                   (File->SectionObjectPointers.DataSectionObject ||
                    File->SectionObjectPointers.SharedCacheMap ||
                    File->SectionObjectPointers.ImageSectionObject);
        ExReleaseResourceLite(&File->MainResource);
        ExReleaseResourceLite(&Volume->FcbListResource);
        KeLeaveCriticalRegion();
    }

    if (DeleteFile) {
        DeleteResult = Ntfs3gRosDeleteFile(DeleteFile);
        if (DeleteResult < 0) {
            DbgPrintEx(DPFLTR_NTFS_ID,
                       DPFLTR_ERROR_LEVEL,
                       "NTFS3G: failed to delete %wZ: %d\n",
                       &File->FileName,
                       -DeleteResult);
        }
    }
    if (DirectoryFile)
        Ntfs3gRosCloseFile(DirectoryFile);
    if (DirectoryPattern.Buffer)
        ExFreePoolWithTag(DirectoryPattern.Buffer, TAG_NTFS);
    if (TrimFile)
        Ntfs3gRosTrimFile(File->File);
    if (NotifyUnlock)
        FsRtlNotifyVolumeEvent(
            FileObject, FSRTL_VOLUME_UNLOCK);
}

NTSTATUS
NTAPI
NtfsFsdClose(_In_ PDEVICE_OBJECT DeviceObject,
             _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File = NULL;
    PHandleContextBlock Handle = NULL;
    PVolumeContextBlock Volume;
    BOOLEAN DeleteVolumeDevice;
    KIRQL VpbIrql;

    if (!FileObject)
        return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

    File = FileObject->FsContext;
    Handle = FileObject->FsContext2;
    if (!File)
        return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
    if (Handle && !Handle->CleanupComplete)
        NtfsCleanupFileObject(FileObject, IoGetRequestorProcess(Irp));

    Volume = File->Volume;
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Volume->FcbListResource, TRUE);
    FileObject->FsContext = NULL;
    FileObject->FsContext2 = NULL;
    FileObject->SectionObjectPointer = NULL;
    if (Handle)
        ExFreePoolWithTag(Handle, TAG_NTFS);
    NtfsDereferenceFcb(File);
    DeleteVolumeDevice =
        Volume->Dismounted &&
        IsListEmpty(&Volume->FcbListHead);
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();
    NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

    if (DeleteVolumeDevice) {
        if (DeviceObject->Vpb) {
            IoAcquireVpbSpinLock(&VpbIrql);
            if (DeviceObject->Vpb->DeviceObject == DeviceObject)
                DeviceObject->Vpb->DeviceObject = NULL;
            DeviceObject->Vpb->Flags &=
                ~(VPB_MOUNTED |
                  VPB_LOCKED |
                  VPB_DIRECT_WRITES_ALLOWED);
            IoReleaseVpbSpinLock(VpbIrql);
        }
        if (Volume->ShutdownRegistered) {
            IoUnregisterShutdownNotification(DeviceObject);
            Volume->ShutdownRegistered = FALSE;
        }
        if (Volume->StreamFileObject) {
            ObDereferenceObject(Volume->StreamFileObject);
            Volume->StreamFileObject = NULL;
        }
        ExDeleteResourceLite(&Volume->FcbListResource);
        Volume->FcbListResourceInitialized = FALSE;
        IoDeleteDevice(DeviceObject);
    }
    return STATUS_SUCCESS;
}
