#pragma once

#include <ntddk.h>
#include <portcls.h>
#include <ksmedia.h>
#include <hubbusif.h>
#include <usbbusif.h>
#include <usbioctl.h>
#include <usb.h>
#include <usbdlib.h>
#include <debug.h>

#define USBAUDIO_TAG 'AbsU'

/* ── USB Audio descriptor subtype ──────────────────────────────── */
#define USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE       (0x24)

/* ── USB Audio versions ─────────────────────────────────────────── */
#define USB_AUDIO_VERSION_1                              0x0100
#define USB_AUDIO_VERSION_2                              0x0200
#define USB_AUDIO_VERSION_3                              0x0300

/* ── USB Audio Class 2.0 control-selector units ────────────────── */
#define USB_AUDIO_CONTROL_UNDEFINED                      0x09
#define USB_AUDIO_CONTROL_CLOCK_SOURCE                    0x0A
#define USB_AUDIO_CONTROL_CLOCK_SELECTOR                  0x0B
#define USB_AUDIO_CONTROL_CLOCK_MULTIPLIER                0x0C
#define USB_AUDIO_CONTROL_SAMPLING_RATE                   0x0D
#define USB_AUDIO_CONTROL_EFFECT_UNIT                     0x07
#define USB_AUDIO_CONTROL_PROCESSING_UNIT                 0x08
#define USB_AUDIO_CONTROL_EXTENSION_UNIT                  0x09

/* ── USB Audio sampling rate control requests ──────────────────── */
#define USB_AUDIO_SET_CUR                                0x01
#define USB_AUDIO_GET_CUR                                0x81
#define USB_AUDIO_GET_MIN                                0x82
#define USB_AUDIO_GET_MAX                                0x83
#define USB_AUDIO_GET_RES                                0x84
#define USB_AUDIO_SAMPLING_FREQ_CONTROL                  0x0100
#define USB_AUDIO_MUTE_CONTROL                           0x0100
#define USB_AUDIO_VOLUME_CONTROL                         0x0200

/* ── Terminal type constants ────────────────────────────────────── */
#define USB_AUDIO_STREAMING_TERMINAL_TYPE                (0x0101)

/* Input terminal types (0x02xx) */
#define USB_AUDIO_MICROPHONE_TERMINAL_TYPE               (0x0201)
#define USB_AUDIO_DESKTOP_MICROPHONE_TERMINAL_TYPE       (0x0202)
#define USB_AUDIO_PERSONAL_MICROPHONE_TERMINAL_TYPE      (0x0203)
#define USB_AUDIO_OMMNI_MICROPHONE_TERMINAL_TYPE         (0x0204)
#define USB_AUDIO_ARRAY_MICROPHONE_TERMINAL_TYPE         (0x0205)
#define USB_AUDIO_ARRAY_PROCESSING_MICROPHONE_TERMINAL_TYPE (0x0206)

/* Output terminal types (0x03xx) */
#define USB_AUDIO_SPEAKER_TERMINAL_TYPE                  (0x0301)
#define USB_HEADPHONES_SPEAKER_TERMINAL_TYPE             (0x0302)
#define USB_AUDIO_HMDA_TERMINAL_TYPE                     (0x0303)
#define USB_AUDIO_DESKTOP_SPEAKER_TERMINAL_TYPE          (0x0304)
#define USB_AUDIO_ROOM_SPEAKER_TERMINAL_TYPE             (0x0305)
#define USB_AUDIO_COMMUNICATION_SPEAKER_TERMINAL_TYPE    (0x0306)
#define USB_AUDIO_SUBWOOFER_TERMINAL_TYPE                (0x0307)

/* Bidirectional terminal types (0x04xx) */
#define USB_AUDIO_UNDEFINED_BIDIRECTIONAL_TERMINAL_TYPE  (0x0400)
#define USB_AUDIO_HANDSET_TERMINAL_TYPE                  (0x0401)
#define USB_AUDIO_HEADSET_TERMINAL_TYPE                  (0x0402)
#define USB_AUDIO_SPEAKERPHONE_TERMINAL_TYPE             (0x0403)
#define USB_AUDIO_ECHO_SUPPRESSING_SPEAKERPHONE_TERMINAL_TYPE (0x0404)
#define USB_AUDIO_ECHO_CANCELING_SPEAKERPHONE_TERMINAL_TYPE  (0x0405)

/* Telephony terminal types (0x05xx) */
#define USB_AUDIO_UNDEFINED_TELEPHONY_TERMINAL_TYPE      (0x0500)
#define USB_AUDIO_PHONE_LINE_TERMINAL_TYPE               (0x0501)
#define USB_AUDIO_TELEPHONE_TERMINAL_TYPE                (0x0502)
#define USB_AUDIO_DOWNLINE_PHONE_TERMINAL_TYPE           (0x0503)

/* External terminal types (0x06xx) */
#define USB_AUDIO_UNDEFINED_EXTERNAL_TERMINAL_TYPE       (0x0600)
#define USB_AUDIO_ANALOG_CONNECTOR_TERMINAL_TYPE         (0x0601)
#define USB_AUDIO_DIGITAL_AUDIO_INTERFACE_TERMINAL_TYPE  (0x0602)
#define USB_AUDIO_LINE_CONNECTOR_TERMINAL_TYPE           (0x0603)
#define USB_AUDIO_LEGACY_AUDIO_CONNECTOR_TERMINAL_TYPE   (0x0604)
#define USB_AUDIO_SPDIF_INTERFACE_TERMINAL_TYPE          (0x0605)
#define USB_AUDIO_1394_DA_STREAM_TERMINAL_TYPE           (0x0606)
#define USB_AUDIO_1394_DV_STREAM_SOUNDTRACK_TERMINAL_TYPE (0x0607)
#define USB_AUDIO_ADAT_INTERFACE_TERMINAL_TYPE           (0x0608)
#define USB_AUDIO_TDIF_INTERFACE_TERMINAL_TYPE           (0x0609)
#define USB_AUDIO_MADI_INTERFACE_TERMINAL_TYPE           (0x060A)

/* Embedded function terminal types (0x07xx) */
#define USB_AUDIO_UNDEFINED_EMBEDDED_TERMINAL_TYPE       (0x0700)
#define USB_AUDIO_LEVEL_CALIBRATION_TERMINAL_TYPE        (0x0701)
#define USB_AUDIO_EQUALIZATION_TERMINAL_TYPE             (0x0702)
#define USB_AUDIO_HEADPHONE_NOISE_TERMINAL_TYPE          (0x0703)
#define USB_AUDIO_CROSSTALK_CANCELLATION_TERMINAL_TYPE   (0x0704)
#define USB_AUDIO_ECHO_CANCELLATION_TERMINAL_TYPE        (0x0705)
#define USB_AUDIO_NOISE_SUPPRESSION_TERMINAL_TYPE        (0x0706)
#define USB_AUDIO_VOLUME_CONTROL_TERMINAL_TYPE           (0x0707)
#define USB_AUDIO_ACOUSTIC_ECHO_CANCELLATION_TYPE        (0x0708)

/* Terminal type group masks */
#define USB_AUDIO_TERMINAL_TYPE_GROUP_MASK               (0xFF00)
#define USB_AUDIO_INPUT_TERMINAL_TYPE_GROUP              (0x0200)
#define USB_AUDIO_OUTPUT_TERMINAL_TYPE_GROUP             (0x0300)
#define USB_AUDIO_BIDIRECTIONAL_TERMINAL_TYPE_GROUP      (0x0400)
#define USB_AUDIO_TELEPHONY_TERMINAL_TYPE_GROUP          (0x0500)
#define USB_AUDIO_EXTERNAL_TERMINAL_TYPE_GROUP           (0x0600)
#define USB_AUDIO_EMBEDDED_FUNCTION_TERMINAL_TYPE_GROUP  (0x0700)

#define USB_AUDIO_UNDEFINED_TERMINAL_TYPE                (0xFFFF)

/* ── USB Audio descriptor subtypes ──────────────────────────────── */
#define USB_AUDIO_INPUT_TERMINAL                         (0x02)
#define USB_AUDIO_OUTPUT_TERMINAL                        (0x03)
#define USB_AUDIO_MIXER_UNIT                             (0x04)
#define USB_AUDIO_SELECTOR_UNIT                          (0x05)
#define USB_AUDIO_FEATURE_UNIT                           (0x06)
#define USB_AUDIO_EFFECT_UNIT                            (0x07)
#define USB_AUDIO_PROCESSING_UNIT                        (0x08)
#define USB_AUDIO_EXTENSION_UNIT                         (0x09)

/* ── UAC2 descriptor subtype IDs ────────────────────────────────── */
#define USB_AUDIO_CS_INTERFACE                           0x24
#define USB_AUDIO_CS_ENDPOINT                            0x25
#define USB_AUDIO_EP_GENERAL                             0x01
#define USB_AUDIO_EP_SAMPLING_FREQUENCY_CONTROL          0x01
#define USB_AUDIO_EP_PITCH_CONTROL                       0x02

#define USB_AUDIO_AC_HEADER_UNDEFINED                    0x00
#define USB_AUDIO_AC_INPUT_TERMINAL                      0x02
#define USB_AUDIO_AC_OUTPUT_TERMINAL                     0x03
#define USB_AUDIO_AC_MIXER_UNIT                          0x04
#define USB_AUDIO_AC_SELECTOR_UNIT                       0x05
#define USB_AUDIO_AC_FEATURE_UNIT                        0x06
#define USB_AUDIO_AC_EFFECT_UNIT                         0x07
#define USB_AUDIO_AC_PROCESSING_UNIT                     0x08
#define USB_AUDIO_AC_EXTENSION_UNIT                      0x09
#define USB_AUDIO_AC_CLOCK_SOURCE                        0x0A
#define USB_AUDIO_AC_CLOCK_SELECTOR                      0x0B
#define USB_AUDIO_AC_CLOCK_MULTIPLIER                    0x0C
#define USB_AUDIO_AC_SAMPLE_RATE_CONVERTER               0x0D

/* ── UAC2 Channel Config ───────────────────────────────────────── */
#define UAC2_CHANNEL_CONFIG_NON_PREDEFINED               0x00000000
#define UAC2_CHANNEL_CONFIG_MONO                         0x00000001
#define UAC2_CHANNEL_CONFIG_STEREO                       0x00000003
#define UAC2_CHANNEL_CONFIG_2_1                          0x00000007
#define UAC2_CHANNEL_CONFIG_3_0                          0x00000008
#define UAC2_CHANNEL_CONFIG_3_1                          0x0000000B
#define UAC2_CHANNEL_CONFIG_4_0                          0x00000107
#define UAC2_CHANNEL_CONFIG_5_0                          0x00000037
#define UAC2_CHANNEL_CONFIG_5_1                          0x0000003F
#define UAC2_CHANNEL_CONFIG_6_1                          0x000000FF
#define UAC2_CHANNEL_CONFIG_7_1                          0x000007FF

/* ── Feature unit control bits ──────────────────────────────────── */
#define USB_AUDIO_FU_MUTE                                0x01
#define USB_AUDIO_FU_VOLUME                              0x02
#define USB_AUDIO_FU_BASS                                0x04
#define USB_AUDIO_FU_MID                                 0x08
#define USB_AUDIO_FU_TREBLE                              0x10
#define USB_AUDIO_FU_GRAPHIC_EQUALIZER                   0x20
#define USB_AUDIO_FU_AUTOMATIC_GAIN                      0x40
#define USB_AUDIO_FU_DELAY                               0x80

/* ── KSPROPERTY helpers ─────────────────────────────────────────── */
#define DEFINE_KSPROPERTY_ITEM_AUDIO_VOLUME(Handler, SupportHandler)\
    DEFINE_KSPROPERTY_ITEM(\
        KSPROPERTY_AUDIO_VOLUMELEVEL,\
        (Handler),\
        sizeof(KSNODEPROPERTY_AUDIO_CHANNEL),\
        sizeof(LONG),\
        (Handler), NULL, 0, NULL, (SupportHandler), 0)

#define DEFINE_KSPROPERTY_TABLE_AUDIO_VOLUME(TopologySet, Handler, SupportHandler)\
DEFINE_KSPROPERTY_TABLE(TopologySet) {\
    DEFINE_KSPROPERTY_ITEM_AUDIO_VOLUME(Handler, SupportHandler)\
}

#define DEFINE_KSPROPERTY_ITEM_AUDIO_MUTE(Handler, SupportHandler)\
    DEFINE_KSPROPERTY_ITEM(\
        KSPROPERTY_AUDIO_MUTE,\
        (Handler),\
        sizeof(KSNODEPROPERTY_AUDIO_CHANNEL),\
        sizeof(BOOL),\
        (Handler), NULL, 0, NULL, (SupportHandler), 0)

#define DEFINE_KSPROPERTY_TABLE_AUDIO_MUTE(TopologySet, Handler, SupportHandler)\
DEFINE_KSPROPERTY_TABLE(TopologySet) {\
    DEFINE_KSPROPERTY_ITEM_AUDIO_MUTE(Handler, SupportHandler)\
}

/* ──────────────────────────────────────────────────────────────────
 *  USB Audio Class descriptor structures (pshpack1)
 * ────────────────────────────────────────────────────────────────── */
#include <pshpack1.h>

/* ── UAC1 Control Interface Descriptor (CS_INTERFACE) ───────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    USHORT bcdADC;
    USHORT wTotalLength;
    UCHAR  bInCollection;
    UCHAR  baInterfaceNr[1];
} USB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR,
  *PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR;

/* ── UAC2 Control Interface Descriptor ──────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    USHORT bcdADC;
    UCHAR  bCategory;
    USHORT wTotalLength;
    UCHAR  bmControls;
} USB_AUDIO2_CONTROL_INTERFACE_HEADER_DESCRIPTOR,
  *PUSB_AUDIO2_CONTROL_INTERFACE_HEADER_DESCRIPTOR;

/* ── UAC2 Clock Source Descriptor ───────────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bClockID;
    UCHAR  bmAttributes;
    UCHAR  bmControls;
    UCHAR  bAssocTerminal;
    UCHAR  iClockSource;
} USB_AUDIO2_CLOCK_SOURCE_DESCRIPTOR,
  *PUSB_AUDIO2_CLOCK_SOURCE_DESCRIPTOR;

/* ── UAC2 Clock Selector Descriptor ─────────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bClockID;
    UCHAR  bNrInPins;
    UCHAR  baCSourceID[1];
    UCHAR  bmControls;
    UCHAR  iClockSelector;
} USB_AUDIO2_CLOCK_SELECTOR_DESCRIPTOR,
  *PUSB_AUDIO2_CLOCK_SELECTOR_DESCRIPTOR;

/* ── UAC1 Input Terminal Descriptor ─────────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bTerminalID;
    USHORT wTerminalType;
    UCHAR  bAssocTerminal;
    UCHAR  bNrChannels;
    USHORT wChannelConfig;
    UCHAR  iChannelNames;
    UCHAR  iTerminal;
} USB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR,
  *PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR;

/* ── UAC1 Output Terminal Descriptor ────────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bTerminalID;
    USHORT wTerminalType;
    UCHAR  bAssocTerminal;
    UCHAR  bSourceID;
    UCHAR  iTerminal;
} USB_AUDIO_CONTROL_OUTPUT_TERMINAL_DESCRIPTOR,
  *PUSB_AUDIO_CONTROL_OUTPUT_TERMINAL_DESCRIPTOR;

/* ── UAC2 Input Terminal Descriptor ─────────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bTerminalID;
    USHORT wTerminalType;
    UCHAR  bAssocTerminal;
    UCHAR  bCSourceID;
    UCHAR  bNrChannels;
    ULONG  bmChannelConfig;
    UCHAR  iChannelNames;
    USHORT bmControls;
    UCHAR  iTerminal;
} USB_AUDIO2_INPUT_TERMINAL_DESCRIPTOR,
  *PUSB_AUDIO2_INPUT_TERMINAL_DESCRIPTOR;

/* ── UAC2 Output Terminal Descriptor ────────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bTerminalID;
    USHORT wTerminalType;
    UCHAR  bAssocTerminal;
    UCHAR  bSourceID;
    UCHAR  bCSourceID;
    USHORT bmControls;
    UCHAR  iTerminal;
} USB_AUDIO2_OUTPUT_TERMINAL_DESCRIPTOR,
  *PUSB_AUDIO2_OUTPUT_TERMINAL_DESCRIPTOR;

/* ── UAC1 Feature Unit Descriptor ───────────────────────────────── */
typedef struct
{
    UCHAR bLength;
    UCHAR bDescriptorType;
    UCHAR bDescriptorSubtype;
    UCHAR bUnitID;
    UCHAR bSourceID;
    UCHAR bControlSize;
    UCHAR bmaControls[1];
    UCHAR iFeature;
} USB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR,
  *PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR;

/* ── UAC2 Feature Unit Descriptor ───────────────────────────────── */
typedef struct
{
    UCHAR bLength;
    UCHAR bDescriptorType;
    UCHAR bDescriptorSubtype;
    UCHAR bUnitID;
    UCHAR bSourceID;
    ULONG bmaControls[1];
    UCHAR iFeature;
} USB_AUDIO2_FEATURE_UNIT_DESCRIPTOR,
  *PUSB_AUDIO2_FEATURE_UNIT_DESCRIPTOR;

/* ── Mixer Unit Descriptor ──────────────────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bUnitID;
    UCHAR  bNrInPins;
    UCHAR  baSourceID[1];
    UCHAR  bNrChannels;
    USHORT wChannelConfig;
    UCHAR  iChannelNames;
    UCHAR  bmControls;
    UCHAR  iMixer;
} USB_AUDIO_CONTROL_MIXER_UNIT_DESCRIPTOR,
  *PUSB_AUDIO_CONTROL_MIXER_UNIT_DESCRIPTOR;

/* ── Selector Unit Descriptor ───────────────────────────────────── */
typedef struct
{
    UCHAR bLength;
    UCHAR bDescriptorType;
    UCHAR bDescriptorSubtype;
    UCHAR bUnitID;
    UCHAR bNrInPins;
    UCHAR baSourceID[1];
    UCHAR iSelector;
} USB_AUDIO_CONTROL_SELECTOR_UNIT_DESCRIPTOR,
  *PUSB_AUDIO_CONTROL_SELECTOR_UNIT_DESCRIPTOR;

/* ── UAC1 Streaming Interface Descriptor ────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bTerminalLink;
    UCHAR  bDelay;
    USHORT wFormatTag;
} USB_AUDIO_STREAMING_INTERFACE_DESCRIPTOR,
  *PUSB_AUDIO_STREAMING_INTERFACE_DESCRIPTOR;

/* ── UAC1 Format Type Descriptor ────────────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bFormatType;
    UCHAR  bNrChannels;
    UCHAR  bSubframeSize;
    UCHAR  bBitResolution;
    UCHAR  bSamFreqType;
    UCHAR  tSamFreq[3];
} USB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR,
  *PUSB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR;

/* UAC1 class-specific isochronous audio data endpoint descriptor */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bmAttributes;
    UCHAR  bLockDelayUnits;
    USHORT wLockDelay;
} USB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR,
  *PUSB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR;

/* ── UAC3 BADD profiles ─────────────────────────────────────────── */
#define UAC3_FUNCTION_SUBCLASS_UNDEFINED             0x00
#define UAC3_FUNCTION_SUBCLASS_FULL_ADC_3_0          0x01
#define UAC3_FUNCTION_SUBCLASS_GENERIC_IO            0x20
#define UAC3_FUNCTION_SUBCLASS_HEADPHONE             0x21
#define UAC3_FUNCTION_SUBCLASS_SPEAKER               0x22
#define UAC3_FUNCTION_SUBCLASS_MICROPHONE            0x23
#define UAC3_FUNCTION_SUBCLASS_HEADSET               0x24
#define UAC3_FUNCTION_SUBCLASS_HEADSET_ADAPTER       0x25
#define UAC3_FUNCTION_SUBCLASS_SPEAKERPHONE          0x26

/* ── UAC3 Audio Control Interface Header ────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    USHORT bcdADC;
    UCHAR  bCategory;
    USHORT wTotalLength;
    ULONG  bmControls;
} USB_AUDIO3_CONTROL_INTERFACE_HEADER_DESCRIPTOR,
  *PUSB_AUDIO3_CONTROL_INTERFACE_HEADER_DESCRIPTOR;

/* ── UAC3 Clock Source ──────────────────────────────────────────── */
typedef struct
{
    UCHAR bLength;
    UCHAR bDescriptorType;
    UCHAR bDescriptorSubtype;
    UCHAR bClockID;
    UCHAR bmAttributes;
    UCHAR bmControls;
    UCHAR bReferenceTerminal;
    UCHAR iClockSource;
} USB_AUDIO3_CLOCK_SOURCE_DESCRIPTOR,
  *PUSB_AUDIO3_CLOCK_SOURCE_DESCRIPTOR;

/* ── UAC3 Input Terminal ────────────────────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bTerminalID;
    USHORT wTerminalType;
    UCHAR  bAssocTerminal;
    UCHAR  bCSourceID;
    UCHAR  bNrChannels;
    ULONG  bmChannelConfig;
    USHORT bmControls;
    UCHAR  iTerminal;
} USB_AUDIO3_INPUT_TERMINAL_DESCRIPTOR,
  *PUSB_AUDIO3_INPUT_TERMINAL_DESCRIPTOR;

/* ── UAC3 Output Terminal ───────────────────────────────────────── */
typedef struct
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bDescriptorSubtype;
    UCHAR  bTerminalID;
    USHORT wTerminalType;
    UCHAR  bAssocTerminal;
    UCHAR  bSourceID;
    UCHAR  bCSourceID;
    USHORT bmControls;
    UCHAR  iTerminal;
} USB_AUDIO3_OUTPUT_TERMINAL_DESCRIPTOR,
  *PUSB_AUDIO3_OUTPUT_TERMINAL_DESCRIPTOR;

#include <poppack.h>

/* ──────────────────────────────────────────────────────────────────
 *  Internal structures
 * ────────────────────────────────────────────────────────────────── */

#define USBAUDIO_MAX_NODES_PER_CONTEXT   20

/* ── Device quirk flags ─────────────────────────────────────────── */
#define USBAUDIO_QUIRK_NONE                     0x00000000
#define USBAUDIO_QUIRK_IGNORE_CTL_ERROR         0x00000001 /* ignore USB ctl errors */
#define USBAUDIO_QUIRK_STANDARD_MIXER           0x00000002 /* use standard unit only */
#define USBAUDIO_QUIRK_SKIP_CLOCK_SELECTOR      0x00000004 /* skip clock source */
#define USBAUDIO_QUIRK_NO_FEEDBACK              0x00000008 /* no async feedback */
#define USBAUDIO_QUIRK_NO_VOLUME                0x00000010 /* broken volume */
#define USBAUDIO_QUIRK_NO_MUTE                  0x00000020 /* broken mute */
#define USBAUDIO_QUIRK_FIXED_RATE               0x00000040 /* fixed sample rate */
#define USBAUDIO_QUIRK_DELAYED_REGISTER         0x00000080 /* deferred init */
#define USBAUDIO_QUIRK_USE_CHANNEL_MAP          0x00000100 /* override channel map */

typedef struct _USBAUDIO_DEVICE_QUIRK
{
    USHORT VendorId;
    USHORT ProductId;
    ULONG  Flags;
    const CHAR *Description;
} USBAUDIO_DEVICE_QUIRK, *PUSBAUDIO_DEVICE_QUIRK;

typedef struct _NODE_CONTEXT
{
    PUSB_COMMON_DESCRIPTOR Descriptor;
    ULONG NodeCount;
    ULONG Nodes[USBAUDIO_MAX_NODES_PER_CONTEXT];
} NODE_CONTEXT, *PNODE_CONTEXT;

typedef struct _DEVICE_EXTENSION
{
    PDEVICE_OBJECT LowerDevice;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    PUSB_DEVICE_DESCRIPTOR DeviceDescriptor;
    PUSBD_INTERFACE_INFORMATION InterfaceInfo;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;
    PNODE_CONTEXT NodeContext;
    ULONG NodeContextCount;
    ULONG AudioVersion;                         /* USB Audio 1.0, 2.0, or 3.0 */
    ULONG BaddProfile;                          /* UAC3 BADD profile or 0 */
    ULONG QuirkFlags;                           /* active quirk flags */
    BOOLEAN HasMidiInterface;
    BOOLEAN HasFeedbackEndpoint;
    USBD_PIPE_HANDLE FeedbackPipeHandle;         /* async feedback pipe */
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

typedef struct _FILTER_CONTEXT
{
    PDEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT LowerDevice;
} FILTER_CONTEXT, *PFILTER_CONTEXT;

typedef struct _PIN_CONTEXT
{
    PDEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT LowerDevice;
    LIST_ENTRY IrpListHead;
    LIST_ENTRY DoneIrpListHead;
    KSPIN_LOCK IrpListLock;
    PUCHAR Buffer;
    ULONG BufferSize;
    ULONG BufferOffset;
    ULONG BufferLength;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    WORK_QUEUE_ITEM CaptureWorkItem;
    PKSWORKER       CaptureWorker;
    WORK_QUEUE_ITEM StarvationWorkItem;
    PKSWORKER       StarvationWorker;
    USBD_PIPE_HANDLE DataPipeHandle;            /* correct iso data pipe */
    ULONG            MaxPacketSize;              /* actual max packet size */
} PIN_CONTEXT, *PPIN_CONTEXT;

/* ──────────────────────────────────────────────────────────────────
 *  Forward declarations — filter.c
 * ────────────────────────────────────────────────────────────────── */

NTSTATUS
NTAPI
USBAudioCreateFilterContext(
    _In_ PKSDEVICE Device);

PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR
UsbAudioGetStreamingTerminalDescriptorByIndex(
    _In_ PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    _In_ ULONG Index);

/* ──────────────────────────────────────────────────────────────────
 *  Forward declarations — pool.c
 * ────────────────────────────────────────────────────────────────── */

PVOID
NTAPI
AllocFunction(
    _In_ ULONG ItemSize);

VOID
NTAPI
FreeFunction(
    _In_ PVOID Item);

VOID
NTAPI
CountTerminalUnits(
    _In_ PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    _Out_ PULONG NonStreamingTerminalDescriptorCount,
    _Out_ PULONG TotalTerminalDescriptorCount);

/* ──────────────────────────────────────────────────────────────────
 *  Forward declarations — usbaudio.c
 * ────────────────────────────────────────────────────────────────── */

NTSTATUS
SubmitUrbSync(
    _In_ PDEVICE_OBJECT Device,
    _In_ PURB Urb);

NTSTATUS
NTAPI
USBAudioAddDevice(
    _In_ PKSDEVICE Device);

NTSTATUS
NTAPI
USBAudioPnPStart(
    _In_     PKSDEVICE         Device,
    _In_     PIRP              Irp,
    _In_opt_ PCM_RESOURCE_LIST TranslatedResourceList,
    _In_opt_ PCM_RESOURCE_LIST UntranslatedResourceList);

NTSTATUS
NTAPI
USBAudioPnPQueryStop(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp);

VOID
NTAPI
USBAudioPnPCancelStop(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp);

VOID
NTAPI
USBAudioPnPStop(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp);

NTSTATUS
NTAPI
USBAudioPnPQueryRemove(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp);

VOID
NTAPI
USBAudioPnPCancelRemove(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp);

VOID
NTAPI
USBAudioPnPRemove(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp);

NTSTATUS
NTAPI
USBAudioPnPQueryCapabilities(
    _In_    PKSDEVICE            Device,
    _In_    PIRP                 Irp,
    _Inout_ PDEVICE_CAPABILITIES Capabilities);

VOID
NTAPI
USBAudioPnPSurpriseRemoval(
    _In_ PKSDEVICE Device,
    _In_ PIRP      Irp);

NTSTATUS
NTAPI
USBAudioPnPQueryPower(
    _In_ PKSDEVICE          Device,
    _In_ PIRP               Irp,
    _In_ DEVICE_POWER_STATE DeviceTo,
    _In_ DEVICE_POWER_STATE DeviceFrom,
    _In_ SYSTEM_POWER_STATE SystemTo,
    _In_ SYSTEM_POWER_STATE SystemFrom,
    _In_ POWER_ACTION       Action);

VOID
NTAPI
USBAudioPnPSetPower(
    _In_ PKSDEVICE          Device,
    _In_ PIRP               Irp,
    _In_ DEVICE_POWER_STATE To,
    _In_ DEVICE_POWER_STATE From);

/* ──────────────────────────────────────────────────────────────────
 *  Forward declarations — pin.c
 * ────────────────────────────────────────────────────────────────── */

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
    _Out_ PULONG       DataSize);

NTSTATUS
NTAPI
UsbAudioCaptureComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PVOID          Context);

NTSTATUS
NTAPI
UsbAudioRenderComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PVOID          Context);

NTSTATUS
NTAPI
USBAudioPinCreate(
    _In_ PKSPIN Pin,
    _In_ PIRP   Irp);

NTSTATUS
NTAPI
USBAudioPinClose(
    _In_ PKSPIN Pin,
    _In_ PIRP   Irp);

NTSTATUS
NTAPI
USBAudioPinProcess(
    _In_ PKSPIN Pin);

VOID
NTAPI
USBAudioPinReset(
    _In_ PKSPIN Pin);

NTSTATUS
NTAPI
USBAudioPinSetDataFormat(
    _In_ PKSPIN Pin,
    _In_opt_ PKSDATAFORMAT OldFormat,
    _In_opt_ PKSMULTIPLE_ITEM OldAttributeList,
    _In_ const KSDATARANGE *DataRange,
    _In_opt_ const KSATTRIBUTE_LIST *AttributeRange);

NTSTATUS
NTAPI
USBAudioPinSetDeviceState(
    _In_ PKSPIN Pin,
    _In_ KSSTATE ToState,
    _In_ KSSTATE FromState);
