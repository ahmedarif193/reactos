/*
 * PROJECT:     ReactOS ITE IT8613E Sensor Provider
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     Read-only voltage monitoring for the LattePanda Mu
 */

#include <ntddk.h>
#include <initguid.h>
#include <reactos/drivers/sensorprovider.h>

#define NDEBUG
#include <debug.h>

#define IT8613_TAG '31tI'
#define IT8613_SUPERIO_PORT 0x2E
#define IT8613_SUPERIO_LENGTH 2
#define IT8613_HWM_BASE 0xA40
#define IT8613_HWM_INDEX_PORT (IT8613_HWM_BASE + 5)
#define IT8613_HWM_LENGTH 2
#define IT8613_DEVICE_ID 0x8613
#define IT8613_LOGICAL_DEVICE 4
#define IT8613_HWM_CHIP_ID 0x90
#define IT8613_VALID_VIN_MASK 0xB7

#define IT8613_SIO_DEVICE_SELECT 0x07
#define IT8613_SIO_DEVICE_ID_MSB 0x20
#define IT8613_SIO_DEVICE_ID_LSB 0x21
#define IT8613_SIO_ACTIVATE 0x30
#define IT8613_SIO_BASE_MSB 0x60
#define IT8613_SIO_BASE_LSB 0x61

#define IT8613_REG_CONFIG 0x00
#define IT8613_REG_VIN_BASE 0x20
#define IT8613_REG_VIN_ENABLE 0x50
#define IT8613_REG_CHIP_ID 0x58

typedef struct _IT8613_CHANNEL_TEMPLATE
{
    UCHAR Vin;
    ULONG Flags;
    ULONG MicrovoltsPerCount;
    GUID SensorId;
    PCWSTR Name;
} IT8613_CHANNEL_TEMPLATE, *PIT8613_CHANNEL_TEMPLATE;

typedef struct _IT8613_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    UNICODE_STRING InterfaceName;
    KSPIN_LOCK PortLock;
    USHORT SuperIoPort;
    USHORT HwmIndexPort;
    UCHAR VinMask;
    UCHAR ChannelCount;
    UCHAR Channels[REACTOS_SENSOR_PROVIDER_MAX_CHANNELS];
    BOOLEAN Started;
} IT8613_DEVICE_EXTENSION, *PIT8613_DEVICE_EXTENSION;

static KSPIN_LOCK It8613DetectionLock;

/* {35806A50-45AB-4946-A774-438F69B22836} */
static const GUID It8613ProviderId =
    {0x35806A50, 0x45AB, 0x4946, {0xA7, 0x74, 0x43, 0x8F, 0x69, 0xB2, 0x28, 0x36}};

static const IT8613_CHANNEL_TEMPLATE It8613ChannelTemplates[] =
{
    {0, REACTOS_SENSOR_CHANNEL_FLAG_READ_ONLY | REACTOS_SENSOR_CHANNEL_FLAG_UNCALIBRATED, 11000,
     {0x00F01C21, 0xF5E6, 0x40FC, {0xA5, 0x9B, 0xE8, 0xF3, 0xF0, 0x09, 0x7E, 0x66}}, L"IT8613E VIN0"},
    {1, REACTOS_SENSOR_CHANNEL_FLAG_READ_ONLY | REACTOS_SENSOR_CHANNEL_FLAG_UNCALIBRATED, 11000,
     {0xD0240722, 0xE0AF, 0x467E, {0x85, 0x41, 0x7A, 0x52, 0x98, 0x67, 0xC5, 0xC7}}, L"IT8613E VIN1"},
    {2, REACTOS_SENSOR_CHANNEL_FLAG_READ_ONLY | REACTOS_SENSOR_CHANNEL_FLAG_UNCALIBRATED, 11000,
     {0xE936D182, 0x0E34, 0x491D, {0x9B, 0xA1, 0x26, 0x52, 0xB5, 0x6E, 0x54, 0x04}}, L"IT8613E VIN2"},
    {4, REACTOS_SENSOR_CHANNEL_FLAG_READ_ONLY | REACTOS_SENSOR_CHANNEL_FLAG_UNCALIBRATED, 11000,
     {0x1E59E8E8, 0x4D20, 0x4E03, {0x97, 0x22, 0x6C, 0xB1, 0x33, 0xDB, 0xB4, 0x86}}, L"IT8613E VIN4"},
    {5, REACTOS_SENSOR_CHANNEL_FLAG_READ_ONLY | REACTOS_SENSOR_CHANNEL_FLAG_UNCALIBRATED, 11000,
     {0x4B2211B5, 0xAF5A, 0x473A, {0x90, 0xD7, 0xA6, 0x34, 0x45, 0x86, 0x1F, 0x74}}, L"IT8613E VIN5"},
    {7, REACTOS_SENSOR_CHANNEL_FLAG_READ_ONLY | REACTOS_SENSOR_CHANNEL_FLAG_INTERNAL, 22000,
     {0xABBFD503, 0xCC00, 0x4781, {0x90, 0xDD, 0x7B, 0xC6, 0x41, 0x3F, 0x24, 0xA2}}, L"IT8613E VIN7"}
};

static
VOID
It8613CopyString(
    _Out_writes_(DestinationCount) PWCHAR Destination,
    _In_ SIZE_T DestinationCount,
    _In_ PCWSTR Source)
{
    SIZE_T Index;

    for (Index = 0; Index + 1 < DestinationCount && Source[Index] != UNICODE_NULL; Index++)
        Destination[Index] = Source[Index];
    Destination[Index] = UNICODE_NULL;
}

static
const IT8613_CHANNEL_TEMPLATE *
It8613FindChannelTemplate(
    _In_ UCHAR Vin)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(It8613ChannelTemplates); Index++)
    {
        if (It8613ChannelTemplates[Index].Vin == Vin)
            return &It8613ChannelTemplates[Index];
    }
    return NULL;
}

static
VOID
It8613WritePort(
    _In_ USHORT Port,
    _In_ UCHAR Value)
{
    WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)Port, Value);
}

static
UCHAR
It8613ReadPort(
    _In_ USHORT Port)
{
    return READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)Port);
}

static
UCHAR
It8613ReadIndexed(
    _In_ USHORT IndexPort,
    _In_ UCHAR Register)
{
    It8613WritePort(IndexPort, Register);
    return It8613ReadPort(IndexPort + 1);
}

static
VOID
It8613WriteIndexed(
    _In_ USHORT IndexPort,
    _In_ UCHAR Register,
    _In_ UCHAR Value)
{
    It8613WritePort(IndexPort, Register);
    It8613WritePort(IndexPort + 1, Value);
}

static
VOID
It8613EnterConfiguration(
    _In_ USHORT SuperIoPort)
{
    It8613WritePort(SuperIoPort, 0x87);
    It8613WritePort(SuperIoPort, 0x01);
    It8613WritePort(SuperIoPort, 0x55);
    It8613WritePort(SuperIoPort, 0x55);
}

static
VOID
It8613ExitConfiguration(
    _In_ USHORT SuperIoPort)
{
    It8613WriteIndexed(SuperIoPort, 0x02, 0x02);
}

static
NTSTATUS
It8613DetectHardware(
    _In_ PKSPIN_LOCK PortLock,
    _In_ USHORT SuperIoPort,
    _In_ USHORT HwmIndexPort,
    _Out_ PUCHAR VinMask)
{
    KIRQL OldIrql;
    USHORT DeviceId;
    USHORT HwmBase;
    UCHAR Active;
    UCHAR Configuration;
    UCHAR ChipId;
    UCHAR Mask;

    *VinMask = 0;
    KeAcquireSpinLock(PortLock, &OldIrql);
    It8613EnterConfiguration(SuperIoPort);
    DeviceId = ((USHORT)It8613ReadIndexed(SuperIoPort, IT8613_SIO_DEVICE_ID_MSB) << 8) | It8613ReadIndexed(SuperIoPort, IT8613_SIO_DEVICE_ID_LSB);
    It8613WriteIndexed(SuperIoPort, IT8613_SIO_DEVICE_SELECT, IT8613_LOGICAL_DEVICE);
    Active = It8613ReadIndexed(SuperIoPort, IT8613_SIO_ACTIVATE);
    HwmBase = ((USHORT)It8613ReadIndexed(SuperIoPort, IT8613_SIO_BASE_MSB) << 8) | It8613ReadIndexed(SuperIoPort, IT8613_SIO_BASE_LSB);
    It8613ExitConfiguration(SuperIoPort);
    if (DeviceId != IT8613_DEVICE_ID || !(Active & 0x01) || (HwmBase & ~7) != IT8613_HWM_BASE)
    {
        KeReleaseSpinLock(PortLock, OldIrql);
        return STATUS_NO_SUCH_DEVICE;
    }
    Configuration = It8613ReadIndexed(HwmIndexPort, IT8613_REG_CONFIG);
    ChipId = It8613ReadIndexed(HwmIndexPort, IT8613_REG_CHIP_ID);
    Mask = It8613ReadIndexed(HwmIndexPort, IT8613_REG_VIN_ENABLE) & IT8613_VALID_VIN_MASK;
    KeReleaseSpinLock(PortLock, OldIrql);
    if ((Configuration & 0x81) != 0x01 || ChipId != IT8613_HWM_CHIP_ID || Mask == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    *VinMask = Mask;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
It8613Completion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
It8613ForwardSynchronously(
    _In_ PIT8613_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, It8613Completion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
It8613ParseResources(
    _In_ PCM_RESOURCE_LIST Resources,
    _Out_ PUSHORT SuperIoPort,
    _Out_ PUSHORT HwmIndexPort)
{
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index;

    *SuperIoPort = 0;
    *HwmIndexPort = 0;
    if (!Resources || Resources->Count == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    PartialList = &Resources->List[0].PartialResourceList;
    for (Index = 0; Index < PartialList->Count; Index++)
    {
        Descriptor = &PartialList->PartialDescriptors[Index];
        if (Descriptor->Type != CmResourceTypePort || Descriptor->u.Port.Start.HighPart != 0)
            continue;
        if (Descriptor->u.Port.Start.LowPart == IT8613_SUPERIO_PORT && Descriptor->u.Port.Length >= IT8613_SUPERIO_LENGTH)
            *SuperIoPort = IT8613_SUPERIO_PORT;
        else if (Descriptor->u.Port.Start.LowPart == IT8613_HWM_INDEX_PORT && Descriptor->u.Port.Length >= IT8613_HWM_LENGTH)
            *HwmIndexPort = IT8613_HWM_INDEX_PORT;
    }
    if (*SuperIoPort != IT8613_SUPERIO_PORT || *HwmIndexPort != IT8613_HWM_INDEX_PORT)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return STATUS_SUCCESS;
}

static
NTSTATUS
It8613StartHardware(
    _Inout_ PIT8613_DEVICE_EXTENSION DeviceExtension,
    _In_ PCM_RESOURCE_LIST Resources)
{
    NTSTATUS Status;
    UCHAR VinMask;
    UCHAR Vin;

    Status = It8613ParseResources(Resources, &DeviceExtension->SuperIoPort, &DeviceExtension->HwmIndexPort);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = It8613DetectHardware(&DeviceExtension->PortLock, DeviceExtension->SuperIoPort, DeviceExtension->HwmIndexPort, &VinMask);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension->VinMask = VinMask;
    DeviceExtension->ChannelCount = 0;
    for (Vin = 0; Vin < 8; Vin++)
    {
        if ((VinMask & (1 << Vin)) && It8613FindChannelTemplate(Vin))
            DeviceExtension->Channels[DeviceExtension->ChannelCount++] = Vin;
    }
    DeviceExtension->Started = TRUE;
    Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DeviceExtension->Started = FALSE;
        return Status;
    }
    DPRINT1("IT8613SENSOR: detected IT8613E at SIO 0x%x, HWM 0x%x, VIN mask 0x%02x\n", DeviceExtension->SuperIoPort, DeviceExtension->HwmIndexPort - 5, DeviceExtension->VinMask);
    return STATUS_SUCCESS;
}

static
VOID
It8613StopHardware(
    _Inout_ PIT8613_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->PortLock, &OldIrql);
    DeviceExtension->Started = FALSE;
    DeviceExtension->ChannelCount = 0;
    DeviceExtension->VinMask = 0;
    KeReleaseSpinLock(&DeviceExtension->PortLock, OldIrql);
    IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
}

static
NTSTATUS
It8613QueryProvider(
    _In_ PIT8613_DEVICE_EXTENSION DeviceExtension,
    _Out_ PREACTOS_SENSOR_PROVIDER_INFORMATION Information)
{
    const IT8613_CHANNEL_TEMPLATE *Template;
    PREACTOS_SENSOR_CHANNEL_INFORMATION Channel;
    KIRQL OldIrql;
    ULONG Index;

    RtlZeroMemory(Information, sizeof(*Information));
    KeAcquireSpinLock(&DeviceExtension->PortLock, &OldIrql);
    if (!DeviceExtension->Started)
    {
        KeReleaseSpinLock(&DeviceExtension->PortLock, OldIrql);
        return STATUS_DEVICE_NOT_READY;
    }
    Information->Version = REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION;
    Information->Size = sizeof(*Information);
    Information->ProviderId = It8613ProviderId;
    Information->Flags = REACTOS_SENSOR_PROVIDER_FLAG_READ_ONLY;
    Information->ChannelCount = DeviceExtension->ChannelCount;
    It8613CopyString(Information->Manufacturer, RTL_NUMBER_OF(Information->Manufacturer), L"ITE");
    It8613CopyString(Information->Model, RTL_NUMBER_OF(Information->Model), L"IT8613E Hardware Monitor");
    for (Index = 0; Index < DeviceExtension->ChannelCount; Index++)
    {
        Template = It8613FindChannelTemplate(DeviceExtension->Channels[Index]);
        if (!Template)
            continue;
        Channel = &Information->Channels[Index];
        Channel->SensorId = Template->SensorId;
        Channel->Type = REACTOS_SENSOR_TYPE_VOLTAGE;
        Channel->Unit = REACTOS_SENSOR_UNIT_MICROVOLTS;
        Channel->Flags = Template->Flags;
        Channel->Resolution = Template->MicrovoltsPerCount;
        It8613CopyString(Channel->Name, RTL_NUMBER_OF(Channel->Name), Template->Name);
    }
    KeReleaseSpinLock(&DeviceExtension->PortLock, OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
It8613ReadChannel(
    _In_ PIT8613_DEVICE_EXTENSION DeviceExtension,
    _In_ PREACTOS_SENSOR_READ_INPUT Input,
    _Out_ PREACTOS_SENSOR_READING Reading)
{
    const IT8613_CHANNEL_TEMPLATE *Template;
    LARGE_INTEGER Timestamp;
    KIRQL OldIrql;
    UCHAR Vin;
    UCHAR RawValue;

    if (Input->Version != REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION || Input->Size < sizeof(*Input) || Input->Reserved != 0)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&DeviceExtension->PortLock, &OldIrql);
    if (!DeviceExtension->Started)
    {
        KeReleaseSpinLock(&DeviceExtension->PortLock, OldIrql);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Input->ChannelIndex >= DeviceExtension->ChannelCount)
    {
        KeReleaseSpinLock(&DeviceExtension->PortLock, OldIrql);
        return STATUS_INVALID_PARAMETER;
    }
    Vin = DeviceExtension->Channels[Input->ChannelIndex];
    Template = It8613FindChannelTemplate(Vin);
    if (!Template)
    {
        KeReleaseSpinLock(&DeviceExtension->PortLock, OldIrql);
        return STATUS_DEVICE_DATA_ERROR;
    }
    RawValue = It8613ReadIndexed(DeviceExtension->HwmIndexPort, IT8613_REG_VIN_BASE + Vin);
    KeReleaseSpinLock(&DeviceExtension->PortLock, OldIrql);
    KeQuerySystemTime(&Timestamp);
    RtlZeroMemory(Reading, sizeof(*Reading));
    Reading->Version = REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION;
    Reading->Size = sizeof(*Reading);
    Reading->SensorId = Template->SensorId;
    Reading->Type = REACTOS_SENSOR_TYPE_VOLTAGE;
    Reading->Unit = REACTOS_SENSOR_UNIT_MICROVOLTS;
    Reading->Flags = Template->Flags;
    Reading->RawValue = RawValue;
    Reading->Value = (LONGLONG)RawValue * Template->MicrovoltsPerCount;
    Reading->Timestamp = Timestamp.QuadPart;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
It8613CreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIT8613_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (NT_SUCCESS(Status))
        IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
It8613DeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIT8613_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    REACTOS_SENSOR_READ_INPUT Input;
    NTSTATUS Status;
    ULONG_PTR Information = 0;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        goto Complete;
    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_REACTOS_SENSOR_QUERY_PROVIDER:
            if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(REACTOS_SENSOR_PROVIDER_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                Status = It8613QueryProvider(DeviceExtension, (PREACTOS_SENSOR_PROVIDER_INFORMATION)Irp->AssociatedIrp.SystemBuffer);
                if (NT_SUCCESS(Status))
                    Information = sizeof(REACTOS_SENSOR_PROVIDER_INFORMATION);
            }
            break;

        case IOCTL_REACTOS_SENSOR_READ_CHANNEL:
            if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(Input))
                Status = STATUS_BUFFER_TOO_SMALL;
            else if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(REACTOS_SENSOR_READING))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                Input = *(PREACTOS_SENSOR_READ_INPUT)Irp->AssociatedIrp.SystemBuffer;
                Status = It8613ReadChannel(DeviceExtension, &Input, (PREACTOS_SENSOR_READING)Irp->AssociatedIrp.SystemBuffer);
                if (NT_SUCCESS(Status))
                    Information = sizeof(REACTOS_SENSOR_READING);
            }
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);

Complete:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
It8613Pnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIT8613_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = It8613ForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = It8613StartHardware(DeviceExtension, Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            It8613StopHardware(DeviceExtension);
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            It8613StopHardware(DeviceExtension);
            Status = It8613ForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
It8613Power(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIT8613_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
It8613Unsupported(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

static
NTSTATUS
NTAPI
It8613AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PIT8613_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*DeviceExtension), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, IT8613_TAG, 0, 0);
    KeInitializeSpinLock(&DeviceExtension->PortLock);
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_DEVINTERFACE_REACTOS_SENSOR_PROVIDER, NULL, &DeviceExtension->InterfaceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceObject->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
PCM_RESOURCE_LIST
It8613CreateResourceList(VOID)
{
    PCM_RESOURCE_LIST Resources;
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    ULONG Size;

    Size = sizeof(CM_RESOURCE_LIST) + sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    Resources = ExAllocatePoolWithTag(PagedPool, Size, IT8613_TAG);
    if (!Resources)
        return NULL;
    RtlZeroMemory(Resources, Size);
    Resources->Count = 1;
    Resources->List[0].InterfaceType = Internal;
    Resources->List[0].BusNumber = 0;
    PartialList = &Resources->List[0].PartialResourceList;
    PartialList->Version = 1;
    PartialList->Revision = 1;
    PartialList->Count = 2;
    PartialList->PartialDescriptors[0].Type = CmResourceTypePort;
    PartialList->PartialDescriptors[0].ShareDisposition = CmResourceShareShared;
    PartialList->PartialDescriptors[0].Flags = CM_RESOURCE_PORT_IO;
    PartialList->PartialDescriptors[0].u.Port.Start.QuadPart = IT8613_SUPERIO_PORT;
    PartialList->PartialDescriptors[0].u.Port.Length = IT8613_SUPERIO_LENGTH;
    PartialList->PartialDescriptors[1].Type = CmResourceTypePort;
    PartialList->PartialDescriptors[1].ShareDisposition = CmResourceShareShared;
    PartialList->PartialDescriptors[1].Flags = CM_RESOURCE_PORT_IO;
    PartialList->PartialDescriptors[1].u.Port.Start.QuadPart = IT8613_HWM_INDEX_PORT;
    PartialList->PartialDescriptors[1].u.Port.Length = IT8613_HWM_LENGTH;
    return Resources;
}

static
VOID
NTAPI
It8613Unload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    PCM_RESOURCE_LIST Resources;
    PDEVICE_OBJECT PhysicalDeviceObject = NULL;
    NTSTATUS Status;
    UCHAR VinMask;
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    KeInitializeSpinLock(&It8613DetectionLock);
    Status = It8613DetectHardware(&It8613DetectionLock, IT8613_SUPERIO_PORT, IT8613_HWM_INDEX_PORT, &VinMask);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = It8613Unsupported;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = It8613CreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = It8613CreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = It8613DeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = It8613Pnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = It8613Power;
    DriverObject->DriverExtension->AddDevice = It8613AddDevice;
    DriverObject->DriverUnload = It8613Unload;
    Resources = It8613CreateResourceList();
    if (!Resources)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoReportDetectedDevice(DriverObject, Internal, (ULONG)-1, (ULONG)-1, Resources, NULL, TRUE, &PhysicalDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Resources, IT8613_TAG);
        return Status;
    }
    Status = It8613AddDevice(DriverObject, PhysicalDeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    PhysicalDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
