/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G volume management
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

static VOID
NtfsSetVpbMetadata(_In_ PNTFS3G_ROS_KM_VOLUME Volume,
                   _Inout_ PVPB Vpb)
{
    size_t NameLength;

    Vpb->SerialNumber = (ULONG)Ntfs3gRosGetVolumeSerialNumber(Volume);
    Vpb->VolumeLabelLength = 0;
    if (!Ntfs3gRosGetVolumeNameUtf16(
            Volume,
            (uint16_t *)Vpb->VolumeLabel,
            RTL_NUMBER_OF(Vpb->VolumeLabel),
            &NameLength))
        Vpb->VolumeLabelLength = (USHORT)(NameLength * sizeof(WCHAR));
}

static NTSTATUS
NtfsQueryVolumeInformation(_In_ PDEVICE_OBJECT DeviceObject,
                           _In_ FS_INFORMATION_CLASS InformationClass,
                           _Out_writes_bytes_(Length) PVOID Buffer,
                           _In_ ULONG Length,
                           _Out_ PULONG BytesWritten)
{
    PVolumeContextBlock Volume = DeviceObject->DeviceExtension;
    ULONG Required;

    *BytesWritten = 0;
    switch (InformationClass) {
        case FileFsVolumeInformation:
        {
            PFILE_FS_VOLUME_INFORMATION Information = Buffer;

            Required = FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel) +
                       DeviceObject->Vpb->VolumeLabelLength;
            if (Length < Required)
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(Information, Required);
            Information->VolumeSerialNumber = DeviceObject->Vpb->SerialNumber;
            Information->VolumeLabelLength = DeviceObject->Vpb->VolumeLabelLength;
            RtlCopyMemory(Information->VolumeLabel,
                          DeviceObject->Vpb->VolumeLabel,
                          DeviceObject->Vpb->VolumeLabelLength);
            *BytesWritten = Required;
            return STATUS_SUCCESS;
        }

        case FileFsSizeInformation:
        {
            PFILE_FS_SIZE_INFORMATION Information = Buffer;

            if (Length < sizeof(*Information))
                return STATUS_BUFFER_TOO_SMALL;
            Information->TotalAllocationUnits.QuadPart =
                Ntfs3gRosGetClusterCount(Volume->Volume);
            Information->AvailableAllocationUnits.QuadPart =
                Ntfs3gRosGetFreeClusterCount(Volume->Volume);
            Information->SectorsPerAllocationUnit =
                Ntfs3gRosGetSectorsPerCluster(Volume->Volume);
            Information->BytesPerSector =
                Ntfs3gRosGetBytesPerSector(Volume->Volume);
            *BytesWritten = sizeof(*Information);
            return STATUS_SUCCESS;
        }

        case FileFsFullSizeInformation:
        {
            PFILE_FS_FULL_SIZE_INFORMATION Information = Buffer;

            if (Length < sizeof(*Information))
                return STATUS_BUFFER_TOO_SMALL;
            Information->TotalAllocationUnits.QuadPart =
                Ntfs3gRosGetClusterCount(Volume->Volume);
            Information->CallerAvailableAllocationUnits.QuadPart =
                Ntfs3gRosGetFreeClusterCount(Volume->Volume);
            Information->ActualAvailableAllocationUnits =
                Information->CallerAvailableAllocationUnits;
            Information->SectorsPerAllocationUnit =
                Ntfs3gRosGetSectorsPerCluster(Volume->Volume);
            Information->BytesPerSector =
                Ntfs3gRosGetBytesPerSector(Volume->Volume);
            *BytesWritten = sizeof(*Information);
            return STATUS_SUCCESS;
        }

        case FileFsAttributeInformation:
        {
            static const WCHAR FileSystemName[] = L"NTFS";
            PFILE_FS_ATTRIBUTE_INFORMATION Information = Buffer;
            ULONG NameLength = sizeof(FileSystemName) - sizeof(WCHAR);

            Required = FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION,
                                    FileSystemName) + NameLength;
            if (Length < Required)
                return STATUS_BUFFER_TOO_SMALL;
            Information->FileSystemAttributes = FILE_CASE_PRESERVED_NAMES |
                                                FILE_UNICODE_ON_DISK |
                                                FILE_SUPPORTS_EXTENDED_ATTRIBUTES;
            Information->MaximumComponentNameLength =
                NTFS3G_ROS_MAX_NAME_LENGTH;
            Information->FileSystemNameLength = NameLength;
            RtlCopyMemory(Information->FileSystemName,
                          FileSystemName,
                          NameLength);
            *BytesWritten = Required;
            return STATUS_SUCCESS;
        }

        case FileFsDeviceInformation:
        {
            PFILE_FS_DEVICE_INFORMATION Information = Buffer;

            if (Length < sizeof(*Information))
                return STATUS_BUFFER_TOO_SMALL;
            Information->DeviceType = FILE_DEVICE_DISK;
            Information->Characteristics = Volume->StorageDevice->Characteristics;
            *BytesWritten = sizeof(*Information);
            return STATUS_SUCCESS;
        }

        case FileFsSectorSizeInformation:
        {
            PFILE_FS_SECTOR_SIZE_INFORMATION Information = Buffer;
            ULONG SectorSize =
                Ntfs3gRosGetBytesPerSector(Volume->Volume);

            if (Length < sizeof(*Information))
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(Information, sizeof(*Information));
            Information->LogicalBytesPerSector = SectorSize;
            Information->PhysicalBytesPerSectorForAtomicity =
                SectorSize;
            Information->PhysicalBytesPerSectorForPerformance =
                SectorSize;
            Information->
                FileSystemEffectivePhysicalBytesPerSectorForAtomicity =
                    SectorSize;
            *BytesWritten = sizeof(*Information);
            return STATUS_SUCCESS;
        }

        default:
            return STATUS_INVALID_INFO_CLASS;
    }
}

NTSTATUS
NTAPI
NtfsFsdQueryVolumeInformation(_In_ PDEVICE_OBJECT DeviceObject,
                              _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    PVolumeContextBlock Volume;
    ULONG BytesWritten;
    NTSTATUS Status;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject || !Buffer)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    Volume = DeviceObject->DeviceExtension;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(
        &Volume->FcbListResource, TRUE);
    if (Volume->Dismounted || !Volume->Volume) {
        Status = STATUS_VOLUME_DISMOUNTED;
        BytesWritten = 0;
    } else if (Volume->ShutdownStarted) {
        Status = STATUS_SYSTEM_SHUTDOWN;
        BytesWritten = 0;
    } else {
        Status = NtfsQueryVolumeInformation(
            DeviceObject,
            IrpSp->Parameters.QueryVolume.FsInformationClass,
            Buffer,
            IrpSp->Parameters.QueryVolume.Length,
            &BytesWritten);
    }
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();
    return NtfsCompleteRequest(Irp, Status, BytesWritten);
}

NTSTATUS
NTAPI
NtfsFsdSetVolumeInformation(_In_ PDEVICE_OBJECT DeviceObject,
                            _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp =
        IoGetCurrentIrpStackLocation(Irp);
    PVolumeContextBlock Volume;
    PFILE_FS_LABEL_INFORMATION Information =
        Irp->AssociatedIrp.SystemBuffer;
    PHandleContextBlock Handle;
    ULONG HeaderLength =
        FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel);
    KIRQL VpbIrql;
    NTSTATUS Status;
    int Result;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject)
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (IrpSp->Parameters.SetVolume.FsInformationClass !=
        FileFsLabelInformation)
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_INFO_CLASS, 0);
    if (!IrpSp->FileObject ||
        !(Handle = IrpSp->FileObject->FsContext2))
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_PARAMETER, 0);
    if (Handle->CleanupComplete)
        return NtfsCompleteRequest(
            Irp, STATUS_FILE_CLOSED, 0);
    if (Irp->RequestorMode != KernelMode &&
        !(Handle->DesiredAccess & FILE_WRITE_DATA))
        return NtfsCompleteRequest(
            Irp, STATUS_ACCESS_DENIED, 0);
    if (!Information ||
        IrpSp->Parameters.SetVolume.Length < HeaderLength ||
        Information->VolumeLabelLength >
            IrpSp->Parameters.SetVolume.Length - HeaderLength ||
        (Information->VolumeLabelLength & (sizeof(WCHAR) - 1)) ||
        Information->VolumeLabelLength >
            sizeof(DeviceObject->Vpb->VolumeLabel))
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_VOLUME_LABEL, 0);

    Volume = DeviceObject->DeviceExtension;
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &Volume->FcbListResource, TRUE);
    if (Volume->ShutdownStarted) {
        Status = STATUS_SYSTEM_SHUTDOWN;
    } else if (Volume->Dismounted || !Volume->Volume) {
        Status = STATUS_VOLUME_DISMOUNTED;
    } else if (Ntfs3gRosIsReadOnly(Volume->Volume)) {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
    } else {
        Result = Ntfs3gRosSetVolumeNameUtf16(
            Volume->Volume,
            (const uint16_t *)Information->VolumeLabel,
            Information->VolumeLabelLength / sizeof(WCHAR));
        Status = Result < 0 ?
            Ntfs3gRosStatusFromError(-Result) :
            STATUS_SUCCESS;
        if (NT_SUCCESS(Status)) {
            IoAcquireVpbSpinLock(&VpbIrql);
            DeviceObject->Vpb->VolumeLabelLength =
                (USHORT)Information->VolumeLabelLength;
            RtlCopyMemory(
                DeviceObject->Vpb->VolumeLabel,
                Information->VolumeLabel,
                Information->VolumeLabelLength);
            IoReleaseVpbSpinLock(VpbIrql);
        }
    }
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();
    return NtfsCompleteRequest(Irp, Status, 0);
}

NTSTATUS
NtfsMountVolume(_In_ PDEVICE_OBJECT TargetDeviceObject,
                _In_ PVPB Vpb)
{
    PNTFS3G_ROS_KM_VOLUME CoreVolume = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    PVolumeContextBlock Volume;
    NTSTATUS Status;

    Status = Ntfs3gRosMountDevice(TargetDeviceObject, FALSE, &CoreVolume);
    if (!NT_SUCCESS(Status)) {
        /*
         * A volume this driver cannot read has to be reported as
         * STATUS_UNRECOGNIZED_VOLUME, or the I/O manager treats the mount as
         * a hard failure and stops offering the volume to the remaining
         * filesystems. libntfs-3g reports a missing NTFS signature as EINVAL,
         * which maps to STATUS_INVALID_PARAMETER, so without this every FAT,
         * FAT32 and exFAT volume fails to mount once this driver is loaded.
         */
        if (Status == STATUS_INVALID_PARAMETER)
            Status = STATUS_UNRECOGNIZED_VOLUME;
        return Status;
    }

    Status = IoCreateDevice(NtfsDiskFileSystemDeviceObject->DriverObject,
                            sizeof(*Volume),
                            NULL,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Volume = DeviceObject->DeviceExtension;
    Volume->Volume = CoreVolume;
    Volume->StorageDevice = TargetDeviceObject;
    Status = ExInitializeResourceLite(&Volume->FcbListResource);
    if (!NT_SUCCESS(Status))
        goto Failure;
    Volume->FcbListResourceInitialized = TRUE;
    InitializeListHead(&Volume->FcbListHead);
    Volume->StreamFileObject = IoCreateStreamFileObject(NULL,
                                                        TargetDeviceObject);
    if (!Volume->StreamFileObject) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }
    Status = IoRegisterShutdownNotification(DeviceObject);
    if (!NT_SUCCESS(Status))
        goto Failure;
    Volume->ShutdownRegistered = TRUE;

    DeviceObject->Vpb = Vpb;
    DeviceObject->StackSize = TargetDeviceObject->StackSize + 1;
    DeviceObject->AlignmentRequirement =
        TargetDeviceObject->AlignmentRequirement;
    DeviceObject->SectorSize =
        (USHORT)Ntfs3gRosGetBytesPerSector(CoreVolume);
    DeviceObject->Flags |= DO_DIRECT_IO;
    NtfsSetVpbMetadata(CoreVolume, Vpb);
    Vpb->DeviceObject = DeviceObject;
    Vpb->RealDevice = TargetDeviceObject;
    Vpb->Flags |= VPB_MOUNTED;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    FsRtlNotifyVolumeEvent(Volume->StreamFileObject, FSRTL_VOLUME_MOUNT);
    return STATUS_SUCCESS;

Failure:
    if (DeviceObject) {
        Volume = DeviceObject->DeviceExtension;
        if (Volume->ShutdownRegistered)
            IoUnregisterShutdownNotification(DeviceObject);
        if (Volume->StreamFileObject)
            ObDereferenceObject(Volume->StreamFileObject);
        if (Volume->FcbListResourceInitialized) {
            ASSERT(IsListEmpty(&Volume->FcbListHead));
            ExDeleteResourceLite(&Volume->FcbListResource);
        }
        IoDeleteDevice(DeviceObject);
    }
    Ntfs3gRosUnmountDevice(CoreVolume);
    return Status;
}
