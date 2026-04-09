/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     USBPort interface functions
 * COPYRIGHT:   Copyright 2017 Vadim Galyant <vgal@rambler.ru>
 */

#include "usbport.h"
#include <acpiioct.h>

#ifndef ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX
#define ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX 'AieA'
#endif
#ifndef IOCTL_ACPI_EVAL_METHOD_EX
#define IOCTL_ACPI_EVAL_METHOD_EX IOCTL_ACPI_EVAL_METHOD
#endif

#ifndef ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE
#define ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE 'HieA'
#endif

#ifndef IOCTL_ACPI_ENUM_CHILDREN
#define IOCTL_ACPI_ENUM_CHILDREN \
    CTL_CODE(FILE_DEVICE_ACPI, 8, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#endif

#ifndef ENUM_CHILDREN_IMMEDIATE_ONLY
#define ENUM_CHILDREN_IMMEDIATE_ONLY        0x1
#define ENUM_CHILDREN_MULTILEVEL            0x2
#define ENUM_CHILDREN_NAME_IS_FILTER        0x4
#endif

#define NDEBUG
#include <debug.h>

typedef struct _USBPORT_ACPI_QUERY_CONTEXT {
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
} USBPORT_ACPI_QUERY_CONTEXT, *PUSBPORT_ACPI_QUERY_CONTEXT;

static
NTSTATUS
USBPORT_SendAcpiIoctl(
    _In_ PUSBPORT_DEVICE_EXTENSION FdoExtension,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    PDEVICE_OBJECT TargetDevice;
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    TargetDevice = FdoExtension->CommonExtension.LowerDevice;
    if (!TargetDevice)
        return STATUS_INVALID_DEVICE_REQUEST;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        TargetDevice,
                                        InputBuffer,
                                        InputBufferLength,
                                        OutputBuffer,
                                        OutputBufferLength,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(TargetDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

static
NTSTATUS
USBPORT_EvalAcpiMethodAnsi(
    _In_ PUSBPORT_DEVICE_EXTENSION FdoExtension,
    _In_ PCSTR MethodName,
    _Out_writes_bytes_(OutputBufferLength) PACPI_EVAL_OUTPUT_BUFFER OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    ACPI_EVAL_INPUT_BUFFER_EX InputBuffer;
    NTSTATUS Status;

    RtlZeroMemory(&InputBuffer, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;

    Status = RtlStringCchCopyA(InputBuffer.MethodName,
                               RTL_NUMBER_OF(InputBuffer.MethodName),
                               MethodName);
    if (!NT_SUCCESS(Status))
        return Status;

    return USBPORT_SendAcpiIoctl(FdoExtension,
                                 IOCTL_ACPI_EVAL_METHOD_EX,
                                 &InputBuffer,
                                 sizeof(InputBuffer),
                                 OutputBuffer,
                                 OutputBufferLength);
}

static
NTSTATUS
USBPORT_EnumerateAcpiChildren(
    _In_ PUSBPORT_DEVICE_EXTENSION FdoExtension,
    _Outptr_result_bytebuffer_(*BufferSize) PACPI_ENUM_CHILDREN_OUTPUT_BUFFER *EnumBuffer,
    _Out_ PULONG BufferSize)
{
    ACPI_ENUM_CHILDREN_INPUT_BUFFER InputBuffer;
    NTSTATUS Status;
    ULONG Size;
    PACPI_ENUM_CHILDREN_OUTPUT_BUFFER LocalBuffer;
    ULONG Attempt;

    RtlZeroMemory(&InputBuffer, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE;
    InputBuffer.Flags = ENUM_CHILDREN_MULTILEVEL;
    InputBuffer.NameLength = 0;

    Size = sizeof(ACPI_ENUM_CHILDREN_OUTPUT_BUFFER) + 0x400;

    for (Attempt = 0; Attempt < 4; ++Attempt)
    {
        LocalBuffer = ExAllocatePoolWithTag(PagedPool,
                                            Size,
                                            USB_PORT_TAG);
        if (!LocalBuffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory(LocalBuffer, Size);

        Status = USBPORT_SendAcpiIoctl(FdoExtension,
                                       IOCTL_ACPI_ENUM_CHILDREN,
                                       &InputBuffer,
                                       sizeof(InputBuffer),
                                       LocalBuffer,
                                       Size);

        if (Status == STATUS_BUFFER_OVERFLOW)
        {
            ExFreePoolWithTag(LocalBuffer, USB_PORT_TAG);
            Size *= 2;
            continue;
        }

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(LocalBuffer, USB_PORT_TAG);
            return Status;
        }

        *EnumBuffer = LocalBuffer;
        *BufferSize = Size;
        return STATUS_SUCCESS;
    }

    return STATUS_BUFFER_OVERFLOW;
}

static
BOOLEAN
USBPORT_ParseAcpiUpcPackage(
    _In_ PACPI_METHOD_ARGUMENT PackageArgument,
    _Out_ PBOOLEAN Connectable,
    _Out_ PUCHAR ConnectorType,
    _Out_opt_ PULONG TypeCCapabilities)
{
    PACPI_METHOD_ARGUMENT Field;
    ULONG Remaining;
    ULONG Index = 0;

    if (PackageArgument->Type != ACPI_METHOD_ARGUMENT_PACKAGE ||
        PackageArgument->DataLength == 0)
    {
        return FALSE;
    }

    Field = (PACPI_METHOD_ARGUMENT)PackageArgument->Data;
    Remaining = PackageArgument->DataLength;
    *Connectable = FALSE;
    *ConnectorType = 0;
    if (TypeCCapabilities)
        *TypeCCapabilities = 0;

    while (Remaining >= ACPI_METHOD_ARGUMENT_LENGTH(0) && Index < 3)
    {
        ULONG FieldLength;

        if (Field->Type != ACPI_METHOD_ARGUMENT_INTEGER)
            return FALSE;

        if (Index == 0)
            *Connectable = (Field->Argument != 0);
        else if (Index == 1)
            *ConnectorType = (UCHAR)(Field->Argument & 0xFF);
        else if (Index == 2 && TypeCCapabilities)
            *TypeCCapabilities = Field->Argument;

        FieldLength = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Field);
        if (FieldLength > Remaining)
            break;

        Remaining -= FieldLength;
        Field = ACPI_METHOD_NEXT_ARGUMENT(Field);
        ++Index;
    }

    return (Index >= 2);
}

static
BOOLEAN
USBPORT_ParseAcpiPldBuffer(
    _In_reads_bytes_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PBOOLEAN UserVisible)
{
    if (BufferLength < 9)
        return FALSE;

    *UserVisible = (Buffer[8] & 0x01) ? TRUE : FALSE;
    return TRUE;
}

static
BOOLEAN
USBPORT_ParseAcpiPldPackage(
    _In_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PBOOLEAN UserVisible)
{
    if (Argument->Type == ACPI_METHOD_ARGUMENT_BUFFER)
        return USBPORT_ParseAcpiPldBuffer(Argument->Data,
                                          Argument->DataLength,
                                          UserVisible);

    if (Argument->Type == ACPI_METHOD_ARGUMENT_PACKAGE &&
        Argument->DataLength >= ACPI_METHOD_ARGUMENT_LENGTH(0))
    {
        PACPI_METHOD_ARGUMENT Field = (PACPI_METHOD_ARGUMENT)Argument->Data;

        if (!Field)
            return FALSE;

        return USBPORT_ParseAcpiPldPackage(Field, UserVisible);
    }

    return FALSE;
}

static
PULONG
USBPORT_QueryAcpiPortAttributesAll(
    _In_ PUSBPORT_DEVICE_EXTENSION FdoExtension,
    _In_ ULONG PortCount)
{
    PACPI_ENUM_CHILDREN_OUTPUT_BUFFER EnumBuffer;
    ULONG EnumBufferSize;
    NTSTATUS Status;
    PULONG Attributes = NULL;
    UCHAR OutputBufferSpace[sizeof(ACPI_EVAL_OUTPUT_BUFFER) + 256];
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_ENUM_CHILD Child;
    ULONG ChildIndex;

    if (PortCount == 0)
        return NULL;

    Status = USBPORT_EnumerateAcpiChildren(FdoExtension,
                                           &EnumBuffer,
                                           &EnumBufferSize);
    if (!NT_SUCCESS(Status))
        return NULL;

    Attributes = ExAllocatePoolWithTag(PagedPool,
                                       sizeof(ULONG) * PortCount,
                                       USB_PORT_TAG);
    if (!Attributes)
    {
        ExFreePoolWithTag(EnumBuffer, USB_PORT_TAG);
        return NULL;
    }

    RtlZeroMemory(Attributes, sizeof(ULONG) * PortCount);
    Output = (PACPI_EVAL_OUTPUT_BUFFER)OutputBufferSpace;

    Child = &EnumBuffer->Children[0];
    for (ChildIndex = 0;
         ChildIndex < EnumBuffer->NumberOfChildren;
         ++ChildIndex)
    {
        CHAR MethodName[256];
        ULONG AdrValue;
        BOOLEAN Connectable;
        UCHAR ConnectorType;
        ULONG PortIndex;

        if (!NT_SUCCESS(RtlStringCchPrintfA(MethodName,
                                            RTL_NUMBER_OF(MethodName),
                                            "%s._ADR",
                                            Child->Name)))
        {
            goto NextChild;
        }

        RtlZeroMemory(OutputBufferSpace, sizeof(OutputBufferSpace));
        Status = USBPORT_EvalAcpiMethodAnsi(FdoExtension,
                                            MethodName,
                                            Output,
                                            sizeof(OutputBufferSpace));
        if (!NT_SUCCESS(Status) || Output->Count < 1)
            goto NextChild;

        if (Output->Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER)
            goto NextChild;

        AdrValue = Output->Argument->Argument;
        if (AdrValue == 0 || AdrValue > PortCount)
            goto NextChild;

        PortIndex = AdrValue - 1;

        if (!NT_SUCCESS(RtlStringCchPrintfA(MethodName,
                                            RTL_NUMBER_OF(MethodName),
                                            "%s._UPC",
                                            Child->Name)))
        {
            goto NextChild;
        }

        RtlZeroMemory(OutputBufferSpace, sizeof(OutputBufferSpace));
        Status = USBPORT_EvalAcpiMethodAnsi(FdoExtension,
                                            MethodName,
                                            Output,
                                            sizeof(OutputBufferSpace));
        if (!NT_SUCCESS(Status) || Output->Count < 1)
            goto NextChild;

        ULONG TypecCaps = 0;
        if (!USBPORT_ParseAcpiUpcPackage(Output->Argument,
                                         &Connectable,
                                         &ConnectorType,
                                         &TypecCaps))
        {
            goto NextChild;
        }

        if (!Connectable)
            Attributes[PortIndex] |= USB_PORTATTR_NO_CONNECTOR;

        if (ConnectorType == 0x08 ||
            ConnectorType == 0x09 ||
            ConnectorType == 0x0A)
        {
            Attributes[PortIndex] |= USB_PORTATTR_TYPEC_CONNECTOR;
            if (TypecCaps & (1 << 4))
                Attributes[PortIndex] |= USB_PORTATTR_TYPEC_USB4_CAPABLE;
            if (TypecCaps & (1 << 5))
                Attributes[PortIndex] |= USB_PORTATTR_TYPEC_TBT3_CAPABLE;
            if (TypecCaps & (1 << 2))
                Attributes[PortIndex] |= USB_PORTATTR_TYPEC_PCIE_TUNNELING;
            if (TypecCaps & (1 << 3))
                Attributes[PortIndex] |= USB_PORTATTR_TYPEC_DP_ALT_MODE;
            Attributes[PortIndex] &= ~USB_PORTATTR_TYPEC_RETIMER_MASK;
            Attributes[PortIndex] |= ((TypecCaps & 0x3) << USB_PORTATTR_TYPEC_RETIMER_SHIFT);
        }
        else if (ConnectorType == 0x01 ||
                 ConnectorType == 0x05 ||
                 ConnectorType == 0x06)
        {
            Attributes[PortIndex] |= USB_PORTATTR_MINI_CONNECTOR;
        }
        else if (ConnectorType == 0xFF)
        {
            Attributes[PortIndex] |= USB_PORTATTR_OEM_CONNECTOR;
        }

        if (!NT_SUCCESS(RtlStringCchPrintfA(MethodName,
                                            RTL_NUMBER_OF(MethodName),
                                            "%s._PLD",
                                            Child->Name)))
        {
            goto NextChild;
        }

        RtlZeroMemory(OutputBufferSpace, sizeof(OutputBufferSpace));
        Status = USBPORT_EvalAcpiMethodAnsi(FdoExtension,
                                            MethodName,
                                            Output,
                                            sizeof(OutputBufferSpace));
        if (NT_SUCCESS(Status) && Output->Count >= 1)
        {
            BOOLEAN UserVisible;

            if (USBPORT_ParseAcpiPldPackage(Output->Argument, &UserVisible) &&
                !UserVisible)
            {
                Attributes[PortIndex] |= USB_PORTATTR_NO_CONNECTOR;
            }
        }

NextChild:
        Child = ACPI_ENUM_CHILD_NEXT(Child);
    }

    ExFreePoolWithTag(EnumBuffer, USB_PORT_TAG);
    return Attributes;
}

VOID
USB_BUSIFFN
USBI_InterfaceReference(IN PVOID BusContext)
{
    DPRINT("USBI_InterfaceReference\n");
}

VOID
USB_BUSIFFN
USBI_InterfaceDereference(IN PVOID BusContext)
{
    DPRINT("USBI_InterfaceDereference\n");
}

/* USB port driver Interface functions */

NTSTATUS
USB_BUSIFFN
USBHI_CreateUsbDevice(IN PVOID BusContext,
                      IN OUT PUSB_DEVICE_HANDLE *UsbdDeviceHandle,
                      IN PUSB_DEVICE_HANDLE UsbdHubDeviceHandle,
                      IN USHORT PortStatus,
                      IN USHORT PortNumber)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSB_DEVICE_HANDLE deviceHandle = NULL;
    NTSTATUS Status;

    DPRINT("USBHI_CreateUsbDevice: ...\n");

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;

    Status = USBPORT_CreateDevice(&deviceHandle,
                                  PdoExtension->FdoDevice,
                                  (PUSBPORT_DEVICE_HANDLE)UsbdHubDeviceHandle,
                                  PortStatus,
                                  PortNumber);

    *UsbdDeviceHandle = deviceHandle;

    return Status;
}

NTSTATUS
USB_BUSIFFN
USBHI_InitializeUsbDevice(IN PVOID BusContext,
                          OUT PUSB_DEVICE_HANDLE UsbdDeviceHandle)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;

    DPRINT("USBHI_InitializeUsbDevice\n");

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;

    return USBPORT_InitializeDevice((PUSBPORT_DEVICE_HANDLE)UsbdDeviceHandle,
                                    PdoExtension->FdoDevice);
}

NTSTATUS
USB_BUSIFFN
USBHI_GetUsbDescriptors(IN PVOID BusContext,
                        IN PUSB_DEVICE_HANDLE UsbdDeviceHandle,
                        IN PUCHAR DeviceDescBuffer,
                        IN PULONG DeviceDescBufferLen,
                        IN PUCHAR ConfigDescBuffer,
                        IN PULONG ConfigDescBufferLen)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSBPORT_DEVICE_HANDLE DeviceHandle;

    NTSTATUS Status;

    DPRINT("USBHI_GetUsbDescriptors ...\n");

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;
    DeviceHandle = (PUSBPORT_DEVICE_HANDLE)UsbdDeviceHandle;

    if (DeviceDescBuffer && *DeviceDescBufferLen)
    {
        if (*DeviceDescBufferLen > sizeof(USB_DEVICE_DESCRIPTOR))
            *DeviceDescBufferLen = sizeof(USB_DEVICE_DESCRIPTOR);

        RtlCopyMemory(DeviceDescBuffer,
                      &DeviceHandle->DeviceDescriptor,
                      *DeviceDescBufferLen);
    }

    Status = USBPORT_GetUsbDescriptor(DeviceHandle,
                                      PdoExtension->FdoDevice,
                                      USB_CONFIGURATION_DESCRIPTOR_TYPE,
                                      ConfigDescBuffer,
                                      ConfigDescBufferLen);

    USBPORT_DumpingDeviceDescriptor((PUSB_DEVICE_DESCRIPTOR)DeviceDescBuffer);

    return Status;
}

NTSTATUS
USB_BUSIFFN
USBHI_RemoveUsbDevice(IN PVOID BusContext,
                      IN OUT PUSB_DEVICE_HANDLE UsbdDeviceHandle,
                      IN ULONG Flags)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;

    DPRINT("USBHI_RemoveUsbDevice: UsbdDeviceHandle - %p, Flags - %x\n",
           UsbdDeviceHandle,
           Flags);

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;

    return USBPORT_RemoveDevice(PdoExtension->FdoDevice,
                                (PUSBPORT_DEVICE_HANDLE)UsbdDeviceHandle,
                                Flags);
}

NTSTATUS
USB_BUSIFFN
USBHI_RestoreUsbDevice(IN PVOID BusContext,
                       OUT PUSB_DEVICE_HANDLE OldUsbdDeviceHandle,
                       OUT PUSB_DEVICE_HANDLE NewUsbdDeviceHandle)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;

    DPRINT("USBHI_RestoreUsbDevice: OldUsbdDeviceHandle - %p, NewUsbdDeviceHandle - %x\n",
           OldUsbdDeviceHandle,
           NewUsbdDeviceHandle);

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;

    return USBPORT_RestoreDevice(PdoExtension->FdoDevice,
                                (PUSBPORT_DEVICE_HANDLE)OldUsbdDeviceHandle,
                                (PUSBPORT_DEVICE_HANDLE)NewUsbdDeviceHandle);
}

NTSTATUS
USB_BUSIFFN
USBHI_QueryDeviceInformation(IN PVOID BusContext,
                             IN PUSB_DEVICE_HANDLE UsbdDeviceHandle,
                             OUT PVOID DeviceInfoBuffer,
                             IN ULONG DeviceInfoBufferLen,
                             OUT PULONG LenDataReturned)
{
    PUSB_DEVICE_INFORMATION_0 DeviceInfo;
    PUSBPORT_CONFIGURATION_HANDLE ConfigHandle;
    PLIST_ENTRY InterfaceEntry;
    PUSBPORT_DEVICE_HANDLE DeviceHandle;
    ULONG NumberOfOpenPipes = 0;
    PUSB_PIPE_INFORMATION_0 PipeInfo;
    PUSBPORT_PIPE_HANDLE PipeHandle;
    PUSBPORT_INTERFACE_HANDLE InterfaceHandle;
    ULONG ActualLength;
    ULONG ix;

    DPRINT("USBHI_QueryDeviceInformation: ...\n");

    *LenDataReturned = 0;

    if (DeviceInfoBufferLen < sizeof(USB_LEVEL_INFORMATION))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    DeviceInfo = DeviceInfoBuffer;

    if (DeviceInfo->InformationLevel > 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    DeviceHandle = UsbdDeviceHandle;
    ConfigHandle = DeviceHandle->ConfigHandle;

    if (ConfigHandle)
    {
        InterfaceEntry = ConfigHandle->InterfaceHandleList.Flink;

        while (InterfaceEntry &&
               InterfaceEntry != &ConfigHandle->InterfaceHandleList)
        {
            InterfaceHandle = CONTAINING_RECORD(InterfaceEntry,
                                                USBPORT_INTERFACE_HANDLE,
                                                InterfaceLink);

            NumberOfOpenPipes += InterfaceHandle->InterfaceDescriptor.bNumEndpoints;

            InterfaceEntry = InterfaceEntry->Flink;
        }
    }

    ActualLength = FIELD_OFFSET(USB_DEVICE_INFORMATION_0, PipeList) +
                   NumberOfOpenPipes * sizeof(USB_PIPE_INFORMATION_0);

    if (DeviceInfoBufferLen < ActualLength)
    {
        DeviceInfo->ActualLength = ActualLength;
        *LenDataReturned = sizeof(USB_LEVEL_INFORMATION);

        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(DeviceInfo, ActualLength);

    DeviceInfo->InformationLevel = 0;
    DeviceInfo->ActualLength = ActualLength;
    DeviceInfo->DeviceAddress = DeviceHandle->DeviceAddress;
    DeviceInfo->NumberOfOpenPipes = NumberOfOpenPipes;
    DeviceInfo->DeviceSpeed = DeviceHandle->DeviceSpeed;

    RtlCopyMemory(&DeviceInfo->DeviceDescriptor,
                  &DeviceHandle->DeviceDescriptor,
                  sizeof(USB_DEVICE_DESCRIPTOR));

    USBPORT_DumpingDeviceDescriptor(&DeviceInfo->DeviceDescriptor);

    if (DeviceHandle->DeviceSpeed == UsbFullSpeed ||
        DeviceHandle->DeviceSpeed == UsbLowSpeed)
    {
        DeviceInfo->DeviceType = Usb11Device;
    }
    else
    {
        /* Treat high-speed and SuperSpeed devices as Usb20Device
         * for compatibility with the Windows USB stack. */
        DeviceInfo->DeviceType = Usb20Device;
    }

    DeviceInfo->CurrentConfigurationValue = 0;

    if (!ConfigHandle)
    {
        *LenDataReturned = ActualLength;
        return STATUS_SUCCESS;
    }

    DeviceInfo->CurrentConfigurationValue =
        ConfigHandle->ConfigurationDescriptor->bConfigurationValue;

    InterfaceEntry = ConfigHandle->InterfaceHandleList.Flink;

    while (InterfaceEntry &&
           InterfaceEntry != &ConfigHandle->InterfaceHandleList)
    {
        InterfaceHandle = CONTAINING_RECORD(InterfaceEntry,
                                            USBPORT_INTERFACE_HANDLE,
                                            InterfaceLink);

        if (InterfaceHandle->InterfaceDescriptor.bNumEndpoints > 0)
        {
            PipeInfo = &DeviceInfo->PipeList[0];
            PipeHandle = &InterfaceHandle->PipeHandle[0];

            for (ix = 0;
                 ix < InterfaceHandle->InterfaceDescriptor.bNumEndpoints;
                 ix++)
            {
                if (PipeHandle->Flags & PIPE_HANDLE_FLAG_NULL_PACKET_SIZE)
                {
                    PipeInfo->ScheduleOffset = 1;
                }
                else
                {
                    PipeInfo->ScheduleOffset =
                        PipeHandle->Endpoint->EndpointProperties.ScheduleOffset;
                }

                RtlCopyMemory(&PipeInfo->EndpointDescriptor,
                              &PipeHandle->EndpointDescriptor,
                              sizeof(USB_ENDPOINT_DESCRIPTOR));

                PipeInfo += 1;
                PipeHandle += 1;
            }
        }

        InterfaceEntry = InterfaceEntry->Flink;
    }

    *LenDataReturned = ActualLength;

    return STATUS_SUCCESS;
}

NTSTATUS
USB_BUSIFFN
USBHI_GetControllerInformation(IN PVOID BusContext,
                               OUT PVOID ControllerInfoBuffer,
                               IN ULONG ControllerInfoBufferLen,
                               OUT PULONG LenDataReturned)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSB_CONTROLLER_INFORMATION_0 InfoBuffer;
    NTSTATUS Status;

    DPRINT("USBHI_GetControllerInformation: ControllerInfoBufferLen - %x\n",
           ControllerInfoBufferLen);

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;
    FdoDevice = PdoExtension->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;

    InfoBuffer = ControllerInfoBuffer;

    *LenDataReturned = 0;

    if (ControllerInfoBufferLen < sizeof(USB_LEVEL_INFORMATION))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        return Status;
    }

    *LenDataReturned = sizeof(USB_LEVEL_INFORMATION);

    if (InfoBuffer->InformationLevel > 0)
    {
        Status = STATUS_NOT_SUPPORTED;
        return Status;
    }

    InfoBuffer->ActualLength = sizeof(USB_CONTROLLER_INFORMATION_0);

    if (ControllerInfoBufferLen >= sizeof(USB_CONTROLLER_INFORMATION_0))
    {
        InfoBuffer->SelectiveSuspendEnabled =
            (FdoExtension->Flags & USBPORT_FLAG_SELECTIVE_SUSPEND) ==
            USBPORT_FLAG_SELECTIVE_SUSPEND;
    }

    *LenDataReturned = sizeof(USB_CONTROLLER_INFORMATION_0);

    return STATUS_SUCCESS;
}

NTSTATUS
USB_BUSIFFN
USBHI_ControllerSelectiveSuspend(IN PVOID BusContext,
                                 IN BOOLEAN Enable)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    ULONG Flags;
    ULONG HcDisable;
    NTSTATUS Status;

    DPRINT("USBHI_ControllerSelectiveSuspend: Enable - %x\n", Enable);

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;
    FdoDevice = PdoExtension->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;

    Flags = FdoExtension->Flags;

    if (Flags & USBPORT_FLAG_BIOS_DISABLE_SS)
    {
        return STATUS_SUCCESS;
    }

    if (Enable)
    {
        FdoExtension->Flags |= USBPORT_FLAG_SELECTIVE_SUSPEND;
        HcDisable = 0;
    }
    else
    {
        FdoExtension->Flags &= ~USBPORT_FLAG_SELECTIVE_SUSPEND;
        HcDisable = 1;
    }

    Status = USBPORT_SetRegistryKeyValue(FdoExtension->CommonExtension.LowerPdoDevice,
                                         TRUE,
                                         REG_DWORD,
                                         L"HcDisableSelectiveSuspend",
                                         &HcDisable,
                                         sizeof(HcDisable));

    if (NT_SUCCESS(Status))
    {
        if (Enable)
            FdoExtension->Flags |= USBPORT_FLAG_SELECTIVE_SUSPEND;
        else
            FdoExtension->Flags &= ~USBPORT_FLAG_SELECTIVE_SUSPEND;
    }

    return Status;
}

NTSTATUS
USB_BUSIFFN
USBHI_GetExtendedHubInformation(IN PVOID BusContext,
                                IN PDEVICE_OBJECT HubPhysicalDeviceObject,
                                IN OUT PVOID HubInformationBuffer,
                                IN ULONG HubInfoLen,
                                IN OUT PULONG LenDataReturned)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    ULONG NumPorts;
    ULONG ix;
    PUSB_EXTHUB_INFORMATION_0 HubInfoBuffer;
    USB_PORT_STATUS_AND_CHANGE PortStatus;
    ULONG PortAttrX;
    PDEVICE_RELATIONS CompanionList = NULL;
    PUNICODE_STRING CompanionLinks = NULL;
    ULONG CompanionCount = 0;
    ULONG CompanionIndex;
    NTSTATUS Status;
    WCHAR ValueName[32];
    PULONG AcpiAttributes = NULL;

    DPRINT("USBHI_GetExtendedHubInformation: ...\n");

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;
    FdoDevice = PdoExtension->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;
    PHCI_QUERY_COMPANION_PORT_INFO QueryCompanionPortInfo = Packet->QueryCompanionPortInfo;
    PHCI_QUERY_PORT_ATTRIBUTES QueryPortAttributes = Packet->QueryPortAttributes;

    HubInfoBuffer = HubInformationBuffer;
    PortStatus.AsUlong32 = 0;

    if (HubPhysicalDeviceObject != PdoDevice)
    {
        *LenDataReturned = 0;
        return STATUS_NOT_SUPPORTED;
    }

    if (HubInfoLen < sizeof(USB_EXTHUB_INFORMATION_0))
    {
        *LenDataReturned = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    NumPorts = PdoExtension->RootHubDescriptors->Descriptor.bNumberOfPorts;
    HubInfoBuffer->NumberOfPorts = NumPorts;

    if (NumPorts == 0)
    {
        *LenDataReturned = sizeof(USB_EXTHUB_INFORMATION_0);
        return STATUS_SUCCESS;
    }

    AcpiAttributes = USBPORT_QueryAcpiPortAttributesAll(FdoExtension, NumPorts);

    for (ix = 0; ix < HubInfoBuffer->NumberOfPorts; ++ix)
    {
        HubInfoBuffer->Port[ix].PhysicalPortNumber = ix + 1;
        HubInfoBuffer->Port[ix].PortLabelNumber = ix;
        HubInfoBuffer->Port[ix].VidOverride = 0;
        HubInfoBuffer->Port[ix].PidOverride = 0;
        HubInfoBuffer->Port[ix].PortAttributes = 0;

        if (AcpiAttributes && AcpiAttributes[ix])
        {
            HubInfoBuffer->Port[ix].PortAttributes |= AcpiAttributes[ix];
        }

        if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
        {
            HubInfoBuffer->Port[ix].PortAttributes = USB_PORTATTR_SHARED_USB2;

            Packet->RH_GetPortStatus(FdoExtension->MiniPortExt,
                                     ix + 1,
                                     &PortStatus);

            if (PortStatus.PortStatus.Usb20PortStatus.AsUshort16 & 0x8000)
            {
                HubInfoBuffer->Port[ix].PortAttributes |= USB_PORTATTR_OWNED_BY_CC;
            }
        }
        else
        {
            if (!(FdoExtension->Flags & USBPORT_FLAG_COMPANION_HC))
            {
                continue;
            }

            if (USBPORT_FindUSB2Controller(FdoDevice))
            {
                HubInfoBuffer->Port[ix].PortAttributes |= USB_PORTATTR_NO_OVERCURRENT_UI;
            }
        }
    }

    for (ix = 0; ix < HubInfoBuffer->NumberOfPorts; ++ix)
    {
        NTSTATUS AttrStatus;
        ULONG ValueNameLength;

        PortAttrX = 0;
        AttrStatus = STATUS_UNSUCCESSFUL;

        if (NT_SUCCESS(RtlStringCchPrintfW(ValueName,
                                           ARRAYSIZE(ValueName),
                                           L"PortAttr%02u",
                                           ix + 1)))
        {
            ValueNameLength = (ULONG)((wcslen(ValueName) + 1) * sizeof(WCHAR));
            AttrStatus = USBPORT_GetRegistryKeyValueFullInfo(FdoDevice,
                                                             FdoExtension->CommonExtension.LowerPdoDevice,
                                                             FALSE,
                                                             ValueName,
                                                             ValueNameLength,
                                                             &PortAttrX,
                                                             sizeof(PortAttrX));
        }

        if (!NT_SUCCESS(AttrStatus))
        {
            AttrStatus = USBPORT_GetRegistryKeyValueFullInfo(FdoDevice,
                                                             FdoExtension->CommonExtension.LowerPdoDevice,
                                                             FALSE,
                                                             L"PortAttrX",
                                                             sizeof(L"PortAttrX"),
                                                             &PortAttrX,
                                                             sizeof(PortAttrX));
        }

        if (NT_SUCCESS(AttrStatus))
        {
            HubInfoBuffer->Port[ix].PortAttributes |= PortAttrX;
        }
    }

    CompanionList = USBPORT_FindCompanionControllers(FdoDevice,
                                                      TRUE,
                                                      TRUE);
    if (CompanionList && CompanionList->Count)
    {
        CompanionCount = CompanionList->Count;
        CompanionLinks = ExAllocatePoolWithTag(PagedPool,
                                               CompanionCount * sizeof(UNICODE_STRING),
                                               USB_PORT_TAG);
        if (CompanionLinks)
        {
            RtlZeroMemory(CompanionLinks, CompanionCount * sizeof(UNICODE_STRING));
            for (ix = 0; ix < CompanionCount; ++ix)
            {
                PDEVICE_OBJECT CompanionFdo = CompanionList->Objects[ix];
                PUSBPORT_DEVICE_EXTENSION CompanionExt = CompanionFdo ? CompanionFdo->DeviceExtension : NULL;
                PDEVICE_OBJECT CompanionRootHub = CompanionExt ? CompanionExt->RootHubPdo : NULL;

                if (!CompanionRootHub)
                    continue;

                Status = USBPORT_GetSymbolicName(CompanionRootHub,
                                                 &CompanionLinks[ix]);
                if (!NT_SUCCESS(Status))
                {
                    RtlInitUnicodeString(&CompanionLinks[ix], NULL);
                }
            }
        }
    }

    for (ix = 0; ix < HubInfoBuffer->NumberOfPorts; ++ix)
    {
        ULONG Attributes = HubInfoBuffer->Port[ix].PortAttributes;
        BOOLEAN RouteFromHardware = FALSE;
        BOOLEAN CompanionHubStored = FALSE;
        USHORT PortNumber = (USHORT)(ix + 1);
        ULONG MiniportAttributes;

        if (QueryPortAttributes)
        {
            MiniportAttributes = 0;
            if (QueryPortAttributes(FdoExtension->MiniPortExt,
                                    PortNumber,
                                    &MiniportAttributes) &&
                MiniportAttributes)
            {
                Attributes |= MiniportAttributes;
                HubInfoBuffer->Port[ix].PortAttributes = Attributes;
            }
        }

        if (QueryCompanionPortInfo)
        {
            USBPORT_COMPANION_PORT_INFO PortInfo;

            if (QueryCompanionPortInfo(FdoExtension->MiniPortExt,
                                       PortNumber,
                                       &PortInfo))
            {
                ULONG IndexField = ((ULONG)PortInfo.CompanionIndex << USB_PORTATTR_COMPANION_INDEX_SHIFT);
                ULONG PortField = ((ULONG)PortInfo.CompanionPortNumber << USB_PORTATTR_COMPANION_PORT_SHIFT);

                Attributes &= ~(USB_PORTATTR_COMPANION_INDEX_MASK |
                                USB_PORTATTR_COMPANION_PORT_MASK);
                Attributes |= (IndexField & USB_PORTATTR_COMPANION_INDEX_MASK);
                Attributes |= (PortField & USB_PORTATTR_COMPANION_PORT_MASK);
                HubInfoBuffer->Port[ix].PortAttributes = Attributes;
                RouteFromHardware = TRUE;

                if (CompanionLinks &&
                    CompanionCount &&
                    PortInfo.CompanionIndex >= 1 &&
                    PortInfo.CompanionIndex <= CompanionCount)
                {
                    ULONG RouteIndex = PortInfo.CompanionIndex - 1;

                    if (CompanionLinks[RouteIndex].Buffer &&
                        NT_SUCCESS(RtlStringCchPrintfW(ValueName,
                                                       ARRAYSIZE(ValueName),
                                                       L"CompanionHub%02u",
                                                       PortNumber)))
                    {
                        USBPORT_SetRegistryKeyValue(FdoDevice,
                                                    FALSE,
                                                    REG_SZ,
                                                    ValueName,
                                                    CompanionLinks[RouteIndex].Buffer,
                                                    CompanionLinks[RouteIndex].Length + sizeof(WCHAR));
                        CompanionHubStored = TRUE;
                    }
                }
            }
        }

        if (!RouteFromHardware && CompanionCount)
        {
            CompanionIndex = ix % CompanionCount;
            Attributes &= ~(USB_PORTATTR_COMPANION_INDEX_MASK |
                            USB_PORTATTR_COMPANION_PORT_MASK);
            Attributes |= (((CompanionIndex + 1) << USB_PORTATTR_COMPANION_INDEX_SHIFT) &
                           USB_PORTATTR_COMPANION_INDEX_MASK);
            Attributes |= (((ULONG)PortNumber << USB_PORTATTR_COMPANION_PORT_SHIFT) &
                           USB_PORTATTR_COMPANION_PORT_MASK);
            HubInfoBuffer->Port[ix].PortAttributes = Attributes;

            if (!CompanionHubStored &&
                CompanionLinks &&
                CompanionLinks[CompanionIndex].Buffer &&
                NT_SUCCESS(RtlStringCchPrintfW(ValueName,
                                               ARRAYSIZE(ValueName),
                                               L"CompanionHub%02u",
                                               PortNumber)))
            {
                USBPORT_SetRegistryKeyValue(FdoDevice,
                                            FALSE,
                                            REG_SZ,
                                            ValueName,
                                            CompanionLinks[CompanionIndex].Buffer,
                                            CompanionLinks[CompanionIndex].Length + sizeof(WCHAR));
                CompanionHubStored = TRUE;
            }
        }

        if (NT_SUCCESS(RtlStringCchPrintfW(ValueName,
                                           ARRAYSIZE(ValueName),
                                           L"PortAttr%02u",
                                           PortNumber)))
        {
            USBPORT_SetRegistryKeyValue(FdoDevice,
                                        FALSE,
                                        REG_DWORD,
                                        ValueName,
                                        &Attributes,
                                        sizeof(Attributes));
        }
    }

    if (CompanionLinks)
    {
        for (ix = 0; ix < CompanionCount; ++ix)
        {
            if (CompanionLinks[ix].Buffer)
                RtlFreeUnicodeString(&CompanionLinks[ix]);
        }

        ExFreePoolWithTag(CompanionLinks, USB_PORT_TAG);
    }

    if (CompanionList)
    {
        for (ix = 0; ix < CompanionList->Count; ++ix)
        {
            if (CompanionList->Objects[ix])
                ObDereferenceObject(CompanionList->Objects[ix]);
        }

        ExFreePoolWithTag(CompanionList, USB_PORT_TAG);
    }

    if (AcpiAttributes)
        ExFreePoolWithTag(AcpiAttributes, USB_PORT_TAG);

    *LenDataReturned = sizeof(USB_EXTHUB_INFORMATION_0);

    return STATUS_SUCCESS;
}

NTSTATUS
USB_BUSIFFN
USBHI_GetRootHubSymbolicName(IN PVOID BusContext,
                             IN OUT PVOID HubInfoBuffer,
                             IN ULONG HubInfoBufferLen,
                             OUT PULONG HubNameActualLen)
{
    PDEVICE_OBJECT PdoDevice;
    UNICODE_STRING HubName;
    PUNICODE_STRING InfoBuffer;
    NTSTATUS Status;

    DPRINT("USBHI_GetRootHubSymbolicName: ...\n");

    PdoDevice = BusContext;

    Status = USBPORT_GetSymbolicName(PdoDevice, &HubName);

    if (HubInfoBufferLen < HubName.Length)
    {
        InfoBuffer = HubInfoBuffer;
        InfoBuffer->Length = 0;
    }
    else
    {
        RtlCopyMemory(HubInfoBuffer, HubName.Buffer, HubName.Length);
    }

    *HubNameActualLen = HubName.Length;

    if (NT_SUCCESS(Status))
        RtlFreeUnicodeString(&HubName);

    return Status;
}

PVOID
USB_BUSIFFN
USBHI_GetDeviceBusContext(IN PVOID BusContext,
                          IN PVOID DeviceHandle)
{
    UNREFERENCED_PARAMETER(DeviceHandle);

    DPRINT("USBHI_GetDeviceBusContext: BusContext - %p, DeviceHandle - %p\n",
           BusContext,
           DeviceHandle);

    /*
     * For our implementation the bus context is already the root-hub PDO and
     * is sufficient to identify the bus for subsequent calls. We do not
     * maintain per-device bus contexts yet, so simply return the original
     * bus context.
     */
    return BusContext;
}

NTSTATUS
USB_BUSIFFN
USBHI_Initialize20Hub(IN PVOID BusContext,
                      IN PUSB_DEVICE_HANDLE UsbdHubDeviceHandle,
                      IN ULONG TtCount)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;

    DPRINT("USBHI_Initialize20Hub: UsbdHubDeviceHandle - %p, TtCount - %x\n",
           UsbdHubDeviceHandle,
           TtCount);

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;

    return USBPORT_Initialize20Hub(PdoExtension->FdoDevice,
                                   (PUSBPORT_DEVICE_HANDLE)UsbdHubDeviceHandle,
                                   TtCount);
}

NTSTATUS
USB_BUSIFFN
USBHI_RootHubInitNotification(IN PVOID BusContext,
                              IN PVOID CallbackContext,
                              IN PRH_INIT_CALLBACK CallbackFunction)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_ROOT_HUB_CALLBACK_DATA CallbackData;
    KIRQL OldIrql;

    DPRINT("USBHI_RootHubInitNotification\n");

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;
    FdoDevice = PdoExtension->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;
    CallbackData = (PUSBPORT_ROOT_HUB_CALLBACK_DATA)PdoExtension->RootHubCallbackData;

    if (!CallbackData)
    {
        DPRINT1("USBHI_RootHubInitNotification: missing callback data for PDO %p\n",
                PdoDevice);
        return STATUS_INVALID_DEVICE_STATE;
    }

    /* NULL callback is used to unregister the notification. Just clear state. */
    if (!CallbackFunction)
    {
        KeAcquireSpinLock(&CallbackData->Lock, &OldIrql);
        CallbackData->Context = NULL;
        CallbackData->Callback = NULL;
        CallbackData->Caller = NULL;
        CallbackData->Timestamp = 0;
        KeReleaseSpinLock(&CallbackData->Lock, OldIrql);

        PdoExtension->RootHubInitContext = NULL;
        PdoExtension->RootHubInitCallback = NULL;

        DPRINT1("USBHI_RootHubInitNotification: unregistered callback for PDO %p\n",
                PdoDevice);

        return STATUS_SUCCESS;
    }

    if (!USBPORT_IsKernelPointer((PVOID)CallbackFunction))
    {
        DPRINT1("USBHI_RootHubInitNotification: invalid callback %p (Context=%p Pdo=%p) caller=%p\n",
                CallbackFunction,
                CallbackContext,
                PdoDevice,
                USBPORT_RETURN_ADDRESS());
#if DBG
        DbgBreakPoint();
#endif
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&CallbackData->Lock, &OldIrql);
    CallbackData->Context = CallbackContext;
    CallbackData->Callback = CallbackFunction;
    CallbackData->Caller = USBPORT_RETURN_ADDRESS();
    CallbackData->Sequence += 1;
    CallbackData->Timestamp = KeQueryInterruptTime();
    KeReleaseSpinLock(&CallbackData->Lock, OldIrql);

    PdoExtension->RootHubInitContext = CallbackContext;
    PdoExtension->RootHubInitCallback = CallbackFunction;

    DPRINT1("USBHI_RootHubInitNotification: stored callback %p ctx=%p seq=%lu caller=%p\n",
            CallbackFunction,
            CallbackContext,
            CallbackData->Sequence,
            CallbackData->Caller);

    /* Kick the worker to process the callback now that it is armed. */
    FdoExtension->Flags |= USBPORT_FLAG_RH_INIT_CALLBACK;
    USBPORT_SignalWorkerThread(FdoDevice);

    return STATUS_SUCCESS;
}

VOID
USB_BUSIFFN
USBHI_FlushTransfers(IN PVOID BusContext,
                     OUT PUSB_DEVICE_HANDLE UsbdDeviceHandle)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;

    DPRINT("USBHI_FlushTransfers: ...\n");

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;

    USBPORT_BadRequestFlush(PdoExtension->FdoDevice);
}

VOID
USB_BUSIFFN
USBHI_SetDeviceHandleData(IN PVOID BusContext,
                          IN PVOID DeviceHandle,
                          IN PDEVICE_OBJECT UsbDevicePdo)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_DEVICE_HANDLE Handle;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    PdoDevice = BusContext;

    if (DeviceHandle == NULL || PdoDevice == NULL)
    {
        return;
    }

    PdoExtension = PdoDevice->DeviceExtension;
    FdoExtension = PdoExtension->FdoDevice->DeviceExtension;
    Handle = (PUSBPORT_DEVICE_HANDLE)DeviceHandle;

    KeAcquireSpinLock(&FdoExtension->DeviceHandleSpinLock, &OldIrql);

    Entry = FdoExtension->DeviceHandleList.Flink;
    while (Entry != &FdoExtension->DeviceHandleList)
    {
        if (CONTAINING_RECORD(Entry,
                              USBPORT_DEVICE_HANDLE,
                              DeviceHandleLink) == Handle)
        {
            Found = TRUE;
            break;
        }

        Entry = Entry->Flink;
    }

    KeReleaseSpinLock(&FdoExtension->DeviceHandleSpinLock, OldIrql);

    if (!Found)
    {
        DPRINT1("USBHI_SetDeviceHandleData: invalid handle %p for PDO %p\n",
                DeviceHandle,
                UsbDevicePdo);
    }
    else
    {
        DPRINT("USBHI_SetDeviceHandleData: handle %p PDO %p\n",
               DeviceHandle,
               UsbDevicePdo);
    }
}

/* USB bus driver Interface functions */

VOID
USB_BUSIFFN
USBDI_GetUSBDIVersion(IN PVOID BusContext,
                      OUT PUSBD_VERSION_INFORMATION VersionInfo,
                      OUT PULONG HcdCapabilities)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    ULONG Capabilities = 0;

    DPRINT("USBDI_GetUSBDIVersion: ...\n");

    if (!VersionInfo && !HcdCapabilities)
    {
        return;
    }

    PdoDevice = BusContext;
    if (!PdoDevice)
    {
        return;
    }

    PdoExtension = PdoDevice->DeviceExtension;
    if (!PdoExtension || !PdoExtension->FdoDevice)
    {
        return;
    }

    FdoDevice = PdoExtension->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;

    if (VersionInfo)
    {
        RtlZeroMemory(VersionInfo, sizeof(USBD_VERSION_INFORMATION));

        VersionInfo->USBDI_Version = USBDI_VERSION;

        if (FdoExtension->MiniPortInterface &&
            (FdoExtension->MiniPortInterface->Packet.MiniPortFlags & USB_MINIPORT_FLAGS_USB2))
        {
            VersionInfo->Supported_USB_Version = 0x0200;
        }
        else
        {
            VersionInfo->Supported_USB_Version = 0x0110;
        }
    }

    if (HcdCapabilities)
    {
        *HcdCapabilities = Capabilities;
    }
}

NTSTATUS
USB_BUSIFFN
USBDI_QueryBusTime(IN PVOID BusContext,
                   OUT PULONG CurrentFrame)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    KIRQL OldIrql;

    DPRINT("USBDI_QueryBusTime: ...\n");

    if (!CurrentFrame)
        return STATUS_INVALID_PARAMETER;

    PdoDevice = BusContext;
    if (!PdoDevice)
        return STATUS_INVALID_PARAMETER;

    PdoExtension = PdoDevice->DeviceExtension;
    if (!PdoExtension || !PdoExtension->FdoDevice)
        return STATUS_INVALID_DEVICE_REQUEST;

    FdoDevice = PdoExtension->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
    *CurrentFrame = Packet->Get32BitFrameNumber(FdoExtension->MiniPortExt);
    KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

    return STATUS_SUCCESS;
}

NTSTATUS
USB_BUSIFFN
USBDI_SubmitIsoOutUrb(IN PVOID BusContext,
                      IN PURB Urb)
{
    DPRINT1("USBDI_SubmitIsoOutUrb: UNIMPLEMENTED. FIXME.\n");
    return STATUS_SUCCESS;
}

NTSTATUS
USB_BUSIFFN
USBDI_QueryBusInformation(IN PVOID BusContext,
                          IN ULONG Level,
                          OUT PVOID BusInfoBuffer,
                          OUT PULONG BusInfoBufferLen,
                          OUT PULONG BusInfoActualLen)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    SIZE_T Length;
    PUSB_BUS_INFORMATION_LEVEL_0 Buffer0;
    PUSB_BUS_INFORMATION_LEVEL_1 Buffer1;

    DPRINT("USBDI_QueryBusInformation: Level - %p\n", Level);

    if ((Level != 0) && (Level != 1))
    {
        DPRINT1("USBDI_QueryBusInformation: Level should be 0 or 1\n");
        return STATUS_NOT_SUPPORTED;
    }

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;
    FdoDevice = PdoExtension->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;

    if (Level == 0)
    {
        ULONG TotalBandwidth;
        ULONG ConsumedBandwidth = 0;
        ULONG ix;

        if (!BusInfoBuffer || !BusInfoBufferLen)
            return STATUS_INVALID_PARAMETER;

        if (BusInfoActualLen)
            *BusInfoActualLen = sizeof(USB_BUS_INFORMATION_LEVEL_0);

        if (*BusInfoBufferLen < sizeof(USB_BUS_INFORMATION_LEVEL_0))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        *BusInfoBufferLen = sizeof(USB_BUS_INFORMATION_LEVEL_0);

        Buffer0 = BusInfoBuffer;

        TotalBandwidth = FdoExtension->TotalBusBandwidth;

        for (ix = 0; ix < USB2_FRAMES; ix++)
        {
            ULONG available = FdoExtension->Bandwidth[ix];
            ULONG used;

            if (available >= TotalBandwidth)
                continue;

            used = TotalBandwidth - available;
            if (used > ConsumedBandwidth)
                ConsumedBandwidth = used;
        }

        Buffer0->TotalBandwidth = TotalBandwidth;
        Buffer0->ConsumedBandwidth = ConsumedBandwidth;

        return STATUS_SUCCESS;
    }

    if (Level == 1)
    {
        Length = sizeof(USB_BUS_INFORMATION_LEVEL_1) +
                 FdoExtension->CommonExtension.SymbolicLinkName.Length;

        if (BusInfoActualLen)
            *BusInfoActualLen = Length;

        if (*BusInfoBufferLen < Length)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        *BusInfoBufferLen = Length;

        Buffer1 = BusInfoBuffer;
        Buffer1->TotalBandwidth = FdoExtension->TotalBusBandwidth;

        Buffer1->ConsumedBandwidth = 0;
        if (FdoExtension->TotalBusBandwidth != 0)
        {
            ULONG TotalBandwidth = FdoExtension->TotalBusBandwidth;
            ULONG ConsumedBandwidth = 0;
            ULONG ix;

            for (ix = 0; ix < USB2_FRAMES; ix++)
            {
                ULONG available = FdoExtension->Bandwidth[ix];
                ULONG used;

                if (available >= TotalBandwidth)
                    continue;

                used = TotalBandwidth - available;
                if (used > ConsumedBandwidth)
                    ConsumedBandwidth = used;
            }

            Buffer1->ConsumedBandwidth = ConsumedBandwidth;
        }

        Buffer1->ControllerNameLength = FdoExtension->CommonExtension.SymbolicLinkName.Length;

        RtlCopyMemory(&Buffer1->ControllerNameUnicodeString,
                      FdoExtension->CommonExtension.SymbolicLinkName.Buffer,
                      FdoExtension->CommonExtension.SymbolicLinkName.Length);

        return STATUS_SUCCESS;
    }

    return STATUS_SUCCESS;
}

BOOLEAN
USB_BUSIFFN
USBDI_IsDeviceHighSpeed(IN PVOID BusContext)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;

    DPRINT("USBDI_IsDeviceHighSpeed: ...\n");

    PdoDevice = BusContext;
    PdoExtension = PdoDevice->DeviceExtension;
    FdoDevice = PdoExtension->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    return (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2) != 0;
}

NTSTATUS
USB_BUSIFFN
USBDI_EnumLogEntry(IN PVOID BusContext,
                   IN ULONG DriverTag,
                   IN ULONG EnumTag,
                   IN ULONG P1,
                   IN ULONG P2)
{
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;

    DPRINT("USBDI_EnumLogEntry: Tag=%08lx Enum=%08lx P1=%08lx P2=%08lx\n",
           DriverTag,
           EnumTag,
           P1,
           P2);

    PdoDevice = BusContext;
    if (!PdoDevice)
        return STATUS_INVALID_PARAMETER;

    PdoExtension = PdoDevice->DeviceExtension;
    if (!PdoExtension || !PdoExtension->FdoDevice)
        return STATUS_INVALID_DEVICE_REQUEST;

    FdoDevice = PdoExtension->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    if (Packet->UsbPortLogEntry)
    {
        Packet->UsbPortLogEntry(FdoExtension->MiniPortExt,
                                DriverTag,
                                EnumTag,
                                P1,
                                P2,
                                0);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
USBPORT_PdoQueryInterface(IN PDEVICE_OBJECT FdoDevice,
                          IN PDEVICE_OBJECT PdoDevice,
                          IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PUSB_BUS_INTERFACE_HUB_V5 InterfaceHub;
    PUSB_BUS_INTERFACE_USBDI_V2 InterfaceDI;
    UNICODE_STRING GuidBuffer;
    NTSTATUS Status;

    DPRINT("USBPORT_PdoQueryInterface: ...\n");

    if (IsEqualGUIDAligned(IoStack->Parameters.QueryInterface.InterfaceType,
                           &USB_BUS_INTERFACE_HUB_GUID))
    {
        /* Get request parameters */
        InterfaceHub = (PUSB_BUS_INTERFACE_HUB_V5)IoStack->Parameters.QueryInterface.Interface;
        InterfaceHub->Version = IoStack->Parameters.QueryInterface.Version;

        /* Check version */
        if (IoStack->Parameters.QueryInterface.Version >= 6)
        {
            DPRINT1("USB_BUS_INTERFACE_HUB_GUID version %x not supported!\n",
                    IoStack->Parameters.QueryInterface.Version);

            return Irp->IoStatus.Status; // Version not supported
        }

        /* Interface version 0 */
        InterfaceHub->Size = IoStack->Parameters.QueryInterface.Size;
        InterfaceHub->BusContext = PdoDevice;

        InterfaceHub->InterfaceReference = USBI_InterfaceReference;
        InterfaceHub->InterfaceDereference = USBI_InterfaceDereference;

        /* Interface version 1 */
        if (IoStack->Parameters.QueryInterface.Version >= 1)
        {
            InterfaceHub->CreateUsbDevice = USBHI_CreateUsbDevice;
            InterfaceHub->InitializeUsbDevice = USBHI_InitializeUsbDevice;
            InterfaceHub->GetUsbDescriptors = USBHI_GetUsbDescriptors;
            InterfaceHub->RemoveUsbDevice = USBHI_RemoveUsbDevice;
            InterfaceHub->RestoreUsbDevice = USBHI_RestoreUsbDevice;
            InterfaceHub->QueryDeviceInformation = USBHI_QueryDeviceInformation;
        }

        /* Interface version 2 */
        if (IoStack->Parameters.QueryInterface.Version >= 2)
        {
            InterfaceHub->GetControllerInformation = USBHI_GetControllerInformation;
            InterfaceHub->ControllerSelectiveSuspend = USBHI_ControllerSelectiveSuspend;
            InterfaceHub->GetExtendedHubInformation = USBHI_GetExtendedHubInformation;
            InterfaceHub->GetRootHubSymbolicName = USBHI_GetRootHubSymbolicName;
            InterfaceHub->GetDeviceBusContext = USBHI_GetDeviceBusContext;
            InterfaceHub->Initialize20Hub = USBHI_Initialize20Hub;
        }

        /* Interface version 3 */
        if (IoStack->Parameters.QueryInterface.Version >= 3)
            InterfaceHub->RootHubInitNotification = USBHI_RootHubInitNotification;

        /* Interface version 4 */
        if (IoStack->Parameters.QueryInterface.Version >= 4)
            InterfaceHub->FlushTransfers = USBHI_FlushTransfers;

        /* Interface version 5 */
        if (IoStack->Parameters.QueryInterface.Version >= 5)
            InterfaceHub->SetDeviceHandleData = USBHI_SetDeviceHandleData;

        /* Request completed */
        return STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(IoStack->Parameters.QueryInterface.InterfaceType,
                                &USB_BUS_INTERFACE_USBDI_GUID))
    {
        /* Get request parameters */
        InterfaceDI = (PUSB_BUS_INTERFACE_USBDI_V2)IoStack->Parameters.QueryInterface.Interface;
        InterfaceDI->Version = IoStack->Parameters.QueryInterface.Version;

        /* Check version */
        if (IoStack->Parameters.QueryInterface.Version >= 3)
        {
            DPRINT1("USB_BUS_INTERFACE_USBDI_GUID version %x not supported!\n",
                    IoStack->Parameters.QueryInterface.Version);

            return Irp->IoStatus.Status; // Version not supported
        }

        /* Interface version 0 */
        InterfaceDI->Size = IoStack->Parameters.QueryInterface.Size;
        InterfaceDI->BusContext = PdoDevice;
        InterfaceDI->InterfaceReference = USBI_InterfaceReference;
        InterfaceDI->InterfaceDereference = USBI_InterfaceDereference;
        InterfaceDI->GetUSBDIVersion = USBDI_GetUSBDIVersion;
        InterfaceDI->QueryBusTime = USBDI_QueryBusTime;
        InterfaceDI->SubmitIsoOutUrb = USBDI_SubmitIsoOutUrb;
        InterfaceDI->QueryBusInformation = USBDI_QueryBusInformation;

        /* Interface version 1 */
        if (IoStack->Parameters.QueryInterface.Version >= 1)
            InterfaceDI->IsDeviceHighSpeed = USBDI_IsDeviceHighSpeed;

        /* Interface version 2 */
        if (IoStack->Parameters.QueryInterface.Version >= 2)
            InterfaceDI->EnumLogEntry = USBDI_EnumLogEntry;

        return STATUS_SUCCESS;
    }
    else
    {
        /* Convert GUID to string */
        Status = RtlStringFromGUID(IoStack->Parameters.QueryInterface.InterfaceType,
                                   &GuidBuffer);

        if (NT_SUCCESS(Status))
        {
            /* Print interface */
            DPRINT1("HandleQueryInterface UNKNOWN INTERFACE GUID: %wZ Version %x\n",
                    &GuidBuffer,
                    IoStack->Parameters.QueryInterface.Version);

            RtlFreeUnicodeString(&GuidBuffer); // Free GUID buffer
        }
    }

    return Irp->IoStatus.Status;
}
