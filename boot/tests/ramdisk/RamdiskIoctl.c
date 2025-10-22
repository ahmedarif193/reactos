/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Smoke-test ramdisk IOCTL coverage
 */

#include <kmt_test.h>

#include <mountdev.h>
#include <mountmgr.h>
#include <ntdddisk.h>
#include <ntddstor.h>
#include <ntddscsi.h>
#include <reactos/drivers/ntddrdsk.h>

#define IGNORE_INFORMATION ((ULONG)-1)

#ifndef IOCTL_MOUNTDEV_QUERY_DEVICE_RELATIONS
#define IOCTL_MOUNTDEV_QUERY_DEVICE_RELATIONS \
    CTL_CODE(MOUNTDEVCONTROLTYPE, 7, METHOD_BUFFERED, FILE_ANY_ACCESS)
typedef struct _MOUNTDEV_DEVICE_RELATIONS
{
    ULONG NumberOfObjects;
    PDEVICE_OBJECT Objects[1];
} MOUNTDEV_DEVICE_RELATIONS, *PMOUNTDEV_DEVICE_RELATIONS;
#endif

static NTSTATUS
RamdiskOpenFirstDiskHandle(PHANDLE Handle, PUNICODE_STRING LinkName)
{
    PWSTR SymbolicLinks = NULL;
    UNICODE_STRING FirstLink;
    IO_STATUS_BLOCK IoStatus = {0};
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    Status = IoGetDeviceInterfaces(&RamdiskDiskInterface, NULL, 0, &SymbolicLinks);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (SymbolicLinks[0] == UNICODE_NULL)
    {
        ExFreePool(SymbolicLinks);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    RtlInitUnicodeString(&FirstLink, SymbolicLinks);
    Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                       &FirstLink,
                                       LinkName);
    ExFreePool(SymbolicLinks);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               LinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateFile(Handle,
                          GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatus,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL,
                          0);

    if (!NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(LinkName);
    }

    return Status;
}

static VOID
RamdiskTestIoctl(HANDLE Handle,
                 ULONG IoControlCode,
                 PVOID InBuffer,
                 ULONG InLength,
                 PVOID OutBuffer,
                 ULONG OutLength,
                 NTSTATUS ExpectedStatus,
                 ULONG ExpectedInformation,
                 PIO_STATUS_BLOCK OptionalStatus)
{
    IO_STATUS_BLOCK IoStatus = {0};
    PIO_STATUS_BLOCK StatusBlock;
    NTSTATUS Status;

    StatusBlock = OptionalStatus ? OptionalStatus : &IoStatus;

    Status = ZwDeviceIoControlFile(Handle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   StatusBlock,
                                   IoControlCode,
                                   InBuffer,
                                   InLength,
                                   OutBuffer,
                                   OutLength);

    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    ok_ntstatus(StatusBlock->Status, ExpectedStatus);
    if (ExpectedStatus == STATUS_SUCCESS && ExpectedInformation != IGNORE_INFORMATION)
    {
        ok_eq_ulong((ULONG)StatusBlock->Information, ExpectedInformation);
    }
}

START_TEST(RamdiskIoctl)
{
    UNICODE_STRING DeviceLink;
    HANDLE DiskHandle;
    NTSTATUS Status;
    DISK_GEOMETRY Geometry = {0};
    GET_LENGTH_INFORMATION Length = {{0}};
    VOLUME_GET_GPT_ATTRIBUTES_INFORMATION GptAttributes = {0};
    BOOLEAN IsReadOnly;
    ULONGLONG DiskLength;
    ULONG DiskNumberValue = 0;

    Status = RamdiskOpenFirstDiskHandle(&DiskHandle, &DeviceLink);
    if (!NT_SUCCESS(Status))
    {
        skip(FALSE, "No ramdisk disk interfaces exposed (status %lx).\n", Status);
        return;
    }

    trace("Testing ramdisk device %wZ\n", &DeviceLink);

    /* Simple verification requests */
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_DISK_CHECK_VERIFY,
                     NULL,
                     0,
                     NULL,
                     0,
                     STATUS_SUCCESS,
                     0,
                     NULL);
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_STORAGE_CHECK_VERIFY,
                     NULL,
                     0,
                     NULL,
                     0,
                     STATUS_SUCCESS,
                     0,
                     NULL);
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_STORAGE_CHECK_VERIFY2,
                     NULL,
                     0,
                     NULL,
                     0,
                     STATUS_SUCCESS,
                     0,
                     NULL);

    /* Geometry & length queries */
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_DISK_GET_DRIVE_GEOMETRY,
                     NULL,
                     0,
                     &Geometry,
                     sizeof(Geometry),
                     STATUS_SUCCESS,
                     sizeof(Geometry),
                     NULL);

    RamdiskTestIoctl(DiskHandle,
                     IOCTL_DISK_GET_LENGTH_INFO,
                     NULL,
                     0,
                     &Length,
                     sizeof(Length),
                     STATUS_SUCCESS,
                     sizeof(Length),
                     NULL);

    DiskLength = Length.Length.QuadPart;

    {
        DISK_GEOMETRY_EX GeometryEx = {0};

        RamdiskTestIoctl(DiskHandle,
                         IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                         NULL,
                         0,
                         &GeometryEx,
                         sizeof(GeometryEx),
                         STATUS_SUCCESS,
                         sizeof(GeometryEx),
                         NULL);

        ok_eq_ulonglong(GeometryEx.DiskSize.QuadPart, DiskLength);
    }

    /* Partition info */
    {
        PARTITION_INFORMATION PartitionInfo = {0};
        RamdiskTestIoctl(DiskHandle,
                         IOCTL_DISK_GET_PARTITION_INFO,
                         NULL,
                         0,
                         &PartitionInfo,
                         sizeof(PartitionInfo),
                         STATUS_SUCCESS,
                         sizeof(PartitionInfo),
                         NULL);
    }

    {
        PARTITION_INFORMATION_EX PartitionInfoEx = {0};

        RamdiskTestIoctl(DiskHandle,
                         IOCTL_DISK_GET_PARTITION_INFO_EX,
                         NULL,
                         0,
                         &PartitionInfoEx,
                         sizeof(PartitionInfoEx),
                         STATUS_SUCCESS,
                         sizeof(PartitionInfoEx),
                         NULL);

        ok_eq_ulong(PartitionInfoEx.PartitionStyle, PARTITION_STYLE_MBR);
        ok(PartitionInfoEx.PartitionLength.QuadPart != 0,
           "PartitionInfoEx length is zero.\n");
    }

    /* GPT attributes */
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_VOLUME_GET_GPT_ATTRIBUTES,
                     NULL,
                     0,
                     &GptAttributes,
                     sizeof(GptAttributes),
                     STATUS_SUCCESS,
                     sizeof(GptAttributes),
                     NULL);

    IsReadOnly = (GptAttributes.GptAttributes & GPT_BASIC_DATA_ATTRIBUTE_READ_ONLY) != 0;

    /* Mount manager contracts */
    {
        struct
        {
            MOUNTDEV_NAME NameHeader;
            WCHAR Buffer[64];
        } NameBuffer = {0};
        IO_STATUS_BLOCK IoStatus = {0};
        ULONG ExpectedLength;

        RamdiskTestIoctl(DiskHandle,
                         IOCTL_MOUNTDEV_QUERY_DEVICE_NAME,
                         NULL,
                         0,
                         &NameBuffer,
                         sizeof(NameBuffer),
                         STATUS_SUCCESS,
                         IGNORE_INFORMATION,
                         &IoStatus);

        ExpectedLength = FIELD_OFFSET(MOUNTDEV_NAME, Name) + NameBuffer.NameHeader.NameLength;
        ok_eq_ulong((ULONG)IoStatus.Information, ExpectedLength);
        ok(NameBuffer.NameHeader.NameLength > 0,
           "Device name length should be non-zero.\n");
        ok(NameBuffer.NameHeader.Name[0] == L'\\',
           "Device name should begin with '\\'.\n");
    }

    {
        struct
        {
            MOUNTDEV_DEVICE_RELATIONS Relations;
            PDEVICE_OBJECT Extra;
        } RelationBuffer = {0};
        ULONG RequiredLength;

        RequiredLength = FIELD_OFFSET(MOUNTDEV_DEVICE_RELATIONS, Objects) + sizeof(PDEVICE_OBJECT);
        RamdiskTestIoctl(DiskHandle,
                         IOCTL_MOUNTDEV_QUERY_DEVICE_RELATIONS,
                         NULL,
                         0,
                         &RelationBuffer,
                         sizeof(RelationBuffer),
                         STATUS_SUCCESS,
                         RequiredLength,
                         NULL);

        ok_eq_ulong(RelationBuffer.Relations.NumberOfObjects, 1);
        ok(RelationBuffer.Relations.Objects[0] != NULL,
           "Device relation should return a device object.\n");
        if (RelationBuffer.Relations.Objects[0] != NULL)
        {
            ObDereferenceObject(RelationBuffer.Relations.Objects[0]);
        }
    }

    {
        MOUNTDEV_UNIQUE_ID UniqueId = {0};
        IO_STATUS_BLOCK IoStatus = {0};
        RamdiskTestIoctl(DiskHandle,
                         IOCTL_MOUNTDEV_QUERY_UNIQUE_ID,
                         NULL,
                         0,
                         &UniqueId,
                         sizeof(UniqueId),
                         STATUS_SUCCESS,
                         IGNORE_INFORMATION,
                         &IoStatus);
        ok_ntstatus(IoStatus.Status, STATUS_SUCCESS);
        ok(IoStatus.Information >= FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId),
           "Unique ID buffer unexpectedly short (%lu).\n",
           (ULONG)IoStatus.Information);
        if (IoStatus.Information >= FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId))
        {
            ok_eq_ulong((ULONG)IoStatus.Information,
                        FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId) + UniqueId.UniqueIdLength);
        }
    }

    {
        MOUNTDEV_STABLE_GUID StableGuid = {0};
        RamdiskTestIoctl(DiskHandle,
                         IOCTL_MOUNTDEV_QUERY_STABLE_GUID,
                         NULL,
                         0,
                         &StableGuid,
                         sizeof(StableGuid),
                         STATUS_SUCCESS,
                         sizeof(StableGuid),
                         NULL);
    }

    {
        struct
        {
            MOUNTDEV_SUGGESTED_LINK_NAME SuggestedLink;
            WCHAR Buffer[16];
        } Suggested = {0};
        IO_STATUS_BLOCK IoStatus = {0};

        RamdiskTestIoctl(DiskHandle,
                         IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME,
                         NULL,
                         0,
                         &Suggested,
                         sizeof(Suggested),
                         STATUS_SUCCESS,
                         IGNORE_INFORMATION,
                         &IoStatus);
        ok_ntstatus(IoStatus.Status, STATUS_SUCCESS);
        ok(IoStatus.Information >= FIELD_OFFSET(MOUNTDEV_SUGGESTED_LINK_NAME, Name),
           "Suggested link buffer unexpectedly short (%lu).\n",
           (ULONG)IoStatus.Information);
        if (IoStatus.Information >= FIELD_OFFSET(MOUNTDEV_SUGGESTED_LINK_NAME, Name))
        {
            ok_eq_ulong((ULONG)IoStatus.Information,
                        FIELD_OFFSET(MOUNTDEV_SUGGESTED_LINK_NAME, Name) +
                        Suggested.SuggestedLink.NameLength);
        }
    }

    /* Drive layout */
    {
        struct
        {
            DRIVE_LAYOUT_INFORMATION Layout;
            PARTITION_INFORMATION PartitionEntry;
        } LayoutBuffer;

        RamdiskTestIoctl(DiskHandle,
                         IOCTL_DISK_GET_DRIVE_LAYOUT,
                         NULL,
                         0,
                         &LayoutBuffer,
                         sizeof(LayoutBuffer),
                         STATUS_SUCCESS,
                         sizeof(LayoutBuffer),
                         NULL);

        ok_eq_ulong(LayoutBuffer.Layout.PartitionCount, 1);
        ok(LayoutBuffer.PartitionEntry.PartitionLength.QuadPart != 0,
           "Drive layout partition length is zero.\n");
    }

    {
        struct
        {
            DRIVE_LAYOUT_INFORMATION_EX Layout;
            PARTITION_INFORMATION_EX PartitionEntry;
        } LayoutBufferEx = {0};
        ULONG LayoutExSize = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + sizeof(PARTITION_INFORMATION_EX);

        RamdiskTestIoctl(DiskHandle,
                         IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                         NULL,
                         0,
                         &LayoutBufferEx,
                         sizeof(LayoutBufferEx),
                         STATUS_SUCCESS,
                         LayoutExSize,
                         NULL);

        ok_eq_ulong(LayoutBufferEx.Layout.PartitionStyle, PARTITION_STYLE_MBR);
        ok_eq_ulong(LayoutBufferEx.Layout.PartitionCount, 1);
        ok(LayoutBufferEx.PartitionEntry.PartitionLength.QuadPart != 0,
           "Drive layout (EX) partition length is zero.\n");
    }

    /* Writable flag */
    {
        IO_STATUS_BLOCK IoStatus = {0};
        RamdiskTestIoctl(DiskHandle,
                         IOCTL_DISK_IS_WRITABLE,
                         NULL,
                         0,
                         NULL,
                         0,
                         IsReadOnly ? STATUS_MEDIA_WRITE_PROTECTED : STATUS_SUCCESS,
                         0,
                         &IoStatus);
    }

    /* Storage descriptors */
    {
        STORAGE_PROPERTY_QUERY Query = {0};
        STORAGE_DEVICE_DESCRIPTOR Descriptor = {0};
        IO_STATUS_BLOCK IoStatus = {0};

        Query.PropertyId = StorageDeviceProperty;
        Query.QueryType = PropertyStandardQuery;

        RamdiskTestIoctl(DiskHandle,
                         IOCTL_STORAGE_QUERY_PROPERTY,
                         &Query,
                         sizeof(Query),
                         &Descriptor,
                         sizeof(Descriptor),
                         STATUS_SUCCESS,
                         sizeof(Descriptor),
                         &IoStatus);

        ok_eq_ulong((ULONG)IoStatus.Information, sizeof(Descriptor));
        ok_eq_ulong(Descriptor.Version, sizeof(STORAGE_DEVICE_DESCRIPTOR));
        ok_eq_ulong(Descriptor.Size, sizeof(STORAGE_DEVICE_DESCRIPTOR));
        ok(Descriptor.BusType == BusTypeVirtual,
           "Unexpected bus type %lu.\n",
           Descriptor.BusType);
        ok(Descriptor.DeviceType == DIRECT_ACCESS_DEVICE ||
           Descriptor.DeviceType == READ_ONLY_DIRECT_ACCESS_DEVICE,
           "Unexpected device type %lu.\n",
           Descriptor.DeviceType);

        Query.QueryType = PropertyExistsQuery;
        RamdiskTestIoctl(DiskHandle,
                         IOCTL_STORAGE_QUERY_PROPERTY,
                         &Query,
                         sizeof(Query),
                         NULL,
                         0,
                         STATUS_SUCCESS,
                         0,
                         NULL);
    }

    {
        STORAGE_DEVICE_NUMBER DeviceNumber = {0};
        RamdiskTestIoctl(DiskHandle,
                         IOCTL_STORAGE_GET_DEVICE_NUMBER,
                         NULL,
                         0,
                         &DeviceNumber,
                         sizeof(DeviceNumber),
                         STATUS_SUCCESS,
                         sizeof(DeviceNumber),
                         NULL);

        ok(DeviceNumber.DeviceType == FILE_DEVICE_DISK || DeviceNumber.DeviceType == FILE_DEVICE_CD_ROM,
           "Unexpected storage device type %lu.\n",
           DeviceNumber.DeviceType);
        ok_eq_ulong(DeviceNumber.PartitionNumber, 0);
        DiskNumberValue = DeviceNumber.DeviceNumber;
    }

    {
        STORAGE_HOTPLUG_INFO HotplugInfo = {0};
        RamdiskTestIoctl(DiskHandle,
                         IOCTL_STORAGE_GET_HOTPLUG_INFO,
                         NULL,
                         0,
                         &HotplugInfo,
                         sizeof(HotplugInfo),
                         STATUS_SUCCESS,
                         sizeof(HotplugInfo),
                         NULL);

        ok(!HotplugInfo.MediaRemovable,
           "Ramdisk should not report MediaRemovable set.\n");
        ok(!HotplugInfo.DeviceHotplug,
           "Ramdisk should not report DeviceHotplug set.\n");
    }

    {
        struct
        {
            VOLUME_DISK_EXTENTS Extents;
            DISK_EXTENT Extra;
        } ExtentBuffer;

        RamdiskTestIoctl(DiskHandle,
                         IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                         NULL,
                         0,
                         &ExtentBuffer,
                         sizeof(ExtentBuffer),
                         STATUS_SUCCESS,
                         FIELD_OFFSET(VOLUME_DISK_EXTENTS, Extents) + sizeof(DISK_EXTENT),
                         NULL);

        ok_eq_ulong(ExtentBuffer.Extents.NumberOfDiskExtents, 1);
        ok_eq_ulonglong(ExtentBuffer.Extents.Extents[0].ExtentLength.QuadPart, DiskLength);
    }

    {
        struct
        {
            VOLUME_FAILOVER_SET Set;
            ULONG Extra;
        } Failover = {0};
        ULONG RequiredLength = FIELD_OFFSET(VOLUME_FAILOVER_SET, DiskNumbers) + sizeof(ULONG);

        RamdiskTestIoctl(DiskHandle,
                         IOCTL_VOLUME_QUERY_FAILOVER_SET,
                         NULL,
                         0,
                         &Failover,
                         sizeof(Failover),
                         STATUS_SUCCESS,
                         RequiredLength,
                         NULL);

        ok_eq_ulong(Failover.Set.NumberOfDisks, 1);
        ok_eq_ulong(Failover.Set.DiskNumbers[0], DiskNumberValue);
    }

    /* Not-yet-supported operations should fail gracefully */
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_SCSI_MINIPORT,
                     NULL,
                     0,
                     NULL,
                     0,
                     STATUS_NOT_SUPPORTED,
                     0,
                     NULL);
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_SCSI_PASS_THROUGH,
                     NULL,
                     0,
                     NULL,
                     0,
                     STATUS_INVALID_DEVICE_REQUEST,
                     0,
                     NULL);
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_SCSI_PASS_THROUGH_DIRECT,
                     NULL,
                     0,
                     NULL,
                     0,
                     STATUS_INVALID_DEVICE_REQUEST,
                     0,
                     NULL);
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_SCSI_GET_ADDRESS,
                     NULL,
                     0,
                     NULL,
                     0,
                     STATUS_INVALID_DEVICE_REQUEST,
                     0,
                     NULL);
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_VOLUME_SET_GPT_ATTRIBUTES,
                     NULL,
                     0,
                     NULL,
                     0,
                     STATUS_NOT_SUPPORTED,
                     0,
                     NULL);
    RamdiskTestIoctl(DiskHandle,
                     IOCTL_VOLUME_OFFLINE,
                     NULL,
                     0,
                     NULL,
                     0,
                     STATUS_NOT_SUPPORTED,
                     0,
                     NULL);

    ZwClose(DiskHandle);
    RtlFreeUnicodeString(&DeviceLink);
}
