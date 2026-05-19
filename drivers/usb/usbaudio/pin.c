/*
* PROJECT:     ReactOS Universal Audio Class Driver
* LICENSE:     GPL - See COPYING in the top level directory
* FILE:        drivers/usb/usbaudio/pin.c
* PURPOSE:     USB Audio device driver.
* PROGRAMMERS:
*              Johannes Anderwald (johannes.anderwald@reactos.org)
*/

#include "usbaudio.h"

#define PACKET_COUNT 10

typedef struct
{
    PWAVEFORMATEX WaveFormat;
    USHORT ValidBitsPerSample;
} USB_AUDIO_PCM_FORMAT, *PUSB_AUDIO_PCM_FORMAT;

NTSTATUS
GetMaxPacketSizeForInterface(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor,
    KSPIN_DATAFLOW DataFlow)
{
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor;

    /* loop descriptors */
    CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((ULONG_PTR)InterfaceDescriptor + InterfaceDescriptor->bLength);
    ASSERT(InterfaceDescriptor->bNumEndpoints > 0);
    while (CommonDescriptor)
    {
        if (CommonDescriptor->bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE)
        {
            EndpointDescriptor = (PUSB_ENDPOINT_DESCRIPTOR)CommonDescriptor;
            return EndpointDescriptor->wMaxPacketSize;
        }

        if (CommonDescriptor->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE)
        {
            /* reached next interface descriptor */
            break;
        }

        if ((ULONG_PTR)CommonDescriptor + CommonDescriptor->bLength >= ((ULONG_PTR)ConfigurationDescriptor + ConfigurationDescriptor->wTotalLength))
            break;

        CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((ULONG_PTR)CommonDescriptor + CommonDescriptor->bLength);
    }

    /* default to 100 */
    return 100;
}

static
ULONG
UsbAudioCountSetBits(
    IN ULONG Value)
{
    ULONG Count = 0;

    while (Value != 0)
    {
        Count += Value & 1;
        Value >>= 1;
    }

    return Count;
}

static
BOOLEAN
UsbAudioValidatePcmWaveFormat(
    IN PWAVEFORMATEX WaveFormat)
{
    ULONG BytesPerSample;
    ULONG BlockAlign;

    if (WaveFormat->nChannels == 0 ||
        WaveFormat->nSamplesPerSec == 0 ||
        WaveFormat->wBitsPerSample == 0 ||
        (WaveFormat->wBitsPerSample % 8) != 0)
    {
        return FALSE;
    }

    BytesPerSample = WaveFormat->wBitsPerSample / 8;
    if (BytesPerSample == 0 ||
        WaveFormat->nChannels > MAXUSHORT / BytesPerSample)
    {
        return FALSE;
    }

    BlockAlign = WaveFormat->nChannels * BytesPerSample;
    if (WaveFormat->nBlockAlign != BlockAlign ||
        WaveFormat->nSamplesPerSec > MAXULONG / WaveFormat->nBlockAlign ||
        WaveFormat->nAvgBytesPerSec != WaveFormat->nSamplesPerSec * WaveFormat->nBlockAlign)
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
UsbAudioGetPcmWaveFormat(
    IN PKSDATARANGE ConnectionFormat,
    OUT PUSB_AUDIO_PCM_FORMAT PcmFormat)
{
    PWAVEFORMATEX WaveFormat;
    PWAVEFORMATEXTENSIBLE WaveFormatExtensible;
    ULONG MinimumFormatSize;
    ULONG ExtraFormatSize;
    ULONG ChannelMask;

    if (PcmFormat)
        RtlZeroMemory(PcmFormat, sizeof(*PcmFormat));

    MinimumFormatSize = sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEX);
    if (!ConnectionFormat ||
        ConnectionFormat->FormatSize < MinimumFormatSize ||
        !IsEqualGUIDAligned(&ConnectionFormat->MajorFormat, &KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(&ConnectionFormat->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(&ConnectionFormat->Specifier, &KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
    {
        return FALSE;
    }

    WaveFormat = (PWAVEFORMATEX)(ConnectionFormat + 1);
    ExtraFormatSize = ConnectionFormat->FormatSize - MinimumFormatSize;
    if (WaveFormat->cbSize > ExtraFormatSize ||
        !UsbAudioValidatePcmWaveFormat(WaveFormat))
    {
        return FALSE;
    }

    if (WaveFormat->wFormatTag == WAVE_FORMAT_PCM)
    {
        if (PcmFormat)
        {
            PcmFormat->WaveFormat = WaveFormat;
            PcmFormat->ValidBitsPerSample = WaveFormat->wBitsPerSample;
        }
        return TRUE;
    }

    if (WaveFormat->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        WaveFormat->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
    {
        return FALSE;
    }

    WaveFormatExtensible = (PWAVEFORMATEXTENSIBLE)WaveFormat;
    if (!IsEqualGUIDAligned(&WaveFormatExtensible->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
        return FALSE;

    if (WaveFormatExtensible->Samples.wValidBitsPerSample == 0 ||
        WaveFormatExtensible->Samples.wValidBitsPerSample > WaveFormat->wBitsPerSample)
    {
        return FALSE;
    }

    ChannelMask = WaveFormatExtensible->dwChannelMask;
    if ((ChannelMask & (SPEAKER_RESERVED | SPEAKER_ALL)) != 0 ||
        (ChannelMask != KSAUDIO_SPEAKER_DIRECTOUT &&
         UsbAudioCountSetBits(ChannelMask) != WaveFormat->nChannels))
    {
        return FALSE;
    }

    if (PcmFormat)
    {
        PcmFormat->WaveFormat = WaveFormat;
        PcmFormat->ValidBitsPerSample =
            WaveFormatExtensible->Samples.wValidBitsPerSample;
    }
    return TRUE;
}

static
BOOLEAN
UsbAudioEndpointDirectionMatches(
    IN UCHAR EndpointAddress,
    IN KSPIN_DATAFLOW DataFlow)
{
    if (DataFlow == KSPIN_DATAFLOW_OUT)
        return USB_ENDPOINT_DIRECTION_IN(EndpointAddress);

    return !USB_ENDPOINT_DIRECTION_IN(EndpointAddress);
}

static
PUSB_ENDPOINT_DESCRIPTOR
UsbAudioFindStreamingEndpoint(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor,
    IN KSPIN_DATAFLOW DataFlow)
{
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor;
    PUCHAR DescriptorEnd;

    DescriptorEnd = (PUCHAR)ConfigurationDescriptor +
                    ConfigurationDescriptor->wTotalLength;
    CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)InterfaceDescriptor +
                                                InterfaceDescriptor->bLength);

    while ((PUCHAR)CommonDescriptor + sizeof(USB_COMMON_DESCRIPTOR) <= DescriptorEnd &&
           CommonDescriptor->bLength != 0 &&
           (PUCHAR)CommonDescriptor + CommonDescriptor->bLength <= DescriptorEnd)
    {
        if (CommonDescriptor->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE)
            break;

        if (CommonDescriptor->bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE)
        {
            EndpointDescriptor = (PUSB_ENDPOINT_DESCRIPTOR)CommonDescriptor;
            if ((EndpointDescriptor->bmAttributes & USB_ENDPOINT_TYPE_MASK) ==
                    USB_ENDPOINT_TYPE_ISOCHRONOUS &&
                UsbAudioEndpointDirectionMatches(EndpointDescriptor->bEndpointAddress,
                                                 DataFlow))
            {
                return EndpointDescriptor;
            }
        }

        CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)CommonDescriptor +
                                                    CommonDescriptor->bLength);
    }

    return NULL;
}

static
PUSB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR
UsbAudioFindStreamingEndpointDescriptor(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor,
    IN PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor)
{
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUCHAR DescriptorEnd;

    DescriptorEnd = (PUCHAR)ConfigurationDescriptor +
                    ConfigurationDescriptor->wTotalLength;
    CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)EndpointDescriptor +
                                                EndpointDescriptor->bLength);

    while ((PUCHAR)CommonDescriptor + sizeof(USB_COMMON_DESCRIPTOR) <= DescriptorEnd &&
           CommonDescriptor->bLength != 0 &&
           (PUCHAR)CommonDescriptor + CommonDescriptor->bLength <= DescriptorEnd)
    {
        if (CommonDescriptor->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE ||
            CommonDescriptor->bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE)
        {
            break;
        }

        if (CommonDescriptor->bDescriptorType == USB_AUDIO_CS_ENDPOINT &&
            CommonDescriptor->bLength >= sizeof(USB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR))
        {
            PUSB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR StreamingEndpointDescriptor;

            StreamingEndpointDescriptor =
                (PUSB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR)CommonDescriptor;
            if (StreamingEndpointDescriptor->bDescriptorSubtype == USB_AUDIO_EP_GENERAL)
                return StreamingEndpointDescriptor;
        }

        CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)CommonDescriptor +
                                                    CommonDescriptor->bLength);
    }

    UNREFERENCED_PARAMETER(InterfaceDescriptor);
    return NULL;
}

static
PUSB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR
UsbAudioFindFormatTypeDescriptor(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor)
{
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUSB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR FormatDescriptor;
    PUCHAR DescriptorEnd;

    DescriptorEnd = (PUCHAR)ConfigurationDescriptor +
                    ConfigurationDescriptor->wTotalLength;
    CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)InterfaceDescriptor +
                                                InterfaceDescriptor->bLength);

    while ((PUCHAR)CommonDescriptor + sizeof(USB_COMMON_DESCRIPTOR) <= DescriptorEnd &&
           CommonDescriptor->bLength != 0 &&
           (PUCHAR)CommonDescriptor + CommonDescriptor->bLength <= DescriptorEnd)
    {
        if (CommonDescriptor->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE)
            break;

        if (CommonDescriptor->bDescriptorType == USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE &&
            CommonDescriptor->bLength >= FIELD_OFFSET(USB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR, tSamFreq))
        {
            FormatDescriptor = (PUSB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR)CommonDescriptor;
            if (FormatDescriptor->bDescriptorSubtype == 0x02 &&
                FormatDescriptor->bFormatType == 0x01)
            {
                return FormatDescriptor;
            }
        }

        CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)CommonDescriptor +
                                                    CommonDescriptor->bLength);
    }

    return NULL;
}

static
BOOLEAN
UsbAudioFormatSupportsSampleRate(
    IN PUSB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR FormatDescriptor,
    IN ULONG SampleRate)
{
    ULONG Index;
    ULONG SampleFrequency;
    ULONG NumFrequency;

    NumFrequency = FormatDescriptor->bSamFreqType;
    if (NumFrequency == 0)
    {
        ULONG MinimumSampleFrequency;
        ULONG MaximumSampleFrequency;

        if (FormatDescriptor->bLength <
            FIELD_OFFSET(USB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR, tSamFreq) + 6)
        {
            return FALSE;
        }

        MinimumSampleFrequency =
            ((ULONG)FormatDescriptor->tSamFreq[0]) |
            ((ULONG)FormatDescriptor->tSamFreq[1] << 8) |
            ((ULONG)FormatDescriptor->tSamFreq[2] << 16);
        MaximumSampleFrequency =
            ((ULONG)FormatDescriptor->tSamFreq[3]) |
            ((ULONG)FormatDescriptor->tSamFreq[4] << 8) |
            ((ULONG)FormatDescriptor->tSamFreq[5] << 16);

        return MinimumSampleFrequency <= SampleRate &&
               MaximumSampleFrequency >= SampleRate;
    }

    for (Index = 0; Index < NumFrequency; Index++)
    {
        if (FormatDescriptor->bLength <
            FIELD_OFFSET(USB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR, tSamFreq) +
            ((Index + 1) * 3))
        {
            break;
        }

        SampleFrequency =
            ((ULONG)FormatDescriptor->tSamFreq[Index * 3]) |
            ((ULONG)FormatDescriptor->tSamFreq[Index * 3 + 1] << 8) |
            ((ULONG)FormatDescriptor->tSamFreq[Index * 3 + 2] << 16);
        if (SampleFrequency == SampleRate)
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
UsbAudioFormatMatchesConnectionFormat(
    IN PUSB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR FormatDescriptor,
    IN PKSDATARANGE ConnectionFormat)
{
    USB_AUDIO_PCM_FORMAT PcmFormat;

    if (!FormatDescriptor ||
        !UsbAudioGetPcmWaveFormat(ConnectionFormat, &PcmFormat))
    {
        return FALSE;
    }

    if (FormatDescriptor->bSubframeSize == 0 ||
        FormatDescriptor->bSubframeSize * 8 != PcmFormat.WaveFormat->wBitsPerSample ||
        FormatDescriptor->bNrChannels != PcmFormat.WaveFormat->nChannels ||
        FormatDescriptor->bBitResolution != PcmFormat.ValidBitsPerSample)
    {
        return FALSE;
    }

    return UsbAudioFormatSupportsSampleRate(FormatDescriptor,
                                            PcmFormat.WaveFormat->nSamplesPerSec);
}

NTSTATUS
UsbAudioAllocCaptureUrbIso(
    IN USBD_PIPE_HANDLE PipeHandle,
    IN ULONG MaxPacketSize,
    IN PVOID Buffer,
    IN ULONG BufferLength,
    OUT PURB * OutUrb)
{
    PURB Urb;
    ULONG UrbSize;
    ULONG Index;

    /* calculate urb size*/
    UrbSize = GET_ISO_URB_SIZE(PACKET_COUNT);

    /* allocate urb */
    Urb = AllocFunction(UrbSize);
    if (!Urb)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* init urb */
    Urb->UrbIsochronousTransfer.Hdr.Function = URB_FUNCTION_ISOCH_TRANSFER;
    Urb->UrbIsochronousTransfer.Hdr.Length = UrbSize;
    Urb->UrbIsochronousTransfer.PipeHandle = PipeHandle;
    Urb->UrbIsochronousTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_START_ISO_TRANSFER_ASAP;
    Urb->UrbIsochronousTransfer.TransferBufferLength = BufferLength;
    Urb->UrbIsochronousTransfer.TransferBuffer = Buffer;
    Urb->UrbIsochronousTransfer.NumberOfPackets = PACKET_COUNT;

    for (Index = 0; Index < PACKET_COUNT; Index++)
    {
        Urb->UrbIsochronousTransfer.IsoPacket[Index].Offset = Index * MaxPacketSize;
    }

    *OutUrb = Urb;
    return STATUS_SUCCESS;

}

NTSTATUS
UsbAudioQuerySampleRate(
    IN PPIN_CONTEXT PinContext,
    IN UCHAR Request,
    OUT PULONG OutSampleRate)
{
    PURB Urb;
    PUCHAR Buffer;
    NTSTATUS Status;
    ULONG SampleRate;

    if (!OutSampleRate)
        return STATUS_INVALID_PARAMETER;

    Buffer = AllocFunction(sizeof(ULONG));
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    Urb = AllocFunction(sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));
    if (!Urb)
    {
        FreeFunction(Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UsbBuildVendorRequest(Urb,
        URB_FUNCTION_CLASS_ENDPOINT,
        sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
        USBD_TRANSFER_DIRECTION_IN,
        0,
        Request,
        USB_AUDIO_SAMPLING_FREQ_CONTROL,
        PinContext->DeviceExtension->InterfaceInfo->Pipes[0].EndpointAddress,
        Buffer,
        NULL,
        3,
        NULL);

    Status = SubmitUrbSync(PinContext->LowerDevice, Urb);

    if (NT_SUCCESS(Status))
    {
        SampleRate = ((ULONG)Buffer[2] << 16) | ((ULONG)Buffer[1] << 8) | Buffer[0];
        *OutSampleRate = SampleRate;
    }

    FreeFunction(Urb);
    FreeFunction(Buffer);
    return Status;
}
NTSTATUS
UsbAudioSetFormat(
    IN PKSPIN Pin)
{
    PURB Urb;
    PUCHAR SampleRateBuffer;
    PPIN_CONTEXT PinContext;
    PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor;
    PUSB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR StreamingEndpointDescriptor;
    NTSTATUS Status;
    USB_AUDIO_PCM_FORMAT PcmFormat;

    PinContext = Pin->Context;
    if (!PinContext ||
        !PinContext->DeviceExtension ||
        !PinContext->DeviceExtension->ConfigurationDescriptor ||
        !PinContext->InterfaceDescriptor)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    /* allocate sample rate buffer */
    SampleRateBuffer = AllocFunction(sizeof(ULONG));
    if (!SampleRateBuffer)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (UsbAudioGetPcmWaveFormat(Pin->ConnectionFormat, &PcmFormat))
    {
        SampleRateBuffer[2] = (PcmFormat.WaveFormat->nSamplesPerSec >> 16) & 0xFF;
        SampleRateBuffer[1] = (PcmFormat.WaveFormat->nSamplesPerSec >> 8) & 0xFF;
        SampleRateBuffer[0] = (PcmFormat.WaveFormat->nSamplesPerSec >> 0) & 0xFF;
    }
    else
    {
        /* non-PCM and malformed PCM formats are not supported by this driver */
        DPRINT1("UsbAudioSetFormat: unsupported or invalid PCM format\n");
        FreeFunction(SampleRateBuffer);
        return STATUS_NOT_SUPPORTED;
    }

    EndpointDescriptor =
        UsbAudioFindStreamingEndpoint(PinContext->DeviceExtension->ConfigurationDescriptor,
                                      PinContext->InterfaceDescriptor,
                                      Pin->DataFlow);
    if (!EndpointDescriptor)
    {
        DPRINT1("UsbAudioSetFormat: no streaming endpoint for pin %lu\n",
                Pin->Id);
        FreeFunction(SampleRateBuffer);
        return STATUS_INVALID_DEVICE_STATE;
    }

    StreamingEndpointDescriptor =
        UsbAudioFindStreamingEndpointDescriptor(PinContext->DeviceExtension->ConfigurationDescriptor,
                                               PinContext->InterfaceDescriptor,
                                               EndpointDescriptor);
    if (!StreamingEndpointDescriptor ||
        !(StreamingEndpointDescriptor->bmAttributes &
          USB_AUDIO_EP_SAMPLING_FREQUENCY_CONTROL))
    {
        FreeFunction(SampleRateBuffer);
        return STATUS_SUCCESS;
    }

    /* allocate urb */
    Urb = AllocFunction(sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));
    if (!Urb)
    {
        /* no memory */
        FreeFunction(SampleRateBuffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Set sample rate via USB Audio Class endpoint control request */
    UsbBuildVendorRequest(Urb,
        URB_FUNCTION_CLASS_ENDPOINT,
        sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
        USBD_TRANSFER_DIRECTION_OUT,
        0,
        USB_AUDIO_SET_CUR,
        USB_AUDIO_SAMPLING_FREQ_CONTROL,
        EndpointDescriptor->bEndpointAddress,
        SampleRateBuffer,
        NULL,
        3,
        NULL);

    /* submit urb */
    Status = SubmitUrbSync(PinContext->LowerDevice, Urb);

    FreeFunction(Urb);
    FreeFunction(SampleRateBuffer);
    return Status;
}

NTSTATUS
USBAudioSelectAudioStreamingInterface(
    IN PKSPIN Pin,
    IN PPIN_CONTEXT PinContext,
    IN PDEVICE_EXTENSION DeviceExtension,
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN ULONG FormatDescriptorIndex)
{
    PURB Urb;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor;
    NTSTATUS Status;
    ULONG Found, Index;

    PUSB_AUDIO_STREAMING_INTERFACE_DESCRIPTOR StreamingInterfaceDescriptor;
    PUSB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR FormatDescriptor;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR TerminalDescriptor = NULL;

    UNREFERENCED_PARAMETER(FormatDescriptorIndex);

    /* search for terminal descriptor of that irp sink / irp source */
    TerminalDescriptor = UsbAudioGetStreamingTerminalDescriptorByIndex(DeviceExtension->ConfigurationDescriptor, Pin->Id);
    if (!TerminalDescriptor)
        return STATUS_INVALID_DEVICE_REQUEST;

    /* grab interface descriptor */
    InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, ConfigurationDescriptor, -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    if (!InterfaceDescriptor)
    {
        /* no such interface */
        return STATUS_INVALID_PARAMETER;
    }

    Found = FALSE;
    Index = 0;

    /* selects the interface which has an audio streaming interface descriptor associated to the input / output terminal at the given format index */
    while (InterfaceDescriptor != NULL)
    {
        if (InterfaceDescriptor->bInterfaceSubClass == 0x02 /* AUDIO_STREAMING */ && InterfaceDescriptor->bNumEndpoints > 0)
        {
            StreamingInterfaceDescriptor = (PUSB_AUDIO_STREAMING_INTERFACE_DESCRIPTOR)USBD_ParseDescriptors(ConfigurationDescriptor, ConfigurationDescriptor->wTotalLength, InterfaceDescriptor, USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
            if (StreamingInterfaceDescriptor != NULL)
            {
                ASSERT(StreamingInterfaceDescriptor->bDescriptorSubtype == 0x01);
                ASSERT(StreamingInterfaceDescriptor->wFormatTag == WAVE_FORMAT_PCM);
                if (StreamingInterfaceDescriptor->bTerminalLink == TerminalDescriptor->bTerminalID)
                {
                    FormatDescriptor = UsbAudioFindFormatTypeDescriptor(ConfigurationDescriptor,
                                                                        InterfaceDescriptor);
                    EndpointDescriptor = UsbAudioFindStreamingEndpoint(ConfigurationDescriptor,
                                                                       InterfaceDescriptor,
                                                                       Pin->DataFlow);

                    if (FormatDescriptor &&
                        EndpointDescriptor &&
                        UsbAudioFormatMatchesConnectionFormat(FormatDescriptor,
                                                             Pin->ConnectionFormat))
                    {
                        Found = TRUE;
                        break;
                    }

                    Index++;
                }
            }
        }
        InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, (PVOID)((ULONG_PTR)InterfaceDescriptor + InterfaceDescriptor->bLength), -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    }

    if (!Found)
    {
        /* no such interface */
        DPRINT1("No Interface found\n");
        return STATUS_INVALID_PARAMETER;
    }

    Urb = AllocFunction(GET_SELECT_INTERFACE_REQUEST_SIZE(InterfaceDescriptor->bNumEndpoints));
    if (!Urb)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

     /* now prepare interface urb */
     UsbBuildSelectInterfaceRequest(Urb, GET_SELECT_INTERFACE_REQUEST_SIZE(InterfaceDescriptor->bNumEndpoints), DeviceExtension->ConfigurationHandle, InterfaceDescriptor->bInterfaceNumber, InterfaceDescriptor->bAlternateSetting);

     /* now select the interface */
     Status = SubmitUrbSync(DeviceExtension->LowerDevice, Urb);

     DPRINT1("USBAudioSelectAudioStreamingInterface Status %x UrbStatus %x InterfaceNumber %x AlternateSetting %x\n", Status, Urb->UrbSelectInterface.Hdr.Status, InterfaceDescriptor->bInterfaceNumber, InterfaceDescriptor->bAlternateSetting);

     /* did it succeeed */
     if (NT_SUCCESS(Status))
     {
         /* free old interface info */
         if (DeviceExtension->InterfaceInfo)
         {
             /* free old info */
             FreeFunction(DeviceExtension->InterfaceInfo);
         }

         /* alloc interface info */
         DeviceExtension->InterfaceInfo = AllocFunction(Urb->UrbSelectInterface.Interface.Length);
         if (DeviceExtension->InterfaceInfo == NULL)
         {
             /* no memory */
             FreeFunction(Urb);
             return STATUS_INSUFFICIENT_RESOURCES;
         }

         /* copy interface info */
         RtlCopyMemory(DeviceExtension->InterfaceInfo, &Urb->UrbSelectInterface.Interface, Urb->UrbSelectInterface.Interface.Length);
         PinContext->InterfaceDescriptor = InterfaceDescriptor;
     }

     /* free urb */
     FreeFunction(Urb);
     return Status;
}

VOID
NTAPI
CaptureGateOnWorkItem(
    _In_ PVOID Context)
{
    PKSPIN Pin;
    PPIN_CONTEXT PinContext;
    PKSGATE Gate;
    ULONG Count;

    /* get pin */
    Pin = Context;

    /* get pin context */
    PinContext = Pin->Context;

    do
    {
        /* acquire processing mutex */
        KsPinAcquireProcessingMutex(Pin);

        /* get pin control gate */
        Gate = KsPinGetAndGate(Pin);

        /* turn input on */
        KsGateTurnInputOn(Gate);

        /* schedule processing */
        KsPinAttemptProcessing(Pin, TRUE);

        /* release processing mutex */
        KsPinReleaseProcessingMutex(Pin);

        /* decrement worker count */
        Count = KsDecrementCountedWorker(PinContext->CaptureWorker);
    } while (Count);
}

NTSTATUS
RenderInitializeUrbAndIrp(
    IN PKSPIN Pin,
    IN PPIN_CONTEXT PinContext,
    IN OUT PIRP Irp,
    IN PVOID TransferBuffer,
    IN ULONG TransferBufferSize,
    IN ULONG PacketSize)
{
    ULONG Index, PacketCount;
    PURB Urb;
    PIO_STACK_LOCATION IoStack;

    /* initialize irp */
    IoInitializeIrp(Irp, IoSizeOfIrp(PinContext->DeviceExtension->LowerDevice->StackSize), PinContext->DeviceExtension->LowerDevice->StackSize);

    /* set irp members */
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Irp->Flags = 0;
    Irp->UserBuffer = NULL;

    /* init stack location */
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->DeviceObject = PinContext->DeviceExtension->LowerDevice;
    IoStack->Parameters.Others.Argument2 = NULL;
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

    /* set completion routine */
    IoSetCompletionRoutine(Irp, UsbAudioRenderComplete, Pin, TRUE, TRUE, TRUE);

    /* calculate packet count */
    PacketCount = TransferBufferSize / PacketSize;
    ASSERT(TransferBufferSize % PacketSize == 0);

    /* lets allocate urb */
    Urb = (PURB)AllocFunction(GET_ISO_URB_SIZE(PacketCount));
    if (!Urb)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* init urb */
    Urb->UrbIsochronousTransfer.Hdr.Function = URB_FUNCTION_ISOCH_TRANSFER;
    Urb->UrbIsochronousTransfer.Hdr.Length = GET_ISO_URB_SIZE(PacketCount);
    Urb->UrbIsochronousTransfer.PipeHandle = PinContext->DataPipeHandle;
    Urb->UrbIsochronousTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_OUT | USBD_START_ISO_TRANSFER_ASAP;
    Urb->UrbIsochronousTransfer.TransferBufferLength = TransferBufferSize;
    Urb->UrbIsochronousTransfer.TransferBuffer = TransferBuffer;
    Urb->UrbIsochronousTransfer.NumberOfPackets = PacketCount;
    Urb->UrbIsochronousTransfer.StartFrame = 0;

    for (Index = 0; Index < PacketCount; Index++)
    {
        Urb->UrbIsochronousTransfer.IsoPacket[Index].Offset = Index * PacketSize;
    }

    /* store urb */
    IoStack->Parameters.Others.Argument1 = Urb;
    Irp->Tail.Overlay.DriverContext[0] = Urb;


    /* done */
    return STATUS_SUCCESS;
}

VOID
CaptureInitializeUrbAndIrp(
    IN PKSPIN Pin,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PURB Urb;
    PUCHAR TransferBuffer;
    ULONG Index;
    PPIN_CONTEXT PinContext;

    /* get pin context */
    PinContext = Pin->Context;

    /* backup urb and transferbuffer */
    Urb = Irp->Tail.Overlay.DriverContext[0];
    TransferBuffer = Urb->UrbIsochronousTransfer.TransferBuffer;

    /* initialize irp */
    IoInitializeIrp(Irp, IoSizeOfIrp(PinContext->DeviceExtension->LowerDevice->StackSize), PinContext->DeviceExtension->LowerDevice->StackSize);

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Irp->Flags = 0;
    Irp->UserBuffer = NULL;
    Irp->Tail.Overlay.DriverContext[0] = Urb;
    Irp->Tail.Overlay.DriverContext[1] = NULL;

    /* init stack location */
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->DeviceObject = PinContext->DeviceExtension->LowerDevice;
    IoStack->Parameters.Others.Argument1 = Urb;
    IoStack->Parameters.Others.Argument2 = NULL;
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

    IoSetCompletionRoutine(Irp, UsbAudioCaptureComplete, Pin, TRUE, TRUE, TRUE);

    RtlZeroMemory(Urb, GET_ISO_URB_SIZE(PACKET_COUNT));

    /* init urb */
    Urb->UrbIsochronousTransfer.Hdr.Function = URB_FUNCTION_ISOCH_TRANSFER;
    Urb->UrbIsochronousTransfer.Hdr.Length = GET_ISO_URB_SIZE(10);
    Urb->UrbIsochronousTransfer.PipeHandle = PinContext->DeviceExtension->InterfaceInfo->Pipes[0].PipeHandle;
    Urb->UrbIsochronousTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_START_ISO_TRANSFER_ASAP;
    Urb->UrbIsochronousTransfer.TransferBufferLength = PinContext->DeviceExtension->InterfaceInfo->Pipes[0].MaximumPacketSize * 10;
    Urb->UrbIsochronousTransfer.TransferBuffer = TransferBuffer;
    Urb->UrbIsochronousTransfer.NumberOfPackets = PACKET_COUNT;
    Urb->UrbIsochronousTransfer.StartFrame = 0;

    for (Index = 0; Index < PACKET_COUNT; Index++)
    {
        Urb->UrbIsochronousTransfer.IsoPacket[Index].Offset = Index * PinContext->DeviceExtension->InterfaceInfo->Pipes[0].MaximumPacketSize;
    }
}


VOID
NTAPI
CaptureAvoidPipeStarvationWorker(
    _In_ PVOID Context)
{
    PKSPIN Pin;
    PPIN_CONTEXT PinContext;
    KIRQL OldLevel;
    PLIST_ENTRY CurEntry;
    PIRP Irp;

    /* get pin */
    Pin = Context;

    /* get pin context */
    PinContext = Pin->Context;

    /* acquire spin lock */
    KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);

    if (!IsListEmpty(&PinContext->IrpListHead))
    {
        /* sanity check */
        ASSERT(!IsListEmpty(&PinContext->IrpListHead));

        /* remove entry from list */
        CurEntry = RemoveHeadList(&PinContext->IrpListHead);

        /* release lock */
        KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

        /* get irp offset */
        Irp = (PIRP)CONTAINING_RECORD(CurEntry, IRP, Tail.Overlay.ListEntry);

        /* reinitialize irp and urb */
        CaptureInitializeUrbAndIrp(Pin, Irp);

        KsDecrementCountedWorker(PinContext->StarvationWorker);

        /* call driver */
        IoCallDriver(PinContext->DeviceExtension->LowerDevice, Irp);
    }
    else
    {
        /* release lock */
        KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

        KsDecrementCountedWorker(PinContext->StarvationWorker);
    }
}



NTSTATUS
InitCapturePin(
    IN PKSPIN Pin)
{
    NTSTATUS Status;
    ULONG Index;
    ULONG BufferSize;
    ULONG MaximumPacketSize;
    PIRP Irp;
    PURB Urb;
    PPIN_CONTEXT PinContext;
    PIO_STACK_LOCATION IoStack;
    PKSALLOCATOR_FRAMING_EX Framing;
    PKSGATE Gate;


    /* set sample rate */
    Status = UsbAudioSetFormat(Pin);
    if (!NT_SUCCESS(Status))
    {
        /* failed */
        return Status;
    }

    /* get pin context */
    PinContext = Pin->Context;

    /* lets get maximum packet size */
    MaximumPacketSize = GetMaxPacketSizeForInterface(PinContext->DeviceExtension->ConfigurationDescriptor, PinContext->InterfaceDescriptor, Pin->DataFlow);
    PinContext->MaxPacketSize = MaximumPacketSize;

    /* initialize work item for capture worker */
    ExInitializeWorkItem(&PinContext->CaptureWorkItem, CaptureGateOnWorkItem, (PVOID)Pin);

    /* register worker */
    Status = KsRegisterCountedWorker(CriticalWorkQueue, &PinContext->CaptureWorkItem, &PinContext->CaptureWorker);
    if (!NT_SUCCESS(Status))
    {
        /* failed */
        return Status;
    }

    /* initialize work item */
    ExInitializeWorkItem(&PinContext->StarvationWorkItem, CaptureAvoidPipeStarvationWorker, (PVOID)Pin);

    /* register worker */
    Status = KsRegisterCountedWorker(CriticalWorkQueue, &PinContext->StarvationWorkItem, &PinContext->StarvationWorker);
    if (!NT_SUCCESS(Status))
    {
        /* failed */
        KsUnregisterWorker(PinContext->CaptureWorker);
    }

    /* lets edit framing struct */
    Framing = (PKSALLOCATOR_FRAMING_EX)Pin->Descriptor->AllocatorFraming;
    Framing->FramingItem[0].PhysicalRange.MinFrameSize =
        Framing->FramingItem[0].PhysicalRange.MaxFrameSize =
        Framing->FramingItem[0].FramingRange.Range.MinFrameSize =
        Framing->FramingItem[0].FramingRange.Range.MaxFrameSize =
    MaximumPacketSize;

    /* calculate buffer size 8 irps * 10 iso packets * max packet size */
    BufferSize = 8 * PACKET_COUNT * MaximumPacketSize;

    /* allocate pin capture buffer */
    PinContext->BufferSize = BufferSize;
    PinContext->Buffer = AllocFunction(BufferSize);
    if (!PinContext->Buffer)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KsAddItemToObjectBag(Pin->Bag, PinContext->Buffer, ExFreePool);

    /* init irps */
    for (Index = 0; Index < 8; Index++)
    {
        /* allocate irp */
        Irp = AllocFunction(IoSizeOfIrp(PinContext->DeviceExtension->LowerDevice->StackSize));
        if (!Irp)
        {
            /* no memory */
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* initialize irp */
        IoInitializeIrp(Irp, IoSizeOfIrp(PinContext->DeviceExtension->LowerDevice->StackSize), PinContext->DeviceExtension->LowerDevice->StackSize);

        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        Irp->IoStatus.Information = 0;
        Irp->Flags = 0;
        Irp->UserBuffer = NULL;

        IoStack = IoGetNextIrpStackLocation(Irp);
        IoStack->DeviceObject = PinContext->DeviceExtension->LowerDevice;
        IoStack->Parameters.Others.Argument2 = NULL;
        IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

        IoSetCompletionRoutine(Irp, UsbAudioCaptureComplete, Pin, TRUE, TRUE, TRUE);

        /* insert into irp list */
        InsertTailList(&PinContext->IrpListHead, &Irp->Tail.Overlay.ListEntry);

        /* add to object bag*/
        KsAddItemToObjectBag(Pin->Bag, Irp, ExFreePool);

        /* Select pipe matching the data flow direction */
        {
            ULONG PipeIndex;
            USBD_PIPE_HANDLE PipeHandle = NULL;

            for (PipeIndex = 0; PipeIndex < PinContext->InterfaceDescriptor->bNumEndpoints; PipeIndex++)
            {
                if (PinContext->DeviceExtension->InterfaceInfo->Pipes[PipeIndex].PipeType == UsbdPipeTypeIsochronous)
                {
                    PipeHandle = PinContext->DeviceExtension->InterfaceInfo->Pipes[PipeIndex].PipeHandle;
                    break;
                }
            }

            if (!PipeHandle)
            {
                /* fallback to first pipe */
                PipeHandle = PinContext->DeviceExtension->InterfaceInfo->Pipes[0].PipeHandle;
            }

            Status = UsbAudioAllocCaptureUrbIso(PipeHandle,
                                                MaximumPacketSize,
                                                &PinContext->Buffer[MaximumPacketSize * PACKET_COUNT * Index],
                                                MaximumPacketSize * PACKET_COUNT,
                                                &Urb);
        }

        if (NT_SUCCESS(Status))
        {
            /* get next stack location */
            IoStack = IoGetNextIrpStackLocation(Irp);

            /* store urb */
            IoStack->Parameters.Others.Argument1 = Urb;
            Irp->Tail.Overlay.DriverContext[0] = Urb;
        }
        else
        {
            /* failed */
            return Status;
        }
    }

    /* get process control gate */
    Gate = KsPinGetAndGate(Pin);

    /* turn input off */
    KsGateTurnInputOff(Gate);

    return Status;
}

NTSTATUS
InitStreamPin(
    IN PKSPIN Pin)
{
    ULONG Index;
    PIRP Irp;
    PPIN_CONTEXT PinContext;
    PKSDATAFORMAT_WAVEFORMATEX WaveFormatEx;
    PIO_STACK_LOCATION IoStack;

    /* get pin context */
    PinContext = Pin->Context;

    /* allocate 1 sec buffer */
    WaveFormatEx = (PKSDATAFORMAT_WAVEFORMATEX)Pin->ConnectionFormat;
    PinContext->Buffer = AllocFunction(WaveFormatEx->WaveFormatEx.nAvgBytesPerSec);
    if (!PinContext->Buffer)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* init buffer size*/
    PinContext->BufferSize = WaveFormatEx->WaveFormatEx.nAvgBytesPerSec;
    PinContext->BufferOffset = 0;
    PinContext->BufferLength = 0;

    /* Select the correct isochronous OUT pipe for render */
    {
        ULONG PipeIndex;

        PinContext->DataPipeHandle = NULL;
        for (PipeIndex = 0;
             PipeIndex < PinContext->InterfaceDescriptor->bNumEndpoints;
             PipeIndex++)
        {
            if (PinContext->DeviceExtension->InterfaceInfo->Pipes[PipeIndex].PipeType == UsbdPipeTypeIsochronous)
            {
                PinContext->DataPipeHandle =
                    PinContext->DeviceExtension->InterfaceInfo->Pipes[PipeIndex].PipeHandle;
                PinContext->MaxPacketSize =
                    PinContext->DeviceExtension->InterfaceInfo->Pipes[PipeIndex].MaximumPacketSize;
                break;
            }
        }

        if (!PinContext->DataPipeHandle)
        {
            PinContext->DataPipeHandle =
                PinContext->DeviceExtension->InterfaceInfo->Pipes[0].PipeHandle;
            PinContext->MaxPacketSize =
                PinContext->DeviceExtension->InterfaceInfo->Pipes[0].MaximumPacketSize;
        }
    }

    /* init irps */
    for (Index = 0; Index < 12; Index++)
    {
        /* allocate irp */
        Irp = AllocFunction(IoSizeOfIrp(PinContext->DeviceExtension->LowerDevice->StackSize));
        if (!Irp)
        {
            /* no memory */
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* initialize irp */
        IoInitializeIrp(Irp, IoSizeOfIrp(PinContext->DeviceExtension->LowerDevice->StackSize), PinContext->DeviceExtension->LowerDevice->StackSize);

        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        Irp->IoStatus.Information = 0;
        Irp->Flags = 0;
        Irp->UserBuffer = NULL;

        IoStack = IoGetNextIrpStackLocation(Irp);
        IoStack->DeviceObject = PinContext->DeviceExtension->LowerDevice;
        IoStack->Parameters.Others.Argument2 = NULL;
        IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

        IoSetCompletionRoutine(Irp, UsbAudioRenderComplete, Pin, TRUE, TRUE, TRUE);

        /* insert into irp list */
        InsertTailList(&PinContext->IrpListHead, &Irp->Tail.Overlay.ListEntry);

        /* add to object bag*/
        KsAddItemToObjectBag(Pin->Bag, Irp, ExFreePool);
    }

    return STATUS_SUCCESS;
}

ULONG
GetDataRangeIndexForFormat(
    IN PKSDATARANGE ConnectionFormat,
    IN const PKSDATARANGE * DataRanges,
    IN ULONG DataRangesCount)
{
    ULONG Index;
    PKSDATARANGE CurrentDataRange;
    PKSDATARANGE_AUDIO CurrentAudioDataRange;
    USB_AUDIO_PCM_FORMAT PcmFormat;

    if (!UsbAudioGetPcmWaveFormat(ConnectionFormat, &PcmFormat))
    {
        /* unsupported connection format */
        DPRINT1("GetDataRangeIndexForFormat expected KSDATARANGE_AUDIO\n");
        return MAXULONG;
    }

    for (Index = 0; Index < DataRangesCount; Index++)
    {
         /* get current data range */
         CurrentDataRange = DataRanges[Index];
         if (!CurrentDataRange ||
             CurrentDataRange->FormatSize < sizeof(KSDATARANGE_AUDIO))
         {
             continue;
         }

         /* compare guids */
         if (!IsEqualGUIDAligned(&CurrentDataRange->MajorFormat, &ConnectionFormat->MajorFormat) ||
             !IsEqualGUIDAligned(&CurrentDataRange->SubFormat, &ConnectionFormat->SubFormat) ||
             !IsEqualGUIDAligned(&CurrentDataRange->Specifier, &ConnectionFormat->Specifier))
         {
             /* no match */
             continue;
         }

         /* all pin data ranges are KSDATARANGE_AUDIO */
         CurrentAudioDataRange = (PKSDATARANGE_AUDIO)CurrentDataRange;

         /* check if number of channel match */
         if (CurrentAudioDataRange->MaximumChannels != PcmFormat.WaveFormat->nChannels)
         {
             /* number of channels mismatch */
             continue;
         }

         if (CurrentAudioDataRange->MinimumSampleFrequency > PcmFormat.WaveFormat->nSamplesPerSec)
         {
             /* channel frequency too low */
             continue;
         }

         if (CurrentAudioDataRange->MaximumSampleFrequency < PcmFormat.WaveFormat->nSamplesPerSec)
         {
             /* channel frequency too high */
             continue;
         }

         /* Verify bit-depth matches the data range */
         if (CurrentAudioDataRange->MaximumBitsPerSample != PcmFormat.ValidBitsPerSample)
         {
             /* bit depth mismatch */
             continue;
         }

         /* Verify sample size matches */
         if (CurrentAudioDataRange->MinimumBitsPerSample != 0 &&
             CurrentAudioDataRange->MinimumBitsPerSample > PcmFormat.ValidBitsPerSample)
         {
             /* bit depth too low for this range */
             continue;
         }

         if (CurrentDataRange->SampleSize != 0 &&
             CurrentDataRange->SampleSize != PcmFormat.WaveFormat->nBlockAlign)
         {
             /* USB alternate setting uses a different container size */
             continue;
         }

         return Index;
    }

    /* no datarange found */
    return MAXULONG;
}

NTSTATUS
NTAPI
USBAudioPinCreate(
    _In_ PKSPIN Pin,
    _In_ PIRP Irp)
{
    PKSFILTER Filter;
    PFILTER_CONTEXT FilterContext;
    PPIN_CONTEXT PinContext;
    NTSTATUS Status;
    ULONG FormatIndex;

    Filter = KsPinGetParentFilter(Pin);
    if (Filter == NULL)
    {
        /* invalid parameter */
        return STATUS_INVALID_PARAMETER;
    }

    /* get filter context */
    FilterContext = Filter->Context;

    /* allocate pin context */
    PinContext = AllocFunction(sizeof(PIN_CONTEXT));
    if (!PinContext)
    {
        /* no memory*/
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* init pin context */
    PinContext->DeviceExtension = FilterContext->DeviceExtension;
    PinContext->LowerDevice = FilterContext->LowerDevice;
    InitializeListHead(&PinContext->IrpListHead);
    InitializeListHead(&PinContext->DoneIrpListHead);
    KeInitializeSpinLock(&PinContext->IrpListLock);

    /* store pin context*/
    Pin->Context = PinContext;

    Status = KsAddItemToObjectBag(Pin->Bag, PinContext, ExFreePool);
    if (!NT_SUCCESS(Status))
    {
        Pin->Context = NULL;
        FreeFunction(PinContext);
        return Status;
    }

    /* lets edit allocator framing struct */
    Status = _KsEdit(Pin->Bag, (PVOID*)&Pin->Descriptor, sizeof(KSPIN_DESCRIPTOR_EX), sizeof(KSPIN_DESCRIPTOR_EX), USBAUDIO_TAG);
    if (NT_SUCCESS(Status))
    {
        Status = _KsEdit(Pin->Bag, (PVOID*)&Pin->Descriptor->AllocatorFraming, sizeof(KSALLOCATOR_FRAMING_EX), sizeof(KSALLOCATOR_FRAMING_EX), USBAUDIO_TAG);
        ASSERT(Status == STATUS_SUCCESS);
    }

    /* choose correct dataformat */
    FormatIndex = GetDataRangeIndexForFormat(Pin->ConnectionFormat, Pin->Descriptor->PinDescriptor.DataRanges, Pin->Descriptor->PinDescriptor.DataRangesCount);
    if (FormatIndex == MAXULONG)
    {
        /* no format match */
        DPRINT1("USBAudioPinCreate: no data range for pin %lu format %p\n",
                Pin->Id,
                Pin->ConnectionFormat);
        return STATUS_NO_MATCH;
    }

    /* select streaming interface */
    Status = USBAudioSelectAudioStreamingInterface(Pin, PinContext, PinContext->DeviceExtension, PinContext->DeviceExtension->ConfigurationDescriptor, FormatIndex);
    if (!NT_SUCCESS(Status))
    {
        /* failed */
        DPRINT1("USBAudioSelectAudioStreamingInterface failed with %x\n", Status);
        return Status;
    }

    if (Pin->DataFlow == KSPIN_DATAFLOW_OUT)
    {
        /* init capture pin */
        Status = InitCapturePin(Pin);
    }
    else
    {
        /* audio streaming pin*/
        Status = InitStreamPin(Pin);
    }

    return Status;
}

NTSTATUS
NTAPI
USBAudioPinClose(
    _In_ PKSPIN Pin,
    _In_ PIRP Irp)
{
    PPIN_CONTEXT PinContext = Pin->Context;

    UNREFERENCED_PARAMETER(Irp);

    /* Stop any in-flight IRPs by draining the ready list */
    if (PinContext)
    {
        KIRQL OldLevel;
        PLIST_ENTRY Entry;
        PIRP PendingIrp;

        KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);
        while (!IsListEmpty(&PinContext->IrpListHead))
        {
            Entry = RemoveHeadList(&PinContext->IrpListHead);
            PendingIrp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
            /* Cancel the IRP if still active */
            if (PendingIrp->Cancel)
                IoCancelIrp(PendingIrp);
        }
        while (!IsListEmpty(&PinContext->DoneIrpListHead))
        {
            Entry = RemoveHeadList(&PinContext->DoneIrpListHead);
        }
        KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

        /* Unregister workers */
        if (PinContext->CaptureWorker)
        {
            KsUnregisterWorker(PinContext->CaptureWorker);
            PinContext->CaptureWorker = NULL;
        }
        if (PinContext->StarvationWorker)
        {
            KsUnregisterWorker(PinContext->StarvationWorker);
            PinContext->StarvationWorker = NULL;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
UsbAudioRenderComplete(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    PKSPIN Pin;
    PPIN_CONTEXT PinContext;
    KIRQL OldLevel;
    PKSSTREAM_POINTER StreamPointerClone;
    NTSTATUS Status;
    PURB Urb;

    /* get pin context */
    Pin = Context;
    PinContext = Pin->Context;

    /* get status */
    Status = Irp->IoStatus.Status;

    /* get streampointer */
    StreamPointerClone = Irp->Tail.Overlay.DriverContext[1];

    /* get urb */
    Urb = Irp->Tail.Overlay.DriverContext[0];

    /* and free it */
    FreeFunction(Urb);

    /* acquire lock */
    KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);

    /* insert entry into ready list */
    InsertTailList(&PinContext->IrpListHead, &Irp->Tail.Overlay.ListEntry);

    /* release lock */
    KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

    if (!NT_SUCCESS(Status) && StreamPointerClone)
    {
        /* set status code because it failed */
        KsStreamPointerSetStatusCode(StreamPointerClone, STATUS_DEVICE_DATA_ERROR);
        DPRINT1("UsbAudioRenderComplete failed with %x\n", Status);
    }

    if (StreamPointerClone)
    {
        /* lets delete the stream pointer clone */
        KsStreamPointerDelete(StreamPointerClone);
    }

    /* done */
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NTAPI
UsbAudioCaptureComplete(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    PKSPIN Pin;
    PPIN_CONTEXT PinContext;
    KIRQL OldLevel;
    PURB Urb;

    /* get pin context */
    Pin = Context;
    PinContext = Pin->Context;

    /* get urb */
    Urb = Irp->Tail.Overlay.DriverContext[0];

    /* acquire lock */
    KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);

    if (!NT_SUCCESS(Urb->UrbIsochronousTransfer.Hdr.Status))
    {
        //DPRINT("UsbAudioCaptureComplete Irp %p Urb %p Status %x Packet Status %x\n", Irp, Urb, Urb->UrbIsochronousTransfer.Hdr.Status, Urb->UrbIsochronousTransfer.IsoPacket[0].Status);

        /* insert entry into ready list */
        InsertTailList(&PinContext->IrpListHead, &Irp->Tail.Overlay.ListEntry);

        /* release lock */
        KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

        KsIncrementCountedWorker(PinContext->StarvationWorker);
    }
    else
    {
        /* insert entry into done list */
        InsertTailList(&PinContext->DoneIrpListHead, &Irp->Tail.Overlay.ListEntry);

        /* release lock */
        KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

        KsIncrementCountedWorker(PinContext->CaptureWorker);
    }

    /* done */
    return STATUS_MORE_PROCESSING_REQUIRED;
}

PIRP
PinGetIrpFromReadyList(
    IN PKSPIN Pin)
{
    PPIN_CONTEXT PinContext;
    PLIST_ENTRY CurEntry;
    KIRQL OldLevel;
    PIRP Irp = NULL;

    /* get pin context */
    PinContext = Pin->Context;

    /* acquire spin lock */
    KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);

    if (!IsListEmpty(&PinContext->IrpListHead))
    {
        /* remove entry from list */
        CurEntry = RemoveHeadList(&PinContext->IrpListHead);

        /* get irp offset */
        Irp = (PIRP)CONTAINING_RECORD(CurEntry, IRP, Tail.Overlay.ListEntry);
    }

    /* release lock */
    KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

    return Irp;
}

static
ULONG
UsbAudioGetIsoDataLength(
    IN PURB Urb)
{
    ULONG Index;
    ULONG Length = 0;

    for (Index = 0; Index < Urb->UrbIsochronousTransfer.NumberOfPackets; Index++)
    {
        if (USBD_SUCCESS(Urb->UrbIsochronousTransfer.IsoPacket[Index].Status))
            Length += Urb->UrbIsochronousTransfer.IsoPacket[Index].Length;
    }

    return Length;
}

static
ULONG
UsbAudioCopyIsoData(
    IN PURB Urb,
    IN ULONG SourceOffset,
    OUT PUCHAR Buffer,
    IN ULONG BufferLength)
{
    PUCHAR TransferBuffer;
    ULONG TransferLength;
    ULONG Index;
    ULONG Copied = 0;

    TransferBuffer = Urb->UrbIsochronousTransfer.TransferBuffer;
    TransferLength = Urb->UrbIsochronousTransfer.TransferBufferLength;

    for (Index = 0;
         Index < Urb->UrbIsochronousTransfer.NumberOfPackets && Copied < BufferLength;
         Index++)
    {
        PUSBD_ISO_PACKET_DESCRIPTOR Packet;
        ULONG PacketLength;
        ULONG PacketOffset;
        ULONG CopyLength;

        Packet = &Urb->UrbIsochronousTransfer.IsoPacket[Index];
        if (!USBD_SUCCESS(Packet->Status) || Packet->Length == 0)
            continue;

        PacketOffset = Packet->Offset;
        PacketLength = Packet->Length;
        if (PacketOffset > TransferLength)
            continue;

        if (PacketLength > TransferLength - PacketOffset)
            PacketLength = TransferLength - PacketOffset;

        if (SourceOffset >= PacketLength)
        {
            SourceOffset -= PacketLength;
            continue;
        }

        CopyLength = min(BufferLength - Copied, PacketLength - SourceOffset);
        RtlCopyMemory(Buffer + Copied,
                      TransferBuffer + PacketOffset + SourceOffset,
                      CopyLength);

        Copied += CopyLength;
        SourceOffset = 0;
    }

    return Copied;
}

NTSTATUS
PinRenderProcess(
    IN PKSPIN Pin)
{
    PKSSTREAM_POINTER LeadingStreamPointer;
    PKSSTREAM_POINTER CloneStreamPointer;
    NTSTATUS Status;
    PPIN_CONTEXT PinContext;
    ULONG PacketCount, TotalPacketSize, Offset;
    PKSDATAFORMAT_WAVEFORMATEX WaveFormatEx;
    PUCHAR TransferBuffer;
    PIRP Irp = NULL;

            //DPRINT("PinRenderProcess\n");

    LeadingStreamPointer = KsPinGetLeadingEdgeStreamPointer(Pin, KSSTREAM_POINTER_STATE_LOCKED);
    if (LeadingStreamPointer == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (NULL == LeadingStreamPointer->StreamHeader->Data)
    {
        Status = KsStreamPointerAdvance(LeadingStreamPointer);
    }


    /* get pin context */
    PinContext = Pin->Context;

    /* get irp from ready list */
    Irp = PinGetIrpFromReadyList(Pin);

    if (!Irp)
    {
        /* no irps available — normal back-pressure, stream will retry */
        KsStreamPointerUnlock(LeadingStreamPointer, TRUE);
        return STATUS_SUCCESS;
    }

    /* clone stream pointer */
    Status = KsStreamPointerClone(LeadingStreamPointer, NULL, 0, &CloneStreamPointer);
    if (!NT_SUCCESS(Status))
    {
        /* failed */
        KsStreamPointerUnlock(LeadingStreamPointer, TRUE);
        DPRINT1("PinRenderProcess: stream clone failed, IRP dropped %p\n", Irp);
        return STATUS_SUCCESS;
    }

    /* calculate packet count based on sample rate */
    WaveFormatEx = (PKSDATAFORMAT_WAVEFORMATEX)Pin->ConnectionFormat;
    TotalPacketSize = WaveFormatEx->WaveFormatEx.nAvgBytesPerSec / 1000;

    /* init transfer buffer*/
    TransferBuffer = CloneStreamPointer->StreamHeader->Data;

    Offset = 0;

    /* are there bytes from previous request*/
    if (PinContext->BufferLength)
    {
        ASSERT(PinContext->BufferLength < TotalPacketSize);

        /* calculate offset*/
        Offset = TotalPacketSize - PinContext->BufferLength;

        if (PinContext->BufferOffset + TotalPacketSize >= PinContext->BufferSize)
        {
            RtlMoveMemory(PinContext->Buffer, &PinContext->Buffer[PinContext->BufferOffset - PinContext->BufferLength], PinContext->BufferLength);
            PinContext->BufferOffset = PinContext->BufferLength;
        }

        /* copy audio bytes */
        RtlCopyMemory(&PinContext->Buffer[PinContext->BufferOffset], TransferBuffer, Offset);

        /* init irp*/
        Status = RenderInitializeUrbAndIrp(Pin, PinContext, Irp, &PinContext->Buffer[PinContext->BufferOffset-PinContext->BufferLength], TotalPacketSize, TotalPacketSize);
        if (NT_SUCCESS(Status))
        {
            /* render audio bytes */
            Status = IoCallDriver(PinContext->LowerDevice, Irp);
        }
        else
        {
            ASSERT(FALSE);
        }

        PinContext->BufferLength = 0;
        PinContext->BufferOffset += Offset;

        /* get new irp from ready list */
        Irp = PinGetIrpFromReadyList(Pin);
        ASSERT(Irp);

    }

    /* calculate full packet count for the remaining buffer */
    PacketCount = (CloneStreamPointer->OffsetIn.Remaining - Offset) / TotalPacketSize;

    Status = RenderInitializeUrbAndIrp(Pin, PinContext, Irp, &TransferBuffer[Offset], PacketCount * TotalPacketSize, TotalPacketSize);
    if (NT_SUCCESS(Status))
    {
        /* store in irp context */
        Irp->Tail.Overlay.DriverContext[1] = CloneStreamPointer;

        if ((PacketCount * TotalPacketSize) + Offset < CloneStreamPointer->OffsetIn.Remaining)
        {
            /* calculate remaining buffer bytes */
            PinContext->BufferLength = CloneStreamPointer->OffsetIn.Remaining - ((PacketCount * TotalPacketSize) + Offset);

            /* check for overflow */
            if (PinContext->BufferOffset + TotalPacketSize >= PinContext->BufferSize)
            {
                /* reset buffer offset*/
                PinContext->BufferOffset = 0;
            }
            RtlCopyMemory(&PinContext->Buffer[PinContext->BufferOffset], &TransferBuffer[(PacketCount * TotalPacketSize) + Offset], PinContext->BufferLength);
            PinContext->BufferOffset += PinContext->BufferLength;
        }

        /* render audio bytes */
        Status = IoCallDriver(PinContext->LowerDevice, Irp);
    }


    /* unlock stream pointer and finish*/
    KsStreamPointerUnlock(LeadingStreamPointer, TRUE);
    return STATUS_PENDING;
}

NTSTATUS
PinCaptureProcess(
    IN PKSPIN Pin)
{
    PKSSTREAM_POINTER LeadingStreamPointer;
    KIRQL OldLevel;
    PPIN_CONTEXT PinContext;
    PLIST_ENTRY CurEntry;
    PIRP Irp;
    PURB Urb;
    PUCHAR TransferBuffer, OutBuffer;
    ULONG Offset, Length, AvailableLength, RemainingLength;
    NTSTATUS Status;
    PKSGATE Gate;

    //DPRINT1("PinCaptureProcess\n");
    LeadingStreamPointer = KsPinGetLeadingEdgeStreamPointer(Pin, KSSTREAM_POINTER_STATE_LOCKED);
    if (LeadingStreamPointer == NULL)
    {
        /* get process control gate */
        Gate = KsPinGetAndGate(Pin);

        /* shutdown processing */
        KsGateTurnInputOff(Gate);

        return STATUS_SUCCESS;
    }

    /* get pin context */
    PinContext = Pin->Context;

    /* acquire spin lock */
    KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);

    while (!IsListEmpty(&PinContext->DoneIrpListHead))
    {
        /* remove entry from list */
        CurEntry = RemoveHeadList(&PinContext->DoneIrpListHead);

        /* release lock */
        KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

        /* get irp offset */
        Irp = (PIRP)CONTAINING_RECORD(CurEntry, IRP, Tail.Overlay.ListEntry);

        /* get urb from irp */
        Urb = (PURB)Irp->Tail.Overlay.DriverContext[0];
        ASSERT(Urb);

        Offset = PtrToUlong(Irp->Tail.Overlay.DriverContext[1]);

        /* get transfer buffer */
        TransferBuffer = Urb->UrbIsochronousTransfer.TransferBuffer;
        UNREFERENCED_PARAMETER(TransferBuffer);

        /* get target buffer */
        OutBuffer = (PUCHAR)LeadingStreamPointer->StreamHeader->Data;

        AvailableLength = UsbAudioGetIsoDataLength(Urb);
        if (Offset >= AvailableLength)
        {
            Irp->Tail.Overlay.DriverContext[1] = NULL;

            KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);
            InsertTailList(&PinContext->IrpListHead,
                           &Irp->Tail.Overlay.ListEntry);
            continue;
        }

        RemainingLength = LeadingStreamPointer->OffsetOut.Remaining;
        Length = min(RemainingLength, AvailableLength - Offset);
        Length = UsbAudioCopyIsoData(Urb,
                                     Offset,
                                     OutBuffer + LeadingStreamPointer->StreamHeader->DataUsed,
                                     Length);

        if (Length == 0)
        {
            Irp->Tail.Overlay.DriverContext[1] = NULL;

            KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);
            InsertTailList(&PinContext->IrpListHead,
                           &Irp->Tail.Overlay.ListEntry);
            continue;
        }

        /* adjust streampointer */
        LeadingStreamPointer->StreamHeader->DataUsed += Length;
        Offset += Length;

        if (Length == RemainingLength)
        {
            KsStreamPointerAdvanceOffsetsAndUnlock(LeadingStreamPointer, 0, Length, TRUE);

            /* acquire spin lock */
            KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);

            if (Offset < AvailableLength)
            {
                /* keep the remaining captured bytes for the next stream header */
                Irp->Tail.Overlay.DriverContext[1] = UlongToPtr(Offset);
                InsertHeadList(&PinContext->DoneIrpListHead,
                               &Irp->Tail.Overlay.ListEntry);
            }
            else
            {
                Irp->Tail.Overlay.DriverContext[1] = NULL;
                InsertTailList(&PinContext->IrpListHead,
                               &Irp->Tail.Overlay.ListEntry);
            }

            /* release lock */
            KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

            LeadingStreamPointer = KsPinGetLeadingEdgeStreamPointer(Pin, KSSTREAM_POINTER_STATE_LOCKED);
            if (LeadingStreamPointer == NULL)
            {
                /* no more work to be done*/
                return STATUS_PENDING;
            }
            else
            {
                /* resume work on this irp */
                continue;
            }
        }
        else
        {
            Status = KsStreamPointerAdvanceOffsets(LeadingStreamPointer, 0, Length, FALSE);
            NT_ASSERT(NT_SUCCESS(Status));
        }


        /* acquire spin lock */
        KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);

        Irp->Tail.Overlay.DriverContext[1] = NULL;
        InsertTailList(&PinContext->IrpListHead, &Irp->Tail.Overlay.ListEntry);
    }

    while (!IsListEmpty(&PinContext->IrpListHead))
    {
        /* remove entry from list */
        CurEntry = RemoveHeadList(&PinContext->IrpListHead);

        /* release lock */
        KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

        /* get irp offset */
        Irp = (PIRP)CONTAINING_RECORD(CurEntry, IRP, Tail.Overlay.ListEntry);

        /* reinitialize irp and urb */
        CaptureInitializeUrbAndIrp(Pin, Irp);

        IoCallDriver(PinContext->DeviceExtension->LowerDevice, Irp);

        /* acquire spin lock */
        KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);

    }

    /* release lock */
    KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

    if (LeadingStreamPointer != NULL)
        KsStreamPointerUnlock(LeadingStreamPointer, FALSE);

    /* get process control gate */
    Gate = KsPinGetAndGate(Pin);

    /* shutdown processing */
    KsGateTurnInputOff(Gate);

    return STATUS_PENDING;
}


NTSTATUS
NTAPI
USBAudioPinProcess(
    _In_ PKSPIN Pin)
{
    NTSTATUS Status;

    if (Pin->DataFlow == KSPIN_DATAFLOW_OUT)
    {
        Status = PinCaptureProcess(Pin);
    }
    else
    {
        Status = PinRenderProcess(Pin);
    }

    return Status;
}


VOID
NTAPI
USBAudioPinReset(
    _In_ PKSPIN Pin)
{
    PPIN_CONTEXT PinContext = Pin->Context;

    if (PinContext)
    {
        /* Reset buffer tracking state */
        PinContext->BufferOffset = 0;
        PinContext->BufferLength = 0;

        /* Reinitialize all IRPs in the ready list */
        if (Pin->DataFlow == KSPIN_DATAFLOW_OUT)
        {
            KIRQL OldLevel;
            PLIST_ENTRY Entry;
            PIRP Irp;

            KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);
            while (!IsListEmpty(&PinContext->IrpListHead))
            {
                Entry = RemoveHeadList(&PinContext->IrpListHead);
                KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

                Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
                CaptureInitializeUrbAndIrp(Pin, Irp);

                IoCallDriver(PinContext->DeviceExtension->LowerDevice, Irp);

                KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);
            }
            KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);
        }
    }
}

NTSTATUS
NTAPI
USBAudioPinSetDataFormat(
    _In_ PKSPIN Pin,
    _In_opt_ PKSDATAFORMAT OldFormat,
    _In_opt_ PKSMULTIPLE_ITEM OldAttributeList,
    _In_ const KSDATARANGE* DataRange,
    _In_opt_ const KSATTRIBUTE_LIST* AttributeRange)
{
    if (OldFormat == NULL)
    {
        /* First-time format set — accept without validation.
         * Format will be validated during stream creation. */
        return STATUS_SUCCESS;
    }

    return UsbAudioSetFormat(Pin);
}

NTSTATUS
StartCaptureIsocTransfer(
    IN PKSPIN Pin)
{
    PPIN_CONTEXT PinContext;
    PLIST_ENTRY CurEntry;
    PIRP Irp;
    KIRQL OldLevel;

    /* get pin context */
    PinContext = Pin->Context;

    /* acquire spin lock */
    KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);

    while(!IsListEmpty(&PinContext->IrpListHead))
    {
        /* remove entry from list */
        CurEntry = RemoveHeadList(&PinContext->IrpListHead);

        /* get irp offset */
        Irp = (PIRP)CONTAINING_RECORD(CurEntry, IRP, Tail.Overlay.ListEntry);

        /* release lock */
        KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

        /* reinitialize irp and urb */
        CaptureInitializeUrbAndIrp(Pin, Irp);

        DPRINT("StartCaptureIsocTransfer Irp %p\n", Irp);
        IoCallDriver(PinContext->DeviceExtension->LowerDevice, Irp);

        /* acquire spin lock */
        KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);

    }

    /* release lock */
    KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);

    return STATUS_SUCCESS;
}

NTSTATUS
CapturePinStateChange(
    _In_ PKSPIN Pin,
    _In_ KSSTATE ToState,
    _In_ KSSTATE FromState)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (FromState != ToState)
    {
        if (ToState)
        {
            if (ToState == KSSTATE_PAUSE)
            {
                if (FromState == KSSTATE_RUN)
                {
                    /* wait until pin processing is finished*/
                }
            }
            else
            {
                if (ToState == KSSTATE_RUN)
                {
                    Status = StartCaptureIsocTransfer(Pin);
                }
            }
        }
    }
    return Status;
}


NTSTATUS
NTAPI
USBAudioPinSetDeviceState(
    _In_ PKSPIN Pin,
    _In_ KSSTATE ToState,
    _In_ KSSTATE FromState)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PPIN_CONTEXT PinContext;
    PLIST_ENTRY Entry;
    PIRP Irp;
    KIRQL OldLevel;

    if (FromState == ToState)
        return STATUS_SUCCESS;

    PinContext = Pin->Context;

    if (Pin->DataFlow == KSPIN_DATAFLOW_OUT)
    {
        /* Capture pin state transitions */
        if (ToState == KSSTATE_RUN)
        {
            Status = StartCaptureIsocTransfer(Pin);
        }
        else if (ToState == KSSTATE_PAUSE)
        {
            /* Flush pending ISOC URBs and stop the pipe */
            KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);
            while (!IsListEmpty(&PinContext->DoneIrpListHead))
            {
                Entry = RemoveHeadList(&PinContext->DoneIrpListHead);
            }
            while (!IsListEmpty(&PinContext->IrpListHead))
            {
                Entry = RemoveHeadList(&PinContext->IrpListHead);
            }
            KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);
        }
        else if (ToState == KSSTATE_STOP)
        {
            /* Stop all ISOC transfers */
            KeAcquireSpinLock(&PinContext->IrpListLock, &OldLevel);
            while (!IsListEmpty(&PinContext->IrpListHead))
            {
                Entry = RemoveHeadList(&PinContext->IrpListHead);
                Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
                IoCancelIrp(Irp);
            }
            while (!IsListEmpty(&PinContext->DoneIrpListHead))
            {
                Entry = RemoveHeadList(&PinContext->DoneIrpListHead);
            }
            KeReleaseSpinLock(&PinContext->IrpListLock, OldLevel);
        }
    }
    else
    {
        /* Render (streaming) pin state transitions */
        if (ToState == KSSTATE_RUN)
        {
            /* Start streaming — buffer will be consumed in PinRenderProcess */
            PinContext->BufferOffset = 0;
            PinContext->BufferLength = 0;
        }
        else if (ToState == KSSTATE_PAUSE || ToState == KSSTATE_STOP)
        {
            /* Flush remaining buffer */
            PinContext->BufferLength = 0;
        }
    }

    return Status;
}


NTSTATUS
NTAPI
UsbAudioPinDataIntersect(
    _In_  PVOID        Context,
    _In_  PIRP         Irp,
    _In_  PKSP_PIN     Pin,
    _In_  PKSDATARANGE DataRange,
    _In_  PKSDATARANGE MatchingDataRange,
    _In_  ULONG        DataBufferSize,
    _Out_ PVOID        Data,
    _Out_ PULONG       DataSize)
{
    PKSFILTER Filter;
    PKSPIN_DESCRIPTOR_EX PinDescriptor;
    PKSDATAFORMAT_WAVEFORMATEX DataFormat;
    PKSDATAFORMAT DataFormatHeader;
    PWAVEFORMATEXTENSIBLE WaveFormatExt;
    PKSDATARANGE_AUDIO DataRangeAudio;
    ULONG ContainerBitsPerSample;
    ULONG FormatSize;

    /* get filter from irp*/
    Filter = KsGetFilterFromIrp(Irp);
    if (!Filter)
    {
        /* no match*/
        return STATUS_NO_MATCH;
    }

    /* get pin descriptor */
    PinDescriptor = (PKSPIN_DESCRIPTOR_EX)&Filter->Descriptor->PinDescriptors[Pin->PinId];

    /* sanity checks*/
    ASSERT(PinDescriptor->PinDescriptor.DataRangesCount >= 0);
    ASSERT(PinDescriptor->PinDescriptor.DataRanges[0]->FormatSize == sizeof(KSDATARANGE_AUDIO));

    DataRangeAudio = (PKSDATARANGE_AUDIO)MatchingDataRange;
    if (!DataRangeAudio ||
        DataRangeAudio->DataRange.FormatSize < sizeof(KSDATARANGE_AUDIO))
    {
        DataRangeAudio = (PKSDATARANGE_AUDIO)PinDescriptor->PinDescriptor.DataRanges[0];
    }

    if (DataRangeAudio->MaximumChannels == 0 ||
        DataRangeAudio->DataRange.SampleSize == 0 ||
        DataRangeAudio->DataRange.SampleSize % DataRangeAudio->MaximumChannels != 0)
    {
        return STATUS_NO_MATCH;
    }

    ContainerBitsPerSample =
        (DataRangeAudio->DataRange.SampleSize / DataRangeAudio->MaximumChannels) * 8;
    if (ContainerBitsPerSample < DataRangeAudio->MaximumBitsPerSample)
        return STATUS_NO_MATCH;

    FormatSize = (ContainerBitsPerSample == DataRangeAudio->MaximumBitsPerSample) ?
                 sizeof(KSDATAFORMAT_WAVEFORMATEX) :
                 sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE);

    *DataSize = FormatSize;
    if (DataBufferSize < FormatSize)
        return STATUS_BUFFER_OVERFLOW;

    DataFormatHeader = Data;
    DataFormatHeader->FormatSize = FormatSize;
    DataFormatHeader->Flags = 0;
    DataFormatHeader->Reserved = 0;
    DataFormatHeader->MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    DataFormatHeader->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    DataFormatHeader->Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    DataFormatHeader->SampleSize = DataRangeAudio->DataRange.SampleSize;

    if (ContainerBitsPerSample == DataRangeAudio->MaximumBitsPerSample)
    {
        DataFormat = Data;
        DataFormat->WaveFormatEx.wFormatTag = WAVE_FORMAT_PCM;
        DataFormat->WaveFormatEx.nChannels = DataRangeAudio->MaximumChannels;
        DataFormat->WaveFormatEx.nSamplesPerSec = DataRangeAudio->MaximumSampleFrequency;
        DataFormat->WaveFormatEx.nBlockAlign = DataRangeAudio->DataRange.SampleSize;
        DataFormat->WaveFormatEx.nAvgBytesPerSec =
            DataRangeAudio->MaximumSampleFrequency * DataFormat->WaveFormatEx.nBlockAlign;
        DataFormat->WaveFormatEx.wBitsPerSample = (USHORT)ContainerBitsPerSample;
        DataFormat->WaveFormatEx.cbSize = 0;
    }
    else
    {
        WaveFormatExt = (PWAVEFORMATEXTENSIBLE)(DataFormatHeader + 1);
        WaveFormatExt->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        WaveFormatExt->Format.nChannels = DataRangeAudio->MaximumChannels;
        WaveFormatExt->Format.nSamplesPerSec = DataRangeAudio->MaximumSampleFrequency;
        WaveFormatExt->Format.nBlockAlign = DataRangeAudio->DataRange.SampleSize;
        WaveFormatExt->Format.nAvgBytesPerSec =
            DataRangeAudio->MaximumSampleFrequency * WaveFormatExt->Format.nBlockAlign;
        WaveFormatExt->Format.wBitsPerSample = (USHORT)ContainerBitsPerSample;
        WaveFormatExt->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        WaveFormatExt->Samples.wValidBitsPerSample = DataRangeAudio->MaximumBitsPerSample;
        WaveFormatExt->dwChannelMask = KSAUDIO_SPEAKER_DIRECTOUT;
        WaveFormatExt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }

    return STATUS_SUCCESS;
}
