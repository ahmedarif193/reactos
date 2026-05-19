/*
 * PROJECT:     ReactOS Universal Audio Class Driver
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/usb/usbaudio/usbaudio.c
 * PURPOSE:     USB Audio Class 1.0 / 2.0 device driver
 * PROGRAMMERS:
 *              Johannes Anderwald (johannes.anderwald@reactos.org)
 */

#include "usbaudio.h"

static KSDEVICE_DISPATCH KsDeviceDispatch = {
    USBAudioAddDevice,
    USBAudioPnPStart,
    NULL,
    USBAudioPnPQueryStop,
    USBAudioPnPCancelStop,
    USBAudioPnPStop,
    USBAudioPnPQueryRemove,
    USBAudioPnPCancelRemove,
    USBAudioPnPRemove,
    USBAudioPnPQueryCapabilities,
    USBAudioPnPSurpriseRemoval,
    USBAudioPnPQueryPower,
    USBAudioPnPSetPower
};

static KSDEVICE_DESCRIPTOR KsDeviceDescriptor = {
    &KsDeviceDispatch,
    0,
    NULL,
    0x100,
    0
};

NTSTATUS
SubmitUrbSync(
    IN PDEVICE_OBJECT DeviceObject,
    IN PURB Urb)
{
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB,
        DeviceObject,
        NULL, 0,
        NULL, 0,
        TRUE,
        &Event,
        &IoStatus);

    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->Parameters.Others.Argument1 = Urb;

    Status = IoCallDriver(DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

/* ──────────────────────────────────────────────────────────────────
 *  Select USB configuration — claims audio control + MIDI interfaces
 * ────────────────────────────────────────────────────────────────── */

NTSTATUS
NTAPI
USBAudioSelectConfiguration(
    IN PKSDEVICE Device,
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor)
{
    PDEVICE_EXTENSION DeviceExtension;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    PUSBD_INTERFACE_LIST_ENTRY InterfaceList;
    PURB Urb;
    NTSTATUS Status;
    ULONG InterfaceDescriptorCount;

    InterfaceList = AllocFunction(sizeof(USBD_INTERFACE_LIST_ENTRY) *
                                  (ConfigurationDescriptor->bNumInterfaces + 1));
    if (!InterfaceList)
        return USBD_STATUS_INSUFFICIENT_RESOURCES;

    InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(
        ConfigurationDescriptor, ConfigurationDescriptor,
        -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);

    InterfaceDescriptorCount = 0;
    while (InterfaceDescriptor)
    {
        if ((InterfaceDescriptor->bInterfaceSubClass == 0x01 ||
             InterfaceDescriptor->bInterfaceSubClass == 0x02 ||
             InterfaceDescriptor->bInterfaceSubClass == 0x03) &&
            InterfaceDescriptor->bAlternateSetting == 0)
        {
            if (InterfaceDescriptorCount >= ConfigurationDescriptor->bNumInterfaces)
            {
                DPRINT1("USBAudio: too many audio interfaces in configuration\n");
                FreeFunction(InterfaceList);
                return STATUS_INVALID_DEVICE_REQUEST;
            }

            InterfaceList[InterfaceDescriptorCount++].InterfaceDescriptor =
                InterfaceDescriptor;

            if (InterfaceDescriptor->bInterfaceSubClass == 0x03)
            {
                DeviceExtension = Device->Context;
                DeviceExtension->HasMidiInterface = TRUE;
            }
        }
        InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(
            ConfigurationDescriptor,
            (PVOID)((ULONG_PTR)InterfaceDescriptor + InterfaceDescriptor->bLength),
            -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    }

    if (InterfaceDescriptorCount == 0)
    {
        FreeFunction(InterfaceList);
        return STATUS_NOT_SUPPORTED;
    }

    Urb = USBD_CreateConfigurationRequestEx(ConfigurationDescriptor, InterfaceList);
    if (!Urb)
    {
        FreeFunction(InterfaceList);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DeviceExtension = Device->Context;

    Status = SubmitUrbSync(DeviceExtension->LowerDevice, Urb);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(Urb);
        FreeFunction(InterfaceList);
        return Status;
    }

    DeviceExtension->ConfigurationHandle =
        Urb->UrbSelectConfiguration.ConfigurationHandle;

    DeviceExtension->InterfaceInfo = AllocFunction(
        Urb->UrbSelectConfiguration.Interface.Length);
    if (DeviceExtension->InterfaceInfo)
    {
        RtlCopyMemory(DeviceExtension->InterfaceInfo,
                      &Urb->UrbSelectConfiguration.Interface,
                      Urb->UrbSelectConfiguration.Interface.Length);
    }

    ExFreePool(Urb);
    FreeFunction(InterfaceList);
    return STATUS_SUCCESS;
}

/* ──────────────────────────────────────────────────────────────────
 *  Detect USB Audio version from control interface header
 *  For UAC3, also reads the BADD (Basic Audio Device Definition)
 *  profile from the IAD or CS_INTERFACE category field.
 * ────────────────────────────────────────────────────────────────── */

static ULONG
USBAudioDetectVersion(
    IN PDEVICE_EXTENSION DeviceExtension)
{
    PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR Header;
    PUSB_AUDIO3_CONTROL_INTERFACE_HEADER_DESCRIPTOR Header3;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDesc;
    USHORT bcdADC;

    DeviceExtension->BaddProfile = 0;

    InterfaceDesc = USBD_ParseConfigurationDescriptorEx(
        DeviceExtension->ConfigurationDescriptor,
        DeviceExtension->ConfigurationDescriptor,
        -1, -1, USB_DEVICE_CLASS_AUDIO, 0x01, -1);
    if (!InterfaceDesc)
        return USB_AUDIO_VERSION_1;

    Header = (PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR)
        USBD_ParseDescriptors(
            DeviceExtension->ConfigurationDescriptor,
            DeviceExtension->ConfigurationDescriptor->wTotalLength,
            InterfaceDesc,
            USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);

    if (!Header || Header->bLength < sizeof(*Header))
        return USB_AUDIO_VERSION_1;

    bcdADC = Header->bcdADC;

    if (bcdADC >= 0x0300)
    {
        Header3 = (PUSB_AUDIO3_CONTROL_INTERFACE_HEADER_DESCRIPTOR)Header;

        if (Header3->bCategory >= UAC3_FUNCTION_SUBCLASS_GENERIC_IO &&
            Header3->bCategory <= UAC3_FUNCTION_SUBCLASS_SPEAKERPHONE)
        {
            DeviceExtension->BaddProfile = Header3->bCategory;
        }

        return USB_AUDIO_VERSION_3;
    }

    if (bcdADC >= 0x0200)
        return USB_AUDIO_VERSION_2;

    return USB_AUDIO_VERSION_1;
}

/* ──────────────────────────────────────────────────────────────────
 *  Device quirk table
 *
 *  add entries for devices that need non-standard handling.
 *  format: { VID, PID, Flags, "description" }
 * ────────────────────────────────────────────────────────────────── */

static const USBAUDIO_DEVICE_QUIRK UsbAudioQuirkTable[] =
{
    /*
     * Common USB audio chipsets that report valid HID descriptors
     * but need relaxed error checking on control requests.
     */
    { 0x08BB, 0x2702, USBAUDIO_QUIRK_IGNORE_CTL_ERROR,
      "Texas Instruments PCM2702" },
    { 0x08BB, 0x2902, USBAUDIO_QUIRK_IGNORE_CTL_ERROR,
      "Texas Instruments PCM2902" },
    { 0x08BB, 0x2904, USBAUDIO_QUIRK_IGNORE_CTL_ERROR,
      "Texas Instruments PCM2904" },
    { 0x0D8C, 0x0102, USBAUDIO_QUIRK_IGNORE_CTL_ERROR,
      "C-Media CM106" },
    { 0x0D8C, 0x0103, USBAUDIO_QUIRK_IGNORE_CTL_ERROR,
      "C-Media CM108" },

    /*
     * Devices that lack a usable feedback endpoint (async mode)
     * and must use adaptive synchronization.
     */
    { 0x046D, 0x0A01, USBAUDIO_QUIRK_NO_FEEDBACK,
      "Logitech USB Headset" },
    { 0x046D, 0x0A02, USBAUDIO_QUIRK_NO_FEEDBACK,
      "Logitech USB Headset H340" },
    { 0x046D, 0x0A44, USBAUDIO_QUIRK_NO_FEEDBACK,
      "Logitech USB Headset H540" },

    /*
     * Devices with non-functional volume controls on the feature unit.
     * Mute-only or nothing — skip the volume node.
     */
    { 0x046D, 0x0990, USBAUDIO_QUIRK_NO_VOLUME,
      "Logitech QuickCam Pro 9000" },

    /* Terminator */
    { 0, 0, 0, NULL }
};

static PUSBAUDIO_DEVICE_QUIRK
USBAudioLookupQuirk(
    IN USHORT VendorId,
    IN USHORT ProductId)
{
    PUSBAUDIO_DEVICE_QUIRK Quirk = (PUSBAUDIO_DEVICE_QUIRK)UsbAudioQuirkTable;

    while (Quirk->VendorId)
    {
        if (Quirk->VendorId == VendorId && Quirk->ProductId == ProductId)
            return Quirk;
        Quirk++;
    }

    return NULL;
}

/* ──────────────────────────────────────────────────────────────────
 *  Scan for feedback endpoint
 * ────────────────────────────────────────────────────────────────── */

static VOID
USBAudioDetectFeedbackEndpoint(
    IN PDEVICE_EXTENSION DeviceExtension)
{
    PUSB_INTERFACE_DESCRIPTOR InterfaceDesc;
    PUSB_COMMON_DESCRIPTOR CommonDesc;
    PUSB_ENDPOINT_DESCRIPTOR EndpointDesc;
    PUCHAR DescriptorEnd;

    DeviceExtension->HasFeedbackEndpoint = FALSE;
    DeviceExtension->FeedbackPipeHandle = NULL;

    if (!DeviceExtension->ConfigurationDescriptor)
        return;

    DescriptorEnd = (PUCHAR)DeviceExtension->ConfigurationDescriptor +
                    DeviceExtension->ConfigurationDescriptor->wTotalLength;

    /* Scan all audio streaming interfaces for a feedback endpoint */
    InterfaceDesc = USBD_ParseConfigurationDescriptorEx(
        DeviceExtension->ConfigurationDescriptor,
        DeviceExtension->ConfigurationDescriptor,
        -1, -1, USB_DEVICE_CLASS_AUDIO, 2, -1);

    while (InterfaceDesc)
    {
        CommonDesc = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)InterfaceDesc +
                                              InterfaceDesc->bLength);

        while ((PUCHAR)CommonDesc + sizeof(USB_COMMON_DESCRIPTOR) <= DescriptorEnd &&
               CommonDesc->bLength != 0 &&
               (PUCHAR)CommonDesc + CommonDesc->bLength <= DescriptorEnd)
        {
            if (CommonDesc->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE)
                break;

            if (CommonDesc->bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE)
            {
                EndpointDesc = (PUSB_ENDPOINT_DESCRIPTOR)CommonDesc;
                if ((EndpointDesc->bmAttributes & USB_ENDPOINT_TYPE_MASK) ==
                        USB_ENDPOINT_TYPE_ISOCHRONOUS &&
                    USB_ENDPOINT_DIRECTION_IN(EndpointDesc->bEndpointAddress) &&
                    (EndpointDesc->bmAttributes & 0x30) == 0x10)
                {
                    DeviceExtension->HasFeedbackEndpoint = TRUE;
                    return;
                }
            }

            CommonDesc = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)CommonDesc +
                                                  CommonDesc->bLength);
        }

        InterfaceDesc = USBD_ParseConfigurationDescriptorEx(
            DeviceExtension->ConfigurationDescriptor,
            (PVOID)((ULONG_PTR)InterfaceDesc + InterfaceDesc->bLength),
            -1, -1, USB_DEVICE_CLASS_AUDIO, 2, -1);
    }
}

/* ──────────────────────────────────────────────────────────────────
 *  Start device — probe USB descriptors, detect UAC version
 * ────────────────────────────────────────────────────────────────── */

NTSTATUS
NTAPI
USBAudioStartDevice(
    IN PKSDEVICE Device)
{
    PURB Urb;
    PUSB_DEVICE_DESCRIPTOR DeviceDescriptor;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;
    ULONG Length;

    DeviceExtension = Device->Context;

    Urb = AllocFunction(sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST));
    if (!Urb)
        return STATUS_INSUFFICIENT_RESOURCES;

    DeviceDescriptor = AllocFunction(sizeof(USB_DEVICE_DESCRIPTOR));
    if (!DeviceDescriptor)
    {
        FreeFunction(Urb);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UsbBuildGetDescriptorRequest(Urb,
        sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
        USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
        DeviceDescriptor, NULL,
        sizeof(USB_DEVICE_DESCRIPTOR), NULL);

    Status = SubmitUrbSync(DeviceExtension->LowerDevice, Urb);
    if (!NT_SUCCESS(Status))
    {
        FreeFunction(Urb);
        FreeFunction(DeviceDescriptor);
        return Status;
    }

    ConfigurationDescriptor = AllocFunction(sizeof(USB_CONFIGURATION_DESCRIPTOR));
    if (!ConfigurationDescriptor)
    {
        FreeFunction(Urb);
        FreeFunction(DeviceDescriptor);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UsbBuildGetDescriptorRequest(Urb,
        sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
        USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0,
        ConfigurationDescriptor, NULL,
        sizeof(USB_CONFIGURATION_DESCRIPTOR), NULL);

    Status = SubmitUrbSync(DeviceExtension->LowerDevice, Urb);
    if (!NT_SUCCESS(Status))
    {
        FreeFunction(Urb);
        FreeFunction(DeviceDescriptor);
        FreeFunction(ConfigurationDescriptor);
        return Status;
    }

    Length = ConfigurationDescriptor->wTotalLength;
    FreeFunction(ConfigurationDescriptor);

    ConfigurationDescriptor = AllocFunction(Length);
    if (!ConfigurationDescriptor)
    {
        FreeFunction(Urb);
        FreeFunction(DeviceDescriptor);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UsbBuildGetDescriptorRequest(Urb,
        sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
        USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0,
        ConfigurationDescriptor, NULL, Length, NULL);

    Status = SubmitUrbSync(DeviceExtension->LowerDevice, Urb);
    FreeFunction(Urb);

    if (!NT_SUCCESS(Status))
    {
        FreeFunction(DeviceDescriptor);
        FreeFunction(ConfigurationDescriptor);
        return Status;
    }

    KsAddItemToObjectBag(Device->Bag, DeviceDescriptor, ExFreePool);
    KsAddItemToObjectBag(Device->Bag, ConfigurationDescriptor, ExFreePool);

    DeviceExtension->DeviceDescriptor = DeviceDescriptor;
    DeviceExtension->ConfigurationDescriptor = ConfigurationDescriptor;
    DeviceExtension->AudioVersion = USBAudioDetectVersion(DeviceExtension);
    DeviceExtension->QuirkFlags = 0;
    USBAudioDetectFeedbackEndpoint(DeviceExtension);

    /* Look up any device-specific quirks */
    {
        PUSBAUDIO_DEVICE_QUIRK Quirk = USBAudioLookupQuirk(
            DeviceDescriptor->idVendor,
            DeviceDescriptor->idProduct);
        if (Quirk)
        {
            DeviceExtension->QuirkFlags = Quirk->Flags;
        }
    }

    Status = USBAudioSelectConfiguration(Device, ConfigurationDescriptor);

    return Status;
}

/* ──────────────────────────────────────────────────────────────────
 *  PnP dispatch
 * ────────────────────────────────────────────────────────────────── */

NTSTATUS
NTAPI
USBAudioAddDevice(
    _In_ PKSDEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
USBAudioPnPStart(
    _In_     PKSDEVICE         Device,
    _In_     PIRP              Irp,
    _In_opt_ PCM_RESOURCE_LIST TranslatedResourceList,
    _In_opt_ PCM_RESOURCE_LIST UntranslatedResourceList)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PDEVICE_EXTENSION DeviceExtension;

    UNREFERENCED_PARAMETER(TranslatedResourceList);
    UNREFERENCED_PARAMETER(UntranslatedResourceList);

    if (!Device->Started)
    {
        DeviceExtension = AllocFunction(sizeof(DEVICE_EXTENSION));
        if (!DeviceExtension)
            return STATUS_INSUFFICIENT_RESOURCES;

        Device->Context = DeviceExtension;
        DeviceExtension->LowerDevice = Device->NextDeviceObject;

        KsAddItemToObjectBag(Device->Bag, Device->Context, ExFreePool);

        Status = USBAudioStartDevice(Device);
        if (NT_SUCCESS(Status))
        {
            Status = USBAudioCreateFilterContext(Device);
        }
    }

    return Status;
}

NTSTATUS
NTAPI
USBAudioPnPQueryStop(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);
    return STATUS_SUCCESS;
}

VOID
NTAPI
USBAudioPnPCancelStop(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);
}

VOID
NTAPI
USBAudioPnPStop(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp)
{
    PDEVICE_EXTENSION DeviceExtension = Device->Context;

    UNREFERENCED_PARAMETER(Irp);

    if (DeviceExtension && DeviceExtension->InterfaceInfo)
    {
        FreeFunction(DeviceExtension->InterfaceInfo);
        DeviceExtension->InterfaceInfo = NULL;
    }
}

NTSTATUS
NTAPI
USBAudioPnPQueryRemove(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);
    return STATUS_SUCCESS;
}

VOID
NTAPI
USBAudioPnPCancelRemove(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);
}

VOID
NTAPI
USBAudioPnPRemove(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp)
{
    PDEVICE_EXTENSION DeviceExtension = Device->Context;

    UNREFERENCED_PARAMETER(Irp);

    if (DeviceExtension && DeviceExtension->InterfaceInfo)
    {
        FreeFunction(DeviceExtension->InterfaceInfo);
        DeviceExtension->InterfaceInfo = NULL;
    }
}

NTSTATUS
NTAPI
USBAudioPnPQueryCapabilities(
    _In_    PKSDEVICE            Device,
    _In_    PIRP                 Irp,
    _Inout_ PDEVICE_CAPABILITIES Capabilities)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);

    if (!Capabilities)
        return STATUS_INVALID_PARAMETER;

    Capabilities->SilentInstall = TRUE;
    Capabilities->UniqueID = TRUE;
    Capabilities->SurpriseRemovalOK = TRUE;

    return STATUS_SUCCESS;
}

VOID
NTAPI
USBAudioPnPSurpriseRemoval(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp)
{
    PDEVICE_EXTENSION DeviceExtension = Device->Context;

    UNREFERENCED_PARAMETER(Irp);

    if (DeviceExtension && DeviceExtension->InterfaceInfo)
    {
        FreeFunction(DeviceExtension->InterfaceInfo);
        DeviceExtension->InterfaceInfo = NULL;
    }
}

NTSTATUS
NTAPI
USBAudioPnPQueryPower(
    _In_ PKSDEVICE          Device,
    _In_ PIRP               Irp,
    _In_ DEVICE_POWER_STATE DeviceTo,
    _In_ DEVICE_POWER_STATE DeviceFrom,
    _In_ SYSTEM_POWER_STATE SystemTo,
    _In_ SYSTEM_POWER_STATE SystemFrom,
    _In_ POWER_ACTION       Action)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(DeviceTo);
    UNREFERENCED_PARAMETER(DeviceFrom);
    UNREFERENCED_PARAMETER(SystemTo);
    UNREFERENCED_PARAMETER(SystemFrom);
    UNREFERENCED_PARAMETER(Action);

    return STATUS_SUCCESS;
}

VOID
NTAPI
USBAudioPnPSetPower(
    _In_ PKSDEVICE          Device,
    _In_ PIRP               Irp,
    _In_ DEVICE_POWER_STATE To,
    _In_ DEVICE_POWER_STATE From)
{
    PDEVICE_EXTENSION DeviceExtension = Device->Context;

    UNREFERENCED_PARAMETER(Irp);

    if (To == PowerDeviceD3 && From == PowerDeviceD0)
    {
        if (DeviceExtension && DeviceExtension->InterfaceInfo)
        {
            FreeFunction(DeviceExtension->InterfaceInfo);
            DeviceExtension->InterfaceInfo = NULL;
        }
    }
}

/* ──────────────────────────────────────────────────────────────────
 *  DriverEntry
 * ────────────────────────────────────────────────────────────────── */

NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;

    Status = KsInitializeDriver(DriverObject, RegistryPath, &KsDeviceDescriptor);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBAudio: KsInitializeDriver failed %x\n", Status);
        return Status;
    }

    return Status;
}
