/*
 * PROJECT:     ReactOS USB Video Class Stub Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Minimal function driver for USB video class devices
 */

#include <ntddk.h>
#include <wdm.h>
#include <usb.h>
#include <usbioctl.h>
#include <usbdlib.h>

#define NDEBUG
#include <debug.h>

#define USBVIDEO_TAG 'vdvu'

#define USB_VIDEO_SUBCLASS_CONTROL           0x01
#define USB_VIDEO_SUBCLASS_STREAMING         0x02

#define USB_VIDEO_CS_INTERFACE               0x24

/* VideoControl descriptor subtypes (partial, UVC 1.x) */
#define USB_VIDEO_VC_INPUT_TERMINAL          0x02
#define USB_VIDEO_VC_OUTPUT_TERMINAL         0x03
#define USB_VIDEO_VC_SELECTOR_UNIT           0x04
#define USB_VIDEO_VC_PROCESSING_UNIT         0x05
#define USB_VIDEO_VC_EXTENSION_UNIT          0x06

/* VideoStreaming descriptor subtypes we care about (UVC 1.x) */
#define USB_VIDEO_VS_FORMAT_UNCOMPRESSED     0x04
#define USB_VIDEO_VS_FRAME_UNCOMPRESSED      0x05
#define USB_VIDEO_VS_FORMAT_MJPEG            0x06
#define USB_VIDEO_VS_FRAME_MJPEG             0x07

/* UVC VideoControl request codes */
#define USB_VIDEO_REQ_SET_CUR                0x01
#define USB_VIDEO_REQ_GET_CUR                0x81
#define USB_VIDEO_REQ_GET_MIN                0x82
#define USB_VIDEO_REQ_GET_MAX                0x83
#define USB_VIDEO_REQ_GET_RES                0x84
#define USB_VIDEO_REQ_GET_LEN                0x85
#define USB_VIDEO_REQ_GET_INFO               0x86
#define USB_VIDEO_REQ_GET_DEF                0x87

/* UVC Processing Unit control selectors (partial) */
#define USB_VIDEO_PU_BRIGHTNESS_CONTROL              0x02
#define USB_VIDEO_PU_CONTRAST_CONTROL                0x03
#define USB_VIDEO_PU_HUE_CONTROL                     0x04
#define USB_VIDEO_PU_SATURATION_CONTROL              0x05
#define USB_VIDEO_PU_SHARPNESS_CONTROL               0x06
#define USB_VIDEO_PU_GAMMA_CONTROL                   0x07
#define USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL 0x08
#define USB_VIDEO_PU_GAIN_CONTROL                    0x0B

typedef struct _USB_VIDEO_CS_INTERFACE_DESCRIPTOR
{
    UCHAR bLength;
    UCHAR bDescriptorType;
    UCHAR bDescriptorSubType;
    UCHAR bData[1];
} USB_VIDEO_CS_INTERFACE_DESCRIPTOR, *PUSB_VIDEO_CS_INTERFACE_DESCRIPTOR;

typedef enum _USBVIDEO_VC_NODE_TYPE
{
    UsbVideoVcNodeInputTerminal = 1,
    UsbVideoVcNodeOutputTerminal,
    UsbVideoVcNodeSelectorUnit,
    UsbVideoVcNodeProcessingUnit,
    UsbVideoVcNodeExtensionUnit
} USBVIDEO_VC_NODE_TYPE;

typedef struct _USBVIDEO_VC_NODE_INFO
{
    UCHAR Id;
    UCHAR NodeType;
    UCHAR SourceId;
    UCHAR NumInputs;
    USHORT TerminalType;
    USHORT Reserved;
    PUSB_VIDEO_CS_INTERFACE_DESCRIPTOR CsDescriptor;
} USBVIDEO_VC_NODE_INFO, *PUSBVIDEO_VC_NODE_INFO;

typedef enum _USBVIDEO_PROPERTY_TYPE
{
    UsbVideoPropertyBrightness = 1,
    UsbVideoPropertyContrast,
    UsbVideoPropertyHue,
    UsbVideoPropertySaturation,
    UsbVideoPropertySharpness,
    UsbVideoPropertyGamma,
    UsbVideoPropertyWhiteBalanceTemperature,
    UsbVideoPropertyGain
} USBVIDEO_PROPERTY_TYPE;

typedef struct _USBVIDEO_CONTROL_PROPERTY
{
    UCHAR NodeId;
    UCHAR ControlIndex;
    UCHAR PropertyType;
    UCHAR Reserved;
} USBVIDEO_CONTROL_PROPERTY, *PUSBVIDEO_CONTROL_PROPERTY;

typedef struct _USBVIDEO_STREAM_FRAME_INFO
{
    UCHAR FrameIndex;
    USHORT Width;
    USHORT Height;
    ULONG DefaultInterval;
} USBVIDEO_STREAM_FRAME_INFO, *PUSBVIDEO_STREAM_FRAME_INFO;

typedef struct _USBVIDEO_STREAM_FORMAT_INFO
{
    UCHAR FormatIndex;
    UCHAR DescriptorSubtype;
    UCHAR NumFrameDescriptors;
    UCHAR Reserved;
    GUID FormatGuid;
    ULONG FrameCount;
    PUSBVIDEO_STREAM_FRAME_INFO Frames;
} USBVIDEO_STREAM_FORMAT_INFO, *PUSBVIDEO_STREAM_FORMAT_INFO;

typedef struct _USBVIDEO_CONTROL_INTERFACE_INFO
{
    UCHAR InterfaceNumber;
    UCHAR AlternateSetting;
    UCHAR NumEndpoints;
    UCHAR Reserved;
    PUSB_VIDEO_CS_INTERFACE_DESCRIPTOR HeaderCsDescriptor;
} USBVIDEO_CONTROL_INTERFACE_INFO, *PUSBVIDEO_CONTROL_INTERFACE_INFO;

typedef struct _USBVIDEO_STREAM_INTERFACE_INFO
{
    UCHAR InterfaceNumber;
    UCHAR AlternateSetting;
    UCHAR EndpointAddress;
    UCHAR EndpointAttributes;
    USHORT MaxPacketSize;
    UCHAR Interval;
    UCHAR Reserved;
    PUSB_VIDEO_CS_INTERFACE_DESCRIPTOR InputHeaderCsDescriptor;
    ULONG FormatCount;
    PUSBVIDEO_STREAM_FORMAT_INFO Formats;
} USBVIDEO_STREAM_INTERFACE_INFO, *PUSBVIDEO_STREAM_INTERFACE_INFO;

typedef struct _USBVIDEO_TOPOLOGY
{
    ULONG ControlInterfaceCount;
    PUSBVIDEO_CONTROL_INTERFACE_INFO ControlInterfaces;
    ULONG StreamInterfaceCount;
    PUSBVIDEO_STREAM_INTERFACE_INFO StreamInterfaces;
    ULONG VcNodeCount;
    PUSBVIDEO_VC_NODE_INFO VcNodes;
    ULONG PropertyCount;
    PUSBVIDEO_CONTROL_PROPERTY Properties;
} USBVIDEO_TOPOLOGY, *PUSBVIDEO_TOPOLOGY;

typedef struct _USBVIDEO_DEVICE_EXTENSION
{
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigDescriptor;
    ULONG ConfigDescriptorLength;
    USBVIDEO_TOPOLOGY Topology;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;
    PUSBD_INTERFACE_INFORMATION StreamInterface;
    UCHAR StreamPipeIndex;
} USBVIDEO_DEVICE_EXTENSION, *PUSBVIDEO_DEVICE_EXTENSION;

static
VOID
UsbVideo_FreeTopology(IN PUSBVIDEO_DEVICE_EXTENSION Dx)
{
    ULONG i, j;

    /* Free per-stream format/frame tables first */
    if (Dx->Topology.StreamInterfaces)
    {
        for (i = 0; i < Dx->Topology.StreamInterfaceCount; i++)
        {
            PUSBVIDEO_STREAM_INTERFACE_INFO Si =
                &Dx->Topology.StreamInterfaces[i];

            if (Si->Formats)
            {
                for (j = 0; j < Si->FormatCount; j++)
                {
                    PUSBVIDEO_STREAM_FORMAT_INFO Fmt = &Si->Formats[j];

                    if (Fmt->Frames)
                    {
                        ExFreePoolWithTag(Fmt->Frames, USBVIDEO_TAG);
                        Fmt->Frames = NULL;
                        Fmt->FrameCount = 0;
                    }
                }

                ExFreePoolWithTag(Si->Formats, USBVIDEO_TAG);
                Si->Formats = NULL;
                Si->FormatCount = 0;
            }
        }
    }

    if (Dx->Topology.ControlInterfaces)
    {
        ExFreePoolWithTag(Dx->Topology.ControlInterfaces, USBVIDEO_TAG);
        Dx->Topology.ControlInterfaces = NULL;
        Dx->Topology.ControlInterfaceCount = 0;
    }

    if (Dx->Topology.StreamInterfaces)
    {
        ExFreePoolWithTag(Dx->Topology.StreamInterfaces, USBVIDEO_TAG);
        Dx->Topology.StreamInterfaces = NULL;
        Dx->Topology.StreamInterfaceCount = 0;
    }

    if (Dx->Topology.VcNodes)
    {
        ExFreePoolWithTag(Dx->Topology.VcNodes, USBVIDEO_TAG);
        Dx->Topology.VcNodes = NULL;
        Dx->Topology.VcNodeCount = 0;
    }

    if (Dx->Topology.Properties)
    {
        ExFreePoolWithTag(Dx->Topology.Properties, USBVIDEO_TAG);
        Dx->Topology.Properties = NULL;
        Dx->Topology.PropertyCount = 0;
    }

    if (Dx->StreamInterface)
    {
        ExFreePoolWithTag(Dx->StreamInterface, USBVIDEO_TAG);
        Dx->StreamInterface = NULL;
    }
}

static
NTSTATUS
NTAPI
UsbVideo_SyncSubmitUrb(IN PUSBVIDEO_DEVICE_EXTENSION Dx,
                       IN PURB Urb)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB,
                                        Dx->LowerDevice,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->Parameters.Others.Argument1 = Urb;

    Status = IoCallDriver(Dx->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

static
NTSTATUS
UsbVideo_GetConfigurationDescriptor(IN PUSBVIDEO_DEVICE_EXTENSION Dx);

static
VOID
UsbVideo_SelectStreamingConfiguration(IN PUSBVIDEO_DEVICE_EXTENSION Dx)
{
    PUSB_CONFIGURATION_DESCRIPTOR Config;
    USBD_INTERFACE_LIST_ENTRY InterfaceList[2];
    PUSB_INTERFACE_DESCRIPTOR VsInterfaceDesc;
    PURB Urb;
    NTSTATUS Status;
    PUSBD_INTERFACE_INFORMATION IfInfo;

    Config = Dx->ConfigDescriptor;
    if (!Config || Dx->Topology.StreamInterfaceCount == 0)
        return;

    if (Dx->StreamInterface != NULL)
        return;

    VsInterfaceDesc = USBD_ParseConfigurationDescriptorEx(
                            Config,
                            Config,
                            Dx->Topology.StreamInterfaces[0].InterfaceNumber,
                            Dx->Topology.StreamInterfaces[0].AlternateSetting,
                            USB_DEVICE_CLASS_VIDEO,
                            USB_VIDEO_SUBCLASS_STREAMING,
                            -1);
    if (!VsInterfaceDesc)
        return;

    InterfaceList[0].InterfaceDescriptor = VsInterfaceDesc;
    InterfaceList[0].Interface = NULL;
    InterfaceList[1].InterfaceDescriptor = NULL;
    InterfaceList[1].Interface = NULL;

    Urb = USBD_CreateConfigurationRequestEx(Config, InterfaceList);
    if (!Urb)
        return;

    Status = UsbVideo_SyncSubmitUrb(Dx, Urb);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(Urb);
        return;
    }

    Dx->ConfigurationHandle = Urb->UrbSelectConfiguration.ConfigurationHandle;

    IfInfo = &Urb->UrbSelectConfiguration.Interface;

    Dx->StreamInterface = ExAllocatePoolWithTag(NonPagedPool,
                                                IfInfo->Length,
                                                USBVIDEO_TAG);
    if (Dx->StreamInterface)
    {
        ULONG i;

        RtlCopyMemory(Dx->StreamInterface,
                      IfInfo,
                      IfInfo->Length);

        Dx->StreamPipeIndex = 0xFF;

        for (i = 0; i < Dx->StreamInterface->NumberOfPipes; i++)
        {
            PUSBD_PIPE_INFORMATION Pipe = &Dx->StreamInterface->Pipes[i];

            if (!(Pipe->EndpointAddress & 0x80))
                continue;

            if (Pipe->PipeType == UsbdPipeTypeIsochronous)
            {
                Dx->StreamPipeIndex = (UCHAR)i;
                break;
            }

            if (Pipe->PipeType == UsbdPipeTypeBulk &&
                Dx->StreamPipeIndex == 0xFF)
            {
                Dx->StreamPipeIndex = (UCHAR)i;
            }
        }
    }

    ExFreePool(Urb);
}

static
UCHAR
UsbVideo_GetPuControlSelector(IN UCHAR PropertyType)
{
    switch (PropertyType)
    {
        case UsbVideoPropertyBrightness:
            return USB_VIDEO_PU_BRIGHTNESS_CONTROL;
        case UsbVideoPropertyContrast:
            return USB_VIDEO_PU_CONTRAST_CONTROL;
        case UsbVideoPropertyHue:
            return USB_VIDEO_PU_HUE_CONTROL;
        case UsbVideoPropertySaturation:
            return USB_VIDEO_PU_SATURATION_CONTROL;
        case UsbVideoPropertySharpness:
            return USB_VIDEO_PU_SHARPNESS_CONTROL;
        case UsbVideoPropertyGamma:
            return USB_VIDEO_PU_GAMMA_CONTROL;
        case UsbVideoPropertyWhiteBalanceTemperature:
            return USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL;
        case UsbVideoPropertyGain:
            return USB_VIDEO_PU_GAIN_CONTROL;
        default:
            break;
    }

    return 0;
}

static
NTSTATUS
UsbVideo_VcGetOrSetProperty(IN PUSBVIDEO_DEVICE_EXTENSION Dx,
                            IN PUSBVIDEO_CONTROL_PROPERTY Property,
                            IN UCHAR Request,
                            IN OUT PVOID Buffer,
                            IN ULONG BufferLength,
                            IN BOOLEAN Get)
{
    struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *Urb;
    USHORT Value;
    USHORT Index;
    UCHAR InterfaceNumber;
    UCHAR ControlSelector;
    ULONG TransferFlags;
    NTSTATUS Status;

    if (Dx->Topology.ControlInterfaceCount == 0)
        return STATUS_INVALID_DEVICE_REQUEST;

    ControlSelector = UsbVideo_GetPuControlSelector(Property->PropertyType);
    if (ControlSelector == 0)
        return STATUS_NOT_SUPPORTED;

    InterfaceNumber = Dx->Topology.ControlInterfaces[0].InterfaceNumber;

    Value = (ControlSelector << 8);
    Index = (Property->NodeId << 8) | InterfaceNumber;

    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(*Urb),
                                USBVIDEO_TAG);
    if (!Urb)
        return STATUS_INSUFFICIENT_RESOURCES;

    TransferFlags = Get ? (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK)
                        : USBD_TRANSFER_DIRECTION_OUT;

    UsbBuildVendorRequest((PURB)Urb,
                          URB_FUNCTION_CLASS_INTERFACE,
                          sizeof(*Urb),
                          TransferFlags,
                          0,
                          Request,
                          Value,
                          Index,
                          Buffer,
                          NULL,
                          BufferLength,
                          NULL);

    Status = UsbVideo_SyncSubmitUrb(Dx, (PURB)Urb);

    ExFreePoolWithTag(Urb, USBVIDEO_TAG);
    return Status;
}

static
PUSBVIDEO_CONTROL_PROPERTY
UsbVideo_AppendProperty(IN PUSBVIDEO_DEVICE_EXTENSION Dx)
{
    PUSBVIDEO_CONTROL_PROPERTY NewProps;
    ULONG NewCount;

    NewCount = Dx->Topology.PropertyCount + 1;

    NewProps = ExAllocatePoolWithTag(NonPagedPool,
                                     NewCount * sizeof(USBVIDEO_CONTROL_PROPERTY),
                                     USBVIDEO_TAG);
    if (!NewProps)
        return NULL;

    if (Dx->Topology.Properties)
    {
        RtlCopyMemory(NewProps,
                      Dx->Topology.Properties,
                      Dx->Topology.PropertyCount * sizeof(USBVIDEO_CONTROL_PROPERTY));
        ExFreePoolWithTag(Dx->Topology.Properties, USBVIDEO_TAG);
    }

    RtlZeroMemory(&NewProps[NewCount - 1], sizeof(USBVIDEO_CONTROL_PROPERTY));

    Dx->Topology.Properties = NewProps;
    Dx->Topology.PropertyCount = NewCount;

    return &NewProps[NewCount - 1];
}

static
PUSBVIDEO_VC_NODE_INFO
UsbVideo_AppendVcNode(IN PUSBVIDEO_DEVICE_EXTENSION Dx)
{
    PUSBVIDEO_VC_NODE_INFO NewNodes;
    ULONG NewCount;

    NewCount = Dx->Topology.VcNodeCount + 1;

    NewNodes = ExAllocatePoolWithTag(NonPagedPool,
                                     NewCount * sizeof(USBVIDEO_VC_NODE_INFO),
                                     USBVIDEO_TAG);
    if (!NewNodes)
        return NULL;

    if (Dx->Topology.VcNodes)
    {
        RtlCopyMemory(NewNodes,
                      Dx->Topology.VcNodes,
                      Dx->Topology.VcNodeCount * sizeof(USBVIDEO_VC_NODE_INFO));
        ExFreePoolWithTag(Dx->Topology.VcNodes, USBVIDEO_TAG);
    }

    RtlZeroMemory(&NewNodes[NewCount - 1], sizeof(USBVIDEO_VC_NODE_INFO));

    Dx->Topology.VcNodes = NewNodes;
    Dx->Topology.VcNodeCount = NewCount;

    return &NewNodes[NewCount - 1];
}

static
NTSTATUS
UsbVideo_GetConfigurationDescriptor(IN PUSBVIDEO_DEVICE_EXTENSION Dx)
{
    struct _URB_CONTROL_DESCRIPTOR_REQUEST *Urb;
    PUSB_CONFIGURATION_DESCRIPTOR Config;
    ULONG Size;
    NTSTATUS Status;

    if (Dx->ConfigDescriptor)
    {
        return STATUS_SUCCESS;
    }

    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(*Urb),
                                USBVIDEO_TAG);
    if (!Urb)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Urb, sizeof(*Urb));

    Size = sizeof(USB_CONFIGURATION_DESCRIPTOR);
    Config = ExAllocatePoolWithTag(NonPagedPool,
                                   Size,
                                   USBVIDEO_TAG);
    if (!Config)
    {
        ExFreePoolWithTag(Urb, USBVIDEO_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Config, Size);

    Urb->Hdr.Length = sizeof(*Urb);
    Urb->Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    Urb->TransferBufferLength = Size;
    Urb->TransferBuffer = Config;
    Urb->TransferBufferMDL = NULL;
    Urb->Index = 0;
    Urb->DescriptorType = USB_CONFIGURATION_DESCRIPTOR_TYPE;
    Urb->LanguageId = 0;

    Status = UsbVideo_SyncSubmitUrb(Dx, (PURB)Urb);
    if (!NT_SUCCESS(Status))
        goto Fail;

    if (Config->wTotalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR))
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto Fail;
    }

    Size = Config->wTotalLength;
    ExFreePoolWithTag(Config, USBVIDEO_TAG);
    Config = ExAllocatePoolWithTag(NonPagedPool,
                                   Size,
                                   USBVIDEO_TAG);
    if (!Config)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail2;
    }

    RtlZeroMemory(Config, Size);
    RtlZeroMemory(Urb, sizeof(*Urb));

    Urb->Hdr.Length = sizeof(*Urb);
    Urb->Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    Urb->TransferBufferLength = Size;
    Urb->TransferBuffer = Config;
    Urb->TransferBufferMDL = NULL;
    Urb->Index = 0;
    Urb->DescriptorType = USB_CONFIGURATION_DESCRIPTOR_TYPE;
    Urb->LanguageId = 0;

    Status = UsbVideo_SyncSubmitUrb(Dx, (PURB)Urb);
    if (!NT_SUCCESS(Status))
        goto Fail2;

    Dx->ConfigDescriptor = Config;
    Dx->ConfigDescriptorLength = Size;

    ExFreePoolWithTag(Urb, USBVIDEO_TAG);
    return STATUS_SUCCESS;

Fail2:
    ExFreePoolWithTag(Config, USBVIDEO_TAG);
Fail:
    ExFreePoolWithTag(Urb, USBVIDEO_TAG);
    return Status;
}

static
VOID
UsbVideo_ParseUvcInterfaces(IN PUSBVIDEO_DEVICE_EXTENSION Dx)
{
    PUSB_CONFIGURATION_DESCRIPTOR Config;
    PUSB_INTERFACE_DESCRIPTOR IfDesc;
    PVOID StartPosition;
    ULONG ControlCount = 0;
    ULONG StreamCount = 0;
    ULONG ControlIndex = 0;
    ULONG StreamIndex = 0;

    Config = Dx->ConfigDescriptor;
    if (!Config)
        return;

    /* Drop any previous topology before rebuilding it */
    UsbVideo_FreeTopology(Dx);

    /* First pass: count control and streaming interfaces */
    StartPosition = Config;

    for (;;)
    {
        IfDesc = USBD_ParseConfigurationDescriptorEx(Config,
                                                     StartPosition,
                                                     -1,
                                                     -1,
                                                     USB_DEVICE_CLASS_VIDEO,
                                                     -1,
                                                     -1);
        if (!IfDesc)
            break;

        if (IfDesc->bInterfaceClass == USB_DEVICE_CLASS_VIDEO &&
            (IfDesc->bInterfaceSubClass == USB_VIDEO_SUBCLASS_CONTROL ||
             IfDesc->bInterfaceSubClass == USB_VIDEO_SUBCLASS_STREAMING))
        {
            if (IfDesc->bInterfaceSubClass == USB_VIDEO_SUBCLASS_CONTROL)
                ControlCount++;
            else
                StreamCount++;
        }

        StartPosition = (PUCHAR)IfDesc + IfDesc->bLength;
    }

    if (ControlCount != 0)
    {
        Dx->Topology.ControlInterfaces =
            ExAllocatePoolWithTag(NonPagedPool,
                                  ControlCount * sizeof(USBVIDEO_CONTROL_INTERFACE_INFO),
                                  USBVIDEO_TAG);
        if (!Dx->Topology.ControlInterfaces)
        {
            DPRINT1("usbvideo: failed to allocate %lu control interfaces\n", ControlCount);
            ControlCount = 0;
        }
        else
        {
            RtlZeroMemory(Dx->Topology.ControlInterfaces,
                          ControlCount * sizeof(USBVIDEO_CONTROL_INTERFACE_INFO));
        }
    }

    if (StreamCount != 0)
    {
        Dx->Topology.StreamInterfaces =
            ExAllocatePoolWithTag(NonPagedPool,
                                  StreamCount * sizeof(USBVIDEO_STREAM_INTERFACE_INFO),
                                  USBVIDEO_TAG);
        if (!Dx->Topology.StreamInterfaces)
        {
            DPRINT1("usbvideo: failed to allocate %lu stream interfaces\n", StreamCount);
            StreamCount = 0;
        }
        else
        {
            RtlZeroMemory(Dx->Topology.StreamInterfaces,
                          StreamCount * sizeof(USBVIDEO_STREAM_INTERFACE_INFO));
        }
    }

    Dx->Topology.ControlInterfaceCount = ControlCount;
    Dx->Topology.StreamInterfaceCount = StreamCount;

    if (ControlCount == 0 && StreamCount == 0)
        return;

    /* Second pass: fill topology entries */
    StartPosition = Config;

    for (;;)
    {
        PUCHAR Ptr;
        PUCHAR End;

        IfDesc = USBD_ParseConfigurationDescriptorEx(Config,
                                                     StartPosition,
                                                     -1,
                                                     -1,
                                                     USB_DEVICE_CLASS_VIDEO,
                                                     -1,
                                                     -1);
        if (!IfDesc)
            break;

        if (IfDesc->bInterfaceClass != USB_DEVICE_CLASS_VIDEO ||
            (IfDesc->bInterfaceSubClass != USB_VIDEO_SUBCLASS_CONTROL &&
             IfDesc->bInterfaceSubClass != USB_VIDEO_SUBCLASS_STREAMING))
        {
            StartPosition = (PUCHAR)IfDesc + IfDesc->bLength;
            continue;
        }

        DPRINT1("usbvideo: interface %u alt %u class %u subclass %u proto %u\n",
                IfDesc->bInterfaceNumber,
                IfDesc->bAlternateSetting,
                IfDesc->bInterfaceClass,
                IfDesc->bInterfaceSubClass,
                IfDesc->bInterfaceProtocol);

        Ptr = (PUCHAR)IfDesc + IfDesc->bLength;
        End = (PUCHAR)Config + Config->wTotalLength;

        if (IfDesc->bInterfaceSubClass == USB_VIDEO_SUBCLASS_CONTROL &&
            ControlIndex < Dx->Topology.ControlInterfaceCount)
        {
            PUSBVIDEO_CONTROL_INTERFACE_INFO Ci =
                &Dx->Topology.ControlInterfaces[ControlIndex++];

            Ci->InterfaceNumber = IfDesc->bInterfaceNumber;
            Ci->AlternateSetting = IfDesc->bAlternateSetting;
            Ci->NumEndpoints = IfDesc->bNumEndpoints;

            while (Ptr + sizeof(USB_COMMON_DESCRIPTOR) <= End)
            {
                PUSB_COMMON_DESCRIPTOR Desc = (PUSB_COMMON_DESCRIPTOR)Ptr;

                if (Desc->bLength == 0)
                    break;

                if (Desc->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE)
                    break;

                if (Desc->bDescriptorType == USB_VIDEO_CS_INTERFACE &&
                    Desc->bLength >= sizeof(USB_VIDEO_CS_INTERFACE_DESCRIPTOR))
                {
                    PUSB_VIDEO_CS_INTERFACE_DESCRIPTOR Cs =
                        (PUSB_VIDEO_CS_INTERFACE_DESCRIPTOR)Desc;

                    DPRINT1("usbvideo:   VC CS_INTERFACE subtype %u len %u\n",
                            Cs->bDescriptorSubType,
                            Cs->bLength);

                    if (Ci->HeaderCsDescriptor == NULL &&
                        Cs->bDescriptorSubType == 0x01) /* VC_HEADER */
                    {
                        Ci->HeaderCsDescriptor = Cs;
                    }
                    else if (Cs->bDescriptorSubType == USB_VIDEO_VC_INPUT_TERMINAL ||
                             Cs->bDescriptorSubType == USB_VIDEO_VC_OUTPUT_TERMINAL ||
                             Cs->bDescriptorSubType == USB_VIDEO_VC_SELECTOR_UNIT ||
                             Cs->bDescriptorSubType == USB_VIDEO_VC_PROCESSING_UNIT ||
                             Cs->bDescriptorSubType == USB_VIDEO_VC_EXTENSION_UNIT)
                    {
                        PUSBVIDEO_VC_NODE_INFO Node;

                        Node = UsbVideo_AppendVcNode(Dx);
                        if (Node)
                        {
                            USHORT TermType;

                            Node->Id = Cs->bData[0];
                            Node->CsDescriptor = Cs;

                            switch (Cs->bDescriptorSubType)
                            {
                                case USB_VIDEO_VC_INPUT_TERMINAL:
                                    Node->NodeType = UsbVideoVcNodeInputTerminal;
                                    if (Cs->bLength >= 8)
                                    {
                                        RtlCopyMemory(&TermType,
                                                      &Cs->bData[1],
                                                      sizeof(USHORT));
                                        Node->TerminalType = TermType;
                                    }
                                    break;

                                case USB_VIDEO_VC_OUTPUT_TERMINAL:
                                    Node->NodeType = UsbVideoVcNodeOutputTerminal;
                                    if (Cs->bLength >= 9)
                                    {
                                        RtlCopyMemory(&TermType,
                                                      &Cs->bData[1],
                                                      sizeof(USHORT));
                                        Node->TerminalType = TermType;
                                        Node->NumInputs = 1;
                                        Node->SourceId = Cs->bData[4];
                                    }
                                    break;

                                case USB_VIDEO_VC_SELECTOR_UNIT:
                                    Node->NodeType = UsbVideoVcNodeSelectorUnit;
                                    if (Cs->bLength >= 6)
                                    {
                                        UCHAR NrInPins = Cs->bData[1];

                                        Node->NumInputs = NrInPins;
                                        if (NrInPins != 0 &&
                                            Cs->bLength >= 5 + NrInPins)
                                        {
                                            Node->SourceId = Cs->bData[2];
                                        }
                                    }
                                    break;

                                case USB_VIDEO_VC_PROCESSING_UNIT:
                                    Node->NodeType = UsbVideoVcNodeProcessingUnit;
                                    if (Cs->bLength >= 6)
                                    {
                                        Node->NumInputs = 1;
                                        Node->SourceId = Cs->bData[1];

                                        if (Cs->bLength >= 9)
                                        {
                                            UCHAR ControlSize;
                                            UCHAR *ControlBits;
                                            UCHAR BitIndex;
                                            static const struct
                                            {
                                                UCHAR Bit;
                                                UCHAR Type;
                                            } PuMap[] =
                                            {
                                                { 0, UsbVideoPropertyBrightness },
                                                { 1, UsbVideoPropertyContrast },
                                                { 2, UsbVideoPropertyHue },
                                                { 3, UsbVideoPropertySaturation },
                                                { 4, UsbVideoPropertySharpness },
                                                { 5, UsbVideoPropertyGamma },
                                                { 6, UsbVideoPropertyWhiteBalanceTemperature },
                                                { 9, UsbVideoPropertyGain }
                                            };

                                            ControlSize = Cs->bData[4];
                                            ControlBits = &Cs->bData[5];

                                            for (BitIndex = 0; BitIndex < sizeof(PuMap) / sizeof(PuMap[0]); BitIndex++)
                                            {
                                                UCHAR Bit = PuMap[BitIndex].Bit;
                                                UCHAR ByteOffset = Bit / 8;
                                                UCHAR Mask = 1 << (Bit % 8);

                                                if (ByteOffset >= ControlSize)
                                                    continue;

                                                if (ControlBits[ByteOffset] & Mask)
                                                {
                                                    PUSBVIDEO_CONTROL_PROPERTY Prop;

                                                    Prop = UsbVideo_AppendProperty(Dx);
                                                    if (!Prop)
                                                        break;

                                                    Prop->NodeId = Node->Id;
                                                    Prop->ControlIndex = Bit;
                                                    Prop->PropertyType = PuMap[BitIndex].Type;
                                                }
                                            }
                                        }
                                    }
                                    break;

                                case USB_VIDEO_VC_EXTENSION_UNIT:
                                    Node->NodeType = UsbVideoVcNodeExtensionUnit;
                                    if (Cs->bLength >= 24)
                                    {
                                        UCHAR NrInPins = Cs->bData[18];

                                        Node->NumInputs = NrInPins;
                                        if (NrInPins != 0 &&
                                            Cs->bLength >= 20 + NrInPins)
                                        {
                                            Node->SourceId = Cs->bData[19];
                                        }
                                    }
                                    break;

                                default:
                                    break;
                            }
                        }
                    }
                }

                Ptr += Desc->bLength;
            }
        }
        else if (IfDesc->bInterfaceSubClass == USB_VIDEO_SUBCLASS_STREAMING &&
                 StreamIndex < Dx->Topology.StreamInterfaceCount)
        {
            PUSBVIDEO_STREAM_INTERFACE_INFO Si =
                &Dx->Topology.StreamInterfaces[StreamIndex++];

            Si->InterfaceNumber = IfDesc->bInterfaceNumber;
            Si->AlternateSetting = IfDesc->bAlternateSetting;

            while (Ptr + sizeof(USB_COMMON_DESCRIPTOR) <= End)
            {
                PUSB_COMMON_DESCRIPTOR Desc = (PUSB_COMMON_DESCRIPTOR)Ptr;

                if (Desc->bLength == 0)
                    break;

                if (Desc->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE)
                    break;

                if (Desc->bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE &&
                    Desc->bLength >= sizeof(USB_ENDPOINT_DESCRIPTOR))
                {
                    PUSB_ENDPOINT_DESCRIPTOR Ep = (PUSB_ENDPOINT_DESCRIPTOR)Desc;

                    Si->EndpointAddress = Ep->bEndpointAddress;
                    Si->EndpointAttributes = Ep->bmAttributes;
                    Si->MaxPacketSize = Ep->wMaxPacketSize;
                    Si->Interval = Ep->bInterval;
                }
                else if (Desc->bDescriptorType == USB_VIDEO_CS_INTERFACE &&
                         Desc->bLength >= sizeof(USB_VIDEO_CS_INTERFACE_DESCRIPTOR))
                {
                    PUSB_VIDEO_CS_INTERFACE_DESCRIPTOR Cs =
                        (PUSB_VIDEO_CS_INTERFACE_DESCRIPTOR)Desc;

                    DPRINT1("usbvideo:   VS CS_INTERFACE subtype %u len %u\n",
                            Cs->bDescriptorSubType,
                            Cs->bLength);

                    if (Si->InputHeaderCsDescriptor == NULL &&
                        Cs->bDescriptorSubType == 0x01) /* VS_INPUT_HEADER */
                    {
                        Si->InputHeaderCsDescriptor = Cs;
                    }
                    else if (Cs->bDescriptorSubType == USB_VIDEO_VS_FORMAT_UNCOMPRESSED ||
                             Cs->bDescriptorSubType == USB_VIDEO_VS_FORMAT_MJPEG)
                    {
                        UCHAR FormatIndex;
                        UCHAR NumFrames;
                        PUSBVIDEO_STREAM_FORMAT_INFO NewFormats;

                        if (Cs->bLength < 5)
                        {
                            /* Need at least bFormatIndex/bNumFrameDescriptors */
                            DPRINT1("usbvideo:   VS_FORMAT descriptor too short (%u)\n",
                                    Cs->bLength);
                        }
                        else
                        {
                            FormatIndex = Cs->bData[0];
                            NumFrames = Cs->bData[1];

                            NewFormats = ExAllocatePoolWithTag(NonPagedPool,
                                                               (Si->FormatCount + 1) * sizeof(USBVIDEO_STREAM_FORMAT_INFO),
                                                               USBVIDEO_TAG);
                            if (!NewFormats)
                            {
                                DPRINT1("usbvideo:   failed to grow format table\n");
                            }
                            else
                            {
                                if (Si->Formats)
                                {
                                    RtlCopyMemory(NewFormats,
                                                  Si->Formats,
                                                  Si->FormatCount * sizeof(USBVIDEO_STREAM_FORMAT_INFO));
                                    ExFreePoolWithTag(Si->Formats, USBVIDEO_TAG);
                                }

                                RtlZeroMemory(&NewFormats[Si->FormatCount],
                                              sizeof(USBVIDEO_STREAM_FORMAT_INFO));

                                NewFormats[Si->FormatCount].FormatIndex = FormatIndex;
                                NewFormats[Si->FormatCount].DescriptorSubtype = Cs->bDescriptorSubType;
                                NewFormats[Si->FormatCount].NumFrameDescriptors = NumFrames;

                                /* Copy GUID for uncompressed formats when present */
                                RtlZeroMemory(&NewFormats[Si->FormatCount].FormatGuid,
                                              sizeof(GUID));
                                if (Cs->bDescriptorSubType == USB_VIDEO_VS_FORMAT_UNCOMPRESSED &&
                                    Cs->bLength >= 5 + sizeof(GUID))
                                {
                                    RtlCopyMemory(&NewFormats[Si->FormatCount].FormatGuid,
                                                  &Cs->bData[2],
                                                  sizeof(GUID));
                                }

                                if (NumFrames != 0)
                                {
                                    NewFormats[Si->FormatCount].Frames =
                                        ExAllocatePoolWithTag(NonPagedPool,
                                                              NumFrames * sizeof(USBVIDEO_STREAM_FRAME_INFO),
                                                              USBVIDEO_TAG);
                                    if (!NewFormats[Si->FormatCount].Frames)
                                    {
                                        DPRINT1("usbvideo:   failed to allocate frame table (FormatIndex=%u)\n",
                                                FormatIndex);
                                        NewFormats[Si->FormatCount].NumFrameDescriptors = 0;
                                    }
                                    else
                                    {
                                        RtlZeroMemory(NewFormats[Si->FormatCount].Frames,
                                                      NumFrames * sizeof(USBVIDEO_STREAM_FRAME_INFO));
                                    }
                                }

                                Si->Formats = NewFormats;
                                Si->FormatCount++;
                            }
                        }
                    }
                    else if ((Cs->bDescriptorSubType == USB_VIDEO_VS_FRAME_UNCOMPRESSED ||
                              Cs->bDescriptorSubType == USB_VIDEO_VS_FRAME_MJPEG) &&
                             Si->FormatCount != 0)
                    {
                        PUSBVIDEO_STREAM_FORMAT_INFO Fmt;
                        UCHAR FrameIndex;
                        USBVIDEO_STREAM_FRAME_INFO *Frame;
                        ULONG DefaultInterval;
                        USHORT Width, Height;
                        UCHAR i;

                        Fmt = &Si->Formats[Si->FormatCount - 1];
                        if (!Fmt->Frames || Fmt->NumFrameDescriptors == 0)
                        {
                            /* No frame storage available */
                            DPRINT1("usbvideo:   frame descriptor with no format context\n");
                        }
                        else
                        {
                            /* Find next free frame slot */
                            Frame = NULL;
                            for (i = 0; i < Fmt->NumFrameDescriptors; i++)
                            {
                                if (Fmt->Frames[i].FrameIndex == 0)
                                {
                                    Frame = &Fmt->Frames[i];
                                    break;
                                }
                            }

                            if (Frame == NULL)
                            {
                                DPRINT1("usbvideo:   more frame descriptors than advertised\n");
                            }
                            else if (Cs->bLength < 26)
                            {
                                DPRINT1("usbvideo:   VS_FRAME descriptor too short (%u)\n",
                                        Cs->bLength);
                            }
                            else
                            {
                                FrameIndex = Cs->bData[0];

                                RtlCopyMemory(&Width, &Cs->bData[2], sizeof(USHORT));
                                RtlCopyMemory(&Height, &Cs->bData[4], sizeof(USHORT));
                                RtlCopyMemory(&DefaultInterval, &Cs->bData[18], sizeof(ULONG));

                                Frame->FrameIndex = FrameIndex;
                                Frame->Width = Width;
                                Frame->Height = Height;
                                Frame->DefaultInterval = DefaultInterval;

                                if (Fmt->FrameCount < Fmt->NumFrameDescriptors)
                                    Fmt->FrameCount++;
                            }
                        }
                    }
                }

                Ptr += Desc->bLength;
            }
        }

        StartPosition = (PUCHAR)IfDesc + IfDesc->bLength;
    }
}

static
NTSTATUS
NTAPI
UsbVideo_PnpComplete(IN PDEVICE_OBJECT DeviceObject,
                     IN PIRP Irp,
                     IN PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
UsbVideo_StartDevice(IN PDEVICE_OBJECT DeviceObject,
                     IN PIRP Irp)
{
    PUSBVIDEO_DEVICE_EXTENSION Dx;
    KEVENT Event;
    NTSTATUS Status;

    Dx = (PUSBVIDEO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           UsbVideo_PnpComplete,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(Dx->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = Irp->IoStatus.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    Status = UsbVideo_GetConfigurationDescriptor(Dx);
    if (NT_SUCCESS(Status))
    {
        UsbVideo_ParseUvcInterfaces(Dx);

        UsbVideo_SelectStreamingConfiguration(Dx);

#if DBG
        DPRINT1("usbvideo: topology: %lu VC, %lu VS interfaces\n",
                Dx->Topology.ControlInterfaceCount,
                Dx->Topology.StreamInterfaceCount);
        {
            ULONG i, j, k;
            NTSTATUS InfoStatus;

            DPRINT1("usbvideo: VC nodes: %lu\n", Dx->Topology.VcNodeCount);
            for (i = 0; i < Dx->Topology.VcNodeCount; i++)
            {
                PUSBVIDEO_VC_NODE_INFO Node = &Dx->Topology.VcNodes[i];

                DPRINT1("usbvideo: VCNode[%lu]: id=%u type=%u src=%u inputs=%u termType=0x%04x\n",
                        i,
                        Node->Id,
                        Node->NodeType,
                        Node->SourceId,
                        Node->NumInputs,
                        Node->TerminalType);
            }

            DPRINT1("usbvideo: properties: %lu\n", Dx->Topology.PropertyCount);
            for (i = 0; i < Dx->Topology.PropertyCount; i++)
            {
                PUSBVIDEO_CONTROL_PROPERTY Prop = &Dx->Topology.Properties[i];
                UCHAR Info = 0;

                DPRINT1("usbvideo:  Prop[%lu]: nodeId=%u ctrlBit=%u type=%u\n",
                        i,
                        Prop->NodeId,
                        Prop->ControlIndex,
                        Prop->PropertyType);

                InfoStatus = UsbVideo_VcGetOrSetProperty(Dx,
                                                         Prop,
                                                         USB_VIDEO_REQ_GET_INFO,
                                                         &Info,
                                                         sizeof(Info),
                                                         TRUE);
                DPRINT1("usbvideo:   Prop[%lu] GET_INFO: status=0x%08lx info=0x%02x\n",
                        i,
                        InfoStatus,
                        Info);
            }

            if (Dx->StreamInterface)
            {
                DPRINT1("usbvideo: stream interface: if=%u alt=%u pipes=%lu selPipe=%u\n",
                        Dx->StreamInterface->InterfaceNumber,
                        Dx->StreamInterface->AlternateSetting,
                        Dx->StreamInterface->NumberOfPipes,
                        Dx->StreamPipeIndex);

                for (i = 0; i < Dx->StreamInterface->NumberOfPipes; i++)
                {
                    PUSBD_PIPE_INFORMATION Pipe = &Dx->StreamInterface->Pipes[i];

                    DPRINT1("usbvideo:  pipe[%lu]: addr=0x%02x type=%u mps=0x%04x interval=%u\n",
                            i,
                            Pipe->EndpointAddress,
                            Pipe->PipeType,
                            Pipe->MaximumPacketSize,
                            Pipe->Interval);
                }
            }

            for (i = 0; i < Dx->Topology.ControlInterfaceCount; i++)
            {
                PUSBVIDEO_CONTROL_INTERFACE_INFO Ci =
                    &Dx->Topology.ControlInterfaces[i];

                DPRINT1("usbvideo: VC[%lu]: if=%u alt=%u ep=%u\n",
                        i,
                        Ci->InterfaceNumber,
                        Ci->AlternateSetting,
                        Ci->NumEndpoints);

                if (Ci->HeaderCsDescriptor)
                {
                    USHORT BcdUvc = 0;

                    if (Ci->HeaderCsDescriptor->bLength >= 5)
                    {
                        RtlCopyMemory(&BcdUvc,
                                      &Ci->HeaderCsDescriptor->bData[0],
                                      sizeof(USHORT));
                    }

                    DPRINT1("usbvideo:  VC_HEADER: bcdUVC=0x%04x len=%u\n",
                            BcdUvc,
                            Ci->HeaderCsDescriptor->bLength);
                }
            }

            for (i = 0; i < Dx->Topology.StreamInterfaceCount; i++)
            {
                PUSBVIDEO_STREAM_INTERFACE_INFO Si =
                    &Dx->Topology.StreamInterfaces[i];

                DPRINT1("usbvideo: VS[%lu]: if=%u alt=%u ep=0x%02x attr=0x%02x mps=0x%04x int=%u formats=%lu\n",
                        i,
                        Si->InterfaceNumber,
                        Si->AlternateSetting,
                        Si->EndpointAddress,
                        Si->EndpointAttributes,
                        Si->MaxPacketSize,
                        Si->Interval,
                        Si->FormatCount);

                for (j = 0; j < Si->FormatCount; j++)
                {
                    PUSBVIDEO_STREAM_FORMAT_INFO Fmt = &Si->Formats[j];

                    DPRINT1("usbvideo:  F[%lu]: idx=%u subtype=%u frames=%lu\n",
                            j,
                            Fmt->FormatIndex,
                            Fmt->DescriptorSubtype,
                            Fmt->FrameCount);

                    for (k = 0; k < Fmt->FrameCount; k++)
                    {
                        PUSBVIDEO_STREAM_FRAME_INFO Fr = &Fmt->Frames[k];

                        DPRINT1("usbvideo:   FR[%lu]: idx=%u %ux%u defInt=%lu\n",
                                k,
                                Fr->FrameIndex,
                                Fr->Width,
                                Fr->Height,
                                Fr->DefaultInterval);
                    }
                }
            }
        }
#endif
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
UsbVideo_DispatchPass(IN PDEVICE_OBJECT DeviceObject,
                      IN PIRP Irp)
{
    PUSBVIDEO_DEVICE_EXTENSION dx;

    dx = (PUSBVIDEO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(dx->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
UsbVideo_DispatchPnp(IN PDEVICE_OBJECT DeviceObject,
                     IN PIRP Irp)
{
    PUSBVIDEO_DEVICE_EXTENSION dx;
    PIO_STACK_LOCATION irpSp;
    NTSTATUS Status;

    dx = (PUSBVIDEO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    irpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (irpSp->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            return UsbVideo_StartDevice(DeviceObject, Irp);

        case IRP_MN_REMOVE_DEVICE:
            if (dx->ConfigDescriptor)
            {
                ExFreePoolWithTag(dx->ConfigDescriptor, USBVIDEO_TAG);
                dx->ConfigDescriptor = NULL;
                dx->ConfigDescriptorLength = 0;
            }

            UsbVideo_FreeTopology(dx);

            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(dx->LowerDevice, Irp);
            IoDetachDevice(dx->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            return UsbVideo_DispatchPass(DeviceObject, Irp);
    }
}

static
NTSTATUS
NTAPI
UsbVideo_DispatchPower(IN PDEVICE_OBJECT DeviceObject,
                       IN PIRP Irp)
{
    PUSBVIDEO_DEVICE_EXTENSION dx;

    dx = (PUSBVIDEO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(dx->LowerDevice, Irp);
}

static
VOID
NTAPI
UsbVideo_Unload(IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

static
NTSTATUS
NTAPI
UsbVideo_AddDevice(IN PDRIVER_OBJECT DriverObject,
                   IN PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject;
    PUSBVIDEO_DEVICE_EXTENSION dx;

    DPRINT("UsbVideo_AddDevice: PDO %p\n", PhysicalDeviceObject);

    Status = IoCreateDevice(DriverObject,
                            sizeof(USBVIDEO_DEVICE_EXTENSION),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            0,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("UsbVideo_AddDevice: IoCreateDevice failed, Status 0x%lx\n", Status);
        return Status;
    }

    dx = (PUSBVIDEO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    RtlZeroMemory(dx, sizeof(*dx));

    dx->Pdo = PhysicalDeviceObject;
    dx->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (dx->LowerDevice == NULL)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags |= (dx->LowerDevice->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO));
    DeviceObject->DeviceType = dx->LowerDevice->DeviceType;
    DeviceObject->Characteristics = dx->LowerDevice->Characteristics;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(IN PDRIVER_OBJECT DriverObject,
            IN PUNICODE_STRING RegistryPath)
{
    ULONG i;

    UNREFERENCED_PARAMETER(RegistryPath);

    DPRINT("usbvideo: DriverEntry\n");

    DriverObject->DriverUnload = UsbVideo_Unload;
    DriverObject->DriverExtension->AddDevice = UsbVideo_AddDevice;

    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        DriverObject->MajorFunction[i] = UsbVideo_DispatchPass;
    }

    DriverObject->MajorFunction[IRP_MJ_PNP] = UsbVideo_DispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = UsbVideo_DispatchPower;

    return STATUS_SUCCESS;
}
