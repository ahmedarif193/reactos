/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     File system and volume attachment, volume objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"
#include <mountmgr.h>
#include <ntdddisk.h>

#define NDEBUG
#include <debug.h>

typedef struct _FLTP_FS_TYPE_NAME
{
    PCWSTR DriverName;
    FLT_FILESYSTEM_TYPE Type;
} FLTP_FS_TYPE_NAME;

static CONST FLTP_FS_TYPE_NAME FltpFileSystemTypes[] =
{
    {L"\\FileSystem\\RAW", FLT_FSTYPE_RAW},
    {L"\\FileSystem\\Ntfs", FLT_FSTYPE_NTFS},
    {L"\\FileSystem\\Fastfat", FLT_FSTYPE_FAT},
    {L"\\FileSystem\\Cdfs", FLT_FSTYPE_CDFS},
    {L"\\FileSystem\\Udfs", FLT_FSTYPE_UDFS},
    {L"\\FileSystem\\MRxSmb", FLT_FSTYPE_LANMAN},
    {L"\\FileSystem\\MRxDAV", FLT_FSTYPE_WEBDAV},
    {L"\\FileSystem\\rdpdr", FLT_FSTYPE_RDPDR},
    {L"\\FileSystem\\Mup", FLT_FSTYPE_MUP},
    {L"\\FileSystem\\Fs_Rec", FLT_FSTYPE_FS_REC},
    {L"\\FileSystem\\exfat", FLT_FSTYPE_EXFAT},
    {L"\\FileSystem\\Npfs", FLT_FSTYPE_NPFS},
    {L"\\FileSystem\\Msfs", FLT_FSTYPE_MSFS},
    {L"\\FileSystem\\ReFS", FLT_FSTYPE_REFS},
};

NTSTATUS
FltpQueryObjectName(
    _In_ PVOID Object,
    _Out_ PUNICODE_STRING Name)
{
    POBJECT_NAME_INFORMATION Information;
    ULONG Length = 0;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(Name, NULL, 0);

    Status = ObQueryNameString(Object, NULL, 0, &Length);
    if (Status != STATUS_INFO_LENGTH_MISMATCH && Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
    {
        return NT_SUCCESS(Status) ? STATUS_OBJECT_NAME_NOT_FOUND : Status;
    }

    Information = ExAllocatePoolWithTag(PagedPool, Length, FLT_TAG_NAME);
    if (Information == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ObQueryNameString(Object, Information, Length, &Length);
    if (NT_SUCCESS(Status))
    {
        Status = FltpDuplicateString(Name, &Information->Name);
    }

    ExFreePoolWithTag(Information, FLT_TAG_NAME);
    return Status;
}

static
BOOLEAN
FltpIsAttached(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Current, Next;
    BOOLEAN Attached = FALSE;

    Current = IoGetAttachedDeviceReference(DeviceObject);
    while (Current != NULL)
    {
        if (Current->DriverObject == FltGlobals.DriverObject)
        {
            Attached = TRUE;
        }

        Next = Attached ? NULL : IoGetLowerDeviceObject(Current);
        ObDereferenceObject(Current);
        Current = Next;
    }

    return Attached;
}

static
NTSTATUS
FltpCreateFilterDevice(
    _In_ PDEVICE_OBJECT Target,
    _In_ BOOLEAN ControlDevice,
    _Out_ PDEVICE_OBJECT *FilterDevice)
{
    PFLTP_DEVICE_EXTENSION Extension;
    PDEVICE_OBJECT Device;
    NTSTATUS Status;

    Status = IoCreateDevice(FltGlobals.DriverObject,
                            sizeof(FLTP_DEVICE_EXTENSION),
                            NULL,
                            Target->DeviceType,
                            0,
                            FALSE,
                            &Device);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Extension = Device->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Signature = FLTP_DEVICE_EXTENSION_SIGNATURE;
    Extension->FileSystemControlDevice = ControlDevice;
    *FilterDevice = Device;
    return STATUS_SUCCESS;
}

static
NTSTATUS
FltpAttachFilterDevice(
    _In_ PDEVICE_OBJECT FilterDevice,
    _In_ PDEVICE_OBJECT Target)
{
    PFLTP_DEVICE_EXTENSION Extension = FilterDevice->DeviceExtension;
    NTSTATUS Status;

    if (Target->Flags & DO_BUFFERED_IO)
    {
        FilterDevice->Flags |= DO_BUFFERED_IO;
    }
    if (Target->Flags & DO_DIRECT_IO)
    {
        FilterDevice->Flags |= DO_DIRECT_IO;
    }
    if (Target->Characteristics & FILE_DEVICE_SECURE_OPEN)
    {
        FilterDevice->Characteristics |= FILE_DEVICE_SECURE_OPEN;
    }

    Status = IoAttachDeviceToDeviceStackSafe(FilterDevice, Target, &Extension->AttachedToDeviceObject);
    if (NT_SUCCESS(Status))
    {
        FilterDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    }
    return Status;
}

static
FLT_FILESYSTEM_TYPE
FltpFileSystemTypeFromName(
    _In_ PCUNICODE_STRING DriverName)
{
    UNICODE_STRING Name;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(FltpFileSystemTypes); Index++)
    {
        RtlInitUnicodeString(&Name, FltpFileSystemTypes[Index].DriverName);
        if (RtlEqualUnicodeString(DriverName, &Name, TRUE))
        {
            return FltpFileSystemTypes[Index].Type;
        }
    }

    return FLT_FSTYPE_UNKNOWN;
}

NTSTATUS
FltpCreateVolume(
    _In_ PDEVICE_OBJECT FilterDevice,
    _Out_ PFLT_VOLUME *RetVolume)
{
    PFLTP_DEVICE_EXTENSION Extension = FilterDevice->DeviceExtension;
    PFLT_VOLUME Volume;
    NTSTATUS Status;

    *RetVolume = NULL;

    Volume = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Volume), FLT_TAG_VOLUME);
    if (Volume == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Volume, sizeof(*Volume));
    FltpInitializeObject(&Volume->Base, FLT_SIGNATURE_VOLUME);
    Volume->DeviceObject = FilterDevice;
    Volume->AttachedToDeviceObject = Extension->AttachedToDeviceObject;
    Volume->BaseDeviceObject = IoGetDeviceAttachmentBaseRef(Extension->AttachedToDeviceObject);
    Volume->DeviceType = FilterDevice->DeviceType;
    FltpInitializeLock(&Volume->InstanceLock);
    InitializeListHead(&Volume->InstanceList);
    KeInitializeSpinLock(&Volume->ActiveLock);
    InitializeListHead(&Volume->ActiveList);
    FltpInitializeLock(&Volume->ContextLock);
    InitializeListHead(&Volume->ContextList);

    if (Extension->StorageStackDeviceObject != NULL)
    {
        Volume->DiskDeviceObject = Extension->StorageStackDeviceObject;
        ObReferenceObject(Volume->DiskDeviceObject);
    }
    else if (!NT_SUCCESS(IoGetDiskDeviceObject(Volume->BaseDeviceObject, &Volume->DiskDeviceObject)))
    {
        Volume->DiskDeviceObject = NULL;
    }

    if (FilterDevice->DeviceType == FILE_DEVICE_NETWORK_FILE_SYSTEM)
    {
        Volume->Flags |= FLTP_VOLUME_NETWORK;
    }

    Status = FltpQueryObjectName(Volume->BaseDeviceObject->DriverObject, &Volume->FileSystemDriverName);
    if (NT_SUCCESS(Status))
    {
        Volume->FileSystemType = FltpFileSystemTypeFromName(&Volume->FileSystemDriverName);
        Status = FltpQueryObjectName(Volume->DiskDeviceObject ? Volume->DiskDeviceObject : Volume->BaseDeviceObject,
                                     &Volume->DeviceName);
    }
    if (!NT_SUCCESS(Status))
    {
        FltpDereferencePointer(&Volume->Base);
        return Status;
    }

    Extension->Volume = Volume;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&FltGlobals.Lock, TRUE);
    InsertTailList(&FltGlobals.VolumeList, &Volume->Base.PrimaryLink);
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    *RetVolume = Volume;
    return STATUS_SUCCESS;
}

VOID
FltpTeardownVolume(
    _In_ PFLT_VOLUME Volume)
{
    PFLT_INSTANCE Instance;
    PLIST_ENTRY Link;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&FltGlobals.Lock, TRUE);
    Volume->Flags |= FLTP_VOLUME_DISMOUNTED;
    RemoveEntryList(&Volume->Base.PrimaryLink);
    InitializeListHead(&Volume->Base.PrimaryLink);
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    for (;;)
    {
        Instance = NULL;

        KeEnterCriticalRegion();
        ExAcquirePushLockShared(&Volume->InstanceLock);
        Link = Volume->InstanceList.Flink;
        if (Link != &Volume->InstanceList)
        {
            Instance = CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink);
            FltpReferencePointer(&Instance->Base);
        }
        ExReleasePushLockShared(&Volume->InstanceLock);
        KeLeaveCriticalRegion();

        if (Instance == NULL)
        {
            break;
        }

        FltpTeardownInstance(Instance, FLTFL_INSTANCE_TEARDOWN_VOLUME_DISMOUNT);
        FltpDereferencePointer(&Instance->Base);
    }

    ExWaitForRundownProtectionRelease(&Volume->Base.RundownRef);
    FltpDeleteVolumeContexts(Volume, NULL);
    FltpDereferencePointer(&Volume->Base);
}

static
VOID
FltpAttachToVolumeDevice(
    _In_ PDEVICE_OBJECT VolumeDevice,
    _In_opt_ PDEVICE_OBJECT StorageDevice,
    _In_opt_ PDEVICE_OBJECT PreparedFilterDevice,
    _In_ BOOLEAN NewlyMounted)
{
    PDEVICE_OBJECT FilterDevice = PreparedFilterDevice;
    PFLTP_DEVICE_EXTENSION Extension;
    PFLT_VOLUME Volume = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    ExAcquireFastMutex(&FltGlobals.AttachLock);

    if (FltpIsAttached(VolumeDevice))
    {
        Status = STATUS_DEVICE_ALREADY_ATTACHED;
    }
    else if (FilterDevice == NULL)
    {
        Status = FltpCreateFilterDevice(VolumeDevice, FALSE, &FilterDevice);
    }

    if (NT_SUCCESS(Status))
    {
        Extension = FilterDevice->DeviceExtension;
        Extension->StorageStackDeviceObject = StorageDevice;
        Status = FltpAttachFilterDevice(FilterDevice, VolumeDevice);
        if (NT_SUCCESS(Status))
        {
            Status = FltpCreateVolume(FilterDevice, &Volume);
            if (!NT_SUCCESS(Status))
            {
                IoDetachDevice(Extension->AttachedToDeviceObject);
            }
        }
    }

    ExReleaseFastMutex(&FltGlobals.AttachLock);

    if (!NT_SUCCESS(Status))
    {
        if (FilterDevice != NULL)
        {
            IoDeleteDevice(FilterDevice);
        }
        return;
    }

    if (NewlyMounted)
    {
        InterlockedOr((PLONG)&Volume->Flags, FLTP_VOLUME_SETUP_PENDING);
    }
    else
    {
        FltpAttachFiltersToVolume(Volume, FALSE);
    }
}

static
VOID
FltpEnumerateFileSystemVolumes(
    _In_ PDEVICE_OBJECT FileSystemDevice)
{
    PDEVICE_OBJECT *Devices;
    PDEVICE_OBJECT StorageDevice;
    ULONG Count = 0, Index;
    NTSTATUS Status;

    Status = IoEnumerateDeviceObjectList(FileSystemDevice->DriverObject, NULL, 0, &Count);
    if (Status != STATUS_BUFFER_TOO_SMALL || Count == 0)
    {
        return;
    }

    Count += 8;
    Devices = ExAllocatePoolWithTag(PagedPool, Count * sizeof(PDEVICE_OBJECT), FLT_TAG_GENERAL);
    if (Devices == NULL)
    {
        return;
    }

    Status = IoEnumerateDeviceObjectList(FileSystemDevice->DriverObject,
                                         Devices,
                                         Count * sizeof(PDEVICE_OBJECT),
                                         &Count);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Devices, FLT_TAG_GENERAL);
        return;
    }

    for (Index = 0; Index < Count; Index++)
    {
        if (Devices[Index] != FileSystemDevice &&
            Devices[Index]->DeviceType == FileSystemDevice->DeviceType &&
            NT_SUCCESS(IoGetDiskDeviceObject(Devices[Index], &StorageDevice)))
        {
            FltpAttachToVolumeDevice(Devices[Index], StorageDevice, NULL, FALSE);
            ObDereferenceObject(StorageDevice);
        }

        ObDereferenceObject(Devices[Index]);
    }

    ExFreePoolWithTag(Devices, FLT_TAG_GENERAL);
}

static
BOOLEAN
FltpIsSupportedDeviceType(
    _In_ DEVICE_TYPE DeviceType)
{
    return DeviceType == FILE_DEVICE_DISK_FILE_SYSTEM ||
           DeviceType == FILE_DEVICE_CD_ROM_FILE_SYSTEM ||
           DeviceType == FILE_DEVICE_NETWORK_FILE_SYSTEM;
}

static
VOID
NTAPI
FltpFsNotification(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN FsActive)
{
    static UNICODE_STRING Recognizer = RTL_CONSTANT_STRING(L"\\FileSystem\\Fs_Rec");
    PFLTP_DEVICE_EXTENSION Extension;
    PDEVICE_OBJECT FilterDevice, Current;
    UNICODE_STRING DriverName;
    NTSTATUS Status;
    PAGED_CODE();

    if (!FltpIsSupportedDeviceType(DeviceObject->DeviceType))
    {
        return;
    }

    if (!FsActive)
    {
        for (Current = DeviceObject->AttachedDevice; Current != NULL; Current = Current->AttachedDevice)
        {
            if (Current->DriverObject == FltGlobals.DriverObject)
            {
                Extension = Current->DeviceExtension;
                IoDetachDevice(Extension->AttachedToDeviceObject);
                IoDeleteDevice(Current);
                break;
            }
        }
        return;
    }

    Status = FltpQueryObjectName(DeviceObject->DriverObject, &DriverName);
    if (!NT_SUCCESS(Status))
    {
        return;
    }
    if (RtlEqualUnicodeString(&DriverName, &Recognizer, TRUE))
    {
        FltpFreeString(&DriverName);
        return;
    }
    FltpFreeString(&DriverName);

    ExAcquireFastMutex(&FltGlobals.AttachLock);
    if (FltpIsAttached(DeviceObject))
    {
        Status = STATUS_DEVICE_ALREADY_ATTACHED;
    }
    else
    {
        Status = FltpCreateFilterDevice(DeviceObject, TRUE, &FilterDevice);
        if (NT_SUCCESS(Status))
        {
            Status = FltpAttachFilterDevice(FilterDevice, DeviceObject);
            if (!NT_SUCCESS(Status))
            {
                IoDeleteDevice(FilterDevice);
            }
        }
    }
    ExReleaseFastMutex(&FltGlobals.AttachLock);

    if (NT_SUCCESS(Status))
    {
        FltpEnumerateFileSystemVolumes(DeviceObject);
    }
}

static
NTSTATUS
NTAPI
FltpMountCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
FltpMountVolume(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFLTP_DEVICE_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT StorageDevice, FilterDevice = NULL, VolumeDevice;
    KEVENT Event;
    NTSTATUS Status;
    PAGED_CODE();

    StorageDevice = Stack->Parameters.MountVolume.Vpb->RealDevice;
    ObReferenceObject(StorageDevice);

    if (!NT_SUCCESS(FltpCreateFilterDevice(DeviceObject, FALSE, &FilterDevice)))
    {
        FilterDevice = NULL;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, FltpMountCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(Extension->AttachedToDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }
    Status = Irp->IoStatus.Status;

    if (NT_SUCCESS(Status) && FilterDevice != NULL && StorageDevice->Vpb != NULL)
    {
        VolumeDevice = StorageDevice->Vpb->DeviceObject;
        if (VolumeDevice != NULL)
        {
            FltpAttachToVolumeDevice(VolumeDevice, StorageDevice, FilterDevice, TRUE);
            FilterDevice = NULL;
        }
    }

    if (FilterDevice != NULL)
    {
        IoDeleteDevice(FilterDevice);
    }

    ObDereferenceObject(StorageDevice);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

VOID
FltpDetachDevice(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice)
{
    PFLTP_DEVICE_EXTENSION Extension = SourceDevice->DeviceExtension;
    PFLT_VOLUME Volume;

    if (Extension == NULL || Extension->Signature != FLTP_DEVICE_EXTENSION_SIGNATURE)
    {
        return;
    }

    Volume = Extension->Volume;
    Extension->Volume = NULL;
    if (Volume != NULL)
    {
        FltpTeardownVolume(Volume);
    }

    IoDetachDevice(TargetDevice);
    IoDeleteDevice(SourceDevice);
}

NTSTATUS
FltpInitializeVolumes(
    _In_ PDRIVER_OBJECT DriverObject)
{
    static CONST PCWSTR RawNames[] = {L"\\Device\\RawDisk", L"\\Device\\RawCdRom"};
    PDEVICE_OBJECT RawDevice;
    PFILE_OBJECT RawFile;
    UNICODE_STRING Name;
    NTSTATUS Status;
    ULONG Index;

    Status = IoRegisterFsRegistrationChange(DriverObject, FltpFsNotification);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(RawNames); Index++)
    {
        RtlInitUnicodeString(&Name, RawNames[Index]);
        if (NT_SUCCESS(IoGetDeviceObjectPointer(&Name, FILE_READ_ATTRIBUTES, &RawFile, &RawDevice)))
        {
            FltpFsNotification(RawDevice, TRUE);
            ObDereferenceObject(RawFile);
        }
    }

    return STATUS_SUCCESS;
}

PFLT_VOLUME
FltpReferenceVolumeFromDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Base;
    PFLT_VOLUME Volume, Found = NULL;
    PLIST_ENTRY Link;

    Base = IoGetDeviceAttachmentBaseRef(DeviceObject);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);
    for (Link = FltGlobals.VolumeList.Flink; Link != &FltGlobals.VolumeList; Link = Link->Flink)
    {
        Volume = CONTAINING_RECORD(Link, FLT_VOLUME, Base.PrimaryLink);
        if (Volume->BaseDeviceObject == Base || Volume->DiskDeviceObject == DeviceObject)
        {
            if (ExAcquireRundownProtection(&Volume->Base.RundownRef))
            {
                Found = Volume;
            }
            break;
        }
    }
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    ObDereferenceObject(Base);
    return Found;
}

static
PFLT_VOLUME
FltpVolumeFromObject(
    _In_ PVOID FltObject)
{
    PFLT_OBJECT Object = FltObject;

    if (Object->Signature == FLT_SIGNATURE_VOLUME)
    {
        return CONTAINING_RECORD(Object, FLT_VOLUME, Base);
    }
    if (Object->Signature == FLT_SIGNATURE_INSTANCE)
    {
        return CONTAINING_RECORD(Object, FLT_INSTANCE, Base)->Volume;
    }
    return NULL;
}

static
NTSTATUS
FltpReturnString(
    _In_ PCUNICODE_STRING Source,
    _Inout_opt_ PUNICODE_STRING Destination,
    _Out_opt_ PULONG BufferSizeNeeded)
{
    if (Destination == NULL && BufferSizeNeeded == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (BufferSizeNeeded != NULL)
    {
        *BufferSizeNeeded = Source->Length;
    }

    if (Destination == NULL || Destination->MaximumLength < Source->Length)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyUnicodeString(Destination, Source);
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltGetVolumeName(
    _In_ PFLT_VOLUME Volume,
    _Inout_opt_ PUNICODE_STRING VolumeName,
    _Out_opt_ PULONG BufferSizeNeeded)
{
    return FltpReturnString(&Volume->DeviceName, VolumeName, BufferSizeNeeded);
}

static
NTSTATUS
FltpQueryGuidName(
    _In_ PFLT_VOLUME Volume,
    _Out_ PUNICODE_STRING GuidName)
{
    static UNICODE_STRING MountManager = RTL_CONSTANT_STRING(MOUNTMGR_DEVICE_NAME);
    static UNICODE_STRING Prefix = RTL_CONSTANT_STRING(L"\\??\\Volume{");
    PMOUNTMGR_MOUNT_POINT Input;
    PMOUNTMGR_MOUNT_POINTS Output;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Link;
    KEVENT Event;
    PIRP Irp;
    ULONG InputSize, OutputSize = 1024, Index;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(GuidName, NULL, 0);

    Status = IoGetDeviceObjectPointer(&MountManager, FILE_READ_ATTRIBUTES, &FileObject, &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    InputSize = sizeof(MOUNTMGR_MOUNT_POINT) + Volume->DeviceName.Length;
    Input = ExAllocatePoolWithTag(PagedPool, InputSize, FLT_TAG_GENERAL);
    if (Input == NULL)
    {
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Input, InputSize);
    Input->DeviceNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
    Input->DeviceNameLength = Volume->DeviceName.Length;
    RtlCopyMemory(Input + 1, Volume->DeviceName.Buffer, Volume->DeviceName.Length);

    for (;;)
    {
        Output = ExAllocatePoolWithTag(PagedPool, OutputSize, FLT_TAG_GENERAL);
        if (Output == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        Irp = IoBuildDeviceIoControlRequest(IOCTL_MOUNTMGR_QUERY_POINTS,
                                            DeviceObject,
                                            Input,
                                            InputSize,
                                            Output,
                                            OutputSize,
                                            FALSE,
                                            &Event,
                                            &IoStatus);
        if (Irp == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            ExFreePoolWithTag(Output, FLT_TAG_GENERAL);
            break;
        }

        Status = IoCallDriver(DeviceObject, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatus.Status;
        }

        if (Status == STATUS_BUFFER_OVERFLOW)
        {
            OutputSize = Output->Size;
            ExFreePoolWithTag(Output, FLT_TAG_GENERAL);
            continue;
        }

        if (NT_SUCCESS(Status))
        {
            Status = STATUS_FLT_VOLUME_NOT_FOUND;
            for (Index = 0; Index < Output->NumberOfMountPoints; Index++)
            {
                Link.Buffer = (PWCHAR)((PUCHAR)Output + Output->MountPoints[Index].SymbolicLinkNameOffset);
                Link.Length = Link.MaximumLength = Output->MountPoints[Index].SymbolicLinkNameLength;
                if (RtlPrefixUnicodeString(&Prefix, &Link, TRUE))
                {
                    Status = FltpDuplicateString(GuidName, &Link);
                    break;
                }
            }
        }

        ExFreePoolWithTag(Output, FLT_TAG_GENERAL);
        break;
    }

    ExFreePoolWithTag(Input, FLT_TAG_GENERAL);
    ObDereferenceObject(FileObject);
    return Status;
}

NTSTATUS
FLTAPI
FltGetVolumeGuidName(
    _In_ PFLT_VOLUME Volume,
    _Inout_opt_ PUNICODE_STRING VolumeGuidName,
    _Out_opt_ PULONG BufferSizeNeeded)
{
    UNICODE_STRING GuidName;
    NTSTATUS Status;
    PAGED_CODE();

    if (Volume->Flags & FLTP_VOLUME_NETWORK)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (Volume->GuidName.Buffer == NULL)
    {
        Status = FltpQueryGuidName(Volume, &GuidName);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&Volume->ContextLock);
        if (Volume->GuidName.Buffer == NULL)
        {
            Volume->GuidName = GuidName;
            GuidName.Buffer = NULL;
        }
        ExReleasePushLockExclusive(&Volume->ContextLock);
        KeLeaveCriticalRegion();

        if (GuidName.Buffer != NULL)
        {
            FltpFreeString(&GuidName);
        }
    }

    if (VolumeGuidName == NULL && BufferSizeNeeded == NULL)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    return FltpReturnString(&Volume->GuidName, VolumeGuidName, BufferSizeNeeded);
}

NTSTATUS
FLTAPI
FltGetVolumeProperties(
    _In_ PFLT_VOLUME Volume,
    _Out_writes_bytes_to_opt_(VolumePropertiesLength, *LengthReturned) PFLT_VOLUME_PROPERTIES VolumeProperties,
    _In_ ULONG VolumePropertiesLength,
    _Out_ PULONG LengthReturned)
{
    UNICODE_STRING FileSystemDeviceName = {0};
    PDEVICE_OBJECT Device = Volume->BaseDeviceObject;
    ULONG Needed;
    PWCHAR Buffer;
    NTSTATUS Status;

    (VOID)FltpQueryObjectName(Device, &FileSystemDeviceName);

    Needed = sizeof(FLT_VOLUME_PROPERTIES) +
             Volume->FileSystemDriverName.Length +
             FileSystemDeviceName.Length +
             Volume->DeviceName.Length;

    if (VolumePropertiesLength < sizeof(FLT_VOLUME_PROPERTIES))
    {
        *LengthReturned = Needed;
        FltpFreeString(&FileSystemDeviceName);
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(VolumeProperties, sizeof(*VolumeProperties));
    VolumeProperties->DeviceType = Device->DeviceType;
    VolumeProperties->DeviceCharacteristics = Device->Characteristics;
    VolumeProperties->DeviceObjectFlags = Device->Flags;
    VolumeProperties->AlignmentRequirement = Device->AlignmentRequirement;
    VolumeProperties->SectorSize = Device->SectorSize;
    if (Volume->DiskDeviceObject != NULL)
    {
        VolumeProperties->DeviceCharacteristics |= Volume->DiskDeviceObject->Characteristics;
        if (VolumeProperties->SectorSize == 0)
        {
            VolumeProperties->SectorSize = Volume->DiskDeviceObject->SectorSize;
        }
    }

    if (VolumePropertiesLength < Needed)
    {
        *LengthReturned = sizeof(FLT_VOLUME_PROPERTIES);
        Status = STATUS_BUFFER_OVERFLOW;
    }
    else
    {
        Buffer = (PWCHAR)(VolumeProperties + 1);

        VolumeProperties->FileSystemDriverName.Buffer = Buffer;
        VolumeProperties->FileSystemDriverName.Length = Volume->FileSystemDriverName.Length;
        VolumeProperties->FileSystemDriverName.MaximumLength = Volume->FileSystemDriverName.Length;
        RtlCopyMemory(Buffer, Volume->FileSystemDriverName.Buffer, Volume->FileSystemDriverName.Length);
        Buffer = (PWCHAR)((PUCHAR)Buffer + Volume->FileSystemDriverName.Length);

        VolumeProperties->FileSystemDeviceName.Buffer = Buffer;
        VolumeProperties->FileSystemDeviceName.Length = FileSystemDeviceName.Length;
        VolumeProperties->FileSystemDeviceName.MaximumLength = FileSystemDeviceName.Length;
        if (FileSystemDeviceName.Length != 0)
        {
            RtlCopyMemory(Buffer, FileSystemDeviceName.Buffer, FileSystemDeviceName.Length);
        }
        Buffer = (PWCHAR)((PUCHAR)Buffer + FileSystemDeviceName.Length);

        VolumeProperties->RealDeviceName.Buffer = Buffer;
        VolumeProperties->RealDeviceName.Length = Volume->DeviceName.Length;
        VolumeProperties->RealDeviceName.MaximumLength = Volume->DeviceName.Length;
        RtlCopyMemory(Buffer, Volume->DeviceName.Buffer, Volume->DeviceName.Length);

        *LengthReturned = Needed;
        Status = STATUS_SUCCESS;
    }

    FltpFreeString(&FileSystemDeviceName);
    return Status;
}

NTSTATUS
FLTAPI
FltEnumerateVolumes(
    _In_ PFLT_FILTER Filter,
    _Out_writes_to_opt_(VolumeListSize, *NumberVolumesReturned) PFLT_VOLUME *VolumeList,
    _In_ ULONG VolumeListSize,
    _Out_ PULONG NumberVolumesReturned)
{
    PFLT_VOLUME Volume;
    PLIST_ENTRY Link;
    ULONG Count = 0, Index;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Filter);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);

    for (Link = FltGlobals.VolumeList.Flink; Link != &FltGlobals.VolumeList; Link = Link->Flink)
    {
        Volume = CONTAINING_RECORD(Link, FLT_VOLUME, Base.PrimaryLink);
        if (!ExAcquireRundownProtection(&Volume->Base.RundownRef))
        {
            continue;
        }

        if (VolumeList != NULL && Count < VolumeListSize)
        {
            VolumeList[Count] = Volume;
        }
        else
        {
            ExReleaseRundownProtection(&Volume->Base.RundownRef);
        }
        Count++;
    }

    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    if (VolumeList != NULL && Count > VolumeListSize)
    {
        for (Index = 0; Index < VolumeListSize; Index++)
        {
            ExReleaseRundownProtection(&VolumeList[Index]->Base.RundownRef);
        }
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else if (VolumeList == NULL && VolumeListSize != 0)
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    *NumberVolumesReturned = Count;
    return Status;
}

NTSTATUS
FLTAPI
FltGetVolumeFromInstance(
    _In_ PFLT_INSTANCE Instance,
    _Outptr_ PFLT_VOLUME *RetVolume)
{
    *RetVolume = NULL;

    if (!ExAcquireRundownProtection(&Instance->Volume->Base.RundownRef))
    {
        return STATUS_FLT_DELETING_OBJECT;
    }

    *RetVolume = Instance->Volume;
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltGetVolumeFromDeviceObject(
    _In_ PFLT_FILTER Filter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _Outptr_ PFLT_VOLUME *RetVolume)
{
    UNREFERENCED_PARAMETER(Filter);

    *RetVolume = FltpReferenceVolumeFromDevice(DeviceObject);
    return (*RetVolume != NULL) ? STATUS_SUCCESS : STATUS_FLT_VOLUME_NOT_FOUND;
}

NTSTATUS
FLTAPI
FltGetVolumeFromFileObject(
    _In_ PFLT_FILTER Filter,
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ PFLT_VOLUME *RetVolume)
{
    UNREFERENCED_PARAMETER(Filter);

    *RetVolume = FltpReferenceVolumeFromDevice(IoGetBaseFileSystemDeviceObject(FileObject));
    return (*RetVolume != NULL) ? STATUS_SUCCESS : STATUS_FLT_VOLUME_NOT_FOUND;
}

NTSTATUS
FLTAPI
FltGetVolumeFromName(
    _In_ PFLT_FILTER Filter,
    _In_ PCUNICODE_STRING VolumeName,
    _Outptr_ PFLT_VOLUME *RetVolume)
{
    static UNICODE_STRING DosPrefix = RTL_CONSTANT_STRING(L"\\??\\");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Name = *VolumeName, Full = {0};
    PFILE_OBJECT FileObject;
    HANDLE Handle;
    NTSTATUS Status;
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Filter);

    *RetVolume = NULL;

    if (Name.Length == 0 || Name.Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Name.Buffer[0] != L'\\')
    {
        Full.MaximumLength = DosPrefix.Length + Name.Length;
        Full.Buffer = ExAllocatePoolWithTag(PagedPool, Full.MaximumLength, FLT_TAG_NAME);
        if (Full.Buffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyUnicodeString(&Full, &DosPrefix);
        RtlAppendUnicodeStringToString(&Full, &Name);
        Name = Full;
    }

    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&Handle,
                        FILE_READ_DATA | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (Full.Buffer != NULL)
    {
        ExFreePoolWithTag(Full.Buffer, FLT_TAG_NAME);
    }
    if (!NT_SUCCESS(Status))
    {
        return (Status == STATUS_ACCESS_DENIED) ? Status : STATUS_FLT_VOLUME_NOT_FOUND;
    }

    Status = ObReferenceObjectByHandle(Handle, 0, *IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL);
    if (NT_SUCCESS(Status))
    {
        *RetVolume = FltpReferenceVolumeFromDevice(IoGetBaseFileSystemDeviceObject(FileObject));
        Status = (*RetVolume != NULL) ? STATUS_SUCCESS : STATUS_FLT_VOLUME_NOT_FOUND;
        ObDereferenceObject(FileObject);
    }

    ZwClose(Handle);
    return Status;
}

BOOLEAN
FLTAPI
FltIsFltMgrVolumeDeviceObject(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return DeviceObject->DriverObject == FltGlobals.DriverObject && FltpIsVolumeDevice(DeviceObject);
}

NTSTATUS
FLTAPI
FltGetDeviceObject(
    _In_ PFLT_VOLUME Volume,
    _Outptr_ PDEVICE_OBJECT *DeviceObject)
{
    ObReferenceObject(Volume->DeviceObject);
    *DeviceObject = Volume->DeviceObject;
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltGetDiskDeviceObject(
    _In_ PFLT_VOLUME Volume,
    _Outptr_ PDEVICE_OBJECT *DiskDeviceObject)
{
    *DiskDeviceObject = NULL;

    if (Volume->DiskDeviceObject == NULL)
    {
        return STATUS_FLT_NO_DEVICE_OBJECT;
    }

    ObReferenceObject(Volume->DiskDeviceObject);
    *DiskDeviceObject = Volume->DiskDeviceObject;
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltGetFileSystemType(
    _In_ PVOID FltObject,
    _Out_ PFLT_FILESYSTEM_TYPE FileSystemType)
{
    PFLT_VOLUME Volume = FltpVolumeFromObject(FltObject);

    if (Volume == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *FileSystemType = Volume->FileSystemType;
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltIsVolumeWritable(
    _In_ PVOID FltObject,
    _Out_ PBOOLEAN IsWritable)
{
    PFLT_VOLUME Volume = FltpVolumeFromObject(FltObject);
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;
    PAGED_CODE();

    if (Volume == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Volume->DiskDeviceObject == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_IS_WRITABLE,
                                        Volume->DiskDeviceObject,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(Volume->DiskDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (Status == STATUS_MEDIA_WRITE_PROTECTED)
    {
        *IsWritable = FALSE;
        return STATUS_SUCCESS;
    }
    if (NT_SUCCESS(Status))
    {
        *IsWritable = TRUE;
    }
    return Status;
}

NTSTATUS
FLTAPI
FltIsVolumeSnapshot(
    _In_ PVOID FltObject,
    _Out_ PBOOLEAN IsSnapshotVolume)
{
    if (FltpVolumeFromObject(FltObject) == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *IsSnapshotVolume = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltGetVolumeInformation(
    _In_ PFLT_VOLUME Volume,
    _In_ FILTER_VOLUME_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    PFILTER_VOLUME_BASIC_INFORMATION Basic = Buffer;
    PFILTER_VOLUME_STANDARD_INFORMATION Standard = Buffer;
    ULONG Needed;

    switch (InformationClass)
    {
        case FilterVolumeBasicInformation:
            Needed = FIELD_OFFSET(FILTER_VOLUME_BASIC_INFORMATION, FilterVolumeName) + Volume->DeviceName.Length;
            *BytesReturned = Needed;
            if (BufferSize < Needed || Buffer == NULL)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            Basic->FilterVolumeNameLength = Volume->DeviceName.Length;
            RtlCopyMemory(Basic->FilterVolumeName, Volume->DeviceName.Buffer, Volume->DeviceName.Length);
            return STATUS_SUCCESS;

        case FilterVolumeStandardInformation:
            Needed = FIELD_OFFSET(FILTER_VOLUME_STANDARD_INFORMATION, FilterVolumeName) + Volume->DeviceName.Length;
            *BytesReturned = Needed;
            if (BufferSize < Needed || Buffer == NULL)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            Standard->NextEntryOffset = 0;
            Standard->Flags = 0;
            Standard->FrameID = 0;
            Standard->FileSystemType = Volume->FileSystemType;
            Standard->FilterVolumeNameLength = Volume->DeviceName.Length;
            RtlCopyMemory(Standard->FilterVolumeName, Volume->DeviceName.Buffer, Volume->DeviceName.Length);
            return STATUS_SUCCESS;

        default:
            *BytesReturned = 0;
            return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
FLTAPI
FltEnumerateVolumeInformation(
    _In_ PFLT_FILTER Filter,
    _In_ ULONG Index,
    _In_ FILTER_VOLUME_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    PFLT_VOLUME Volume = NULL, Current;
    PLIST_ENTRY Link;
    ULONG Position = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Filter);

    *BytesReturned = 0;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);
    for (Link = FltGlobals.VolumeList.Flink; Link != &FltGlobals.VolumeList; Link = Link->Flink)
    {
        Current = CONTAINING_RECORD(Link, FLT_VOLUME, Base.PrimaryLink);
        if (Position++ == Index)
        {
            if (ExAcquireRundownProtection(&Current->Base.RundownRef))
            {
                Volume = Current;
            }
            break;
        }
    }
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    if (Volume == NULL)
    {
        return STATUS_NO_MORE_ENTRIES;
    }

    Status = FltGetVolumeInformation(Volume, InformationClass, Buffer, BufferSize, BytesReturned);
    ExReleaseRundownProtection(&Volume->Base.RundownRef);
    return Status;
}
