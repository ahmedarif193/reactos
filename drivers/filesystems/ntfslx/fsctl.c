/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Mount and verify path for staged NTFS port
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

static
VOID
NtfslxFreeUpcaseTable(
    _Inout_opt_ PUSHORT UpcaseTable)
{
    if (UpcaseTable != NULL)
    {
        ExFreePoolWithTag(UpcaseTable, NTFSLX_TAG);
    }
}

static
BOOLEAN
NtfslxIsSupportedClusterSize(
    _In_ ULONG ClusterSize)
{
    switch (ClusterSize)
    {
        case 512:
        case 1024:
        case 2048:
        case 4096:
        case 8192:
        case 16384:
        case 32768:
        case 65536:
            return TRUE;

        default:
            return FALSE;
    }
}

static
NTSTATUS
NtfslxReadBootSector(
    _In_ PDEVICE_OBJECT StorageDevice,
    _Out_ PNTFSLX_BOOT_SECTOR *BootSector,
    _Out_ PULONG DeviceSectorSize)
{
    DISK_GEOMETRY Geometry;
    ULONG GeometryLength;
    ULONG AllocationSize;
    PNTFSLX_BOOT_SECTOR SectorBuffer;
    NTSTATUS Status;

    GeometryLength = sizeof(Geometry);
    Status = NtfslxDeviceIoControl(StorageDevice,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                   NULL,
                                   0,
                                   &Geometry,
                                   &GeometryLength,
                                   TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Geometry.BytesPerSector < sizeof(NTFSLX_BOOT_SECTOR))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    AllocationSize = Geometry.BytesPerSector;
    SectorBuffer = ExAllocatePoolWithTag(NonPagedPool, AllocationSize, NTFSLX_TAG);
    if (SectorBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(SectorBuffer, AllocationSize);
    Status = NtfslxReadDisk(StorageDevice,
                            0,
                            Geometry.BytesPerSector,
                            Geometry.BytesPerSector,
                            (PUCHAR)SectorBuffer,
                            TRUE);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(SectorBuffer, NTFSLX_TAG);
        return Status;
    }

    *BootSector = SectorBuffer;
    *DeviceSectorSize = Geometry.BytesPerSector;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxExtractVolumeInfo(
    _In_ PNTFSLX_BOOT_SECTOR BootSector,
    _In_ ULONG DeviceSectorSize,
    _Out_ PNTFSLX_VOLUME_INFO VolumeInfo)
{
    ULONG BytesPerSector;
    ULONG SectorsPerCluster;
    ULONG BytesPerCluster;
    ULONG BytesPerFileRecord;
    ULONG BytesPerIndexRecord;

    if (RtlCompareMemory(&BootSector->OemId, "NTFS    ", 8) != 8)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (BootSector->EndOfSectorMarker != 0xAA55)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    BytesPerSector = BootSector->Bpb.BytesPerSector;
    SectorsPerCluster = BootSector->Bpb.SectorsPerCluster;
    if (BytesPerSector == 0 || SectorsPerCluster == 0)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (BytesPerSector != DeviceSectorSize)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (!NtfslxIsPowerOfTwo(BytesPerSector) || !NtfslxIsPowerOfTwo(SectorsPerCluster))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    BytesPerCluster = BytesPerSector * SectorsPerCluster;
    if (!NtfslxIsSupportedClusterSize(BytesPerCluster))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    BytesPerFileRecord = NtfslxRecordSizeFromClusters(BootSector->ClustersPerMftRecord,
                                                      BytesPerCluster);
    BytesPerIndexRecord = NtfslxRecordSizeFromClusters(BootSector->ClustersPerIndexRecord,
                                                       BytesPerCluster);
    if (BytesPerFileRecord < sizeof(NTFSLX_MFT_RECORD) || BytesPerIndexRecord == 0)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (BootSector->SectorCount == 0)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    RtlZeroMemory(VolumeInfo, sizeof(*VolumeInfo));
    VolumeInfo->BytesPerSector = BytesPerSector;
    VolumeInfo->SectorsPerCluster = SectorsPerCluster;
    VolumeInfo->BytesPerCluster = BytesPerCluster;
    VolumeInfo->BytesPerFileRecord = BytesPerFileRecord;
    VolumeInfo->BytesPerIndexRecord = BytesPerIndexRecord;
    VolumeInfo->SectorCount = BootSector->SectorCount;
    VolumeInfo->ClusterCount = BootSector->SectorCount / SectorsPerCluster;
    VolumeInfo->MftLcn = BootSector->MftLcn;
    VolumeInfo->MftMirrLcn = BootSector->MftMirrLcn;
    VolumeInfo->SerialNumber = BootSector->SerialNumber;

    if (VolumeInfo->ClusterCount == 0 ||
        VolumeInfo->MftLcn >= VolumeInfo->ClusterCount ||
        VolumeInfo->MftMirrLcn >= VolumeInfo->ClusterCount)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxValidatePrimaryMftRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo)
{
    PNTFSLX_MFT_RECORD MftRecord;
    ULONGLONG MftOffset;
    NTSTATUS Status;

    if (VolumeInfo->MftLcn > (ULONGLONG)(MAXLONGLONG / VolumeInfo->BytesPerCluster))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    MftOffset = VolumeInfo->MftLcn * VolumeInfo->BytesPerCluster;
    MftRecord = ExAllocatePoolWithTag(NonPagedPool,
                                      VolumeInfo->BytesPerFileRecord,
                                      NTFSLX_TAG);
    if (MftRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadDisk(StorageDevice,
                            (LONGLONG)MftOffset,
                            VolumeInfo->BytesPerFileRecord,
                            VolumeInfo->BytesPerSector,
                            (PUCHAR)MftRecord,
                            TRUE);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxPostReadMstFixup(&MftRecord->Ntfs, VolumeInfo->BytesPerFileRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    if (MftRecord->Ntfs.Magic != NTFSLX_RECORD_MAGIC_FILE ||
        (MftRecord->Flags & NTFSLX_MFT_RECORD_IN_USE) == 0 ||
        MftRecord->BytesInUse == 0 ||
        MftRecord->BytesInUse > VolumeInfo->BytesPerFileRecord ||
        MftRecord->BytesAllocated > VolumeInfo->BytesPerFileRecord ||
        MftRecord->AttributesOffset >= MftRecord->BytesInUse)
    {
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ASSERT(MftRecord->Ntfs.Magic == NTFSLX_RECORD_MAGIC_FILE);
    ASSERT(MftRecord->BytesInUse <= VolumeInfo->BytesPerFileRecord);

    ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxProbeVolume(
    _In_ PDEVICE_OBJECT StorageDevice,
    _Out_ PNTFSLX_VOLUME_INFO VolumeInfo)
{
    PNTFSLX_BOOT_SECTOR BootSector;
    ULONG DeviceSectorSize;
    NTSTATUS Status;

    Status = NtfslxReadBootSector(StorageDevice, &BootSector, &DeviceSectorSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = NtfslxExtractVolumeInfo(BootSector, DeviceSectorSize, VolumeInfo);
    ExFreePoolWithTag(BootSector, NTFSLX_TAG);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return NtfslxValidatePrimaryMftRecord(StorageDevice, VolumeInfo);
}

static
NTSTATUS
NtfslxVerifyVolume(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    NTFSLX_VOLUME_INFO VolumeInfo;
    NTSTATUS Status;

    Status = NtfslxProbeVolume(DeviceExtension->StorageDevice, &VolumeInfo);
    if (Status == STATUS_UNRECOGNIZED_VOLUME)
    {
        return STATUS_WRONG_VOLUME;
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (VolumeInfo.SerialNumber != DeviceExtension->VolumeInfo.SerialNumber ||
        VolumeInfo.BytesPerCluster != DeviceExtension->VolumeInfo.BytesPerCluster ||
        VolumeInfo.BytesPerFileRecord != DeviceExtension->VolumeInfo.BytesPerFileRecord ||
        VolumeInfo.MftLcn != DeviceExtension->VolumeInfo.MftLcn)
    {
        return STATUS_WRONG_VOLUME;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxMountVolume(
    _In_ PDEVICE_OBJECT ControlDevice,
    _In_ PDEVICE_OBJECT StorageDevice,
    _Inout_ PVPB Vpb)
{
    PDEVICE_OBJECT VolumeDevice;
    PNTFSLX_DEVICE_EXTENSION VolumeExtension;
    NTFSLX_VOLUME_INFO VolumeInfo;
    PUSHORT UpcaseTable;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ControlDevice);

    if (StorageDevice == NULL || Vpb == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxProbeVolume(StorageDevice, &VolumeInfo);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    UpcaseTable = NtfslxGenerateDefaultUpcase();
    if (UpcaseTable == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ASSERT(VolumeInfo.BytesPerFileRecord != 0);

    Status = IoCreateDevice(NtfslxGlobalData.DriverObject,
                            sizeof(NTFSLX_DEVICE_EXTENSION),
                            NULL,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &VolumeDevice);
    if (!NT_SUCCESS(Status))
    {
        NtfslxFreeUpcaseTable(UpcaseTable);
        return Status;
    }

    VolumeExtension = VolumeDevice->DeviceExtension;
    RtlZeroMemory(VolumeExtension, sizeof(*VolumeExtension));

    VolumeExtension->Signature = NTFSLX_TAG;
    VolumeExtension->Kind = NtfslxDeviceKindVolume;
    VolumeExtension->DeviceObject = VolumeDevice;
    VolumeExtension->StorageDevice = StorageDevice;
    VolumeExtension->Vpb = Vpb;
    VolumeExtension->VolumeInfo = VolumeInfo;
    VolumeExtension->UpcaseTable = UpcaseTable;
    VolumeExtension->UpcaseTableLength = NTFSLX_DEFAULT_UPCASE_LENGTH;

    Status = ExInitializeResourceLite(&VolumeExtension->Resource);
    if (!NT_SUCCESS(Status))
    {
        NtfslxFreeUpcaseTable(UpcaseTable);
        IoDeleteDevice(VolumeDevice);
        return Status;
    }

    ASSERT(VolumeExtension->UpcaseTable != NULL);

    VolumeDevice->Flags |= DO_DIRECT_IO;
    VolumeDevice->StackSize = StorageDevice->StackSize + 1;

    Vpb->DeviceObject = VolumeDevice;
    Vpb->RealDevice = StorageDevice;
    Vpb->Flags |= VPB_MOUNTED;
    Vpb->SerialNumber = (ULONG)VolumeInfo.SerialNumber;
    Vpb->VolumeLabelLength = 0;

    VolumeDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxFileSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_MOUNT_VOLUME:
            if (!NtfslxIsControlDevice(DeviceExtension))
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Status = NtfslxMountVolume(DeviceObject,
                                       Stack->Parameters.MountVolume.DeviceObject,
                                       Stack->Parameters.MountVolume.Vpb);
            break;

        case IRP_MN_VERIFY_VOLUME:
            if (!NtfslxIsVolumeDevice(DeviceExtension))
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Status = NtfslxVerifyVolume(DeviceExtension);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
