/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Volume information queries
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

static
PCSTR
NtfslxFsInfoClassName(
    _In_ FS_INFORMATION_CLASS FsInformationClass)
{
    switch (FsInformationClass)
    {
        case FileFsVolumeInformation:
            return "FileFsVolumeInformation";
        case FileFsLabelInformation:
            return "FileFsLabelInformation";
        case FileFsSizeInformation:
            return "FileFsSizeInformation";
        case FileFsDeviceInformation:
            return "FileFsDeviceInformation";
        case FileFsAttributeInformation:
            return "FileFsAttributeInformation";
        case FileFsControlInformation:
            return "FileFsControlInformation";
        case FileFsFullSizeInformation:
            return "FileFsFullSizeInformation";
        case FileFsObjectIdInformation:
            return "FileFsObjectIdInformation";
        case FileFsDriverPathInformation:
            return "FileFsDriverPathInformation";
        case FileFsVolumeFlagsInformation:
            return "FileFsVolumeFlagsInformation";
        default:
            return "UnknownFsInformation";
    }
}

static
NTSTATUS
NtfslxGetFsVolumeInformation(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(BufferLength) PFILE_FS_VOLUME_INFORMATION FsVolumeInfo,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnLength)
{
    ULONG RequiredLength;

    RequiredLength = FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel) +
                     DeviceExtension->VolumeInfo.VolumeLabelLength;
    if (BufferLength < sizeof(FILE_FS_VOLUME_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (BufferLength < RequiredLength)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    FsVolumeInfo->VolumeCreationTime.QuadPart = 0;
    FsVolumeInfo->VolumeSerialNumber = (ULONG)DeviceExtension->VolumeInfo.SerialNumber;
    FsVolumeInfo->VolumeLabelLength = DeviceExtension->VolumeInfo.VolumeLabelLength;
    FsVolumeInfo->SupportsObjects = FALSE;

    if (DeviceExtension->VolumeInfo.VolumeLabelLength != 0)
    {
        RtlCopyMemory(FsVolumeInfo->VolumeLabel,
                      DeviceExtension->VolumeInfo.VolumeLabel,
                      DeviceExtension->VolumeInfo.VolumeLabelLength);
    }

    DbgPrint("ntfslx: FsVolumeInfo label='%.*S' labelLen=%lu serial=0x%08lx supportsObjects=%u\n",
             (int)(FsVolumeInfo->VolumeLabelLength / sizeof(WCHAR)),
             FsVolumeInfo->VolumeLabel,
             FsVolumeInfo->VolumeLabelLength,
             FsVolumeInfo->VolumeSerialNumber,
             FsVolumeInfo->SupportsObjects);

    *ReturnLength = RequiredLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxGetFsAttributeInformation(
    _Out_writes_bytes_(BufferLength) PFILE_FS_ATTRIBUTE_INFORMATION FsAttributeInfo,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnLength)
{
    static const WCHAR NtfsName[] = L"NTFS";
    ULONG NameLength;
    ULONG RequiredLength;

    NameLength = sizeof(NtfsName) - sizeof(WCHAR);
    RequiredLength = FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName) + NameLength;

    if (BufferLength < sizeof(FILE_FS_ATTRIBUTE_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (BufferLength < RequiredLength)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    FsAttributeInfo->FileSystemAttributes =
        FILE_CASE_PRESERVED_NAMES | FILE_UNICODE_ON_DISK;
    FsAttributeInfo->MaximumComponentNameLength = 255;
    FsAttributeInfo->FileSystemNameLength = NameLength;
    RtlCopyMemory(FsAttributeInfo->FileSystemName, NtfsName, NameLength);

    DbgPrint("ntfslx: FsAttributeInfo fs='%.*S' attrs=0x%08lx maxComp=%lu\n",
             (int)(FsAttributeInfo->FileSystemNameLength / sizeof(WCHAR)),
             FsAttributeInfo->FileSystemName,
             FsAttributeInfo->FileSystemAttributes,
             FsAttributeInfo->MaximumComponentNameLength);

    *ReturnLength = RequiredLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxGetFsSizeInformation(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(BufferLength) PFILE_FS_SIZE_INFORMATION FsSizeInfo,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnLength)
{
    if (BufferLength < sizeof(FILE_FS_SIZE_INFORMATION))
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    FsSizeInfo->AvailableAllocationUnits.QuadPart = DeviceExtension->VolumeInfo.FreeClusters;
    FsSizeInfo->TotalAllocationUnits.QuadPart = DeviceExtension->VolumeInfo.ClusterCount;
    FsSizeInfo->SectorsPerAllocationUnit = DeviceExtension->VolumeInfo.SectorsPerCluster;
    FsSizeInfo->BytesPerSector = DeviceExtension->VolumeInfo.BytesPerSector;

    DbgPrint("ntfslx: FsSizeInfo total=%I64u avail=%I64u spau=%lu bps=%lu\n",
             FsSizeInfo->TotalAllocationUnits.QuadPart,
             FsSizeInfo->AvailableAllocationUnits.QuadPart,
             FsSizeInfo->SectorsPerAllocationUnit,
             FsSizeInfo->BytesPerSector);

    *ReturnLength = sizeof(FILE_FS_SIZE_INFORMATION);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxGetFsFullSizeInformation(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(BufferLength) PFILE_FS_FULL_SIZE_INFORMATION FsFullSizeInfo,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnLength)
{
    if (BufferLength < sizeof(FILE_FS_FULL_SIZE_INFORMATION))
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    FsFullSizeInfo->TotalAllocationUnits.QuadPart = DeviceExtension->VolumeInfo.ClusterCount;
    FsFullSizeInfo->CallerAvailableAllocationUnits.QuadPart = DeviceExtension->VolumeInfo.FreeClusters;
    FsFullSizeInfo->ActualAvailableAllocationUnits.QuadPart = DeviceExtension->VolumeInfo.FreeClusters;
    FsFullSizeInfo->SectorsPerAllocationUnit = DeviceExtension->VolumeInfo.SectorsPerCluster;
    FsFullSizeInfo->BytesPerSector = DeviceExtension->VolumeInfo.BytesPerSector;

    DbgPrint("ntfslx: FsFullSizeInfo total=%I64u callerAvail=%I64u actualAvail=%I64u spau=%lu bps=%lu\n",
             FsFullSizeInfo->TotalAllocationUnits.QuadPart,
             FsFullSizeInfo->CallerAvailableAllocationUnits.QuadPart,
             FsFullSizeInfo->ActualAvailableAllocationUnits.QuadPart,
             FsFullSizeInfo->SectorsPerAllocationUnit,
             FsFullSizeInfo->BytesPerSector);

    *ReturnLength = sizeof(FILE_FS_FULL_SIZE_INFORMATION);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxGetFsDeviceInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_writes_bytes_(BufferLength) PFILE_FS_DEVICE_INFORMATION FsDeviceInfo,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnLength)
{
    if (BufferLength < sizeof(FILE_FS_DEVICE_INFORMATION))
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    FsDeviceInfo->DeviceType = FILE_DEVICE_DISK;
    FsDeviceInfo->Characteristics = DeviceObject->Characteristics;

    DbgPrint("ntfslx: FsDeviceInfo deviceType=0x%08lx characteristics=0x%08lx\n",
             FsDeviceInfo->DeviceType,
             FsDeviceInfo->Characteristics);

    *ReturnLength = sizeof(FILE_FS_DEVICE_INFORMATION);
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxQueryVolumeInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PVOID SystemBuffer;
    ULONG BufferLength;
    ULONG ReturnLength;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    SystemBuffer = Irp->AssociatedIrp.SystemBuffer;
    BufferLength = Stack->Parameters.QueryVolume.Length;
    ReturnLength = 0;

    DbgPrint("ntfslx: QueryVolumeInformation begin class=%s(%d) buflen=%lu DevObj=%p Kind=%d\n",
             NtfslxFsInfoClassName(Stack->Parameters.QueryVolume.FsInformationClass),
             Stack->Parameters.QueryVolume.FsInformationClass,
             BufferLength,
             DeviceObject,
             DeviceExtension->Kind);

    if (!NtfslxIsVolumeDevice(DeviceExtension))
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    else
    {
        switch (Stack->Parameters.QueryVolume.FsInformationClass)
        {
            case FileFsVolumeInformation:
                Status = NtfslxGetFsVolumeInformation(DeviceExtension,
                                                     SystemBuffer,
                                                     BufferLength,
                                                     &ReturnLength);
                break;

            case FileFsAttributeInformation:
                Status = NtfslxGetFsAttributeInformation(SystemBuffer,
                                                        BufferLength,
                                                        &ReturnLength);
                break;

            case FileFsSizeInformation:
                Status = NtfslxGetFsSizeInformation(DeviceExtension,
                                                   SystemBuffer,
                                                   BufferLength,
                                                   &ReturnLength);
                break;

            case FileFsFullSizeInformation:
                Status = NtfslxGetFsFullSizeInformation(DeviceExtension,
                                                        SystemBuffer,
                                                        BufferLength,
                                                        &ReturnLength);
                break;

            case FileFsDeviceInformation:
                Status = NtfslxGetFsDeviceInformation(DeviceObject,
                                                     SystemBuffer,
                                                     BufferLength,
                                                     &ReturnLength);
                break;

            default:
                Status = STATUS_INVALID_INFO_CLASS;
                break;
        }
    }

    DbgPrint("ntfslx: QueryVolumeInformation done class=%s(%d) status=0x%08lx info=%lu\n",
             NtfslxFsInfoClassName(Stack->Parameters.QueryVolume.FsInformationClass),
             Stack->Parameters.QueryVolume.FsInformationClass,
             Status,
             ReturnLength);

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = ReturnLength;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
