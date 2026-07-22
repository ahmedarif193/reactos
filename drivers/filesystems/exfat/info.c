/*
 * PROJECT:     ReactOS exFAT filesystem driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     File and volume information operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "exfat.h"

#define NDEBUG
#include <debug.h>

static VOID
ExFatRefreshFcb(
    PEXFAT_FCB Fcb)
{
    PCHAR Path;
    FILINFO Information;

    if (Fcb->IsVolume)
        return;

    Path = ExFatBuildFatPath(Fcb->Vcb, &Fcb->PathName);
    if (!Path)
        return;
    RtlZeroMemory(&Information, sizeof(Information));
    ExFatAcquireFatFs(Fcb->Vcb);
    if (f_stat(Path, &Information) == FR_OK)
        ExFatUpdateFcbFromInfo(Fcb, &Information);
    ExFatReleaseFatFs(Fcb->Vcb);
    ExFreePoolWithTag(Path, TAG_EXFAT_PATH);
}

static NTSTATUS
ExFatQueryNameInformation(
    PEXFAT_FCB Fcb,
    PFILE_NAME_INFORMATION Information,
    ULONG Length,
    PULONG Written)
{
    ULONG HeaderLength = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
    ULONG CopyLength;

    if (Length < HeaderLength)
        return STATUS_BUFFER_TOO_SMALL;

    Information->FileNameLength = Fcb->PathName.Length;
    CopyLength = min((ULONG)Fcb->PathName.Length, Length - HeaderLength);
    if (CopyLength)
        RtlCopyMemory(Information->FileName, Fcb->PathName.Buffer, CopyLength);
    *Written = HeaderLength + CopyLength;
    return (CopyLength == Fcb->PathName.Length) ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
}

static VOID
ExFatFillBasicInformation(
    PEXFAT_FCB Fcb,
    PFILE_BASIC_INFORMATION Information)
{
    RtlZeroMemory(Information, sizeof(*Information));
    Information->CreationTime = Fcb->CreationTime;
    Information->LastAccessTime = Fcb->LastAccessTime;
    Information->LastWriteTime = Fcb->LastWriteTime;
    Information->ChangeTime = Fcb->ChangeTime;
    Information->FileAttributes = Fcb->FileAttributes;
}

static VOID
ExFatFillStandardInformation(
    PEXFAT_FCB Fcb,
    PFILE_STANDARD_INFORMATION Information)
{
    RtlZeroMemory(Information, sizeof(*Information));
    Information->AllocationSize = Fcb->Header.AllocationSize;
    Information->EndOfFile = Fcb->Header.FileSize;
    Information->NumberOfLinks = 1;
    Information->DeletePending = Fcb->DeletePending;
    Information->Directory = Fcb->IsDirectory;
}

static VOID
ExFatFillNetworkInformation(
    PEXFAT_FCB Fcb,
    PFILE_NETWORK_OPEN_INFORMATION Information)
{
    RtlZeroMemory(Information, sizeof(*Information));
    Information->CreationTime = Fcb->CreationTime;
    Information->LastAccessTime = Fcb->LastAccessTime;
    Information->LastWriteTime = Fcb->LastWriteTime;
    Information->ChangeTime = Fcb->ChangeTime;
    Information->AllocationSize = Fcb->Header.AllocationSize;
    Information->EndOfFile = Fcb->Header.FileSize;
    Information->FileAttributes = Fcb->FileAttributes;
}

NTSTATUS
ExFatQueryInformation(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXFAT_FCB Fcb;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = Stack->Parameters.QueryFile.Length;
    ULONG Written = 0;
    NTSTATUS Status;

    if (DeviceObject == ExFatGlobalData->DeviceObject || !FileObject || !Buffer)
        return STATUS_INVALID_DEVICE_REQUEST;
    Fcb = FileObject->FsContext;
    if (!Fcb)
        return STATUS_INVALID_HANDLE;

    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
    ExFatRefreshFcb(Fcb);
    RtlZeroMemory(Buffer, Length);

    switch (Stack->Parameters.QueryFile.FileInformationClass)
    {
        case FileBasicInformation:
            if (Length < sizeof(FILE_BASIC_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                ExFatFillBasicInformation(Fcb, Buffer);
                Written = sizeof(FILE_BASIC_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            break;

        case FileStandardInformation:
            if (Length < sizeof(FILE_STANDARD_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                ExFatFillStandardInformation(Fcb, Buffer);
                Written = sizeof(FILE_STANDARD_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            break;

        case FileInternalInformation:
            if (Length < sizeof(FILE_INTERNAL_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                ((PFILE_INTERNAL_INFORMATION)Buffer)->IndexNumber.QuadPart = Fcb->IndexNumber;
                Written = sizeof(FILE_INTERNAL_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            break;

        case FileEaInformation:
            if (Length < sizeof(FILE_EA_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                ((PFILE_EA_INFORMATION)Buffer)->EaSize = 0;
                Written = sizeof(FILE_EA_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            break;

        case FilePositionInformation:
            if (Length < sizeof(FILE_POSITION_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                ((PFILE_POSITION_INFORMATION)Buffer)->CurrentByteOffset = FileObject->CurrentByteOffset;
                Written = sizeof(FILE_POSITION_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            break;

        case FileNameInformation:
        case FileNormalizedNameInformation:
            Status = ExFatQueryNameInformation(Fcb, Buffer, Length, &Written);
            break;

        case FileNetworkOpenInformation:
            if (Length < sizeof(FILE_NETWORK_OPEN_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                ExFatFillNetworkInformation(Fcb, Buffer);
                Written = sizeof(FILE_NETWORK_OPEN_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            break;

        case FileAttributeTagInformation:
            if (Length < sizeof(FILE_ATTRIBUTE_TAG_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                ((PFILE_ATTRIBUTE_TAG_INFORMATION)Buffer)->FileAttributes = Fcb->FileAttributes;
                ((PFILE_ATTRIBUTE_TAG_INFORMATION)Buffer)->ReparseTag = 0;
                Written = sizeof(FILE_ATTRIBUTE_TAG_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            break;

        case FileStreamInformation:
        {
            static const WCHAR StreamName[] = L"::$DATA";
            PFILE_STREAM_INFORMATION Stream = Buffer;
            ULONG NameLength = sizeof(StreamName) - sizeof(UNICODE_NULL);
            ULONG Required = FIELD_OFFSET(FILE_STREAM_INFORMATION, StreamName) + NameLength;

            if (Fcb->IsDirectory)
            {
                Status = STATUS_SUCCESS;
            }
            else if (Length < Required)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            else
            {
                Stream->NextEntryOffset = 0;
                Stream->StreamNameLength = NameLength;
                Stream->StreamSize = Fcb->Header.FileSize;
                Stream->StreamAllocationSize = Fcb->Header.AllocationSize;
                RtlCopyMemory(Stream->StreamName, StreamName, NameLength);
                Written = Required;
                Status = STATUS_SUCCESS;
            }
            break;
        }

        case FileAllInformation:
        {
            PFILE_ALL_INFORMATION All = Buffer;
            ULONG FixedLength = FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation.FileName);
            ULONG NameCopy;

            if (Length < FixedLength)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            ExFatFillBasicInformation(Fcb, &All->BasicInformation);
            ExFatFillStandardInformation(Fcb, &All->StandardInformation);
            All->InternalInformation.IndexNumber.QuadPart = Fcb->IndexNumber;
            All->EaInformation.EaSize = 0;
            All->AccessInformation.AccessFlags = 0;
            All->PositionInformation.CurrentByteOffset = FileObject->CurrentByteOffset;
            All->ModeInformation.Mode = 0;
            All->AlignmentInformation.AlignmentRequirement = Fcb->Vcb->DeviceObject->AlignmentRequirement;
            All->NameInformation.FileNameLength = Fcb->PathName.Length;
            NameCopy = min((ULONG)Fcb->PathName.Length, Length - FixedLength);
            RtlCopyMemory(All->NameInformation.FileName, Fcb->PathName.Buffer, NameCopy);
            Written = FixedLength + NameCopy;
            Status = (NameCopy == Fcb->PathName.Length) ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
            break;
        }

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    ExReleaseResourceLite(&Fcb->MainResource);
    Irp->IoStatus.Information = Written;
    return Status;
}

static NTSTATUS
ExFatSetBasicInformation(
    PEXFAT_FCB Fcb,
    PFILE_BASIC_INFORMATION Information)
{
    PCHAR Path;
    FILINFO FatInformation;
    BYTE Attributes;
    FRESULT Result = FR_OK;

    Path = ExFatBuildFatPath(Fcb->Vcb, &Fcb->PathName);
    if (!Path)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&FatInformation, sizeof(FatInformation));
    ExFatAcquireFatFs(Fcb->Vcb);
    if (Information->FileAttributes)
    {
        Attributes = ExFatNtAttributesToFat(Information->FileAttributes);
        Result = f_chmod(Path, Attributes, AM_RDO | AM_HID | AM_SYS | AM_ARC);
    }
    if (Result == FR_OK && Information->LastWriteTime.QuadPart)
    {
        ExFatSystemTimeToFatTime(&Information->LastWriteTime,
                                 &FatInformation.fdate,
                                 &FatInformation.ftime);
        if (Information->CreationTime.QuadPart)
        {
            ExFatSystemTimeToFatTime(&Information->CreationTime,
                                     &FatInformation.crdate,
                                     &FatInformation.crtime);
        }
        Result = f_utime(Path, &FatInformation);
    }
    ExFatReleaseFatFs(Fcb->Vcb);
    ExFreePoolWithTag(Path, TAG_EXFAT_PATH);

    if (Result == FR_OK)
        ExFatRefreshFcb(Fcb);
    return ExFatMapResult(Result);
}

static NTSTATUS
ExFatSetEndOfFile(
    PEXFAT_FCB Fcb,
    PEXFAT_CCB Ccb,
    PLARGE_INTEGER EndOfFile)
{
    FRESULT Result;
    ULONG ClusterSize;

    if (EndOfFile->QuadPart < 0)
        return STATUS_INVALID_PARAMETER;
    if (!Ccb->HandleOpen || Ccb->IsDirectory || !ExFatIsWriteAccess(Ccb->DesiredAccess))
        return STATUS_ACCESS_DENIED;
    if (EndOfFile->QuadPart < Fcb->Header.FileSize.QuadPart &&
        !MmCanFileBeTruncated(&Fcb->SectionObjectPointers, EndOfFile))
    {
        return STATUS_USER_MAPPED_FILE;
    }

    ExFatAcquireFatFs(Fcb->Vcb);
    Result = f_lseek(&Ccb->Handle.File, (FSIZE_t)EndOfFile->QuadPart);
    if (Result == FR_OK)
        Result = f_truncate(&Ccb->Handle.File);
    if (Result == FR_OK)
        Result = f_sync(&Ccb->Handle.File);
    if (Result == FR_OK)
    {
        Fcb->Header.FileSize = *EndOfFile;
        Fcb->Header.ValidDataLength = *EndOfFile;
        ClusterSize = Fcb->Vcb->FileSystem.csize * Fcb->Vcb->BytesPerSector;
        Fcb->Header.AllocationSize.QuadPart = ExFatRoundUp(EndOfFile->QuadPart,
                                                           ClusterSize);
    }
    ExFatReleaseFatFs(Fcb->Vcb);
    return ExFatMapResult(Result);
}

NTSTATUS
ExFatSetInformation(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXFAT_FCB Fcb;
    PEXFAT_CCB Ccb;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = Stack->Parameters.SetFile.Length;
    NTSTATUS Status;

    if (DeviceObject == ExFatGlobalData->DeviceObject || !FileObject || !Buffer)
        return STATUS_INVALID_DEVICE_REQUEST;
    Fcb = FileObject->FsContext;
    Ccb = FileObject->FsContext2;
    if (!Fcb || !Ccb)
        return STATUS_INVALID_HANDLE;
    if (Fcb->Vcb->ReadOnly)
        return STATUS_MEDIA_WRITE_PROTECTED;

    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
    switch (Stack->Parameters.SetFile.FileInformationClass)
    {
        case FilePositionInformation:
            if (Length < sizeof(FILE_POSITION_INFORMATION))
                Status = STATUS_INFO_LENGTH_MISMATCH;
            else if (((PFILE_POSITION_INFORMATION)Buffer)->CurrentByteOffset.QuadPart < 0)
                Status = STATUS_INVALID_PARAMETER;
            else
            {
                FileObject->CurrentByteOffset = ((PFILE_POSITION_INFORMATION)Buffer)->CurrentByteOffset;
                Status = STATUS_SUCCESS;
            }
            break;

        case FileBasicInformation:
            if (Length < sizeof(FILE_BASIC_INFORMATION))
                Status = STATUS_INFO_LENGTH_MISMATCH;
            else if (Fcb->IsVolume)
                Status = STATUS_INVALID_DEVICE_REQUEST;
            else
                Status = ExFatSetBasicInformation(Fcb, Buffer);
            break;

        case FileEndOfFileInformation:
            if (Length < sizeof(FILE_END_OF_FILE_INFORMATION))
                Status = STATUS_INFO_LENGTH_MISMATCH;
            else if (Fcb->IsDirectory || Fcb->IsVolume)
                Status = STATUS_INVALID_DEVICE_REQUEST;
            else
                Status = ExFatSetEndOfFile(Fcb,
                                           Ccb,
                                           &((PFILE_END_OF_FILE_INFORMATION)Buffer)->EndOfFile);
            break;

        case FileAllocationInformation:
            if (Length < sizeof(FILE_ALLOCATION_INFORMATION))
                Status = STATUS_INFO_LENGTH_MISMATCH;
            else if (((PFILE_ALLOCATION_INFORMATION)Buffer)->AllocationSize.QuadPart <
                     Fcb->Header.FileSize.QuadPart)
            {
                LARGE_INTEGER NewSize = ((PFILE_ALLOCATION_INFORMATION)Buffer)->AllocationSize;
                Status = ExFatSetEndOfFile(Fcb, Ccb, &NewSize);
            }
            else
                Status = STATUS_SUCCESS;
            break;

        case FileDispositionInformation:
            if (Length < sizeof(FILE_DISPOSITION_INFORMATION))
                Status = STATUS_INFO_LENGTH_MISMATCH;
            else if (Fcb->IsVolume)
                Status = STATUS_CANNOT_DELETE;
            else if (((PFILE_DISPOSITION_INFORMATION)Buffer)->DeleteFile &&
                     !MmFlushImageSection(&Fcb->SectionObjectPointers, MmFlushForDelete))
                Status = STATUS_CANNOT_DELETE;
            else
            {
                Ccb->DeleteOnClose = ((PFILE_DISPOSITION_INFORMATION)Buffer)->DeleteFile;
                Fcb->DeletePending = Ccb->DeleteOnClose;
                Status = STATUS_SUCCESS;
            }
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }
    ExReleaseResourceLite(&Fcb->MainResource);
    return Status;
}

static NTSTATUS
ExFatQueryVolumeLabel(
    PEXFAT_VCB Vcb,
    PFILE_FS_VOLUME_INFORMATION Information,
    ULONG Length,
    PULONG Written)
{
    CHAR DrivePath[3];
    CHAR Label[FF_LFN_BUF + 1];
    UNICODE_STRING UnicodeLabel;
    ULONG HeaderLength = FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel);
    ULONG CopyLength;
    FRESULT Result;
    NTSTATUS Status;

    if (Length < HeaderLength)
        return STATUS_BUFFER_TOO_SMALL;

    DrivePath[0] = '0' + Vcb->DriveNumber;
    DrivePath[1] = ':';
    DrivePath[2] = ANSI_NULL;
    RtlZeroMemory(Label, sizeof(Label));
    ExFatAcquireFatFs(Vcb);
    Result = f_getlabel(DrivePath, Label, &Vcb->SerialNumber);
    ExFatReleaseFatFs(Vcb);
    if (Result != FR_OK)
        return ExFatMapResult(Result);

    Status = ExFatUtf8ToUnicode(Label, &UnicodeLabel);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Information, HeaderLength);
    Information->VolumeSerialNumber = Vcb->SerialNumber;
    Information->SupportsObjects = FALSE;
    Information->VolumeLabelLength = UnicodeLabel.Length;
    CopyLength = min((ULONG)UnicodeLabel.Length, Length - HeaderLength);
    RtlCopyMemory(Information->VolumeLabel, UnicodeLabel.Buffer, CopyLength);
    ExFatFreeUnicodeString(&UnicodeLabel);
    *Written = HeaderLength + CopyLength;
    return (CopyLength == Information->VolumeLabelLength) ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
}

NTSTATUS
ExFatQueryVolumeInformation(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    static const WCHAR FileSystemName[] = L"exFAT";
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PEXFAT_VCB Vcb;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = Stack->Parameters.QueryVolume.Length;
    ULONG Written = 0;
    DWORD FreeClusters;
    FATFS* FileSystem;
    FRESULT Result;
    NTSTATUS Status;

    if (DeviceObject == ExFatGlobalData->DeviceObject || !Buffer)
        return STATUS_INVALID_DEVICE_REQUEST;
    Vcb = DeviceObject->DeviceExtension;
    RtlZeroMemory(Buffer, Length);

    ExAcquireResourceSharedLite(&Vcb->Resource, TRUE);
    switch (Stack->Parameters.QueryVolume.FsInformationClass)
    {
        case FileFsVolumeInformation:
            Status = ExFatQueryVolumeLabel(Vcb, Buffer, Length, &Written);
            break;

        case FileFsSizeInformation:
            if (Length < sizeof(FILE_FS_SIZE_INFORMATION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            {
                CHAR DrivePath[3] = { '0' + Vcb->DriveNumber, ':', ANSI_NULL };
                ExFatAcquireFatFs(Vcb);
                Result = f_getfree(DrivePath, &FreeClusters, &FileSystem);
                ExFatReleaseFatFs(Vcb);
            }
            if (Result != FR_OK)
            {
                Status = ExFatMapResult(Result);
                break;
            }
            ((PFILE_FS_SIZE_INFORMATION)Buffer)->TotalAllocationUnits.QuadPart = FileSystem->n_fatent - 2;
            ((PFILE_FS_SIZE_INFORMATION)Buffer)->AvailableAllocationUnits.QuadPart = FreeClusters;
            ((PFILE_FS_SIZE_INFORMATION)Buffer)->SectorsPerAllocationUnit = FileSystem->csize;
            ((PFILE_FS_SIZE_INFORMATION)Buffer)->BytesPerSector = Vcb->BytesPerSector;
            Written = sizeof(FILE_FS_SIZE_INFORMATION);
            Status = STATUS_SUCCESS;
            break;

        case FileFsFullSizeInformation:
            if (Length < sizeof(FILE_FS_FULL_SIZE_INFORMATION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            {
                CHAR DrivePath[3] = { '0' + Vcb->DriveNumber, ':', ANSI_NULL };
                ExFatAcquireFatFs(Vcb);
                Result = f_getfree(DrivePath, &FreeClusters, &FileSystem);
                ExFatReleaseFatFs(Vcb);
            }
            if (Result != FR_OK)
            {
                Status = ExFatMapResult(Result);
                break;
            }
            ((PFILE_FS_FULL_SIZE_INFORMATION)Buffer)->TotalAllocationUnits.QuadPart = FileSystem->n_fatent - 2;
            ((PFILE_FS_FULL_SIZE_INFORMATION)Buffer)->CallerAvailableAllocationUnits.QuadPart = FreeClusters;
            ((PFILE_FS_FULL_SIZE_INFORMATION)Buffer)->ActualAvailableAllocationUnits.QuadPart = FreeClusters;
            ((PFILE_FS_FULL_SIZE_INFORMATION)Buffer)->SectorsPerAllocationUnit = FileSystem->csize;
            ((PFILE_FS_FULL_SIZE_INFORMATION)Buffer)->BytesPerSector = Vcb->BytesPerSector;
            Written = sizeof(FILE_FS_FULL_SIZE_INFORMATION);
            Status = STATUS_SUCCESS;
            break;

        case FileFsDeviceInformation:
            if (Length < sizeof(FILE_FS_DEVICE_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                ((PFILE_FS_DEVICE_INFORMATION)Buffer)->DeviceType = FILE_DEVICE_DISK;
                ((PFILE_FS_DEVICE_INFORMATION)Buffer)->Characteristics = Vcb->StorageDevice->Characteristics;
                Written = sizeof(FILE_FS_DEVICE_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            break;

        case FileFsAttributeInformation:
        {
            PFILE_FS_ATTRIBUTE_INFORMATION Attributes = Buffer;
            ULONG NameLength = sizeof(FileSystemName) - sizeof(UNICODE_NULL);
            ULONG Required = FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName) + NameLength;

            if (Length < Required)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            Attributes->FileSystemAttributes = FILE_CASE_PRESERVED_NAMES | FILE_UNICODE_ON_DISK;
            Attributes->MaximumComponentNameLength = FF_MAX_LFN;
            Attributes->FileSystemNameLength = NameLength;
            RtlCopyMemory(Attributes->FileSystemName, FileSystemName, NameLength);
            Written = Required;
            Status = STATUS_SUCCESS;
            break;
        }

        case FileFsSectorSizeInformation:
            if (Length < sizeof(FILE_FS_SECTOR_SIZE_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                PFILE_FS_SECTOR_SIZE_INFORMATION Sector = Buffer;
                Sector->LogicalBytesPerSector = Vcb->BytesPerSector;
                Sector->PhysicalBytesPerSectorForAtomicity = Vcb->BytesPerSector;
                Sector->PhysicalBytesPerSectorForPerformance = Vcb->BytesPerSector;
                Sector->FileSystemEffectivePhysicalBytesPerSectorForAtomicity = Vcb->BytesPerSector;
                Sector->Flags = 0;
                Sector->ByteOffsetForSectorAlignment = 0;
                Sector->ByteOffsetForPartitionAlignment = 0;
                Written = sizeof(FILE_FS_SECTOR_SIZE_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }
    ExReleaseResourceLite(&Vcb->Resource);
    Irp->IoStatus.Information = Written;
    return Status;
}

NTSTATUS
ExFatSetVolumeInformation(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PEXFAT_VCB Vcb;
    PFILE_FS_LABEL_INFORMATION Information;
    UNICODE_STRING Label;
    PCHAR FatLabel;
    FRESULT Result;

    if (DeviceObject == ExFatGlobalData->DeviceObject ||
        Stack->Parameters.SetVolume.FsInformationClass != FileFsLabelInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }
    Vcb = DeviceObject->DeviceExtension;
    if (Vcb->ReadOnly)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (Stack->Parameters.SetVolume.Length < FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel))
        return STATUS_INFO_LENGTH_MISMATCH;

    Information = Irp->AssociatedIrp.SystemBuffer;
    if (!Information || Information->VolumeLabelLength > Stack->Parameters.SetVolume.Length -
                                                       FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel) ||
        Information->VolumeLabelLength > MAXUSHORT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Label.Buffer = Information->VolumeLabel;
    Label.Length = (USHORT)Information->VolumeLabelLength;
    Label.MaximumLength = Label.Length;
    FatLabel = ExFatBuildFatPath(Vcb, &Label);
    if (!FatLabel)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (!Label.Length)
        FatLabel[2] = ANSI_NULL;

    ExFatAcquireFatFs(Vcb);
    Result = f_setlabel(FatLabel);
    ExFatReleaseFatFs(Vcb);
    ExFreePoolWithTag(FatLabel, TAG_EXFAT_PATH);
    return ExFatMapResult(Result);
}
