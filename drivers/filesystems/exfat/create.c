/*
 * PROJECT:     ReactOS exFAT filesystem driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     File creation, cleanup, and close operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "exfat.h"

#define NDEBUG
#include <debug.h>

static BOOLEAN
ExFatDispositionCreatesFile(
    ULONG Disposition)
{
    return Disposition == FILE_CREATE ||
           Disposition == FILE_OPEN_IF ||
           Disposition == FILE_OVERWRITE_IF ||
           Disposition == FILE_SUPERSEDE;
}

static NTSTATUS
ExFatSetShareAccess(
    PEXFAT_FCB Fcb,
    PEXFAT_CCB Ccb,
    PFILE_OBJECT FileObject,
    ACCESS_MASK DesiredAccess,
    ULONG ShareAccess)
{
    NTSTATUS Status;

    if (Fcb->OpenHandleCount)
    {
        Status = IoCheckShareAccess(DesiredAccess,
                                    ShareAccess,
                                    FileObject,
                                    &Fcb->ShareAccess,
                                    TRUE);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    else
    {
        IoSetShareAccess(DesiredAccess,
                         ShareAccess,
                         FileObject,
                         &Fcb->ShareAccess);
    }

    Ccb->ShareAccessSet = TRUE;
    return STATUS_SUCCESS;
}

static VOID
ExFatAttachFileObject(
    PEXFAT_FCB Fcb,
    PEXFAT_CCB Ccb,
    PFILE_OBJECT FileObject)
{
    FileObject->FsContext = Fcb;
    FileObject->FsContext2 = Ccb;
    FileObject->SectionObjectPointer = &Fcb->SectionObjectPointers;
    Ccb->FileObject = FileObject;
}

static NTSTATUS
ExFatOpenVolume(
    PEXFAT_VCB Vcb,
    PFILE_OBJECT FileObject,
    PIO_STACK_LOCATION Stack,
    ACCESS_MASK DesiredAccess)
{
    PEXFAT_CCB Ccb;
    NTSTATUS Status;

    if (Vcb->ReadOnly && ExFatIsWriteAccess(DesiredAccess))
        return STATUS_MEDIA_WRITE_PROTECTED;

    Ccb = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Ccb), TAG_EXFAT_CCB);
    if (!Ccb)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Ccb, sizeof(*Ccb));
    Ccb->NodeTypeCode = EXFAT_CCB_SIGNATURE;
    Ccb->NodeByteSize = sizeof(*Ccb);
    Ccb->DesiredAccess = DesiredAccess;

    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    if (Vcb->Locked && Vcb->LockOwner != FileObject)
    {
        Status = STATUS_ACCESS_DENIED;
        goto Failure;
    }

    ExFatReferenceFcb(Vcb->VolumeFcb);
    Status = ExFatSetShareAccess(Vcb->VolumeFcb,
                                 Ccb,
                                 FileObject,
                                 DesiredAccess,
                                 Stack->Parameters.Create.ShareAccess);
    if (!NT_SUCCESS(Status))
    {
        ExFatDereferenceFcb(Vcb->VolumeFcb);
        goto Failure;
    }

    Vcb->VolumeFcb->OpenHandleCount++;
    Vcb->OpenHandleCount++;
    ExFatAttachFileObject(Vcb->VolumeFcb, Ccb, FileObject);
    FileObject->Flags |= FO_NO_INTERMEDIATE_BUFFERING;
    ExReleaseResourceLite(&Vcb->Resource);
    return STATUS_SUCCESS;

Failure:
    ExReleaseResourceLite(&Vcb->Resource);
    ExFreePoolWithTag(Ccb, TAG_EXFAT_CCB);
    return Status;
}

NTSTATUS
ExFatCreate(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXFAT_VCB Vcb;
    PEXFAT_FCB Fcb = NULL;
    PEXFAT_CCB Ccb = NULL;
    UNICODE_STRING FullPath;
    PCHAR FatPath = NULL;
    FILINFO Information;
    ACCESS_MASK DesiredAccess;
    ULONG Options;
    ULONG Disposition;
    ULONG CreateInformation = 0;
    BYTE OpenMode;
    FRESULT Result;
    NTSTATUS Status;
    BOOLEAN Exists;
    BOOLEAN IsRoot;
    BOOLEAN IsDirectory;
    BOOLEAN DirectoryRequested;
    BOOLEAN ShareSet = FALSE;
    BOOLEAN FcbResourceAcquired = FALSE;
    IO_STATUS_BLOCK CacheIoStatus;
    LARGE_INTEGER ZeroSize;

    if (DeviceObject == ExFatGlobalData->DeviceObject)
    {
        if (FileObject && FileObject->FileName.Length)
            return STATUS_OBJECT_PATH_NOT_FOUND;
        Irp->IoStatus.Information = FILE_OPENED;
        return STATUS_SUCCESS;
    }

    Vcb = DeviceObject->DeviceExtension;
    if (!Vcb->Mounted)
        return STATUS_VOLUME_DISMOUNTED;

    DesiredAccess = Stack->Parameters.Create.SecurityContext->DesiredAccess;
    Options = Stack->Parameters.Create.Options & 0x00FFFFFF;
    Disposition = (Stack->Parameters.Create.Options >> 24) & 0xFF;

    Status = ExFatBuildFullPath(FileObject, &FullPath);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!FullPath.Length)
    {
        Status = ExFatOpenVolume(Vcb, FileObject, Stack, DesiredAccess);
        if (NT_SUCCESS(Status))
            Irp->IoStatus.Information = FILE_OPENED;
        return Status;
    }

    FatPath = ExFatBuildFatPath(Vcb, &FullPath);
    if (!FatPath)
    {
        ExFatFreeUnicodeString(&FullPath);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Ccb = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Ccb), TAG_EXFAT_CCB);
    if (!Ccb)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto FailureWithoutLock;
    }
    RtlZeroMemory(Ccb, sizeof(*Ccb));
    Ccb->NodeTypeCode = EXFAT_CCB_SIGNATURE;
    Ccb->NodeByteSize = sizeof(*Ccb);
    Ccb->DesiredAccess = DesiredAccess;
    Ccb->DeleteOnClose = !!(Options & FILE_DELETE_ON_CLOSE);

    DirectoryRequested = !!(Options & FILE_DIRECTORY_FILE);
    IsRoot = (FullPath.Length == sizeof(WCHAR) && FullPath.Buffer[0] == L'\\');

    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    if (Vcb->Locked && Vcb->LockOwner != FileObject)
    {
        Status = STATUS_ACCESS_DENIED;
        goto Failure;
    }
    if (Vcb->ReadOnly &&
        (ExFatIsWriteAccess(DesiredAccess) || ExFatDispositionCreatesFile(Disposition) ||
         Disposition == FILE_OVERWRITE))
    {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto Failure;
    }

    RtlZeroMemory(&Information, sizeof(Information));
    ExFatAcquireFatFs(Vcb);
    if (IsRoot)
    {
        Result = FR_OK;
        Information.fattrib = AM_DIR;
    }
    else
    {
        Result = f_stat(FatPath, &Information);
    }
    ExFatReleaseFatFs(Vcb);
    Exists = (Result == FR_OK);
    if (!Exists && Result != FR_NO_FILE && Result != FR_NO_PATH)
    {
        Status = ExFatMapResult(Result);
        goto Failure;
    }

    IsDirectory = Exists && !!(Information.fattrib & AM_DIR);
    if (Exists && IsDirectory && (Options & FILE_NON_DIRECTORY_FILE))
    {
        Status = STATUS_FILE_IS_A_DIRECTORY;
        goto Failure;
    }
    if (Exists && !IsDirectory && DirectoryRequested)
    {
        Status = STATUS_NOT_A_DIRECTORY;
        goto Failure;
    }
    if (Exists && Disposition == FILE_CREATE)
    {
        Status = STATUS_OBJECT_NAME_COLLISION;
        goto Failure;
    }
    if (!Exists && (Disposition == FILE_OPEN || Disposition == FILE_OVERWRITE))
    {
        Status = (Result == FR_NO_PATH) ? STATUS_OBJECT_PATH_NOT_FOUND : STATUS_OBJECT_NAME_NOT_FOUND;
        goto Failure;
    }
    if (IsRoot && Disposition != FILE_OPEN && Disposition != FILE_OPEN_IF)
    {
        Status = STATUS_OBJECT_NAME_COLLISION;
        goto Failure;
    }

    if (Exists)
    {
        Fcb = ExFatFindFcb(Vcb, &FullPath);
        if (!Fcb)
            Fcb = ExFatCreateFcb(Vcb, &FullPath, &Information, FALSE);
        if (!Fcb)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Failure;
        }
        if (Fcb->DeletePending)
        {
            Status = STATUS_DELETE_PENDING;
            goto Failure;
        }

        Status = ExFatSetShareAccess(Fcb,
                                     Ccb,
                                     FileObject,
                                     DesiredAccess,
                                     Stack->Parameters.Create.ShareAccess);
        if (!NT_SUCCESS(Status))
            goto Failure;
        ShareSet = TRUE;
    }

    if (DirectoryRequested || IsDirectory)
    {
        if (!Exists)
        {
            ExFatAcquireFatFs(Vcb);
            Result = f_mkdir(FatPath);
            ExFatReleaseFatFs(Vcb);
            if (Result != FR_OK)
            {
                Status = ExFatMapResult(Result);
                goto Failure;
            }

            /* Fresh directory: no need to rescan the parent with f_stat(). */
            {
                DWORD Now = get_fattime();

                RtlZeroMemory(&Information, sizeof(Information));
                Information.fattrib = AM_DIR;
                Information.fdate = (WORD)(Now >> 16);
                Information.ftime = (WORD)Now;
                Information.crdate = Information.fdate;
                Information.crtime = Information.ftime;
            }
            CreateInformation = FILE_CREATED;
        }
        else
        {
            CreateInformation = FILE_OPENED;
        }

        ExFatAcquireFatFs(Vcb);
        Result = f_opendir(&Ccb->Handle.Directory, FatPath);
        ExFatReleaseFatFs(Vcb);
        Ccb->IsDirectory = TRUE;
        Ccb->HandleOpen = (Result == FR_OK);
    }
    else
    {
        OpenMode = FA_READ;
        if (ExFatIsWriteAccess(DesiredAccess) || !Exists ||
            Disposition == FILE_OVERWRITE || Disposition == FILE_OVERWRITE_IF ||
            Disposition == FILE_SUPERSEDE)
        {
            OpenMode |= FA_WRITE;
        }

        switch (Disposition)
        {
            case FILE_CREATE:
                OpenMode |= FA_CREATE_NEW;
                CreateInformation = FILE_CREATED;
                break;
            case FILE_OPEN:
                OpenMode |= FA_OPEN_EXISTING;
                CreateInformation = FILE_OPENED;
                break;
            case FILE_OPEN_IF:
                OpenMode |= FA_OPEN_ALWAYS;
                CreateInformation = Exists ? FILE_OPENED : FILE_CREATED;
                break;
            case FILE_OVERWRITE:
                OpenMode |= FA_CREATE_ALWAYS;
                CreateInformation = FILE_OVERWRITTEN;
                break;
            case FILE_OVERWRITE_IF:
                OpenMode |= FA_CREATE_ALWAYS;
                CreateInformation = Exists ? FILE_OVERWRITTEN : FILE_CREATED;
                break;
            case FILE_SUPERSEDE:
                OpenMode |= FA_CREATE_ALWAYS;
                CreateInformation = Exists ? FILE_SUPERSEDED : FILE_CREATED;
                break;
            default:
                Status = STATUS_INVALID_PARAMETER;
                goto Failure;
        }

        if (Fcb && CreateInformation != FILE_OPENED)
        {
            ZeroSize.QuadPart = 0;
            if (!MmCanFileBeTruncated(&Fcb->SectionObjectPointers, &ZeroSize))
            {
                Status = STATUS_USER_MAPPED_FILE;
                goto Failure;
            }

            ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
            FcbResourceAcquired = TRUE;
            if (Fcb->SectionObjectPointers.DataSectionObject)
            {
                CcFlushCache(&Fcb->SectionObjectPointers, NULL, 0, &CacheIoStatus);
                if (!NT_SUCCESS(CacheIoStatus.Status) ||
                    !CcPurgeCacheSection(&Fcb->SectionObjectPointers, NULL, 0, FALSE))
                {
                    Status = NT_SUCCESS(CacheIoStatus.Status) ?
                             STATUS_USER_MAPPED_FILE : CacheIoStatus.Status;
                    ExReleaseResourceLite(&Fcb->MainResource);
                    FcbResourceAcquired = FALSE;
                    goto Failure;
                }
            }
        }

        ExFatAcquireFatFs(Vcb);
        Result = FR_OK;
        if (Fcb && CreateInformation != FILE_OPENED && Fcb->FatFileOpen)
        {
            ExFatInvalidateFcbClusterMap(Fcb);
            Result = f_close(&Fcb->FatFile);
            Fcb->FatFileOpen = FALSE;
            Fcb->FatFileWritable = FALSE;
        }
        if (Result == FR_OK)
            Result = f_open(&Ccb->Handle.File, FatPath, OpenMode);
        if (Result == FR_OK)
        {
            Ccb->HandleOpen = TRUE;
            if (CreateInformation != FILE_OPENED)
            {
                Result = f_sync(&Ccb->Handle.File);
                if (Result == FR_OK)
                {
                    /*
                     * The entry FatFs just wrote is fully determined: empty
                     * file, archive bit, stamped now. Rescanning the parent
                     * directory with f_stat() would only re-read that.
                     */
                    DWORD Now = get_fattime();

                    RtlZeroMemory(&Information, sizeof(Information));
                    Information.fattrib = AM_ARC;
                    Information.fdate = (WORD)(Now >> 16);
                    Information.ftime = (WORD)Now;
                    Information.crdate = Information.fdate;
                    Information.crtime = Information.ftime;
                }
            }
        }
        ExFatReleaseFatFs(Vcb);
        if (FcbResourceAcquired)
        {
            ExReleaseResourceLite(&Fcb->MainResource);
            FcbResourceAcquired = FALSE;
        }
    }

    if (Result != FR_OK)
    {
        Status = ExFatMapResult(Result);
        goto Failure;
    }
    if (!Fcb)
    {
        Fcb = ExFatCreateFcb(Vcb, &FullPath, &Information, FALSE);
        if (!Fcb)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Failure;
        }
        Status = ExFatSetShareAccess(Fcb,
                                     Ccb,
                                     FileObject,
                                     DesiredAccess,
                                     Stack->Parameters.Create.ShareAccess);
        if (!NT_SUCCESS(Status))
            goto Failure;
        ShareSet = TRUE;
    }
    else if (CreateInformation != FILE_OPENED)
    {
        ExFatUpdateFcbFromInfo(Fcb, &Information);
    }

    Fcb->OpenHandleCount++;
    Vcb->OpenHandleCount++;
    if (Ccb->DeleteOnClose)
        Fcb->DeletePending = TRUE;
    ExFatAttachFileObject(Fcb, Ccb, FileObject);
    if (CreateInformation != FILE_OPENED && Fcb->SectionObjectPointers.SharedCacheMap)
        CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);
    if (!Fcb->IsDirectory)
    {
        if (Options & FILE_NO_INTERMEDIATE_BUFFERING)
            FileObject->Flags |= FO_NO_INTERMEDIATE_BUFFERING;
        else
            FileObject->Flags |= FO_CACHE_SUPPORTED;
    }
    ExReleaseResourceLite(&Vcb->Resource);

    Irp->IoStatus.Information = CreateInformation;
    ExFreePoolWithTag(FatPath, TAG_EXFAT_PATH);
    ExFatFreeUnicodeString(&FullPath);
    return STATUS_SUCCESS;

Failure:
    if (Ccb && Ccb->HandleOpen)
    {
        ExFatAcquireFatFs(Vcb);
        if (Ccb->IsDirectory)
            f_closedir(&Ccb->Handle.Directory);
        else
            f_close(&Ccb->Handle.File);
        ExFatReleaseFatFs(Vcb);
    }
    if (ShareSet && Fcb)
        IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);
    if (Fcb)
        ExFatDereferenceFcb(Fcb);
    ExReleaseResourceLite(&Vcb->Resource);

FailureWithoutLock:
    if (Ccb)
        ExFreePoolWithTag(Ccb, TAG_EXFAT_CCB);
    if (FatPath)
        ExFreePoolWithTag(FatPath, TAG_EXFAT_PATH);
    ExFatFreeUnicodeString(&FullPath);
    return Status;
}

NTSTATUS
ExFatCleanup(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXFAT_VCB Vcb;
    PEXFAT_FCB Fcb;
    PEXFAT_CCB Ccb;

    if (DeviceObject == ExFatGlobalData->DeviceObject || !FileObject)
        return STATUS_SUCCESS;
    Vcb = DeviceObject->DeviceExtension;
    Fcb = FileObject->FsContext;
    Ccb = FileObject->FsContext2;
    if (!Fcb || !Ccb || Ccb->CleanedUp)
        return STATUS_SUCCESS;

    if (!Fcb->IsDirectory && !Fcb->IsVolume && FileObject->PrivateCacheMap)
        CcUninitializeCacheMap(FileObject, NULL, NULL);

    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    if (Ccb->ShareAccessSet)
    {
        IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);
        Ccb->ShareAccessSet = FALSE;
    }
    if (Fcb->OpenHandleCount)
        Fcb->OpenHandleCount--;
    if (Vcb->OpenHandleCount)
        Vcb->OpenHandleCount--;
    if (Vcb->LockOwner == FileObject)
    {
        Vcb->Locked = FALSE;
        Vcb->LockOwner = NULL;
    }
    FsRtlFastUnlockAll(&Fcb->FileLock,
                       FileObject,
                       IoGetRequestorProcess(Irp),
                       NULL);
    Ccb->CleanedUp = TRUE;
    FileObject->Flags |= FO_CLEANUP_COMPLETE;
    ExReleaseResourceLite(&Vcb->Resource);
    return STATUS_SUCCESS;
}

NTSTATUS
ExFatClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXFAT_VCB Vcb;
    PEXFAT_FCB Fcb;
    PEXFAT_CCB Ccb;
    PCHAR FatPath = NULL;

    if (DeviceObject == ExFatGlobalData->DeviceObject || !FileObject)
        return STATUS_SUCCESS;
    Vcb = DeviceObject->DeviceExtension;
    Fcb = FileObject->FsContext;
    Ccb = FileObject->FsContext2;
    if (!Fcb || !Ccb)
        return STATUS_SUCCESS;

    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    if (!Ccb->CleanedUp)
    {
        if (Ccb->ShareAccessSet)
            IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);
        if (Fcb->OpenHandleCount)
            Fcb->OpenHandleCount--;
        if (Vcb->OpenHandleCount)
            Vcb->OpenHandleCount--;
    }

    if (Ccb->HandleOpen)
    {
        ExFatAcquireFatFs(Vcb);
        if (Ccb->IsDirectory)
            f_closedir(&Ccb->Handle.Directory);
        else
            f_close(&Ccb->Handle.File);
        ExFatReleaseFatFs(Vcb);
        Ccb->HandleOpen = FALSE;
    }

    if (Fcb->DeletePending && !Fcb->IsVolume && Fcb->OpenHandleCount == 0)
    {
        FatPath = ExFatBuildFatPath(Vcb, &Fcb->PathName);
        if (FatPath)
        {
            ExFatAcquireFatFs(Vcb);
            if (Fcb->FatFileOpen)
            {
                ExFatInvalidateFcbClusterMap(Fcb);
                f_close(&Fcb->FatFile);
                Fcb->FatFileOpen = FALSE;
                Fcb->FatFileWritable = FALSE;
            }
            if (f_unlink(FatPath) == FR_OK)
                Fcb->DeletePending = TRUE;
            ExFatReleaseFatFs(Vcb);
            ExFreePoolWithTag(FatPath, TAG_EXFAT_PATH);
        }
    }

    ExFatFreeUnicodeString(&Ccb->SearchPattern);
    FileObject->FsContext = NULL;
    FileObject->FsContext2 = NULL;
    FileObject->SectionObjectPointer = NULL;
    ExFreePoolWithTag(Ccb, TAG_EXFAT_CCB);
    ExFatDereferenceFcb(Fcb);
    ExReleaseResourceLite(&Vcb->Resource);
    return STATUS_SUCCESS;
}
