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
                                                FILE_READ_ONLY_VOLUME;
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
    ULONG BytesWritten;
    NTSTATUS Status;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject || !Buffer)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    Status = NtfsQueryVolumeInformation(
        DeviceObject,
        IrpSp->Parameters.QueryVolume.FsInformationClass,
        Buffer,
        IrpSp->Parameters.QueryVolume.Length,
        &BytesWritten);
    return NtfsCompleteRequest(Irp, Status, BytesWritten);
}

NTSTATUS
NTAPI
NtfsFsdSetVolumeInformation(_In_ PDEVICE_OBJECT DeviceObject,
                            _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return NtfsCompleteRequest(Irp, STATUS_MEDIA_WRITE_PROTECTED, 0);
}

NTSTATUS
NtfsMountVolume(_In_ PDEVICE_OBJECT TargetDeviceObject,
                _In_ PVPB Vpb)
{
    PNTFS3G_ROS_KM_VOLUME CoreVolume = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    PVolumeContextBlock Volume;
    NTSTATUS Status;

    Status = Ntfs3gRosMountDevice(TargetDeviceObject, TRUE, &CoreVolume);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IoCreateDevice(NtfsDriverObject,
                            sizeof(*Volume),
                            NULL,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Volume = DeviceObject->DeviceExtension;
    RtlZeroMemory(Volume, sizeof(*Volume));
    Volume->Volume = CoreVolume;
    Volume->StorageDevice = TargetDeviceObject;
    Volume->StreamFileObject = IoCreateStreamFileObject(NULL,
                                                        TargetDeviceObject);
    if (!Volume->StreamFileObject) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }

    DeviceObject->Vpb = Vpb;
    DeviceObject->StackSize = TargetDeviceObject->StackSize + 1;
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
        if (Volume->StreamFileObject)
            ObDereferenceObject(Volume->StreamFileObject);
        IoDeleteDevice(DeviceObject);
    }
    Ntfs3gRosUnmountDevice(CoreVolume);
    return Status;
}
