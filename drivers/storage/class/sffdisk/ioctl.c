/*
 * PROJECT:     ReactOS SD/MMC Storage Class Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     IOCTL dispatch for disk geometry, partition info, and storage queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *******************************************************************/

#include "sffdisk.h"
#include <scsi.h>
#include <storduid.h>
#include <mountmgr.h>
#include <mountdev.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, SffdiskDeviceControl)
#endif

static const CHAR SffdiskFallbackVendorId[] = "SD";
static const CHAR SffdiskFallbackProductId[] = "Card";
static const CHAR SffdiskFallbackProductRevision[] = "1.0";

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
SffdiskFormatVendorId(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize)
{
    if (!DeviceExtension->CidValid)
    {
        ULONG Len = (ULONG)strlen(SffdiskFallbackVendorId) + 1;
        if (BufferSize >= Len)
        {
            RtlCopyMemory(Buffer, SffdiskFallbackVendorId, Len);
        }
        return Len;
    }

    return (ULONG)_snprintf(Buffer, BufferSize, "SD MID=%02X",
                            DeviceExtension->CardId.ManufacturerId) + 1;
}

static
ULONG
SffdiskFormatProductId(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize)
{
    if (!DeviceExtension->CidValid)
    {
        ULONG Len = (ULONG)strlen(SffdiskFallbackProductId) + 1;
        if (BufferSize >= Len)
        {
            RtlCopyMemory(Buffer, SffdiskFallbackProductId, Len);
        }
        return Len;
    }

    if (BufferSize >= 6)
    {
        ULONG i;
        for (i = 0; i < 5; i++)
        {
            UCHAR c = DeviceExtension->CardId.ProductName[i];
            Buffer[i] = (c >= 0x20 && c < 0x7F) ? (CHAR)c : '_';
        }
        Buffer[5] = '\0';
    }
    return 6;
}

static
ULONG
SffdiskFormatProductRevision(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize)
{
    if (!DeviceExtension->CidValid)
    {
        ULONG Len = (ULONG)strlen(SffdiskFallbackProductRevision) + 1;
        if (BufferSize >= Len)
        {
            RtlCopyMemory(Buffer, SffdiskFallbackProductRevision, Len);
        }
        return Len;
    }

    return (ULONG)_snprintf(Buffer, BufferSize, "%u.%u",
                            (DeviceExtension->CardId.ProductRevision >> 4) & 0x0F,
                            DeviceExtension->CardId.ProductRevision & 0x0F) + 1;
}

static
ULONG
SffdiskFormatSerialNumber(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize)
{
    if (!DeviceExtension->CidValid)
    {
        if (BufferSize >= 1)
        {
            Buffer[0] = '\0';
        }
        return 1;
    }

    return (ULONG)_snprintf(Buffer, BufferSize, "%08X",
                            DeviceExtension->CardId.ProductSerialNumber) + 1;
}

/**
 * @brief Handle IOCTL_DISK_GET_DRIVE_GEOMETRY by returning the disk geometry
 *        computed during device start.
 *
 * @param[in]     DeviceExtension  Pointer to the sffdisk device extension.
 * @param[in,out] Irp              Pointer to the I/O request packet.
 *
 * @return STATUS_SUCCESS or STATUS_BUFFER_TOO_SMALL.
 */
static
NTSTATUS
SffdiskHandleGetDriveGeometry(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PDISK_GEOMETRY Geometry;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(DISK_GEOMETRY))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Geometry = (PDISK_GEOMETRY)Irp->AssociatedIrp.SystemBuffer;

    *Geometry = DeviceExtension->DiskGeometry;

    Irp->IoStatus.Information = sizeof(DISK_GEOMETRY);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IOCTL_DISK_GET_DRIVE_GEOMETRY_EX by returning the disk geometry
 *        plus the total disk size.
 *
 * This is the extended version required by partmgr.
 *
 * @param[in]     DeviceExtension  Pointer to the sffdisk device extension.
 * @param[in,out] Irp              Pointer to the I/O request packet.
 *
 * @return STATUS_SUCCESS or STATUS_BUFFER_TOO_SMALL.
 */
static
NTSTATUS
SffdiskHandleGetDriveGeometryEx(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PDISK_GEOMETRY_EX GeometryEx;
    ULONG OutputLength;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    OutputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (OutputLength < FIELD_OFFSET(DISK_GEOMETRY_EX, Data))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    GeometryEx = (PDISK_GEOMETRY_EX)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(GeometryEx, FIELD_OFFSET(DISK_GEOMETRY_EX, Data));

    GeometryEx->Geometry = DeviceExtension->DiskGeometry;
    GeometryEx->DiskSize.QuadPart =
        (LONGLONG)DeviceExtension->TotalSectors *
        (LONGLONG)DeviceExtension->BytesPerSector;

    Irp->IoStatus.Information = FIELD_OFFSET(DISK_GEOMETRY_EX, Data);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IOCTL_DISK_GET_LENGTH_INFO by returning the total disk length.
 *
 * @param[in]     DeviceExtension  Pointer to the sffdisk device extension.
 * @param[in,out] Irp              Pointer to the I/O request packet.
 *
 * @return STATUS_SUCCESS or STATUS_BUFFER_TOO_SMALL.
 */
static
NTSTATUS
SffdiskHandleGetLengthInfo(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PGET_LENGTH_INFORMATION LengthInfo;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(GET_LENGTH_INFORMATION))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    LengthInfo = (PGET_LENGTH_INFORMATION)Irp->AssociatedIrp.SystemBuffer;

    LengthInfo->Length.QuadPart =
        (LONGLONG)DeviceExtension->TotalSectors *
        (LONGLONG)DeviceExtension->BytesPerSector;

    Irp->IoStatus.Information = sizeof(GET_LENGTH_INFORMATION);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IOCTL_STORAGE_GET_MEDIA_TYPES_EX by returning a single media
 *        info entry for the SD/eMMC media.
 *
 * @param[in]     DeviceExtension  Pointer to the sffdisk device extension.
 * @param[in,out] Irp              Pointer to the I/O request packet.
 *
 * @return STATUS_SUCCESS or STATUS_BUFFER_TOO_SMALL.
 */
static
NTSTATUS
SffdiskHandleGetMediaTypes(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PGET_MEDIA_TYPES MediaTypes;
    PDEVICE_MEDIA_INFO MediaInfo;
    ULONG RequiredSize;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    RequiredSize = sizeof(GET_MEDIA_TYPES);

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < RequiredSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    MediaTypes = (PGET_MEDIA_TYPES)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(MediaTypes, RequiredSize);

    MediaTypes->DeviceType = FILE_DEVICE_DISK;
    MediaTypes->MediaInfoCount = 1;

    MediaInfo = &MediaTypes->MediaInfo[0];

    if (DeviceExtension->Removable)
    {
        MediaInfo->DeviceSpecific.RemovableDiskInfo.Cylinders =
            DeviceExtension->DiskGeometry.Cylinders;
        MediaInfo->DeviceSpecific.RemovableDiskInfo.MediaType =
            (STORAGE_MEDIA_TYPE)RemovableMedia;
        MediaInfo->DeviceSpecific.RemovableDiskInfo.TracksPerCylinder =
            DeviceExtension->DiskGeometry.TracksPerCylinder;
        MediaInfo->DeviceSpecific.RemovableDiskInfo.SectorsPerTrack =
            DeviceExtension->DiskGeometry.SectorsPerTrack;
        MediaInfo->DeviceSpecific.RemovableDiskInfo.BytesPerSector =
            DeviceExtension->BytesPerSector;
        MediaInfo->DeviceSpecific.RemovableDiskInfo.NumberMediaSides = 1;
        MediaInfo->DeviceSpecific.RemovableDiskInfo.MediaCharacteristics =
            MEDIA_READ_WRITE;

        if (DeviceExtension->WriteProtected)
        {
            MediaInfo->DeviceSpecific.RemovableDiskInfo.MediaCharacteristics |=
                MEDIA_WRITE_PROTECTED;
        }
    }
    else
    {
        MediaInfo->DeviceSpecific.DiskInfo.Cylinders =
            DeviceExtension->DiskGeometry.Cylinders;
        MediaInfo->DeviceSpecific.DiskInfo.MediaType =
            (STORAGE_MEDIA_TYPE)FixedMedia;
        MediaInfo->DeviceSpecific.DiskInfo.TracksPerCylinder =
            DeviceExtension->DiskGeometry.TracksPerCylinder;
        MediaInfo->DeviceSpecific.DiskInfo.SectorsPerTrack =
            DeviceExtension->DiskGeometry.SectorsPerTrack;
        MediaInfo->DeviceSpecific.DiskInfo.BytesPerSector =
            DeviceExtension->BytesPerSector;
        MediaInfo->DeviceSpecific.DiskInfo.NumberMediaSides = 1;
        MediaInfo->DeviceSpecific.DiskInfo.MediaCharacteristics =
            MEDIA_READ_WRITE;

        if (DeviceExtension->WriteProtected)
        {
            MediaInfo->DeviceSpecific.DiskInfo.MediaCharacteristics |=
                MEDIA_WRITE_PROTECTED;
        }
    }

    Irp->IoStatus.Information = RequiredSize;
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IOCTL_DISK_IS_WRITABLE.
 *
 * @param[in]     DeviceExtension  Pointer to the sffdisk device extension.
 * @param[in,out] Irp              Pointer to the I/O request packet (unused).
 *
 * @return STATUS_SUCCESS if the disk is writable,
 *         STATUS_MEDIA_WRITE_PROTECTED otherwise.
 */
static
NTSTATUS
SffdiskHandleIsWritable(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Irp);

    if (DeviceExtension->WriteProtected)
    {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskQueryDeviceProperty(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ ULONG OutputLength)
{
    PSTORAGE_DEVICE_DESCRIPTOR Descriptor;
    CHAR VendorBuf[32];
    CHAR ProductBuf[16];
    CHAR RevisionBuf[8];
    CHAR SerialBuf[16];
    ULONG VendorLen;
    ULONG ProductLen;
    ULONG RevisionLen;
    ULONG SerialLen;
    ULONG RequiredSize;
    ULONG Offset;

    VendorLen = SffdiskFormatVendorId(DeviceExtension, VendorBuf, sizeof(VendorBuf));
    ProductLen = SffdiskFormatProductId(DeviceExtension, ProductBuf, sizeof(ProductBuf));
    RevisionLen = SffdiskFormatProductRevision(DeviceExtension, RevisionBuf, sizeof(RevisionBuf));
    SerialLen = SffdiskFormatSerialNumber(DeviceExtension, SerialBuf, sizeof(SerialBuf));

    RequiredSize = sizeof(STORAGE_DEVICE_DESCRIPTOR) - 1 +
                   VendorLen + ProductLen + RevisionLen + SerialLen;

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (OutputLength < RequiredSize)
    {
        PSTORAGE_DESCRIPTOR_HEADER Header;
        Header = (PSTORAGE_DESCRIPTOR_HEADER)Irp->AssociatedIrp.SystemBuffer;
        Header->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
        Header->Size = RequiredSize;
        Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
        return STATUS_SUCCESS;
    }

    Descriptor = (PSTORAGE_DEVICE_DESCRIPTOR)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Descriptor, RequiredSize);

    Descriptor->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
    Descriptor->Size = RequiredSize;
    Descriptor->DeviceType = DIRECT_ACCESS_DEVICE;
    Descriptor->DeviceTypeModifier = 0;
    Descriptor->RemovableMedia = DeviceExtension->Removable;
    Descriptor->CommandQueueing = FALSE;
    Descriptor->BusType =
        (DeviceExtension->CardType == SdCardTypeEmmc ||
         DeviceExtension->CardType == SdCardTypeMmc)
            ? BusTypeMmc : BusTypeSd;

    Offset = sizeof(STORAGE_DEVICE_DESCRIPTOR) - 1;

    Descriptor->VendorIdOffset = Offset;
    RtlCopyMemory((PUCHAR)Descriptor + Offset, VendorBuf, VendorLen);
    Offset += VendorLen;

    Descriptor->ProductIdOffset = Offset;
    RtlCopyMemory((PUCHAR)Descriptor + Offset, ProductBuf, ProductLen);
    Offset += ProductLen;

    Descriptor->ProductRevisionOffset = Offset;
    RtlCopyMemory((PUCHAR)Descriptor + Offset, RevisionBuf, RevisionLen);
    Offset += RevisionLen;

    if (SerialLen > 1 && SerialBuf[0] != '\0')
    {
        Descriptor->SerialNumberOffset = Offset;
        RtlCopyMemory((PUCHAR)Descriptor + Offset, SerialBuf, SerialLen);
        Offset += SerialLen;
    }
    else
    {
        Descriptor->SerialNumberOffset = 0;
    }

    Descriptor->RawPropertiesLength = 0;

    Irp->IoStatus.Information = RequiredSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskQueryAdapterProperty(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ ULONG OutputLength)
{
    PSTORAGE_ADAPTER_DESCRIPTOR Descriptor;
    ULONG RequiredSize = sizeof(STORAGE_ADAPTER_DESCRIPTOR);

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (OutputLength < RequiredSize)
    {
        PSTORAGE_DESCRIPTOR_HEADER Header;
        Header = (PSTORAGE_DESCRIPTOR_HEADER)Irp->AssociatedIrp.SystemBuffer;
        Header->Version = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
        Header->Size = RequiredSize;
        Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
        return STATUS_SUCCESS;
    }

    Descriptor = (PSTORAGE_ADAPTER_DESCRIPTOR)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Descriptor, RequiredSize);

    Descriptor->Version = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
    Descriptor->Size = RequiredSize;
    Descriptor->MaximumTransferLength = SFFDISK_MAX_TRANSFER_SIZE;
    Descriptor->MaximumPhysicalPages = (SFFDISK_MAX_TRANSFER_SIZE / PAGE_SIZE) + 1;
    Descriptor->AlignmentMask = DeviceExtension->Self->AlignmentRequirement;
    Descriptor->AdapterUsesPio = FALSE;
    Descriptor->AdapterScansDown = FALSE;
    Descriptor->CommandQueueing = FALSE;
    Descriptor->AcceleratedTransfer = FALSE;
    Descriptor->BusType =
        (DeviceExtension->CardType == SdCardTypeEmmc ||
         DeviceExtension->CardType == SdCardTypeMmc)
            ? BusTypeMmc : BusTypeSd;
    Descriptor->BusMajorVersion = 2;
    Descriptor->BusMinorVersion = 0;

    Irp->IoStatus.Information = RequiredSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskQueryDeviceUniqueId(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ ULONG OutputLength)
{
    PSTORAGE_DEVICE_UNIQUE_IDENTIFIER Duid;
    PSTORAGE_DEVICE_ID_DESCRIPTOR IdDesc;
    PSTORAGE_IDENTIFIER Ident;
    CHAR NameString[64];
    ULONG NameStringLen;
    ULONG AlignedNameStringLen;
    ULONG IdentSize;
    ULONG IdDescSize;
    ULONG RequiredSize;

    if (!DeviceExtension->CidValid)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(NameString, sizeof(NameString));
    NameStringLen = (ULONG)_snprintf(
        NameString, sizeof(NameString),
        "SD:MID=%02X:OID=%04X:PNM=%c%c%c%c%c:PSN=%08X",
        DeviceExtension->CardId.ManufacturerId,
        DeviceExtension->CardId.OemId,
        DeviceExtension->CardId.ProductName[0] >= 0x20 &&
            DeviceExtension->CardId.ProductName[0] < 0x7F
                ? DeviceExtension->CardId.ProductName[0] : '_',
        DeviceExtension->CardId.ProductName[1] >= 0x20 &&
            DeviceExtension->CardId.ProductName[1] < 0x7F
                ? DeviceExtension->CardId.ProductName[1] : '_',
        DeviceExtension->CardId.ProductName[2] >= 0x20 &&
            DeviceExtension->CardId.ProductName[2] < 0x7F
                ? DeviceExtension->CardId.ProductName[2] : '_',
        DeviceExtension->CardId.ProductName[3] >= 0x20 &&
            DeviceExtension->CardId.ProductName[3] < 0x7F
                ? DeviceExtension->CardId.ProductName[3] : '_',
        DeviceExtension->CardId.ProductName[4] >= 0x20 &&
            DeviceExtension->CardId.ProductName[4] < 0x7F
                ? DeviceExtension->CardId.ProductName[4] : '_',
        DeviceExtension->CardId.ProductSerialNumber) + 1;

    AlignedNameStringLen = (NameStringLen + 3) & ~3UL;

    IdentSize = FIELD_OFFSET(STORAGE_IDENTIFIER, Identifier) + AlignedNameStringLen;
    IdDescSize = FIELD_OFFSET(STORAGE_DEVICE_ID_DESCRIPTOR, Identifiers) + IdentSize;
    RequiredSize = sizeof(STORAGE_DEVICE_UNIQUE_IDENTIFIER) + IdDescSize;

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (OutputLength < RequiredSize)
    {
        PSTORAGE_DESCRIPTOR_HEADER Header;
        Header = (PSTORAGE_DESCRIPTOR_HEADER)Irp->AssociatedIrp.SystemBuffer;
        Header->Version = DUID_VERSION_1;
        Header->Size = RequiredSize;
        Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
        return STATUS_SUCCESS;
    }

    Duid = (PSTORAGE_DEVICE_UNIQUE_IDENTIFIER)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Duid, RequiredSize);

    Duid->Version = DUID_VERSION_1;
    Duid->Size = RequiredSize;
    Duid->StorageDeviceIdOffset = sizeof(STORAGE_DEVICE_UNIQUE_IDENTIFIER);
    Duid->StorageDeviceOffset = 0;
    Duid->DriveLayoutSignatureOffset = 0;

    IdDesc = (PSTORAGE_DEVICE_ID_DESCRIPTOR)((PUCHAR)Duid +
                                             Duid->StorageDeviceIdOffset);
    IdDesc->Version = 1;
    IdDesc->Size = IdDescSize;
    IdDesc->NumberOfIdentifiers = 1;

    Ident = (PSTORAGE_IDENTIFIER)IdDesc->Identifiers;
    Ident->CodeSet = StorageIdCodeSetUtf8;
    Ident->Type = StorageIdTypeScsiNameString;
    Ident->IdentifierSize = (USHORT)NameStringLen;
    Ident->NextOffset = 0;
    Ident->Association = StorageIdAssocDevice;
    RtlCopyMemory(Ident->Identifier, NameString, NameStringLen);

    Irp->IoStatus.Information = RequiredSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskQueryAccessAlignment(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ ULONG OutputLength)
{
    PSTORAGE_ACCESS_ALIGNMENT_DESCRIPTOR Descriptor;
    ULONG RequiredSize = sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR);

    UNREFERENCED_PARAMETER(DeviceExtension);

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (OutputLength < RequiredSize)
    {
        PSTORAGE_DESCRIPTOR_HEADER Header;
        Header = (PSTORAGE_DESCRIPTOR_HEADER)Irp->AssociatedIrp.SystemBuffer;
        Header->Version = sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR);
        Header->Size = RequiredSize;
        Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
        return STATUS_SUCCESS;
    }

    Descriptor = (PSTORAGE_ACCESS_ALIGNMENT_DESCRIPTOR)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Descriptor, RequiredSize);

    Descriptor->Version = sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR);
    Descriptor->Size = RequiredSize;
    Descriptor->BytesPerCacheLine = 0;
    Descriptor->BytesOffsetForCacheAlignment = 0;
    Descriptor->BytesPerLogicalSector = SD_DEFAULT_SECTOR_SIZE;
    Descriptor->BytesPerPhysicalSector = SD_DEFAULT_SECTOR_SIZE;
    Descriptor->BytesOffsetForSectorAlignment = 0;

    Irp->IoStatus.Information = RequiredSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskQuerySeekPenalty(
    _Inout_ PIRP Irp,
    _In_ ULONG OutputLength)
{
    PDEVICE_SEEK_PENALTY_DESCRIPTOR Descriptor;
    ULONG RequiredSize = sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR);

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (OutputLength < RequiredSize)
    {
        PSTORAGE_DESCRIPTOR_HEADER Header;
        Header = (PSTORAGE_DESCRIPTOR_HEADER)Irp->AssociatedIrp.SystemBuffer;
        Header->Version = sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR);
        Header->Size = RequiredSize;
        Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
        return STATUS_SUCCESS;
    }

    Descriptor = (PDEVICE_SEEK_PENALTY_DESCRIPTOR)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Descriptor, RequiredSize);

    Descriptor->Version = sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR);
    Descriptor->Size = RequiredSize;
    Descriptor->IncursSeekPenalty = FALSE;

    Irp->IoStatus.Information = RequiredSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskQueryTrim(
    _Inout_ PIRP Irp,
    _In_ ULONG OutputLength)
{
    PDEVICE_TRIM_DESCRIPTOR Descriptor;
    ULONG RequiredSize = sizeof(DEVICE_TRIM_DESCRIPTOR);

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (OutputLength < RequiredSize)
    {
        PSTORAGE_DESCRIPTOR_HEADER Header;
        Header = (PSTORAGE_DESCRIPTOR_HEADER)Irp->AssociatedIrp.SystemBuffer;
        Header->Version = sizeof(DEVICE_TRIM_DESCRIPTOR);
        Header->Size = RequiredSize;
        Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
        return STATUS_SUCCESS;
    }

    Descriptor = (PDEVICE_TRIM_DESCRIPTOR)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Descriptor, RequiredSize);

    Descriptor->Version = sizeof(DEVICE_TRIM_DESCRIPTOR);
    Descriptor->Size = RequiredSize;
    Descriptor->TrimEnabled = FALSE;

    Irp->IoStatus.Information = RequiredSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskQueryWriteCache(
    _Inout_ PIRP Irp,
    _In_ ULONG OutputLength)
{
    PSTORAGE_WRITE_CACHE_PROPERTY Descriptor;
    ULONG RequiredSize = sizeof(STORAGE_WRITE_CACHE_PROPERTY);

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (OutputLength < RequiredSize)
    {
        PSTORAGE_DESCRIPTOR_HEADER Header;
        Header = (PSTORAGE_DESCRIPTOR_HEADER)Irp->AssociatedIrp.SystemBuffer;
        Header->Version = sizeof(STORAGE_WRITE_CACHE_PROPERTY);
        Header->Size = RequiredSize;
        Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
        return STATUS_SUCCESS;
    }

    Descriptor = (PSTORAGE_WRITE_CACHE_PROPERTY)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Descriptor, RequiredSize);

    Descriptor->Version = sizeof(STORAGE_WRITE_CACHE_PROPERTY);
    Descriptor->Size = RequiredSize;
    Descriptor->WriteCacheType = WriteCacheTypeNone;
    Descriptor->WriteCacheEnabled = WriteCacheDisabled;
    Descriptor->WriteCacheChangeable = WriteCacheNotChangeable;
    Descriptor->WriteThroughSupported = WriteThroughSupported;
    Descriptor->FlushCacheSupported = FALSE;
    Descriptor->UserDefinedPowerProtection = FALSE;
    Descriptor->NVCacheEnabled = FALSE;

    Irp->IoStatus.Information = RequiredSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskHandleStorageQueryProperty(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PSTORAGE_PROPERTY_QUERY Query;
    ULONG OutputLength;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    OutputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(STORAGE_PROPERTY_QUERY))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Query = (PSTORAGE_PROPERTY_QUERY)Irp->AssociatedIrp.SystemBuffer;

    if (Query->QueryType == PropertyExistsQuery)
    {
        switch (Query->PropertyId)
        {
            case StorageDeviceProperty:
            case StorageAdapterProperty:
            case StorageDeviceUniqueIdProperty:
            case StorageAccessAlignmentProperty:
            case StorageDeviceSeekPenaltyProperty:
            case StorageDeviceTrimProperty:
            case StorageDeviceWriteCacheProperty:
                Irp->IoStatus.Information = 0;
                return STATUS_SUCCESS;

            default:
                return STATUS_NOT_SUPPORTED;
        }
    }

    if (Query->QueryType != PropertyStandardQuery)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Query->PropertyId == StorageDeviceUniqueIdProperty &&
        Query->AdditionalParameters[0] != DUID_HARDWARE_IDS_ONLY &&
        Query->AdditionalParameters[0] != DUID_INCLUDE_SOFTWARE_IDS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    switch (Query->PropertyId)
    {
        case StorageDeviceProperty:
            return SffdiskQueryDeviceProperty(DeviceExtension, Irp, OutputLength);

        case StorageAdapterProperty:
            return SffdiskQueryAdapterProperty(DeviceExtension, Irp, OutputLength);

        case StorageDeviceUniqueIdProperty:
            return SffdiskQueryDeviceUniqueId(DeviceExtension, Irp, OutputLength);

        case StorageAccessAlignmentProperty:
            return SffdiskQueryAccessAlignment(DeviceExtension, Irp, OutputLength);

        case StorageDeviceSeekPenaltyProperty:
            return SffdiskQuerySeekPenalty(Irp, OutputLength);

        case StorageDeviceTrimProperty:
            return SffdiskQueryTrim(Irp, OutputLength);

        case StorageDeviceWriteCacheProperty:
            return SffdiskQueryWriteCache(Irp, OutputLength);

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
SffdiskHandleMountdevQueryDeviceName(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PMOUNTDEV_NAME Name;
    ULONG OutputLength;
    ULONG RequiredLength;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    OutputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (DeviceExtension->DeviceName.Buffer == NULL ||
        DeviceExtension->DeviceName.Length == 0)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (OutputLength < sizeof(MOUNTDEV_NAME))
    {
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Name = (PMOUNTDEV_NAME)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Name, sizeof(MOUNTDEV_NAME));
    Name->NameLength = DeviceExtension->DeviceName.Length;

    RequiredLength = FIELD_OFFSET(MOUNTDEV_NAME, Name) +
                     DeviceExtension->DeviceName.Length;

    if (OutputLength < RequiredLength)
    {
        Irp->IoStatus.Information = sizeof(MOUNTDEV_NAME);
        return STATUS_BUFFER_OVERFLOW;
    }

    RtlCopyMemory(Name->Name,
                  DeviceExtension->DeviceName.Buffer,
                  DeviceExtension->DeviceName.Length);

    Irp->IoStatus.Information = RequiredLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskHandleMountdevQueryUniqueId(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PMOUNTDEV_UNIQUE_ID UniqueId;
    CHAR IdBuffer[64];
    ULONG IdLength;
    ULONG OutputLength;
    ULONG RequiredLength;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    OutputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (!DeviceExtension->CidValid)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    IdLength = (ULONG)_snprintf(
        IdBuffer, sizeof(IdBuffer),
        "SD:MID=%02X:OID=%04X:PNM=%c%c%c%c%c:PSN=%08X",
        DeviceExtension->CardId.ManufacturerId,
        DeviceExtension->CardId.OemId,
        DeviceExtension->CardId.ProductName[0] >= 0x20 &&
            DeviceExtension->CardId.ProductName[0] < 0x7F
                ? DeviceExtension->CardId.ProductName[0] : '_',
        DeviceExtension->CardId.ProductName[1] >= 0x20 &&
            DeviceExtension->CardId.ProductName[1] < 0x7F
                ? DeviceExtension->CardId.ProductName[1] : '_',
        DeviceExtension->CardId.ProductName[2] >= 0x20 &&
            DeviceExtension->CardId.ProductName[2] < 0x7F
                ? DeviceExtension->CardId.ProductName[2] : '_',
        DeviceExtension->CardId.ProductName[3] >= 0x20 &&
            DeviceExtension->CardId.ProductName[3] < 0x7F
                ? DeviceExtension->CardId.ProductName[3] : '_',
        DeviceExtension->CardId.ProductName[4] >= 0x20 &&
            DeviceExtension->CardId.ProductName[4] < 0x7F
                ? DeviceExtension->CardId.ProductName[4] : '_',
        DeviceExtension->CardId.ProductSerialNumber);

    if (OutputLength < sizeof(MOUNTDEV_UNIQUE_ID))
    {
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    UniqueId = (PMOUNTDEV_UNIQUE_ID)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(UniqueId, sizeof(MOUNTDEV_UNIQUE_ID));
    UniqueId->UniqueIdLength = (USHORT)IdLength;

    RequiredLength = FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId) + IdLength;

    if (OutputLength < RequiredLength)
    {
        Irp->IoStatus.Information = sizeof(MOUNTDEV_UNIQUE_ID);
        return STATUS_BUFFER_OVERFLOW;
    }

    RtlCopyMemory(UniqueId->UniqueId, IdBuffer, IdLength);

    Irp->IoStatus.Information = RequiredLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskHandleMountdevQuerySuggestedLinkName(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PMOUNTDEV_SUGGESTED_LINK_NAME LinkName;
    WCHAR SuggestedName[32];
    USHORT NameLength;
    ULONG OutputLength;
    ULONG RequiredLength;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    OutputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (!DeviceExtension->CidValid)
    {
        return STATUS_NOT_FOUND;
    }

    _snwprintf(SuggestedName,
               RTL_NUMBER_OF(SuggestedName),
               L"\\DosDevices\\SD%08X",
               DeviceExtension->CardId.ProductSerialNumber);
    SuggestedName[RTL_NUMBER_OF(SuggestedName) - 1] = UNICODE_NULL;

    NameLength = (USHORT)(wcslen(SuggestedName) * sizeof(WCHAR));

    if (OutputLength < sizeof(MOUNTDEV_SUGGESTED_LINK_NAME))
    {
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    LinkName = (PMOUNTDEV_SUGGESTED_LINK_NAME)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(LinkName, sizeof(MOUNTDEV_SUGGESTED_LINK_NAME));
    LinkName->UseOnlyIfThereAreNoOtherLinks = FALSE;
    LinkName->NameLength = NameLength;

    RequiredLength = FIELD_OFFSET(MOUNTDEV_SUGGESTED_LINK_NAME, Name) + NameLength;

    if (OutputLength < RequiredLength)
    {
        Irp->IoStatus.Information = sizeof(MOUNTDEV_SUGGESTED_LINK_NAME);
        return STATUS_BUFFER_OVERFLOW;
    }

    RtlCopyMemory(LinkName->Name, SuggestedName, NameLength);

    Irp->IoStatus.Information = RequiredLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskHandleQueryProtocol(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PSFFDISK_QUERY_DEVICE_PROTOCOL_DATA Protocol;
    static const GUID SdProtocolGuid = GUID_SFF_PROTOCOL_SD;
    static const GUID MmcProtocolGuid = GUID_SFF_PROTOCOL_MMC;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Protocol))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Protocol = (PSFFDISK_QUERY_DEVICE_PROTOCOL_DATA)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Protocol, sizeof(*Protocol));
    Protocol->Size = sizeof(*Protocol);

    switch (DeviceExtension->CardType)
    {
        case SdCardTypeSdV1:
        case SdCardTypeSdV2:
        case SdCardTypeSdhc:
        case SdCardTypeSdxc:
        case SdCardTypeCombo:
            Protocol->ProtocolGUID = SdProtocolGuid;
            break;

        case SdCardTypeMmc:
        case SdCardTypeEmmc:
            Protocol->ProtocolGUID = MmcProtocolGuid;
            break;

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }

    Irp->IoStatus.Information = sizeof(*Protocol);
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffdiskHandleDeviceCommand(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PSFFDISK_DEVICE_COMMAND_DATA Data;
    PSDCMD_DESCRIPTOR CmdDesc;
    SDBUS_REQUEST_PACKET Packet;
    PMDL Mdl;
    PUCHAR DataBuffer;
    NTSTATUS Status;
    BOOLEAN CommandOnly;
    ULONG InputLength;
    ULONG OutputLength;
    ULONG HeaderSize;
    ULONG BufferOffset;
    ULONG BufferEnd;
    ULONG BytesReturned;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    InputLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    OutputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (InputLength < FIELD_OFFSET(SFFDISK_DEVICE_COMMAND_DATA, Data))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Data = (PSFFDISK_DEVICE_COMMAND_DATA)Irp->AssociatedIrp.SystemBuffer;
    HeaderSize = Data->HeaderSize;
    if (HeaderSize < FIELD_OFFSET(SFFDISK_DEVICE_COMMAND_DATA, Data) ||
        HeaderSize > InputLength ||
        (Data->Flags & ~SFFDISK_DEVCMD_VALID_FLAGS) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    switch (Data->Command)
    {
        case SFFDISK_DC_GET_VERSION:
            if (Data->ProtocolArgumentSize != 0 ||
                Data->DeviceDataBufferSize != 0 ||
                Data->Flags != 0)
            {
                return STATUS_INVALID_PARAMETER;
            }
            if (OutputLength < HeaderSize)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            Data->Information = SDBUS_DRIVER_VERSION_4;
            Irp->IoStatus.Information = HeaderSize;
            return STATUS_SUCCESS;

        case SFFDISK_DC_LOCK_CHANNEL:
            if (Data->ProtocolArgumentSize != 0 ||
                Data->DeviceDataBufferSize != 0 ||
                Data->Flags != 0)
            {
                return STATUS_INVALID_PARAMETER;
            }
            if (OutputLength < HeaderSize)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            Status = SffdiskLockChannel(DeviceExtension, IrpSp->FileObject);
            if (NT_SUCCESS(Status))
            {
                Data->Information = 0;
                Irp->IoStatus.Information = HeaderSize;
            }
            return Status;

        case SFFDISK_DC_UNLOCK_CHANNEL:
            if (Data->ProtocolArgumentSize != 0 ||
                Data->DeviceDataBufferSize != 0 ||
                Data->Flags != 0)
            {
                return STATUS_INVALID_PARAMETER;
            }
            if (OutputLength < HeaderSize)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            Status = SffdiskUnlockChannel(DeviceExtension, IrpSp->FileObject);
            if (NT_SUCCESS(Status))
            {
                Data->Information = 0;
                Irp->IoStatus.Information = HeaderSize;
            }
            return Status;

        case SFFDISK_DC_DEVICE_COMMAND:
            break;

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (Data->ProtocolArgumentSize < sizeof(SDCMD_DESCRIPTOR) ||
        Data->ProtocolArgumentSize > InputLength - HeaderSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    BufferOffset = HeaderSize + Data->ProtocolArgumentSize;
    if (BufferOffset < HeaderSize ||
        Data->DeviceDataBufferSize > MAXULONG - BufferOffset)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (OutputLength < HeaderSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    CmdDesc = (PSDCMD_DESCRIPTOR)(((PUCHAR)Data) + HeaderSize);
    CommandOnly = (CmdDesc->TransferType == SDTT_CMD_ONLY);
    BufferEnd = BufferOffset + Data->DeviceDataBufferSize;

    if (CommandOnly)
    {
        if (BufferEnd > OutputLength)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (CmdDesc->TransferDirection == SDTD_READ)
    {
        if (BufferEnd > OutputLength)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (CmdDesc->TransferDirection == SDTD_WRITE)
    {
        if (BufferEnd > InputLength)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    else if (CmdDesc->TransferDirection != SDTD_UNSPECIFIED ||
             Data->DeviceDataBufferSize != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Mdl = NULL;
    DataBuffer = NULL;
    if (Data->DeviceDataBufferSize != 0 && !CommandOnly)
    {
        DataBuffer = ((PUCHAR)Data) + BufferOffset;
        Mdl = IoAllocateMdl(DataBuffer,
                            Data->DeviceDataBufferSize,
                            FALSE,
                            FALSE,
                            NULL);
        if (Mdl == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        MmBuildMdlForNonPagedPool(Mdl);
    }

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_DEVICE_COMMAND);
    if (Data->Flags & SFFDISK_DEVCMD_FLAG_APPEND_CMD_SEQ)
    {
        Packet.Flags |= SDRP_FLAG_APPEND_CMD_SEQ;
    }
    if (Data->Flags & SFFDISK_DEVCMD_FLAG_LONG_OPERATION)
    {
        Packet.Flags |= SDRP_FLAG_WAIT_FOR_BUSY;
    }
    Packet.Parameters.DeviceCommand.CmdDesc = *CmdDesc;
    Packet.Parameters.DeviceCommand.Argument = (ULONG)Data->Information;
    Packet.Parameters.DeviceCommand.Mdl = Mdl;
    Packet.Parameters.DeviceCommand.Length = CommandOnly ? 0 : Data->DeviceDataBufferSize;

    Status = SffdiskAcquireBusRequest(DeviceExtension, IrpSp->FileObject);
    if (NT_SUCCESS(Status))
    {
        Status = SdBusSubmitRequest(DeviceExtension->BusInterface.Context, &Packet);
        SffdiskReleaseBusRequest(DeviceExtension);
    }
    Data->Information = Packet.Information;
    BytesReturned = HeaderSize;

    if (NT_SUCCESS(Status) && CommandOnly)
    {
        if (Packet.ResponseLength > sizeof(Packet.ResponseData.AsUCHAR) ||
            Packet.ResponseLength > Data->DeviceDataBufferSize)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            DataBuffer = ((PUCHAR)Data) + BufferOffset;
            if (Packet.ResponseLength != 0)
            {
                RtlCopyMemory(DataBuffer,
                              Packet.ResponseData.AsUCHAR,
                              Packet.ResponseLength);
            }
            BytesReturned = BufferOffset + Packet.ResponseLength;
        }
    }
    else if (NT_SUCCESS(Status) && CmdDesc->TransferDirection == SDTD_READ)
    {
        if (Packet.Information > Data->DeviceDataBufferSize)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
        }
        else
        {
            BytesReturned = BufferOffset + (ULONG)Packet.Information;
        }
    }

    if (Mdl != NULL)
    {
        IoFreeMdl(Mdl);
    }

    Irp->IoStatus.Information = NT_SUCCESS(Status) ? BytesReturned : 0;
    return Status;
}

/* DISPATCH FUNCTION **********************************************************/

/**
 * @brief Main IRP_MJ_DEVICE_CONTROL dispatch routine for sffdisk.
 *
 * Dispatches IOCTL requests for disk geometry, partition information,
 * media types, write-protect status, storage properties, and hotplug info.
 *
 * @param[in]     DeviceObject  Pointer to the device object.
 * @param[in,out] Irp           Pointer to the I/O request packet.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
NTAPI
SffdiskDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSFFDISK_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    PAGED_CODE();

    DeviceExtension = (PSFFDISK_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    Irp->IoStatus.Information = 0;

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_DISK_GET_DRIVE_GEOMETRY:
            Status = SffdiskHandleGetDriveGeometry(DeviceExtension, Irp);
            break;

        case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
            Status = SffdiskHandleGetDriveGeometryEx(DeviceExtension, Irp);
            break;

        case IOCTL_DISK_GET_LENGTH_INFO:
            Status = SffdiskHandleGetLengthInfo(DeviceExtension, Irp);
            break;

        case IOCTL_STORAGE_GET_MEDIA_TYPES_EX:
            Status = SffdiskHandleGetMediaTypes(DeviceExtension, Irp);
            break;

        case IOCTL_DISK_IS_WRITABLE:
            Status = SffdiskHandleIsWritable(DeviceExtension, Irp);
            break;

        case IOCTL_STORAGE_QUERY_PROPERTY:
            Status = SffdiskHandleStorageQueryProperty(DeviceExtension, Irp);
            break;

        case IOCTL_STORAGE_CHECK_VERIFY:
        case IOCTL_STORAGE_CHECK_VERIFY2:
        {
            /* Media is always present if the device is started */
            if (!DeviceExtension->MediaPresent)
            {
                Status = STATUS_NO_MEDIA_IN_DEVICE;
            }
            else
            {
                Status = STATUS_SUCCESS;
                if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength >=
                    sizeof(ULONG))
                {
                    *(PULONG)Irp->AssociatedIrp.SystemBuffer =
                        DeviceExtension->MediaChangeCount;
                    Irp->IoStatus.Information = sizeof(ULONG);
                }
            }
            break;
        }

        case IOCTL_STORAGE_GET_DEVICE_NUMBER:
        {
            PSTORAGE_DEVICE_NUMBER DeviceNumber;

            if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
                sizeof(STORAGE_DEVICE_NUMBER))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            DeviceNumber = (PSTORAGE_DEVICE_NUMBER)Irp->AssociatedIrp.SystemBuffer;
            DeviceNumber->DeviceType = FILE_DEVICE_DISK;
            DeviceNumber->DeviceNumber = DeviceExtension->DiskNumber;
            DeviceNumber->PartitionNumber = 0;

            Irp->IoStatus.Information = sizeof(STORAGE_DEVICE_NUMBER);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_STORAGE_GET_HOTPLUG_INFO:
        {
            PSTORAGE_HOTPLUG_INFO HotplugInfo;

            if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
                sizeof(STORAGE_HOTPLUG_INFO))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            HotplugInfo = (PSTORAGE_HOTPLUG_INFO)Irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(HotplugInfo, sizeof(STORAGE_HOTPLUG_INFO));

            HotplugInfo->Size = sizeof(STORAGE_HOTPLUG_INFO);
            HotplugInfo->MediaRemovable = DeviceExtension->Removable;
            HotplugInfo->MediaHotplug = DeviceExtension->Removable;
            HotplugInfo->DeviceHotplug = DeviceExtension->Removable;
            HotplugInfo->WriteCacheEnableOverride = FALSE;

            Irp->IoStatus.Information = sizeof(STORAGE_HOTPLUG_INFO);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
            Status = SffdiskHandleMountdevQueryDeviceName(DeviceExtension, Irp);
            break;

        case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
            Status = SffdiskHandleMountdevQueryUniqueId(DeviceExtension, Irp);
            break;

        case IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME:
            Status = SffdiskHandleMountdevQuerySuggestedLinkName(DeviceExtension, Irp);
            break;

        case IOCTL_SFFDISK_QUERY_DEVICE_PROTOCOL:
            Status = SffdiskHandleQueryProtocol(DeviceExtension, Irp);
            break;

        case IOCTL_SFFDISK_DEVICE_COMMAND:
            Status = SffdiskHandleDeviceCommand(DeviceExtension, Irp);
            break;

        default:
            return SffdiskForwardIrpWithRemoveLock(DeviceExtension,
                                                   Irp,
                                                   FALSE);
    }

    Irp->IoStatus.Status = Status;
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}
