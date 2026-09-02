/*
 * PROJECT:     ReactOS Raspberry Pi 3 video support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VideoCore MMAL H.264 codec transport
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <windows.h>
#include <winerror.h>
#include <stdint.h>
#include <string.h>

#include "rpi3mmal.h"

#define FILE_DEVICE_VCHIQ                  2835
#define VCHIQ_VERSION                      8
#define VCHIQ_VERSION_MIN                  3
#define VCHIQ_SLOT_SIZE                    4096
#define VCHIQ_MAKE_FOURCC(a, b, c, d) \
    (((UINT32)(a) << 24) | ((UINT32)(b) << 16) | ((UINT32)(c) << 8) | (UINT32)(d))
#define VCHIQ_MAX_MSG_SIZE                 (VCHIQ_SLOT_SIZE - sizeof(VCHIQ_HEADER_T))
#define VCHIQ_SERVICE_HANDLE_INVALID       (~0u)
#define VCHIQ_USERMODE_PATH_W              L"\\\\.\\VCHIQ"

#define VCHIQ_IOC_CONNECT \
    CTL_CODE(FILE_DEVICE_VCHIQ, 0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define VCHIQ_IOC_SHUTDOWN \
    CTL_CODE(FILE_DEVICE_VCHIQ, 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define VCHIQ_IOC_CREATE_SERVICE \
    CTL_CODE(FILE_DEVICE_VCHIQ, 2, METHOD_IN_DIRECT, FILE_ANY_ACCESS)
#define VCHIQ_IOC_QUEUE_MESSAGE \
    CTL_CODE(FILE_DEVICE_VCHIQ, 4, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#define VCHIQ_IOC_QUEUE_BULK_TRANSMIT \
    CTL_CODE(FILE_DEVICE_VCHIQ, 5, METHOD_IN_DIRECT, FILE_ANY_ACCESS)
#define VCHIQ_IOC_QUEUE_BULK_RECEIVE \
    CTL_CODE(FILE_DEVICE_VCHIQ, 6, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#define VCHIQ_IOC_AWAIT_COMPLETION \
    CTL_CODE(FILE_DEVICE_VCHIQ, 7, METHOD_IN_DIRECT, FILE_ANY_ACCESS)
#define VCHIQ_IOC_LIB_VERSION \
    CTL_CODE(FILE_DEVICE_VCHIQ, 16, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef enum _VCHIQ_REASON_T
{
    VCHIQ_SERVICE_OPENED,
    VCHIQ_SERVICE_CLOSED,
    VCHIQ_MESSAGE_AVAILABLE,
    VCHIQ_BULK_TRANSMIT_DONE,
    VCHIQ_BULK_RECEIVE_DONE,
    VCHIQ_BULK_TRANSMIT_ABORTED,
    VCHIQ_BULK_RECEIVE_ABORTED
} VCHIQ_REASON_T;

typedef enum _VCHIQ_BULK_MODE_T
{
    VCHIQ_BULK_MODE_CALLBACK,
    VCHIQ_BULK_MODE_BLOCKING,
    VCHIQ_BULK_MODE_NOCALLBACK
} VCHIQ_BULK_MODE_T;

#include <pshpack1.h>

typedef struct _VCHIQ_HEADER_T
{
    INT32 msgid;
    UINT32 size;
    BYTE data[0];
} VCHIQ_HEADER_T;

typedef struct _VCHIQ_ELEMENT_T
{
    const VOID *data;
    INT32 size;
    VOID *driver_data_handle;
} VCHIQ_ELEMENT_T;

typedef struct _VCHIQ_SERVICE_PARAMS_T
{
    INT32 fourcc;
    VOID *callback;
    VOID *userdata;
    INT16 version;
    INT16 version_min;
} VCHIQ_SERVICE_PARAMS_T;

typedef struct _VCHIQ_CREATE_SERVICE_T
{
    VCHIQ_SERVICE_PARAMS_T params;
    INT32 is_open;
    INT32 is_vchi;
    UINT32 handle;
} VCHIQ_CREATE_SERVICE_T;

typedef struct _VCHIQ_QUEUE_MESSAGE_T
{
    UINT32 handle;
    UINT32 count;
    const VCHIQ_ELEMENT_T *elements;
    VOID *driver_element_handle;
} VCHIQ_QUEUE_MESSAGE_T;

typedef struct _VCHIQ_QUEUE_BULK_TRANSFER_T
{
    UINT32 handle;
    VOID *data;
    UINT32 size;
    VOID *userdata;
    VCHIQ_BULK_MODE_T mode;
    VOID *driver_buffer_handle;
} VCHIQ_QUEUE_BULK_TRANSFER_T;

typedef struct _VCHIQ_COMPLETION_DATA_T
{
    VCHIQ_REASON_T reason;
    VCHIQ_HEADER_T *header;
    VOID *service_userdata;
    VOID *bulk_userdata;
    VOID *driver_buffer_handle;
} VCHIQ_COMPLETION_DATA_T;

typedef struct _VCHIQ_AWAIT_COMPLETION_T
{
    UINT32 count;
    VCHIQ_COMPLETION_DATA_T *buf;
    UINT32 msgbufsize;
    UINT32 msgbufcount;
    VOID **msgbufs;
    VOID *driver_completion_handle;
} VCHIQ_AWAIT_COMPLETION_T;

#include <poppack.h>

#define MMAL_FOURCC(a, b, c, d) \
    ((UINT32)(a) | ((UINT32)(b) << 8) | ((UINT32)(c) << 16) | ((UINT32)(d) << 24))

#define MMAL_MAGIC                         MMAL_FOURCC('m', 'm', 'a', 'l')
#define MMAL_ENCODING_H264                 MMAL_FOURCC('H', '2', '6', '4')
#define MMAL_ENCODING_NV12                 MMAL_FOURCC('N', 'V', '1', '2')
#define MMAL_EVENT_ERROR                   MMAL_FOURCC('E', 'R', 'R', 'O')
#define MMAL_EVENT_FORMAT_CHANGED          MMAL_FOURCC('E', 'F', 'C', 'H')
#define MMAL_TIME_UNKNOWN                  ((INT64)0x8000000000000000ULL)

#define MMAL_WORKER_VERSION                16
#define MMAL_WORKER_VERSION_MINIMUM        10
#define MMAL_FORMAT_EXTRADATA_MAX_SIZE     128
#define MMAL_VC_SHORT_DATA                 128
#define MMAL_WORKER_EVENT_SPACE            256
#define MMAL_WORKER_PORT_PARAMETER_SPACE   96
#define MMAL_MESSAGE_SIZE                  512
#define MMAL_COMPLETION_COUNT              8
#define MMAL_OUTPUT_BUFFER_COUNT           6
#define MMAL_CONTROL_TIMEOUT_MS            10000

#define MMAL_BUFFER_HEADER_FLAG_EOS                 (1u << 0)
#define MMAL_BUFFER_HEADER_FLAG_FRAME_START         (1u << 1)
#define MMAL_BUFFER_HEADER_FLAG_FRAME_END           (1u << 2)
#define MMAL_BUFFER_HEADER_FLAG_KEYFRAME            (1u << 3)
#define MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY       (1u << 4)
#define MMAL_BUFFER_HEADER_FLAG_CONFIG              (1u << 5)
#define MMAL_BUFFER_HEADER_FLAG_CORRUPTED           (1u << 9)
#define MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED (1u << 10)

#define MMAL_ES_FORMAT_FLAG_FRAMED                   0x1u

#define MMAL_TOKEN_OUTPUT                    0x80000000u
#define MMAL_TOKEN_INDEX_MASK                0x000000ffu

#define MMAL_PARAMETER_GROUP_VIDEO                    (2u << 16)
#define MMAL_PARAMETER_PROFILE                        \
    (MMAL_PARAMETER_GROUP_VIDEO + 2u)
#define MMAL_PARAMETER_INTRAPERIOD                    \
    (MMAL_PARAMETER_GROUP_VIDEO + 3u)
#define MMAL_PARAMETER_VIDEO_REQUEST_I_FRAME          \
    (MMAL_PARAMETER_GROUP_VIDEO + 11u)
#define MMAL_PARAMETER_VIDEO_IMMUTABLE_INPUT          \
    (MMAL_PARAMETER_GROUP_VIDEO + 13u)
#define MMAL_PARAMETER_VIDEO_BIT_RATE                \
    (MMAL_PARAMETER_GROUP_VIDEO + 14u)
#define MMAL_PARAMETER_VIDEO_ENCODE_INLINE_HEADER     \
    (MMAL_PARAMETER_GROUP_VIDEO + 42u)
#define MMAL_PARAMETER_VIDEO_VALIDATE_TIMESTAMPS      \
    (MMAL_PARAMETER_GROUP_VIDEO + 55u)

#define MMAL_VIDEO_PROFILE_H264_BASELINE              25u
#define MMAL_VIDEO_PROFILE_H264_MAIN                  26u
#define MMAL_VIDEO_PROFILE_H264_HIGH                  28u
#define MMAL_VIDEO_LEVEL_H264_3                       25u
#define MMAL_VIDEO_LEVEL_H264_31                      26u
#define MMAL_VIDEO_LEVEL_H264_32                      27u
#define MMAL_VIDEO_LEVEL_H264_4                       28u
#define MMAL_VIDEO_LEVEL_H264_41                      29u
#define MMAL_VIDEO_LEVEL_H264_42                      30u

#define ALIGN_UP(Value, Alignment) \
    (((Value) + ((Alignment) - 1)) & ~((Alignment) - 1))

typedef enum _MMAL_WORKER_COMMAND
{
    MmalWorkerQuit = 1,
    MmalWorkerServiceClosed,
    MmalWorkerGetVersion,
    MmalWorkerComponentCreate,
    MmalWorkerComponentDestroy,
    MmalWorkerComponentEnable,
    MmalWorkerComponentDisable,
    MmalWorkerPortInfoGet,
    MmalWorkerPortInfoSet,
    MmalWorkerPortAction,
    MmalWorkerBufferFromHost,
    MmalWorkerBufferToHost,
    MmalWorkerGetStats,
    MmalWorkerPortParameterSet,
    MmalWorkerPortParameterGet,
    MmalWorkerEventToHost,
    MmalWorkerGetCoreStatsForPort,
    MmalWorkerOpaqueAllocator,
    MmalWorkerConsumeMem,
    MmalWorkerLmk,
    MmalWorkerOpaqueAllocatorDesc,
    MmalWorkerDrmGetLhs32,
    MmalWorkerDrmGetTime,
    MmalWorkerBufferFromHostZeroLength,
    MmalWorkerPortFlush,
    MmalWorkerHostLog,
    MmalWorkerCompact
} MMAL_WORKER_COMMAND;

typedef enum _MMAL_PORT_TYPE
{
    MmalPortTypeUnknown,
    MmalPortTypeControl,
    MmalPortTypeInput,
    MmalPortTypeOutput,
    MmalPortTypeClock
} MMAL_PORT_TYPE;

typedef enum _MMAL_PORT_ACTION
{
    MmalPortActionUnknown,
    MmalPortActionEnable,
    MmalPortActionDisable,
    MmalPortActionFlush,
    MmalPortActionConnect,
    MmalPortActionDisconnect,
    MmalPortActionSetRequirements
} MMAL_PORT_ACTION;

/*
 * The VideoCore MMAL worker is a 32-bit endpoint. Pointer-looking members in
 * its protocol are opaque cookies and must remain four bytes on ARM64 hosts.
 */
#include <pshpack4.h>

typedef struct _MMAL_WORKER_HEADER32
{
    UINT32 Magic;
    UINT32 MessageId;
    UINT32 ControlService;
    UINT32 Waiter;
    INT32 Status;
    UINT32 Dummy;
} MMAL_WORKER_HEADER32;

typedef struct _MMAL_RECT32
{
    INT32 X;
    INT32 Y;
    INT32 Width;
    INT32 Height;
} MMAL_RECT32;

typedef struct _MMAL_RATIONAL32
{
    INT32 Numerator;
    INT32 Denominator;
} MMAL_RATIONAL32;

typedef struct _MMAL_VIDEO_FORMAT32
{
    UINT32 Width;
    UINT32 Height;
    MMAL_RECT32 Crop;
    MMAL_RATIONAL32 FrameRate;
    MMAL_RATIONAL32 PixelAspectRatio;
    UINT32 ColorSpace;
} MMAL_VIDEO_FORMAT32;

typedef union _MMAL_ES_SPECIFIC_FORMAT32
{
    MMAL_VIDEO_FORMAT32 Video;
    UINT32 Words[11];
} MMAL_ES_SPECIFIC_FORMAT32;

typedef struct _MMAL_ES_FORMAT32
{
    UINT32 Type;
    UINT32 Encoding;
    UINT32 EncodingVariant;
    UINT32 Es;
    UINT32 Bitrate;
    UINT32 Flags;
    UINT32 ExtraDataSize;
    UINT32 ExtraData;
} MMAL_ES_FORMAT32;

typedef struct _MMAL_PORT32
{
    UINT32 Private;
    UINT32 Name;
    UINT32 Type;
    UINT16 Index;
    UINT16 IndexAll;
    UINT32 IsEnabled;
    UINT32 Format;
    UINT32 BufferNumMin;
    UINT32 BufferSizeMin;
    UINT32 BufferAlignmentMin;
    UINT32 BufferNumRecommended;
    UINT32 BufferSizeRecommended;
    UINT32 BufferNum;
    UINT32 BufferSize;
    UINT32 Component;
    UINT32 UserData;
    UINT32 Capabilities;
} MMAL_PORT32;

typedef struct _MMAL_BUFFER_HEADER32
{
    UINT32 Next;
    UINT32 Private;
    UINT32 Command;
    UINT32 Data;
    UINT32 AllocationSize;
    UINT32 Length;
    UINT32 Offset;
    UINT32 Flags;
    INT64 Pts;
    INT64 Dts;
    UINT32 TypeSpecific;
    UINT32 UserData;
} MMAL_BUFFER_HEADER32;

typedef struct _MMAL_BUFFER_VIDEO32
{
    UINT32 PlaneCount;
    UINT32 Offset[4];
    UINT32 Pitch[4];
    UINT32 Flags;
} MMAL_BUFFER_VIDEO32;

typedef union _MMAL_BUFFER_TYPE_SPECIFIC32
{
    MMAL_BUFFER_VIDEO32 Video;
} MMAL_BUFFER_TYPE_SPECIFIC32;

typedef struct _MMAL_DRIVER_BUFFER32
{
    UINT32 Magic;
    UINT32 ComponentHandle;
    UINT32 PortHandle;
    UINT32 ClientContext;
} MMAL_DRIVER_BUFFER32;

typedef struct _MMAL_COMPONENT_CREATE
{
    MMAL_WORKER_HEADER32 Header;
    UINT32 ClientComponent;
    CHAR Name[128];
    UINT32 ProcessId;
} MMAL_COMPONENT_CREATE;

typedef struct _MMAL_COMPONENT_CREATE_REPLY
{
    MMAL_WORKER_HEADER32 Header;
    INT32 Status;
    UINT32 ComponentHandle;
    UINT32 InputCount;
    UINT32 OutputCount;
    UINT32 ClockCount;
} MMAL_COMPONENT_CREATE_REPLY;

typedef struct _MMAL_COMPONENT_COMMAND
{
    MMAL_WORKER_HEADER32 Header;
    UINT32 ComponentHandle;
} MMAL_COMPONENT_COMMAND;

typedef struct _MMAL_WORKER_REPLY
{
    MMAL_WORKER_HEADER32 Header;
    INT32 Status;
} MMAL_WORKER_REPLY;

typedef struct _MMAL_PORT_INFO_GET
{
    MMAL_WORKER_HEADER32 Header;
    UINT32 ComponentHandle;
    UINT32 PortType;
    UINT32 Index;
} MMAL_PORT_INFO_GET;

typedef struct _MMAL_PORT_INFO_SET
{
    MMAL_WORKER_HEADER32 Header;
    UINT32 ComponentHandle;
    UINT32 PortType;
    UINT32 Index;
    MMAL_PORT32 Port;
    MMAL_ES_FORMAT32 Format;
    MMAL_ES_SPECIFIC_FORMAT32 Es;
    BYTE ExtraData[MMAL_FORMAT_EXTRADATA_MAX_SIZE];
} MMAL_PORT_INFO_SET;

typedef struct _MMAL_PORT_INFO_REPLY
{
    MMAL_WORKER_HEADER32 Header;
    INT32 Status;
    UINT32 ComponentHandle;
    UINT32 PortType;
    UINT32 Index;
    INT32 Found;
    UINT32 PortHandle;
    MMAL_PORT32 Port;
    MMAL_ES_FORMAT32 Format;
    MMAL_ES_SPECIFIC_FORMAT32 Es;
    BYTE ExtraData[MMAL_FORMAT_EXTRADATA_MAX_SIZE];
} MMAL_PORT_INFO_REPLY;

typedef struct _MMAL_PORT_ACTION_MESSAGE
{
    MMAL_WORKER_HEADER32 Header;
    UINT32 ComponentHandle;
    UINT32 PortHandle;
    UINT32 Action;
    union
    {
        struct
        {
            MMAL_PORT32 Port;
        } Enable;
        struct
        {
            UINT32 ComponentHandle;
            UINT32 PortHandle;
        } Connect;
    } Parameter;
} MMAL_PORT_ACTION_MESSAGE;

typedef struct _MMAL_PORT_PARAMETER_SET
{
    MMAL_WORKER_HEADER32 Header;
    UINT32 ComponentHandle;
    UINT32 PortHandle;
    UINT32 Id;
    UINT32 Size;
    UINT32 Value[MMAL_WORKER_PORT_PARAMETER_SPACE];
} MMAL_PORT_PARAMETER_SET;

typedef struct _MMAL_BUFFER_MESSAGE
{
    MMAL_WORKER_HEADER32 Header;
    MMAL_DRIVER_BUFFER32 DriverBuffer;
    MMAL_DRIVER_BUFFER32 ReferenceBuffer;
    MMAL_BUFFER_HEADER32 BufferHeader;
    MMAL_BUFFER_TYPE_SPECIFIC32 TypeSpecific;
    INT32 IsZeroCopy;
    INT32 HasReference;
    UINT32 PayloadInMessage;
    BYTE ShortData[MMAL_VC_SHORT_DATA];
} MMAL_BUFFER_MESSAGE;

typedef struct _MMAL_EVENT_MESSAGE
{
    MMAL_WORKER_HEADER32 Header;
    UINT32 ClientComponent;
    UINT32 PortType;
    UINT32 PortIndex;
    UINT32 Command;
    UINT32 Length;
    BYTE Data[MMAL_WORKER_EVENT_SPACE];
    UINT32 DelayedBuffer;
} MMAL_EVENT_MESSAGE;

typedef struct _MMAL_EVENT_FORMAT_CHANGED32
{
    UINT32 BufferSizeMin;
    UINT32 BufferNumMin;
    UINT32 BufferSizeRecommended;
    UINT32 BufferNumRecommended;
    UINT32 Format;
} MMAL_EVENT_FORMAT_CHANGED32;

#include <poppack.h>

C_ASSERT(sizeof(MMAL_WORKER_HEADER32) == 24);
C_ASSERT(sizeof(MMAL_ES_FORMAT32) == 32);
C_ASSERT(sizeof(MMAL_ES_SPECIFIC_FORMAT32) == 44);
C_ASSERT(sizeof(MMAL_PORT32) == 64);
C_ASSERT(sizeof(MMAL_BUFFER_HEADER32) == 56);
C_ASSERT(sizeof(MMAL_BUFFER_MESSAGE) == 292);
C_ASSERT(sizeof(MMAL_EVENT_FORMAT_CHANGED32) == 20);
C_ASSERT(sizeof(MMAL_PORT_PARAMETER_SET) <= MMAL_MESSAGE_SIZE);
C_ASSERT(sizeof(MMAL_PORT_INFO_SET) <= MMAL_MESSAGE_SIZE);
C_ASSERT(sizeof(MMAL_PORT_INFO_REPLY) <= MMAL_MESSAGE_SIZE);

typedef struct _RPI3_MMAL_PORT
{
    UINT32 Handle;
    MMAL_PORT32 Port;
    MMAL_ES_FORMAT32 Format;
    MMAL_ES_SPECIFIC_FORMAT32 Es;
    BYTE ExtraData[MMAL_FORMAT_EXTRADATA_MAX_SIZE];
} RPI3_MMAL_PORT;

typedef enum _RPI3_MMAL_SLOT_STATE
{
    Rpi3MmalSlotFree,
    Rpi3MmalSlotQueued,
    Rpi3MmalSlotReady,
    Rpi3MmalSlotConsuming
} RPI3_MMAL_SLOT_STATE;

typedef struct _RPI3_MMAL_OUTPUT_SLOT
{
    UINT Size;
    UINT Offset;
    UINT Length;
    UINT Flags;
    UINT PayloadInMessage;
    BYTE ShortData[MMAL_VC_SHORT_DATA];
    LONGLONG Pts;
    LONGLONG Dts;
    ULONGLONG Sequence;
    RPI3_MMAL_SLOT_STATE State;
} RPI3_MMAL_OUTPUT_SLOT;

typedef struct _RPI3_MMAL_READY_OUTPUT
{
    UINT Index;
    UINT Offset;
    UINT Length;
    UINT Flags;
    UINT PayloadInMessage;
    LONGLONG Pts;
    LONGLONG Dts;
    ULONGLONG Sequence;
} RPI3_MMAL_READY_OUTPUT;

typedef struct _RPI3_MMAL_PENDING_FORMAT
{
    UINT BufferSizeMin;
    UINT BufferNumMin;
    UINT BufferSizeRecommended;
    UINT BufferNumRecommended;
    MMAL_ES_FORMAT32 Format;
    MMAL_ES_SPECIFIC_FORMAT32 Es;
    BYTE ExtraData[MMAL_FORMAT_EXTRADATA_MAX_SIZE];
    UINT ExtraDataSize;
} RPI3_MMAL_PENDING_FORMAT;

struct RPI3_MMAL_DECODER
{
    HANDLE Device;
    HANDLE CompletionThread;
    HANDLE StopEvent;
    HANDLE ControlEvent;
    HANDLE InputReturnedEvent;
    HANDLE FrameEvent;
    HANDLE FormatEvent;
    HANDLE OutputDrainedEvent;
    CRITICAL_SECTION ControlLock;
    CRITICAL_SECTION InputLock;
    CRITICAL_SECTION OutputLock;
    CRITICAL_SECTION OutputBulkLock;
    CRITICAL_SECTION StateLock;
    volatile LONG StopRequested;
    volatile LONG FormatChanged;
    volatile LONG Reconfiguring;
    volatile LONG AsyncResult;
    volatile LONG NextToken;
    volatile LONG PendingToken;
    volatile LONG PendingInputToken;
    volatile LONG InputResult;
    BYTE ControlReply[MMAL_MESSAGE_SIZE];
    UINT ControlReplySize;
    UINT32 ComponentHandle;
    RPI3_MMAL_PORT Input;
    RPI3_MMAL_PORT Output;
    UINT Width;
    UINT Height;
    UINT VisibleWidth;
    UINT VisibleHeight;
    UINT Pitch;
    UINT StorageHeight;
    UINT OutputSize;
    UINT OutputCount;
    UINT FrameRateNumerator;
    UINT FrameRateDenominator;
    UINT Bitrate;
    UINT IntraPeriod;
    UINT Profile;
    UINT Level;
    UINT FormatGeneration;
    UINT AppliedFormatGeneration;
    ULONGLONG NextOutputSequence;
    BYTE *DiscardData;
    UINT DiscardSize;
    BOOL ComponentEnabled;
    BOOL InputEnabled;
    BOOL OutputEnabled;
    BOOL InputBulkSent;
    BOOL Encoder;
    BOOL InlineHeaders;
    RPI3_MMAL_PENDING_FORMAT PendingFormat;
    RPI3_MMAL_OUTPUT_SLOT OutputSlots[MMAL_OUTPUT_BUFFER_COUNT];
};

static HRESULT
Rpi3MmalErrorFromLastError(VOID)
{
    DWORD Error = GetLastError();

    if (!Error)
        return E_FAIL;

    return HRESULT_FROM_WIN32(Error);
}

static HRESULT
Rpi3MmalStatusToResult(INT32 Status)
{
    switch (Status)
    {
        case 0:
            return S_OK;
        case 1:
            return E_OUTOFMEMORY;
        case 3:
            return E_INVALIDARG;
        default:
            return E_FAIL;
    }
}

static VOID
Rpi3MmalSetAsyncResult(RPI3_MMAL_DECODER *Decoder, HRESULT Result)
{
    if (SUCCEEDED(Result))
        return;

    InterlockedCompareExchange(&Decoder->AsyncResult, (LONG)Result, (LONG)S_OK);
    InterlockedCompareExchange(&Decoder->InputResult, (LONG)Result, (LONG)S_OK);
    SetEvent(Decoder->InputReturnedEvent);
    SetEvent(Decoder->FrameEvent);
    SetEvent(Decoder->FormatEvent);
    SetEvent(Decoder->OutputDrainedEvent);
}

static BOOL
Rpi3MmalOutputDrainedLocked(const RPI3_MMAL_DECODER *Decoder)
{
    UINT Index;

    for (Index = 0; Index < Decoder->OutputCount; ++Index)
    {
        if (Decoder->OutputSlots[Index].State == Rpi3MmalSlotQueued ||
            Decoder->OutputSlots[Index].State == Rpi3MmalSlotConsuming)
            return FALSE;
    }

    return TRUE;
}

static VOID
Rpi3MmalUpdateOutputEventsLocked(RPI3_MMAL_DECODER *Decoder)
{
    UINT Index;
    BOOL FrameReady = FALSE;

    for (Index = 0; Index < Decoder->OutputCount; ++Index)
    {
        if (Decoder->OutputSlots[Index].State == Rpi3MmalSlotReady)
        {
            FrameReady = TRUE;
            break;
        }
    }

    if (FrameReady)
        SetEvent(Decoder->FrameEvent);
    else
        ResetEvent(Decoder->FrameEvent);

    if (Rpi3MmalOutputDrainedLocked(Decoder))
        SetEvent(Decoder->OutputDrainedEvent);
    else
        ResetEvent(Decoder->OutputDrainedEvent);
}

static BOOL
Rpi3MmalCloseDevice(RPI3_MMAL_DECODER *Decoder, HANDLE Device)
{
    if (Device == INVALID_HANDLE_VALUE)
        return FALSE;

    if (InterlockedCompareExchangePointer(
            (PVOID volatile *)&Decoder->Device,
            INVALID_HANDLE_VALUE,
            Device) != Device)
    {
        return FALSE;
    }

    (VOID)CancelIoEx(Device, NULL);
    CloseHandle(Device);
    return TRUE;
}

static BOOL
Rpi3MmalDeviceIoControlTimeout(RPI3_MMAL_DECODER *Decoder,
                               DWORD IoControlCode,
                               VOID *InputBuffer,
                               DWORD InputSize,
                               VOID *OutputBuffer,
                               DWORD OutputSize,
                               DWORD *BytesReturned,
                               DWORD TimeoutMilliseconds)
{
    OVERLAPPED Overlapped;
    HANDLE Device;
    DWORD LocalBytes;
    DWORD Error;
    DWORD WaitStatus;
    BOOL Result;

    if (!BytesReturned)
        BytesReturned = &LocalBytes;

    Device = Decoder->Device;
    if (Device == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Overlapped.hEvent)
        return FALSE;

    Result = DeviceIoControl(Device,
                             IoControlCode,
                             InputBuffer,
                             InputSize,
                             OutputBuffer,
                             OutputSize,
                             BytesReturned,
                             &Overlapped);
    if (!Result)
    {
        Error = GetLastError();
        if (Error == ERROR_IO_PENDING)
        {
            WaitStatus = WaitForSingleObject(Overlapped.hEvent,
                                             TimeoutMilliseconds);
            if (WaitStatus == WAIT_OBJECT_0)
            {
                Result = GetOverlappedResult(Device, &Overlapped,
                                             BytesReturned, FALSE);
            }
            else
            {
                Error = WaitStatus == WAIT_TIMEOUT ?
                        ERROR_TIMEOUT : ERROR_GEN_FAILURE;
                (VOID)CancelIoEx(Device, &Overlapped);
                (VOID)Rpi3MmalCloseDevice(Decoder, Device);
                (VOID)WaitForSingleObject(Overlapped.hEvent, INFINITE);
                (VOID)GetOverlappedResult(Device, &Overlapped,
                                          BytesReturned, FALSE);
                Result = FALSE;
                SetLastError(Error);
            }
        }
    }

    Error = Result ? ERROR_SUCCESS : GetLastError();
    CloseHandle(Overlapped.hEvent);
    SetLastError(Error);
    return Result;
}

static BOOL
Rpi3MmalDeviceIoControl(RPI3_MMAL_DECODER *Decoder,
                        DWORD IoControlCode,
                        VOID *InputBuffer,
                        DWORD InputSize,
                        VOID *OutputBuffer,
                        DWORD OutputSize,
                        DWORD *BytesReturned)
{
    return Rpi3MmalDeviceIoControlTimeout(Decoder,
                                          IoControlCode,
                                          InputBuffer,
                                          InputSize,
                                          OutputBuffer,
                                          OutputSize,
                                          BytesReturned,
                                          MMAL_CONTROL_TIMEOUT_MS);
}

static BOOL
Rpi3MmalQueueMessage(RPI3_MMAL_DECODER *Decoder,
                     const VOID *Message,
                     UINT MessageSize)
{
    VCHIQ_ELEMENT_T Element;
    VCHIQ_QUEUE_MESSAGE_T Arguments;

    ZeroMemory(&Element, sizeof(Element));
    Element.data = Message;
    Element.size = MessageSize;

    ZeroMemory(&Arguments, sizeof(Arguments));
    Arguments.handle = 0;
    Arguments.count = 1;
    Arguments.elements = &Element;

    return Rpi3MmalDeviceIoControl(Decoder,
                                   VCHIQ_IOC_QUEUE_MESSAGE,
                                   &Arguments,
                                   sizeof(Arguments),
                                   NULL,
                                   0,
                                   NULL);
}

static BOOL
Rpi3MmalBulkTransmit(RPI3_MMAL_DECODER *Decoder,
                     const VOID *Data,
                     UINT Size,
                     UINT32 Token)
{
    VCHIQ_QUEUE_BULK_TRANSFER_T Arguments;

    ZeroMemory(&Arguments, sizeof(Arguments));
    Arguments.handle = 0;
    Arguments.data = (VOID *)Data;
    Arguments.size = Size;
    Arguments.userdata = (VOID *)(ULONG_PTR)Token;
    Arguments.mode = VCHIQ_BULK_MODE_BLOCKING;

    /* METHOD_IN_DIRECT: payload is input, transfer descriptor is output. */
    return Rpi3MmalDeviceIoControl(Decoder,
                                   VCHIQ_IOC_QUEUE_BULK_TRANSMIT,
                                   (VOID *)Data,
                                   Size,
                                   &Arguments,
                                   sizeof(Arguments),
                                   NULL);
}

static BOOL
Rpi3MmalBulkReceive(RPI3_MMAL_DECODER *Decoder,
                    VOID *Data,
                    UINT Size,
                    UINT32 Token)
{
    VCHIQ_QUEUE_BULK_TRANSFER_T Arguments;

    ZeroMemory(&Arguments, sizeof(Arguments));
    Arguments.handle = 0;
    Arguments.data = Data;
    Arguments.size = Size;
    Arguments.userdata = (VOID *)(ULONG_PTR)Token;
    Arguments.mode = VCHIQ_BULK_MODE_BLOCKING;

    /* METHOD_OUT_DIRECT: transfer descriptor is input, payload is output. */
    return Rpi3MmalDeviceIoControl(Decoder,
                                   VCHIQ_IOC_QUEUE_BULK_RECEIVE,
                                   &Arguments,
                                   sizeof(Arguments),
                                   Data,
                                   Size,
                                   NULL);
}

static HRESULT
Rpi3MmalSendWait(RPI3_MMAL_DECODER *Decoder,
                 MMAL_WORKER_HEADER32 *Header,
                 UINT MessageSize,
                 UINT MessageId,
                 VOID *Reply,
                 UINT ReplySize,
                 BOOL SendDummyBulk)
{
    static const BYTE DummyBulk[8];
    HANDLE WaitHandles[2];
    DWORD WaitStatus;
    UINT32 Token;
    HRESULT Result = E_FAIL;

    EnterCriticalSection(&Decoder->ControlLock);

    Token = (UINT32)InterlockedIncrement(&Decoder->NextToken);
    if (!Token || (Token & MMAL_TOKEN_OUTPUT))
    {
        InterlockedExchange(&Decoder->NextToken, 1);
        Token = 1;
    }

    Header->Magic = MMAL_MAGIC;
    Header->MessageId = MessageId;
    Header->ControlService = 0;
    Header->Waiter = Token;
    Header->Status = 0;
    Header->Dummy = 0;

    ResetEvent(Decoder->ControlEvent);
    Decoder->ControlReplySize = 0;
    InterlockedExchange(&Decoder->PendingToken, Token);

    if (SendDummyBulk)
        EnterCriticalSection(&Decoder->InputLock);

    if (!Rpi3MmalQueueMessage(Decoder, Header, MessageSize))
    {
        Result = Rpi3MmalErrorFromLastError();
        if (SendDummyBulk)
            LeaveCriticalSection(&Decoder->InputLock);
        goto Exit;
    }

    if (SendDummyBulk)
    {
        if (!Rpi3MmalBulkTransmit(Decoder, DummyBulk, sizeof(DummyBulk), Token))
        {
            Result = Rpi3MmalErrorFromLastError();
            LeaveCriticalSection(&Decoder->InputLock);
            goto Exit;
        }
        LeaveCriticalSection(&Decoder->InputLock);
    }

    WaitHandles[0] = Decoder->ControlEvent;
    WaitHandles[1] = Decoder->StopEvent;
    WaitStatus = WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles, FALSE,
                                        MMAL_CONTROL_TIMEOUT_MS);
    if (WaitStatus != WAIT_OBJECT_0)
    {
        if (WaitStatus == WAIT_OBJECT_0 + 1)
            Result = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
        else if (WaitStatus == WAIT_TIMEOUT)
            Result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        else
            Result = Rpi3MmalErrorFromLastError();
        goto Exit;
    }

    if (Decoder->ControlReplySize < sizeof(MMAL_WORKER_HEADER32))
        goto Exit;

    if (ReplySize > Decoder->ControlReplySize)
        ReplySize = Decoder->ControlReplySize;
    CopyMemory(Reply, Decoder->ControlReply, ReplySize);
    Result = S_OK;

Exit:
    InterlockedExchange(&Decoder->PendingToken, 0);
    LeaveCriticalSection(&Decoder->ControlLock);
    return Result;
}

static HRESULT
Rpi3MmalComponentCommand(RPI3_MMAL_DECODER *Decoder, UINT MessageId)
{
    MMAL_COMPONENT_COMMAND Message;
    MMAL_WORKER_REPLY Reply;
    HRESULT Result;

    ZeroMemory(&Message, sizeof(Message));
    Message.ComponentHandle = Decoder->ComponentHandle;
    ZeroMemory(&Reply, sizeof(Reply));

    Result = Rpi3MmalSendWait(Decoder,
                              &Message.Header,
                              sizeof(Message),
                              MessageId,
                              &Reply,
                              sizeof(Reply),
                              FALSE);
    if (FAILED(Result))
        return Result;

    return Rpi3MmalStatusToResult(Reply.Status);
}

static HRESULT
Rpi3MmalPortInfoGet(RPI3_MMAL_DECODER *Decoder,
                    UINT PortType,
                    UINT Index,
                    RPI3_MMAL_PORT *Port)
{
    MMAL_PORT_INFO_GET Message;
    MMAL_PORT_INFO_REPLY Reply;
    HRESULT Result;

    ZeroMemory(&Message, sizeof(Message));
    Message.ComponentHandle = Decoder->ComponentHandle;
    Message.PortType = PortType;
    Message.Index = Index;
    ZeroMemory(&Reply, sizeof(Reply));

    Result = Rpi3MmalSendWait(Decoder,
                              &Message.Header,
                              sizeof(Message),
                              MmalWorkerPortInfoGet,
                              &Reply,
                              sizeof(Reply),
                              FALSE);
    if (FAILED(Result))
        return Result;
    Result = Rpi3MmalStatusToResult(Reply.Status);
    if (FAILED(Result))
        return Result;

    Port->Handle = Reply.PortHandle;
    Port->Port = Reply.Port;
    Port->Format = Reply.Format;
    Port->Es = Reply.Es;
    if (Port->Format.ExtraDataSize > MMAL_FORMAT_EXTRADATA_MAX_SIZE)
        return E_FAIL;
    CopyMemory(Port->ExtraData, Reply.ExtraData, Port->Format.ExtraDataSize);
    return S_OK;
}

static HRESULT
Rpi3MmalPortInfoSet(RPI3_MMAL_DECODER *Decoder,
                    UINT PortType,
                    UINT Index,
                    RPI3_MMAL_PORT *Port)
{
    MMAL_PORT_INFO_SET Message;
    MMAL_PORT_INFO_REPLY Reply;
    HRESULT Result;

    ZeroMemory(&Message, sizeof(Message));
    Message.ComponentHandle = Decoder->ComponentHandle;
    Message.PortType = PortType;
    Message.Index = Index;
    Message.Port = Port->Port;
    Message.Format = Port->Format;
    Message.Es = Port->Es;
    if (Message.Format.ExtraDataSize > MMAL_FORMAT_EXTRADATA_MAX_SIZE)
        return E_INVALIDARG;
    CopyMemory(Message.ExtraData, Port->ExtraData, Message.Format.ExtraDataSize);
    ZeroMemory(&Reply, sizeof(Reply));

    Result = Rpi3MmalSendWait(Decoder,
                              &Message.Header,
                              sizeof(Message),
                              MmalWorkerPortInfoSet,
                              &Reply,
                              sizeof(Reply),
                              FALSE);
    if (FAILED(Result))
        return Result;
    Result = Rpi3MmalStatusToResult(Reply.Status);
    if (FAILED(Result))
        return Result;

    Port->Handle = Reply.PortHandle;
    Port->Port = Reply.Port;
    Port->Format = Reply.Format;
    Port->Es = Reply.Es;
    if (Port->Format.ExtraDataSize > MMAL_FORMAT_EXTRADATA_MAX_SIZE)
        return E_FAIL;
    CopyMemory(Port->ExtraData, Reply.ExtraData, Port->Format.ExtraDataSize);
    return S_OK;
}

static HRESULT
Rpi3MmalPortAction(RPI3_MMAL_DECODER *Decoder,
                   RPI3_MMAL_PORT *Port,
                   UINT Action)
{
    MMAL_PORT_ACTION_MESSAGE Message;
    MMAL_WORKER_REPLY Reply;
    HRESULT Result;

    ZeroMemory(&Message, sizeof(Message));
    Message.ComponentHandle = Decoder->ComponentHandle;
    Message.PortHandle = Port->Handle;
    Message.Action = Action;
    if (Action == MmalPortActionEnable)
        Message.Parameter.Enable.Port = Port->Port;
    ZeroMemory(&Reply, sizeof(Reply));

    Result = Rpi3MmalSendWait(Decoder,
                              &Message.Header,
                              sizeof(Message),
                              MmalWorkerPortAction,
                              &Reply,
                              sizeof(Reply),
                              FALSE);
    if (FAILED(Result))
        return Result;

    return Rpi3MmalStatusToResult(Reply.Status);
}

static HRESULT
Rpi3MmalPortParameterSet(RPI3_MMAL_DECODER *Decoder,
                         RPI3_MMAL_PORT *Port,
                         UINT32 Parameter,
                         const VOID *Value,
                         UINT32 ValueSize)
{
    MMAL_PORT_PARAMETER_SET Message;
    MMAL_WORKER_REPLY Reply;
    HRESULT Result;

    if ((!Value && ValueSize) || ValueSize > sizeof(Message.Value))
        return E_INVALIDARG;

    ZeroMemory(&Message, sizeof(Message));
    Message.ComponentHandle = Decoder->ComponentHandle;
    Message.PortHandle = Port->Handle;
    Message.Id = Parameter;
    Message.Size = 2 * sizeof(UINT32) + ValueSize;
    if (ValueSize)
        CopyMemory(Message.Value, Value, ValueSize);
    ZeroMemory(&Reply, sizeof(Reply));

    Result = Rpi3MmalSendWait(
                 Decoder,
                 &Message.Header,
                 FIELD_OFFSET(MMAL_PORT_PARAMETER_SET, Value) + ValueSize,
                 MmalWorkerPortParameterSet,
                 &Reply,
                 sizeof(Reply),
                 FALSE);
    if (FAILED(Result))
        return Result;

    return Rpi3MmalStatusToResult(Reply.Status);
}

static HRESULT
Rpi3MmalPortParameterSetBoolean(RPI3_MMAL_DECODER *Decoder,
                                RPI3_MMAL_PORT *Port,
                                UINT32 Parameter,
                                BOOL Enabled)
{
    const UINT32 Value = Enabled ? 1 : 0;

    return Rpi3MmalPortParameterSet(Decoder,
                                    Port,
                                    Parameter,
                                    &Value,
                                    sizeof(Value));
}

static HRESULT
Rpi3MmalPortParameterSetUint32(RPI3_MMAL_DECODER *Decoder,
                               RPI3_MMAL_PORT *Port,
                               UINT32 Parameter,
                               UINT32 Value)
{
    return Rpi3MmalPortParameterSet(Decoder,
                                    Port,
                                    Parameter,
                                    &Value,
                                    sizeof(Value));
}

static HRESULT
Rpi3MmalPortFlush(RPI3_MMAL_DECODER *Decoder,
                  RPI3_MMAL_PORT *Port,
                  BOOL SynchronizeBulk)
{
    MMAL_BUFFER_MESSAGE Message;
    MMAL_WORKER_REPLY Reply;
    HRESULT Result;

    if (!SynchronizeBulk)
        return Rpi3MmalPortAction(Decoder, Port, MmalPortActionFlush);

    ZeroMemory(&Message, sizeof(Message));
    Message.DriverBuffer.Magic = MMAL_MAGIC;
    Message.DriverBuffer.ComponentHandle = Decoder->ComponentHandle;
    Message.DriverBuffer.PortHandle = Port->Handle;
    ZeroMemory(&Reply, sizeof(Reply));

    Result = Rpi3MmalSendWait(Decoder,
                              &Message.Header,
                              sizeof(Message),
                              MmalWorkerPortFlush,
                              &Reply,
                              sizeof(Reply),
                              TRUE);
    if (FAILED(Result))
        return Result;

    return Rpi3MmalStatusToResult(Reply.Status);
}

static BOOL
Rpi3MmalQueueOutputSlot(RPI3_MMAL_DECODER *Decoder,
                        UINT Index,
                        RPI3_MMAL_SLOT_STATE ExpectedState,
                        BOOL SkipWhileReconfiguring)
{
    MMAL_BUFFER_MESSAGE Message;
    RPI3_MMAL_OUTPUT_SLOT *Slot;
    UINT SlotSize;

    if (Index >= Decoder->OutputCount)
        return FALSE;

    EnterCriticalSection(&Decoder->StateLock);
    Slot = &Decoder->OutputSlots[Index];
    if (!Slot->Size || Slot->State != ExpectedState)
    {
        LeaveCriticalSection(&Decoder->StateLock);
        SetLastError(ERROR_INVALID_STATE);
        return FALSE;
    }
    if (InterlockedCompareExchange(&Decoder->Reconfiguring, 0, 0))
    {
        if (SkipWhileReconfiguring)
        {
            Slot->State = Rpi3MmalSlotFree;
            Rpi3MmalUpdateOutputEventsLocked(Decoder);
            LeaveCriticalSection(&Decoder->StateLock);
            return TRUE;
        }
        LeaveCriticalSection(&Decoder->StateLock);
        SetLastError(ERROR_INVALID_STATE);
        return FALSE;
    }
    SlotSize = Slot->Size;
    Slot->Offset = 0;
    Slot->Length = 0;
    Slot->Flags = 0;
    Slot->PayloadInMessage = 0;
    Slot->Pts = MMAL_TIME_UNKNOWN;
    Slot->Dts = MMAL_TIME_UNKNOWN;
    Slot->Sequence = 0;
    Slot->State = Rpi3MmalSlotQueued;
    Rpi3MmalUpdateOutputEventsLocked(Decoder);
    LeaveCriticalSection(&Decoder->StateLock);

    ZeroMemory(&Message, sizeof(Message));
    Message.Header.Magic = MMAL_MAGIC;
    Message.Header.MessageId = MmalWorkerBufferFromHost;
    Message.DriverBuffer.Magic = MMAL_MAGIC;
    Message.DriverBuffer.ComponentHandle = Decoder->ComponentHandle;
    Message.DriverBuffer.PortHandle = Decoder->Output.Handle;
    Message.DriverBuffer.ClientContext = MMAL_TOKEN_OUTPUT | Index;
    Message.BufferHeader.Data = MMAL_TOKEN_OUTPUT | Index;
    Message.BufferHeader.AllocationSize = SlotSize;
    Message.BufferHeader.Pts = MMAL_TIME_UNKNOWN;
    Message.BufferHeader.Dts = MMAL_TIME_UNKNOWN;
    if (!Decoder->Encoder)
    {
        Message.TypeSpecific.Video.PlaneCount = 2;
        Message.TypeSpecific.Video.Offset[0] = 0;
        Message.TypeSpecific.Video.Offset[1] = Decoder->Pitch * Decoder->StorageHeight;
        Message.TypeSpecific.Video.Pitch[0] = Decoder->Pitch;
        Message.TypeSpecific.Video.Pitch[1] = Decoder->Pitch;
    }

    if (!Rpi3MmalQueueMessage(Decoder, &Message, sizeof(Message)))
    {
        EnterCriticalSection(&Decoder->StateLock);
        if (Slot->State == Rpi3MmalSlotQueued)
            Slot->State = Rpi3MmalSlotFree;
        Rpi3MmalUpdateOutputEventsLocked(Decoder);
        LeaveCriticalSection(&Decoder->StateLock);
        return FALSE;
    }

    return TRUE;
}

static HRESULT
Rpi3MmalUpdateOutputGeometry(RPI3_MMAL_DECODER *Decoder)
{
    ULONGLONG OutputSize;
    UINT Width = Decoder->Output.Es.Video.Width;
    UINT Height = Decoder->Output.Es.Video.Height;
    UINT VisibleWidth;
    UINT VisibleHeight;

    if (!Width)
        Width = ALIGN_UP(Decoder->Width, 32);
    if (!Height)
        Height = ALIGN_UP(Decoder->Height, 16);

    VisibleWidth = Decoder->Output.Es.Video.Crop.Width > 0 ?
                   Decoder->Output.Es.Video.Crop.Width : Decoder->Width;
    VisibleHeight = Decoder->Output.Es.Video.Crop.Height > 0 ?
                    Decoder->Output.Es.Video.Crop.Height : Decoder->Height;
    if (!VisibleWidth || !VisibleHeight ||
        VisibleWidth > Decoder->Width || VisibleHeight > Decoder->Height ||
        VisibleWidth > 1920 || VisibleHeight > 1088 ||
        Width < VisibleWidth || Height < VisibleHeight || Width > 1920 || Height > 1088)
    {
        return E_FAIL;
    }

    OutputSize = (ULONGLONG)Width * Height * 3 / 2;
    if (Decoder->Output.Port.BufferSize > OutputSize)
        OutputSize = Decoder->Output.Port.BufferSize;
    if (OutputSize > MAXUINT)
        return E_OUTOFMEMORY;

    Decoder->Pitch = Width;
    Decoder->StorageHeight = Height;
    Decoder->VisibleWidth = VisibleWidth;
    Decoder->VisibleHeight = VisibleHeight;
    Decoder->OutputSize = (UINT)OutputSize;
    return S_OK;
}

static VOID
Rpi3MmalHandleFormatChanged(RPI3_MMAL_DECODER *Decoder,
                            const MMAL_EVENT_MESSAGE *Message)
{
    const MMAL_EVENT_FORMAT_CHANGED32 *Event;
    const BYTE *FormatData;
    UINT MinimumSize;
    UINT ExtraDataSize;

    MinimumSize = sizeof(*Event) + sizeof(MMAL_ES_FORMAT32) +
                  sizeof(MMAL_ES_SPECIFIC_FORMAT32);
    if (Message->PortType != MmalPortTypeOutput || Message->PortIndex != 0 ||
        Message->Length < MinimumSize || Message->Length > sizeof(Message->Data))
    {
        Rpi3MmalSetAsyncResult(Decoder, E_FAIL);
        return;
    }

    Event = (const MMAL_EVENT_FORMAT_CHANGED32 *)Message->Data;
    FormatData = Message->Data + sizeof(*Event);
    ExtraDataSize = Message->Length - MinimumSize;
    if (ExtraDataSize > MMAL_FORMAT_EXTRADATA_MAX_SIZE)
    {
        Rpi3MmalSetAsyncResult(Decoder, E_FAIL);
        return;
    }

    EnterCriticalSection(&Decoder->StateLock);
    Decoder->PendingFormat.BufferSizeMin = Event->BufferSizeMin;
    Decoder->PendingFormat.BufferNumMin = Event->BufferNumMin;
    Decoder->PendingFormat.BufferSizeRecommended = Event->BufferSizeRecommended;
    Decoder->PendingFormat.BufferNumRecommended = Event->BufferNumRecommended;
    CopyMemory(&Decoder->PendingFormat.Format, FormatData,
               sizeof(Decoder->PendingFormat.Format));
    FormatData += sizeof(Decoder->PendingFormat.Format);
    CopyMemory(&Decoder->PendingFormat.Es, FormatData,
               sizeof(Decoder->PendingFormat.Es));
    FormatData += sizeof(Decoder->PendingFormat.Es);
    Decoder->PendingFormat.Format.Es = 0;
    Decoder->PendingFormat.Format.ExtraData = 0;
    Decoder->PendingFormat.Format.ExtraDataSize = ExtraDataSize;
    Decoder->PendingFormat.ExtraDataSize = ExtraDataSize;
    if (ExtraDataSize)
        CopyMemory(Decoder->PendingFormat.ExtraData, FormatData, ExtraDataSize);
    if (!++Decoder->FormatGeneration)
        ++Decoder->FormatGeneration;
    SetEvent(Decoder->FormatEvent);
    LeaveCriticalSection(&Decoder->StateLock);
}

static VOID
Rpi3MmalDrainReturnedOutputsDuringReconfigure(
    RPI3_MMAL_DECODER *Decoder);

static VOID
Rpi3MmalHandleBuffer(RPI3_MMAL_DECODER *Decoder,
                     const MMAL_BUFFER_MESSAGE *Message)
{
    UINT32 Token = Message->DriverBuffer.ClientContext;
    UINT Index;
    UINT SlotSize;
    UINT OutputOffset = 0;
    UINT OutputLength = 0;
    RPI3_MMAL_OUTPUT_SLOT *Slot;
    BOOL MessageValid = TRUE;
    BOOL QueueAgain = FALSE;
    BOOL Reconfiguring;

    if (!(Token & MMAL_TOKEN_OUTPUT))
    {
        if (Token &&
            Token == (UINT32)InterlockedCompareExchange(
                                 &Decoder->PendingInputToken, 0, 0))
        {
            HRESULT Result = S_OK;

            if (Message->BufferHeader.Flags &
                (MMAL_BUFFER_HEADER_FLAG_CORRUPTED |
                 MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED))
            {
                Result = E_FAIL;
            }
            InterlockedExchange(&Decoder->InputResult, (LONG)Result);
            SetEvent(Decoder->InputReturnedEvent);
        }
        return;
    }

    Index = Token & MMAL_TOKEN_INDEX_MASK;
    if (Index >= Decoder->OutputCount)
        return;

    EnterCriticalSection(&Decoder->StateLock);
    Slot = &Decoder->OutputSlots[Index];
    if (Slot->State != Rpi3MmalSlotQueued || !Slot->Size)
    {
        LeaveCriticalSection(&Decoder->StateLock);
        return;
    }
    SlotSize = Slot->Size;

    if (Message->PayloadInMessage > sizeof(Message->ShortData) ||
        Message->PayloadInMessage > SlotSize)
    {
        MessageValid = FALSE;
    }
    else if (Message->PayloadInMessage)
    {
        CopyMemory(Slot->ShortData,
                   Message->ShortData,
                   Message->PayloadInMessage);
        OutputOffset = 0;
        OutputLength = Message->PayloadInMessage;
    }
    else if (Message->BufferHeader.Offset > SlotSize ||
             Message->BufferHeader.Length >
                 SlotSize - Message->BufferHeader.Offset)
    {
        MessageValid = FALSE;
    }
    else
    {
        OutputOffset = Message->BufferHeader.Offset;
        OutputLength = Message->BufferHeader.Length;
    }

    Slot->Offset = MessageValid ? OutputOffset : 0;
    Slot->Length = MessageValid ? OutputLength : 0;
    Slot->Flags = Message->BufferHeader.Flags;
    Slot->PayloadInMessage = MessageValid ? Message->PayloadInMessage : 0;
    if (!MessageValid)
        Slot->Flags |= MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED;
    Slot->Pts = Message->BufferHeader.Pts;
    Slot->Dts = Message->BufferHeader.Dts;
    if (!++Decoder->NextOutputSequence)
        ++Decoder->NextOutputSequence;
    Slot->Sequence = Decoder->NextOutputSequence;
    Reconfiguring = InterlockedCompareExchange(
                        &Decoder->Reconfiguring, 0, 0) != 0;
    if (!Slot->Length &&
             !(Slot->Flags & (MMAL_BUFFER_HEADER_FLAG_EOS |
                              MMAL_BUFFER_HEADER_FLAG_CORRUPTED |
                              MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED)))
    {
        Slot->State = Rpi3MmalSlotFree;
        QueueAgain = TRUE;
    }
    else
    {
        Slot->State = Rpi3MmalSlotReady;
    }
    Rpi3MmalUpdateOutputEventsLocked(Decoder);
    LeaveCriticalSection(&Decoder->StateLock);

    if (!MessageValid)
    {
        Rpi3MmalSetAsyncResult(Decoder, E_FAIL);
        return;
    }

    if (QueueAgain)
    {
        if (!Reconfiguring &&
            !Rpi3MmalQueueOutputSlot(Decoder,
                                     Index,
                                     Rpi3MmalSlotFree,
                                     TRUE))
        {
            Rpi3MmalSetAsyncResult(Decoder, Rpi3MmalErrorFromLastError());
        }
        return;
    }

    if (Reconfiguring)
        Rpi3MmalDrainReturnedOutputsDuringReconfigure(Decoder);
}

static BOOL
Rpi3MmalFindOldestReadyOutputLocked(
    RPI3_MMAL_DECODER *Decoder,
    UINT *Index)
{
    ULONGLONG OldestSequence = ~(ULONGLONG)0;
    UINT Candidate = 0;
    UINT SlotIndex;

    for (SlotIndex = 0; SlotIndex < Decoder->OutputCount; ++SlotIndex)
    {
        const RPI3_MMAL_OUTPUT_SLOT *Slot =
            &Decoder->OutputSlots[SlotIndex];

        if (Slot->State == Rpi3MmalSlotReady &&
            Slot->Sequence < OldestSequence)
        {
            Candidate = SlotIndex;
            OldestSequence = Slot->Sequence;
        }
    }

    if (OldestSequence == ~(ULONGLONG)0)
        return FALSE;

    *Index = Candidate;
    return TRUE;
}

static HRESULT
Rpi3MmalConsumeReadyOutput(
    RPI3_MMAL_DECODER *Decoder,
    BYTE *Buffer,
    UINT BufferSize,
    BOOL Discard,
    const RPI3_MMAL_READY_OUTPUT *Expected,
    RPI3_MMAL_READY_OUTPUT *Output)
{
    BYTE EmptyBulk[8];
    RPI3_MMAL_OUTPUT_SLOT *Slot;
    BYTE *ReceiveData;
    BOOL QueueBulk = FALSE;
    UINT32 Token;
    UINT TransferSize;
    UINT Index;
    HRESULT Result = S_OK;

    if (!Discard && (!Buffer || !Output))
        return E_INVALIDARG;

    EnterCriticalSection(&Decoder->OutputBulkLock);
    EnterCriticalSection(&Decoder->StateLock);
    if (Expected)
    {
        Index = Expected->Index;
        if (Index >= Decoder->OutputCount ||
            Decoder->OutputSlots[Index].State != Rpi3MmalSlotReady ||
            Decoder->OutputSlots[Index].Sequence != Expected->Sequence)
        {
            LeaveCriticalSection(&Decoder->StateLock);
            LeaveCriticalSection(&Decoder->OutputBulkLock);
            return E_FAIL;
        }
    }
    else if (!Rpi3MmalFindOldestReadyOutputLocked(Decoder, &Index))
    {
        LeaveCriticalSection(&Decoder->StateLock);
        LeaveCriticalSection(&Decoder->OutputBulkLock);
        return S_FALSE;
    }

    Slot = &Decoder->OutputSlots[Index];
    if (Output)
    {
        Output->Index = Index;
        Output->Offset = Slot->Offset;
        Output->Length = Slot->Length;
        Output->Flags = Slot->Flags;
        Output->PayloadInMessage = Slot->PayloadInMessage;
        Output->Pts = Slot->Pts;
        Output->Dts = Slot->Dts;
        Output->Sequence = Slot->Sequence;
    }
    Slot->State = Rpi3MmalSlotConsuming;
    Rpi3MmalUpdateOutputEventsLocked(Decoder);
    LeaveCriticalSection(&Decoder->StateLock);

    if (Slot->PayloadInMessage)
    {
        if (!Discard)
        {
            if (Slot->PayloadInMessage > BufferSize)
                Result = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            else
                CopyMemory(Buffer, Slot->ShortData, Slot->PayloadInMessage);
        }
    }
    else if (Slot->Length ||
             (Slot->Flags & MMAL_BUFFER_HEADER_FLAG_EOS))
    {
        TransferSize = Slot->Length ? ALIGN_UP(Slot->Length, 4) : 8;
        if (Discard)
        {
            if (TransferSize > Decoder->DiscardSize)
            {
                Result = E_FAIL;
            }
            else
            {
                ReceiveData = Slot->Length ? Decoder->DiscardData : EmptyBulk;
                QueueBulk = TRUE;
            }
        }
        else if (Slot->Offset > BufferSize ||
                 TransferSize > BufferSize - Slot->Offset)
        {
            Result = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            if (TransferSize <= Decoder->DiscardSize)
            {
                ReceiveData = Slot->Length ? Decoder->DiscardData : EmptyBulk;
                QueueBulk = TRUE;
            }
        }
        else
        {
            ReceiveData = Slot->Length ? Buffer + Slot->Offset : EmptyBulk;
            QueueBulk = TRUE;
        }

        if (QueueBulk &&
            !Rpi3MmalBulkReceive(Decoder, ReceiveData, TransferSize,
                                 MMAL_TOKEN_OUTPUT | Index))
        {
            Result = Rpi3MmalErrorFromLastError();
        }
    }

    if (FAILED(Result) || Discard)
    {
        EnterCriticalSection(&Decoder->StateLock);
        if (Slot->State == Rpi3MmalSlotConsuming)
            Slot->State = Rpi3MmalSlotFree;
        Rpi3MmalUpdateOutputEventsLocked(Decoder);
        LeaveCriticalSection(&Decoder->StateLock);
    }

    LeaveCriticalSection(&Decoder->OutputBulkLock);
    return Result;
}

static HRESULT
Rpi3MmalDrainReturnedOutputs(RPI3_MMAL_DECODER *Decoder)
{
    HRESULT Result;

    for (;;)
    {
        Result = Rpi3MmalConsumeReadyOutput(
                     Decoder,
                     Decoder->DiscardData,
                     Decoder->DiscardSize,
                     TRUE,
                     NULL,
                     NULL);
        if (Result == S_FALSE)
            return S_OK;
        if (FAILED(Result))
            return Result;
    }
}

static VOID
Rpi3MmalDrainReturnedOutputsDuringReconfigure(
    RPI3_MMAL_DECODER *Decoder)
{
    HRESULT Result;

    if (!InterlockedCompareExchange(&Decoder->Reconfiguring, 0, 0))
        return;

    Result = Rpi3MmalDrainReturnedOutputs(Decoder);
    if (FAILED(Result))
        Rpi3MmalSetAsyncResult(Decoder, Result);
}

static DWORD WINAPI
Rpi3MmalCompletionThread(VOID *Context)
{
    RPI3_MMAL_DECODER *Decoder = Context;
    VCHIQ_AWAIT_COMPLETION_T Arguments;
    VCHIQ_COMPLETION_DATA_T Completions[MMAL_COMPLETION_COUNT];
    BYTE MessageBuffers[MMAL_COMPLETION_COUNT][VCHIQ_MAX_MSG_SIZE + sizeof(VCHIQ_HEADER_T)];
    VOID *MessagePointers[MMAL_COMPLETION_COUNT];
    DWORD BytesReturned;
    UINT TotalMessages;
    UINT Index;

    for (Index = 0; Index < MMAL_COMPLETION_COUNT; ++Index)
        MessagePointers[Index] = MessageBuffers[Index];

    while (!InterlockedCompareExchange(&Decoder->StopRequested, 0, 0))
    {
        ZeroMemory(Completions, sizeof(Completions));
        ZeroMemory(&Arguments, sizeof(Arguments));
        Arguments.count = MMAL_COMPLETION_COUNT;
        Arguments.buf = Completions;
        Arguments.msgbufsize = sizeof(MessageBuffers[0]);
        Arguments.msgbufcount = MMAL_COMPLETION_COUNT;
        Arguments.msgbufs = MessagePointers;
        TotalMessages = 0;

        if (!Rpi3MmalDeviceIoControlTimeout(Decoder,
                                            VCHIQ_IOC_AWAIT_COMPLETION,
                                            &Arguments,
                                            sizeof(Arguments),
                                            &TotalMessages,
                                            sizeof(TotalMessages),
                                            &BytesReturned,
                                            INFINITE))
        {
            if (!InterlockedCompareExchange(&Decoder->StopRequested, 0, 0))
            {
                InterlockedExchange(&Decoder->StopRequested, 1);
                Rpi3MmalSetAsyncResult(Decoder, Rpi3MmalErrorFromLastError());
            }
            break;
        }

        if (TotalMessages > MMAL_COMPLETION_COUNT)
            TotalMessages = MMAL_COMPLETION_COUNT;

        for (Index = 0; Index < TotalMessages; ++Index)
        {
            VCHIQ_HEADER_T *VchiqHeader = Completions[Index].header;
            MMAL_WORKER_HEADER32 *Header;
            UINT CopySize;

            if (Completions[Index].reason == VCHIQ_SERVICE_CLOSED)
            {
                if (!InterlockedCompareExchange(&Decoder->StopRequested, 0, 0))
                {
                    InterlockedExchange(&Decoder->StopRequested, 1);
                    Rpi3MmalSetAsyncResult(Decoder, E_FAIL);
                    SetEvent(Decoder->StopEvent);
                }
                continue;
            }
            if (Completions[Index].reason == VCHIQ_BULK_TRANSMIT_ABORTED ||
                Completions[Index].reason == VCHIQ_BULK_RECEIVE_ABORTED)
            {
                Rpi3MmalSetAsyncResult(Decoder, E_FAIL);
                continue;
            }
            if (Completions[Index].reason != VCHIQ_MESSAGE_AVAILABLE ||
                !VchiqHeader || VchiqHeader->size < sizeof(*Header))
            {
                continue;
            }

            Header = (MMAL_WORKER_HEADER32 *)VchiqHeader->data;
            if (Header->Magic != MMAL_MAGIC)
                continue;

            if (Header->MessageId == MmalWorkerBufferToHost &&
                VchiqHeader->size >= sizeof(MMAL_BUFFER_MESSAGE))
            {
                Rpi3MmalHandleBuffer(Decoder, (MMAL_BUFFER_MESSAGE *)Header);
            }
            else if (Header->MessageId == MmalWorkerEventToHost &&
                     VchiqHeader->size >= sizeof(MMAL_EVENT_MESSAGE))
            {
                MMAL_EVENT_MESSAGE *Event = (MMAL_EVENT_MESSAGE *)Header;
                if (Event->Command == MMAL_EVENT_FORMAT_CHANGED)
                    Rpi3MmalHandleFormatChanged(Decoder, Event);
                else if (Event->Command == MMAL_EVENT_ERROR)
                {
                    HRESULT Result = E_FAIL;

                    if (Event->Length >= sizeof(INT32))
                    {
                        Result = Rpi3MmalStatusToResult(
                                     *(const INT32 *)Event->Data);
                        if (SUCCEEDED(Result))
                            Result = E_FAIL;
                    }
                    Rpi3MmalSetAsyncResult(Decoder, Result);
                }
            }
            else if (Header->Waiter &&
                     Header->Waiter == (UINT32)InterlockedCompareExchange(&Decoder->PendingToken, 0, 0))
            {
                CopySize = VchiqHeader->size;
                if (CopySize > sizeof(Decoder->ControlReply))
                    CopySize = sizeof(Decoder->ControlReply);
                CopyMemory(Decoder->ControlReply, Header, CopySize);
                Decoder->ControlReplySize = CopySize;
                MemoryBarrier();
                SetEvent(Decoder->ControlEvent);
            }
        }
    }

    return 0;
}

static HRESULT
Rpi3MmalCreateComponent(RPI3_MMAL_DECODER *Decoder)
{
    MMAL_COMPONENT_CREATE Message;
    MMAL_COMPONENT_CREATE_REPLY Reply;
    HRESULT Result;

    ZeroMemory(&Message, sizeof(Message));
    Message.ClientComponent = 1;
    if (Decoder->Encoder)
        CopyMemory(Message.Name, "video_encode", sizeof("video_encode"));
    else
        CopyMemory(Message.Name, "video_decode", sizeof("video_decode"));
    Message.ProcessId = GetCurrentProcessId();
    ZeroMemory(&Reply, sizeof(Reply));

    Result = Rpi3MmalSendWait(Decoder,
                              &Message.Header,
                              sizeof(Message),
                              MmalWorkerComponentCreate,
                              &Reply,
                              sizeof(Reply),
                              FALSE);
    if (FAILED(Result))
        return Result;
    Result = Rpi3MmalStatusToResult(Reply.Status);
    if (FAILED(Result) || !Reply.ComponentHandle ||
        !Reply.InputCount || !Reply.OutputCount)
    {
        return FAILED(Result) ? Result : E_FAIL;
    }

    Decoder->ComponentHandle = Reply.ComponentHandle;
    return S_OK;
}

static HRESULT
Rpi3MmalConfigureDecoderPorts(RPI3_MMAL_DECODER *Decoder,
                              const BYTE *ExtraData,
                              UINT ExtraDataSize)
{
    UINT AlignedWidth = ALIGN_UP(Decoder->Width, 32);
    UINT AlignedHeight = ALIGN_UP(Decoder->Height, 16);
    HRESULT Result;

    Result = Rpi3MmalPortInfoGet(Decoder, MmalPortTypeInput, 0, &Decoder->Input);
    if (FAILED(Result))
        return Result;
    Result = Rpi3MmalPortInfoGet(Decoder, MmalPortTypeOutput, 0, &Decoder->Output);
    if (FAILED(Result))
        return Result;

    /* Keep input timestamps attached to reordered decoded pictures. */
    Result = Rpi3MmalPortParameterSetBoolean(
                 Decoder,
                 &Decoder->Output,
                 MMAL_PARAMETER_VIDEO_VALIDATE_TIMESTAMPS,
                 FALSE);
    if (FAILED(Result))
        return Result;

    Decoder->Input.Format.Type = 3;
    Decoder->Input.Format.Encoding = MMAL_ENCODING_H264;
    Decoder->Input.Format.EncodingVariant = 0;
    Decoder->Input.Format.Flags |= MMAL_ES_FORMAT_FLAG_FRAMED;
    Decoder->Input.Format.ExtraDataSize = ExtraDataSize;
    Decoder->Input.Es.Video.Width = AlignedWidth;
    Decoder->Input.Es.Video.Height = AlignedHeight;
    Decoder->Input.Es.Video.Crop.X = 0;
    Decoder->Input.Es.Video.Crop.Y = 0;
    Decoder->Input.Es.Video.Crop.Width = Decoder->Width;
    Decoder->Input.Es.Video.Crop.Height = Decoder->Height;
    Decoder->Input.Es.Video.FrameRate.Numerator = 0;
    Decoder->Input.Es.Video.FrameRate.Denominator = 1;
    Decoder->Input.Es.Video.PixelAspectRatio.Numerator = 1;
    Decoder->Input.Es.Video.PixelAspectRatio.Denominator = 1;
    if (ExtraDataSize)
        CopyMemory(Decoder->Input.ExtraData, ExtraData, ExtraDataSize);

    if (Decoder->Input.Port.BufferNumRecommended > Decoder->Input.Port.BufferNum)
        Decoder->Input.Port.BufferNum = Decoder->Input.Port.BufferNumRecommended;
    if (Decoder->Input.Port.BufferNum < Decoder->Input.Port.BufferNumMin)
        Decoder->Input.Port.BufferNum = Decoder->Input.Port.BufferNumMin;
    if (Decoder->Input.Port.BufferSizeRecommended > Decoder->Input.Port.BufferSize)
        Decoder->Input.Port.BufferSize = Decoder->Input.Port.BufferSizeRecommended;
    if (Decoder->Input.Port.BufferSize < Decoder->Input.Port.BufferSizeMin)
        Decoder->Input.Port.BufferSize = Decoder->Input.Port.BufferSizeMin;

    Result = Rpi3MmalPortInfoSet(Decoder, MmalPortTypeInput, 0, &Decoder->Input);
    if (FAILED(Result))
        return Result;
    Result = Rpi3MmalPortInfoGet(Decoder, MmalPortTypeOutput, 0, &Decoder->Output);
    if (FAILED(Result))
        return Result;

    Decoder->Output.Format.Type = 3;
    Decoder->Output.Format.Encoding = MMAL_ENCODING_NV12;
    Decoder->Output.Format.EncodingVariant = 0;
    Decoder->Output.Format.ExtraDataSize = 0;
    Decoder->Output.Es.Video.Width = AlignedWidth;
    Decoder->Output.Es.Video.Height = AlignedHeight;
    Decoder->Output.Es.Video.Crop.X = 0;
    Decoder->Output.Es.Video.Crop.Y = 0;
    Decoder->Output.Es.Video.Crop.Width = Decoder->Width;
    Decoder->Output.Es.Video.Crop.Height = Decoder->Height;
    Decoder->Output.Es.Video.FrameRate.Numerator = 0;
    Decoder->Output.Es.Video.FrameRate.Denominator = 1;
    Decoder->Output.Es.Video.PixelAspectRatio.Numerator = 1;
    Decoder->Output.Es.Video.PixelAspectRatio.Denominator = 1;
    if (Decoder->Output.Port.BufferNumMin > MMAL_OUTPUT_BUFFER_COUNT)
        return E_NOTIMPL;
    Decoder->Output.Port.BufferNum = MMAL_OUTPUT_BUFFER_COUNT;
    Decoder->Output.Port.BufferSize = AlignedWidth * AlignedHeight * 3 / 2;
    if (Decoder->Output.Port.BufferSize < Decoder->Output.Port.BufferSizeMin)
        Decoder->Output.Port.BufferSize = Decoder->Output.Port.BufferSizeMin;
    if (Decoder->Output.Port.BufferSize < Decoder->Output.Port.BufferSizeRecommended)
        Decoder->Output.Port.BufferSize = Decoder->Output.Port.BufferSizeRecommended;

    Result = Rpi3MmalPortInfoSet(Decoder, MmalPortTypeOutput, 0, &Decoder->Output);
    if (FAILED(Result))
        return Result;

    Result = Rpi3MmalPortInfoGet(Decoder, MmalPortTypeInput, 0, &Decoder->Input);
    if (FAILED(Result))
        return Result;
    Result = Rpi3MmalPortInfoGet(Decoder, MmalPortTypeOutput, 0, &Decoder->Output);
    if (FAILED(Result))
        return Result;

    Result = Rpi3MmalUpdateOutputGeometry(Decoder);
    if (FAILED(Result))
        return Result;
    Decoder->OutputCount = Decoder->Output.Port.BufferNum;
    if (Decoder->Output.Port.BufferNumMin > MMAL_OUTPUT_BUFFER_COUNT)
        return E_NOTIMPL;
    if (!Decoder->OutputCount)
        Decoder->OutputCount = MMAL_OUTPUT_BUFFER_COUNT;
    if (Decoder->OutputCount > MMAL_OUTPUT_BUFFER_COUNT)
        Decoder->OutputCount = MMAL_OUTPUT_BUFFER_COUNT;
    return S_OK;
}

static HRESULT
Rpi3MmalConfigureEncoderPorts(RPI3_MMAL_ENCODER *Encoder)
{
    UINT AlignedWidth = ALIGN_UP(Encoder->Width, 32);
    UINT AlignedHeight = ALIGN_UP(Encoder->Height, 16);
    UINT32 Profile[2];
    ULONGLONG InputSize;
    HRESULT Result;

    Result = Rpi3MmalPortInfoGet(Encoder,
                                 MmalPortTypeInput,
                                 0,
                                 &Encoder->Input);
    if (FAILED(Result))
        return Result;
    Result = Rpi3MmalPortInfoGet(Encoder,
                                 MmalPortTypeOutput,
                                 0,
                                 &Encoder->Output);
    if (FAILED(Result))
        return Result;

    InputSize = (ULONGLONG)AlignedWidth * AlignedHeight * 3 / 2;
    if (InputSize > MAXUINT)
        return E_OUTOFMEMORY;

    Encoder->Input.Format.Type = 3;
    Encoder->Input.Format.Encoding = MMAL_ENCODING_NV12;
    Encoder->Input.Format.EncodingVariant = 0;
    Encoder->Input.Format.Bitrate = 0;
    Encoder->Input.Format.Flags |= MMAL_ES_FORMAT_FLAG_FRAMED;
    Encoder->Input.Format.ExtraDataSize = 0;
    Encoder->Input.Es.Video.Width = AlignedWidth;
    Encoder->Input.Es.Video.Height = AlignedHeight;
    Encoder->Input.Es.Video.Crop.X = 0;
    Encoder->Input.Es.Video.Crop.Y = 0;
    Encoder->Input.Es.Video.Crop.Width = Encoder->Width;
    Encoder->Input.Es.Video.Crop.Height = Encoder->Height;
    Encoder->Input.Es.Video.FrameRate.Numerator = Encoder->FrameRateNumerator;
    Encoder->Input.Es.Video.FrameRate.Denominator = Encoder->FrameRateDenominator;
    Encoder->Input.Es.Video.PixelAspectRatio.Numerator = 1;
    Encoder->Input.Es.Video.PixelAspectRatio.Denominator = 1;
    if (Encoder->Input.Port.BufferNumRecommended > Encoder->Input.Port.BufferNum)
        Encoder->Input.Port.BufferNum = Encoder->Input.Port.BufferNumRecommended;
    if (Encoder->Input.Port.BufferNum < Encoder->Input.Port.BufferNumMin)
        Encoder->Input.Port.BufferNum = Encoder->Input.Port.BufferNumMin;
    Encoder->Input.Port.BufferSize = (UINT)InputSize;
    if (Encoder->Input.Port.BufferSize < Encoder->Input.Port.BufferSizeMin)
        return E_NOTIMPL;

    Result = Rpi3MmalPortInfoSet(Encoder,
                                 MmalPortTypeInput,
                                 0,
                                 &Encoder->Input);
    if (FAILED(Result))
        return Result;

    Encoder->Output.Format.Type = 3;
    Encoder->Output.Format.Encoding = MMAL_ENCODING_H264;
    Encoder->Output.Format.EncodingVariant = 0;
    Encoder->Output.Format.Bitrate = Encoder->Bitrate;
    Encoder->Output.Format.Flags |= MMAL_ES_FORMAT_FLAG_FRAMED;
    Encoder->Output.Format.ExtraDataSize = 0;
    Encoder->Output.Es.Video.Width = AlignedWidth;
    Encoder->Output.Es.Video.Height = AlignedHeight;
    Encoder->Output.Es.Video.Crop.X = 0;
    Encoder->Output.Es.Video.Crop.Y = 0;
    Encoder->Output.Es.Video.Crop.Width = Encoder->Width;
    Encoder->Output.Es.Video.Crop.Height = Encoder->Height;
    Encoder->Output.Es.Video.FrameRate.Numerator = 0;
    Encoder->Output.Es.Video.FrameRate.Denominator = 1;
    Encoder->Output.Es.Video.PixelAspectRatio.Numerator = 1;
    Encoder->Output.Es.Video.PixelAspectRatio.Denominator = 1;
    if (Encoder->Output.Port.BufferNumRecommended > Encoder->Output.Port.BufferNum)
        Encoder->Output.Port.BufferNum = Encoder->Output.Port.BufferNumRecommended;
    if (Encoder->Output.Port.BufferNum < Encoder->Output.Port.BufferNumMin)
        Encoder->Output.Port.BufferNum = Encoder->Output.Port.BufferNumMin;
    if (Encoder->Output.Port.BufferSizeRecommended > Encoder->Output.Port.BufferSize)
        Encoder->Output.Port.BufferSize = Encoder->Output.Port.BufferSizeRecommended;
    if (Encoder->Output.Port.BufferSize < Encoder->Output.Port.BufferSizeMin)
        Encoder->Output.Port.BufferSize = Encoder->Output.Port.BufferSizeMin;
    if (!Encoder->Output.Port.BufferSize)
        Encoder->Output.Port.BufferSize = 256 * 1024;

    Result = Rpi3MmalPortInfoSet(Encoder,
                                 MmalPortTypeOutput,
                                 0,
                                 &Encoder->Output);
    if (FAILED(Result))
        return Result;

    Profile[0] = Encoder->Profile;
    Profile[1] = Encoder->Level;
    Result = Rpi3MmalPortParameterSet(Encoder,
                                      &Encoder->Output,
                                      MMAL_PARAMETER_PROFILE,
                                      Profile,
                                      sizeof(Profile));
    if (FAILED(Result))
        return Result;
    if (Encoder->IntraPeriod)
    {
        Result = Rpi3MmalPortParameterSetUint32(
                     Encoder,
                     &Encoder->Output,
                     MMAL_PARAMETER_INTRAPERIOD,
                     Encoder->IntraPeriod);
        if (FAILED(Result))
            return Result;
    }
    Result = Rpi3MmalPortParameterSetBoolean(
                 Encoder,
                 &Encoder->Input,
                 MMAL_PARAMETER_VIDEO_IMMUTABLE_INPUT,
                 TRUE);
    if (FAILED(Result))
        return Result;
    Result = Rpi3MmalPortParameterSetBoolean(
                 Encoder,
                 &Encoder->Output,
                 MMAL_PARAMETER_VIDEO_ENCODE_INLINE_HEADER,
                 Encoder->InlineHeaders);
    if (FAILED(Result))
        return Result;

    Result = Rpi3MmalPortInfoGet(Encoder,
                                 MmalPortTypeInput,
                                 0,
                                 &Encoder->Input);
    if (FAILED(Result))
        return Result;
    Result = Rpi3MmalPortInfoGet(Encoder,
                                 MmalPortTypeOutput,
                                 0,
                                 &Encoder->Output);
    if (FAILED(Result))
        return Result;
    if (Encoder->Output.Port.BufferNumMin > MMAL_OUTPUT_BUFFER_COUNT)
        return E_NOTIMPL;

    Encoder->Pitch = AlignedWidth;
    Encoder->StorageHeight = AlignedHeight;
    Encoder->VisibleWidth = Encoder->Width;
    Encoder->VisibleHeight = Encoder->Height;
    Encoder->OutputSize = Encoder->Output.Port.BufferSize;
    Encoder->OutputCount = Encoder->Output.Port.BufferNum;
    if (!Encoder->OutputCount)
        Encoder->OutputCount = MMAL_OUTPUT_BUFFER_COUNT;
    if (Encoder->OutputCount > MMAL_OUTPUT_BUFFER_COUNT)
        Encoder->OutputCount = MMAL_OUTPUT_BUFFER_COUNT;
    return S_OK;
}

static VOID
Rpi3MmalFreeOutputBuffers(RPI3_MMAL_DECODER *Decoder)
{
    UINT Index;

    if (Decoder->DiscardData)
        VirtualFree(Decoder->DiscardData, 0, MEM_RELEASE);
    Decoder->DiscardData = NULL;
    Decoder->DiscardSize = 0;
    for (Index = 0; Index < MMAL_OUTPUT_BUFFER_COUNT; ++Index)
        ZeroMemory(&Decoder->OutputSlots[Index], sizeof(Decoder->OutputSlots[Index]));
}

static HRESULT
Rpi3MmalAllocateOutputBuffers(RPI3_MMAL_DECODER *Decoder)
{
    UINT Index;

    if (Decoder->OutputSize > MAXUINT - 3)
        return E_OUTOFMEMORY;
    Decoder->DiscardData = VirtualAlloc(
                               NULL,
                               (SIZE_T)Decoder->OutputSize + 3,
                               MEM_RESERVE | MEM_COMMIT,
                               PAGE_READWRITE);
    if (!Decoder->DiscardData)
        return E_OUTOFMEMORY;
    Decoder->DiscardSize = Decoder->OutputSize + 3;

    for (Index = 0; Index < Decoder->OutputCount; ++Index)
    {
        Decoder->OutputSlots[Index].Size = Decoder->OutputSize;
        Decoder->OutputSlots[Index].State = Rpi3MmalSlotFree;
    }
    return S_OK;
}

static HRESULT
Rpi3MmalWaitForOutputDrain(RPI3_MMAL_DECODER *Decoder)
{
    HANDLE WaitHandles[2];
    DWORD WaitStatus;

    WaitHandles[0] = Decoder->OutputDrainedEvent;
    WaitHandles[1] = Decoder->StopEvent;
    WaitStatus = WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles, FALSE,
                                        MMAL_CONTROL_TIMEOUT_MS);
    if (WaitStatus == WAIT_OBJECT_0)
        return S_OK;
    if (WaitStatus == WAIT_OBJECT_0 + 1)
        return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
    if (WaitStatus == WAIT_TIMEOUT)
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    return Rpi3MmalErrorFromLastError();
}

static HRESULT
Rpi3MmalReconfigureDecoderOutput(RPI3_MMAL_DECODER *Decoder)
{
    RPI3_MMAL_PENDING_FORMAT Pending;
    ULONGLONG BufferSize;
    UINT Generation;
    UINT Width;
    UINT Height;
    UINT Index;
    HRESULT Result;

    EnterCriticalSection(&Decoder->StateLock);
    Generation = Decoder->FormatGeneration;
    if (!Generation || Generation == Decoder->AppliedFormatGeneration)
    {
        ResetEvent(Decoder->FormatEvent);
        LeaveCriticalSection(&Decoder->StateLock);
        return S_FALSE;
    }

    Pending = Decoder->PendingFormat;
    InterlockedExchange(&Decoder->Reconfiguring, 1);
    Rpi3MmalUpdateOutputEventsLocked(Decoder);
    LeaveCriticalSection(&Decoder->StateLock);

    if (Pending.Format.Type != 3 || Pending.BufferNumMin > MMAL_OUTPUT_BUFFER_COUNT)
    {
        Result = E_NOTIMPL;
        goto Failure;
    }

    Result = Rpi3MmalDrainReturnedOutputs(Decoder);
    if (FAILED(Result))
        goto Failure;

    if (Decoder->OutputEnabled)
    {
        Result = Rpi3MmalPortAction(Decoder, &Decoder->Output, MmalPortActionDisable);
        if (FAILED(Result))
            goto Failure;
        Decoder->OutputEnabled = FALSE;
    }

    Result = Rpi3MmalWaitForOutputDrain(Decoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalDrainReturnedOutputs(Decoder);
    if (FAILED(Result))
        goto Failure;

    Rpi3MmalFreeOutputBuffers(Decoder);
    Decoder->Output.Format = Pending.Format;
    Decoder->Output.Format.Type = 3;
    Decoder->Output.Format.Encoding = MMAL_ENCODING_NV12;
    Decoder->Output.Format.EncodingVariant = 0;
    Decoder->Output.Format.Es = 0;
    Decoder->Output.Format.ExtraData = 0;
    Decoder->Output.Format.ExtraDataSize = Pending.ExtraDataSize;
    Decoder->Output.Es = Pending.Es;
    ZeroMemory(Decoder->Output.ExtraData, sizeof(Decoder->Output.ExtraData));
    if (Pending.ExtraDataSize)
    {
        CopyMemory(Decoder->Output.ExtraData, Pending.ExtraData,
                   Pending.ExtraDataSize);
    }

    Decoder->Output.Port.BufferNumMin = Pending.BufferNumMin;
    Decoder->Output.Port.BufferNumRecommended = Pending.BufferNumRecommended;
    Decoder->Output.Port.BufferSizeMin = Pending.BufferSizeMin;
    Decoder->Output.Port.BufferSizeRecommended = Pending.BufferSizeRecommended;
    Decoder->Output.Port.BufferNum = MMAL_OUTPUT_BUFFER_COUNT;

    Width = Decoder->Output.Es.Video.Width;
    Height = Decoder->Output.Es.Video.Height;
    if (!Width)
        Width = ALIGN_UP(Decoder->Width, 32);
    if (!Height)
        Height = ALIGN_UP(Decoder->Height, 16);
    BufferSize = (ULONGLONG)Width * Height * 3 / 2;
    if (Pending.BufferSizeMin > BufferSize)
        BufferSize = Pending.BufferSizeMin;
    if (Pending.BufferSizeRecommended > BufferSize)
        BufferSize = Pending.BufferSizeRecommended;
    if (BufferSize > MAXUINT)
    {
        Result = E_OUTOFMEMORY;
        goto Failure;
    }
    Decoder->Output.Port.BufferSize = (UINT)BufferSize;

    Result = Rpi3MmalPortInfoSet(Decoder, MmalPortTypeOutput, 0, &Decoder->Output);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalPortInfoGet(Decoder, MmalPortTypeOutput, 0, &Decoder->Output);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalUpdateOutputGeometry(Decoder);
    if (FAILED(Result))
        goto Failure;
    if (Decoder->Output.Port.BufferNumMin > MMAL_OUTPUT_BUFFER_COUNT)
    {
        Result = E_NOTIMPL;
        goto Failure;
    }
    Decoder->OutputCount = Decoder->Output.Port.BufferNum;
    if (!Decoder->OutputCount)
        Decoder->OutputCount = MMAL_OUTPUT_BUFFER_COUNT;
    if (Decoder->OutputCount > MMAL_OUTPUT_BUFFER_COUNT)
        Decoder->OutputCount = MMAL_OUTPUT_BUFFER_COUNT;

    Result = Rpi3MmalAllocateOutputBuffers(Decoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalPortAction(Decoder, &Decoder->Output, MmalPortActionEnable);
    if (FAILED(Result))
        goto Failure;
    Decoder->OutputEnabled = TRUE;
    InterlockedExchange(&Decoder->Reconfiguring, 0);

    for (Index = 0; Index < Decoder->OutputCount; ++Index)
    {
        if (!Rpi3MmalQueueOutputSlot(Decoder,
                                     Index,
                                     Rpi3MmalSlotFree,
                                     FALSE))
        {
            Result = Rpi3MmalErrorFromLastError();
            goto Failure;
        }
    }

    EnterCriticalSection(&Decoder->StateLock);
    Decoder->AppliedFormatGeneration = Generation;
    if (Decoder->FormatGeneration == Generation)
        ResetEvent(Decoder->FormatEvent);
    LeaveCriticalSection(&Decoder->StateLock);
    InterlockedExchange(&Decoder->FormatChanged, 1);
    return S_OK;

Failure:
    InterlockedExchange(&Decoder->Reconfiguring, 0);
    Rpi3MmalSetAsyncResult(Decoder, Result);
    return Result;
}

static HRESULT
Rpi3MmalReconfigureEncoderOutput(RPI3_MMAL_ENCODER *Encoder)
{
    RPI3_MMAL_PENDING_FORMAT Pending;
    UINT Generation;
    UINT Index;
    HRESULT Result;

    EnterCriticalSection(&Encoder->StateLock);
    Generation = Encoder->FormatGeneration;
    if (!Generation || Generation == Encoder->AppliedFormatGeneration)
    {
        ResetEvent(Encoder->FormatEvent);
        LeaveCriticalSection(&Encoder->StateLock);
        return S_FALSE;
    }
    Pending = Encoder->PendingFormat;
    InterlockedExchange(&Encoder->Reconfiguring, 1);
    Rpi3MmalUpdateOutputEventsLocked(Encoder);
    LeaveCriticalSection(&Encoder->StateLock);

    if (Pending.Format.Type != 3 ||
        Pending.Format.Encoding != MMAL_ENCODING_H264 ||
        Pending.BufferNumMin > MMAL_OUTPUT_BUFFER_COUNT)
    {
        Result = E_NOTIMPL;
        goto Failure;
    }

    Result = Rpi3MmalDrainReturnedOutputs(Encoder);
    if (FAILED(Result))
        goto Failure;
    if (Encoder->OutputEnabled)
    {
        Result = Rpi3MmalPortAction(Encoder,
                                    &Encoder->Output,
                                    MmalPortActionDisable);
        if (FAILED(Result))
            goto Failure;
        Encoder->OutputEnabled = FALSE;
    }
    Result = Rpi3MmalWaitForOutputDrain(Encoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalDrainReturnedOutputs(Encoder);
    if (FAILED(Result))
        goto Failure;

    Rpi3MmalFreeOutputBuffers(Encoder);
    Encoder->Output.Format = Pending.Format;
    Encoder->Output.Format.Type = 3;
    Encoder->Output.Format.Encoding = MMAL_ENCODING_H264;
    Encoder->Output.Format.EncodingVariant = 0;
    Encoder->Output.Format.Es = 0;
    Encoder->Output.Format.ExtraData = 0;
    Encoder->Output.Format.ExtraDataSize = Pending.ExtraDataSize;
    Encoder->Output.Es = Pending.Es;
    ZeroMemory(Encoder->Output.ExtraData,
               sizeof(Encoder->Output.ExtraData));
    if (Pending.ExtraDataSize)
    {
        CopyMemory(Encoder->Output.ExtraData,
                   Pending.ExtraData,
                   Pending.ExtraDataSize);
    }
    Encoder->Output.Port.BufferNumMin = Pending.BufferNumMin;
    Encoder->Output.Port.BufferNumRecommended = Pending.BufferNumRecommended;
    Encoder->Output.Port.BufferSizeMin = Pending.BufferSizeMin;
    Encoder->Output.Port.BufferSizeRecommended = Pending.BufferSizeRecommended;
    Encoder->Output.Port.BufferNum = Pending.BufferNumRecommended;
    if (Encoder->Output.Port.BufferNum < Pending.BufferNumMin)
        Encoder->Output.Port.BufferNum = Pending.BufferNumMin;
    if (!Encoder->Output.Port.BufferNum)
        Encoder->Output.Port.BufferNum = MMAL_OUTPUT_BUFFER_COUNT;
    Encoder->Output.Port.BufferSize = Pending.BufferSizeRecommended;
    if (Encoder->Output.Port.BufferSize < Pending.BufferSizeMin)
        Encoder->Output.Port.BufferSize = Pending.BufferSizeMin;
    if (!Encoder->Output.Port.BufferSize)
        Encoder->Output.Port.BufferSize = 256 * 1024;

    Result = Rpi3MmalPortInfoSet(Encoder,
                                 MmalPortTypeOutput,
                                 0,
                                 &Encoder->Output);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalPortInfoGet(Encoder,
                                 MmalPortTypeOutput,
                                 0,
                                 &Encoder->Output);
    if (FAILED(Result))
        goto Failure;
    if (Encoder->Output.Port.BufferNumMin > MMAL_OUTPUT_BUFFER_COUNT)
    {
        Result = E_NOTIMPL;
        goto Failure;
    }
    Encoder->OutputSize = Encoder->Output.Port.BufferSize;
    Encoder->OutputCount = Encoder->Output.Port.BufferNum;
    if (!Encoder->OutputCount)
        Encoder->OutputCount = MMAL_OUTPUT_BUFFER_COUNT;
    if (Encoder->OutputCount > MMAL_OUTPUT_BUFFER_COUNT)
        Encoder->OutputCount = MMAL_OUTPUT_BUFFER_COUNT;

    Result = Rpi3MmalAllocateOutputBuffers(Encoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalPortAction(Encoder,
                                &Encoder->Output,
                                MmalPortActionEnable);
    if (FAILED(Result))
        goto Failure;
    Encoder->OutputEnabled = TRUE;
    InterlockedExchange(&Encoder->Reconfiguring, 0);

    for (Index = 0; Index < Encoder->OutputCount; ++Index)
    {
        if (!Rpi3MmalQueueOutputSlot(Encoder,
                                     Index,
                                     Rpi3MmalSlotFree,
                                     FALSE))
        {
            Result = Rpi3MmalErrorFromLastError();
            goto Failure;
        }
    }

    EnterCriticalSection(&Encoder->StateLock);
    Encoder->AppliedFormatGeneration = Generation;
    if (Encoder->FormatGeneration == Generation)
        ResetEvent(Encoder->FormatEvent);
    LeaveCriticalSection(&Encoder->StateLock);
    InterlockedExchange(&Encoder->FormatChanged, 1);
    return S_OK;

Failure:
    InterlockedExchange(&Encoder->Reconfiguring, 0);
    Rpi3MmalSetAsyncResult(Encoder, Result);
    return Result;
}

static HRESULT
Rpi3MmalReconfigureOutput(RPI3_MMAL_DECODER *Session)
{
    if (Session->Encoder)
        return Rpi3MmalReconfigureEncoderOutput(Session);

    return Rpi3MmalReconfigureDecoderOutput(Session);
}

static HRESULT
Rpi3MmalStart(RPI3_MMAL_DECODER *Decoder)
{
    UINT Index;
    HRESULT Result;

    Result = Rpi3MmalComponentCommand(Decoder, MmalWorkerComponentEnable);
    if (FAILED(Result))
        return Result;
    Decoder->ComponentEnabled = TRUE;
    Result = Rpi3MmalPortAction(Decoder, &Decoder->Output, MmalPortActionEnable);
    if (FAILED(Result))
        return Result;
    Decoder->OutputEnabled = TRUE;
    Result = Rpi3MmalPortAction(Decoder, &Decoder->Input, MmalPortActionEnable);
    if (FAILED(Result))
        return Result;
    Decoder->InputEnabled = TRUE;

    for (Index = 0; Index < Decoder->OutputCount; ++Index)
    {
        if (!Rpi3MmalQueueOutputSlot(Decoder,
                                     Index,
                                     Rpi3MmalSlotFree,
                                     FALSE))
            return Rpi3MmalErrorFromLastError();
    }
    return S_OK;
}

static HRESULT
Rpi3MmalOpenSession(RPI3_MMAL_DECODER *Session)
{
    VCHIQ_CREATE_SERVICE_T CreateService;
    UINT Version = VCHIQ_VERSION;
    DWORD ThreadId;
    HRESULT Result;

    Session->Device = INVALID_HANDLE_VALUE;
    Session->NextToken = 1;
    Session->AsyncResult = S_OK;
    InitializeCriticalSection(&Session->ControlLock);
    InitializeCriticalSection(&Session->InputLock);
    InitializeCriticalSection(&Session->OutputLock);
    InitializeCriticalSection(&Session->OutputBulkLock);
    InitializeCriticalSection(&Session->StateLock);

    Session->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Session->ControlEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    Session->InputReturnedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Session->FrameEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Session->FormatEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Session->OutputDrainedEvent = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (!Session->StopEvent || !Session->ControlEvent ||
        !Session->InputReturnedEvent || !Session->FrameEvent ||
        !Session->FormatEvent || !Session->OutputDrainedEvent)
    {
        return Rpi3MmalErrorFromLastError();
    }

    Session->Device = CreateFileW(VCHIQ_USERMODE_PATH_W,
                                  GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                  NULL);
    if (Session->Device == INVALID_HANDLE_VALUE)
        return Rpi3MmalErrorFromLastError();

    if (!Rpi3MmalDeviceIoControl(Session,
                                 VCHIQ_IOC_LIB_VERSION,
                                 &Version,
                                 sizeof(Version),
                                 NULL,
                                 0,
                                 NULL) ||
        !Rpi3MmalDeviceIoControl(Session,
                                 VCHIQ_IOC_CONNECT,
                                 NULL,
                                 0,
                                 NULL,
                                 0,
                                 NULL))
    {
        return Rpi3MmalErrorFromLastError();
    }

    ZeroMemory(&CreateService, sizeof(CreateService));
    CreateService.params.fourcc = VCHIQ_MAKE_FOURCC('m', 'm', 'a', 'l');
    CreateService.params.userdata = Session;
    CreateService.params.version = MMAL_WORKER_VERSION;
    CreateService.params.version_min = MMAL_WORKER_VERSION_MINIMUM;
    CreateService.is_open = TRUE;
    CreateService.handle = VCHIQ_SERVICE_HANDLE_INVALID;
    if (!Rpi3MmalDeviceIoControl(Session,
                                 VCHIQ_IOC_CREATE_SERVICE,
                                 &CreateService,
                                 sizeof(CreateService),
                                 NULL,
                                 0,
                                 NULL))
    {
        return Rpi3MmalErrorFromLastError();
    }

    Session->CompletionThread = CreateThread(NULL,
                                              0,
                                              Rpi3MmalCompletionThread,
                                              Session,
                                              0,
                                              &ThreadId);
    if (!Session->CompletionThread)
        return Rpi3MmalErrorFromLastError();

    Result = Rpi3MmalCreateComponent(Session);
    if (FAILED(Result))
        return Result;

    return S_OK;
}

BOOL WINAPI
Rpi3MmalQueryCaps(RPI3_MMAL_CAPS *Caps)
{
    if (!Caps)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ZeroMemory(Caps, sizeof(*Caps));
    Caps->AbiVersion = RPI3_MMAL_ABI_VERSION;
    Caps->MaximumWidth = 1920;
    Caps->MaximumHeight = 1088;
    Caps->MaximumMacroblocksPerSecond = 244800;
    Caps->OutputFourCC = MMAL_ENCODING_NV12;
    Caps->H264 = TRUE;
    Caps->BulkTransfer = TRUE;
    Caps->SharedOutput = TRUE;
    Caps->H264Encode = TRUE;
    return TRUE;
}

HRESULT WINAPI
Rpi3MmalCreateH264Decoder(UINT Width,
                         UINT Height,
                         const BYTE *ExtraData,
                         UINT ExtraDataSize,
                         RPI3_MMAL_DECODER **DecoderOut)
{
    RPI3_MMAL_DECODER *Decoder;
    HRESULT Result;

    if (!DecoderOut || !Width || !Height || Width > 1920 || Height > 1088 ||
        ExtraDataSize > MMAL_FORMAT_EXTRADATA_MAX_SIZE ||
        (ExtraDataSize && !ExtraData))
    {
        return E_INVALIDARG;
    }
    *DecoderOut = NULL;

    Decoder = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Decoder));
    if (!Decoder)
        return E_OUTOFMEMORY;

    Decoder->Width = Width;
    Decoder->Height = Height;
    Decoder->VisibleWidth = Width;
    Decoder->VisibleHeight = Height;
    Result = Rpi3MmalOpenSession(Decoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalConfigureDecoderPorts(Decoder,
                                            ExtraData,
                                            ExtraDataSize);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalAllocateOutputBuffers(Decoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalStart(Decoder);
    if (FAILED(Result))
        goto Failure;

    *DecoderOut = Decoder;
    return S_OK;

Failure:
    Rpi3MmalDestroyDecoder(Decoder);
    return Result;
}

HRESULT WINAPI
Rpi3MmalSubmit(RPI3_MMAL_DECODER *Decoder,
               const BYTE *Data,
               UINT DataSize,
               UINT Flags,
               LONGLONG Pts,
               LONGLONG Dts)
{
    static const BYTE EmptyBulk[8];
    HANDLE WaitHandles[2];
    MMAL_BUFFER_MESSAGE Message;
    const BYTE *Transfer = NULL;
    BYTE *AllocatedTransfer = NULL;
    UINT TransferSize = 0;
    UINT32 MmalFlags = 0;
    UINT32 Token;
    DWORD WaitStatus;
    HRESULT Result = S_OK;

    if (!Decoder || (!Data && DataSize) || (!DataSize && !(Flags & RPI3_MMAL_SUBMIT_EOS)))
        return E_INVALIDARG;
    if (InterlockedCompareExchange(&Decoder->StopRequested, 0, 0))
        return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
    Result = (HRESULT)InterlockedCompareExchange(&Decoder->AsyncResult, S_OK, S_OK);
    if (FAILED(Result))
        return Result;

    if (Flags & RPI3_MMAL_SUBMIT_FRAME_START)
        MmalFlags |= MMAL_BUFFER_HEADER_FLAG_FRAME_START;
    if (Flags & RPI3_MMAL_SUBMIT_FRAME_END)
        MmalFlags |= MMAL_BUFFER_HEADER_FLAG_FRAME_END;
    if (Flags & RPI3_MMAL_SUBMIT_CONFIG)
        MmalFlags |= MMAL_BUFFER_HEADER_FLAG_CONFIG;
    if (Flags & RPI3_MMAL_SUBMIT_KEYFRAME)
        MmalFlags |= MMAL_BUFFER_HEADER_FLAG_KEYFRAME;
    if (Flags & RPI3_MMAL_SUBMIT_DISCONTINUITY)
        MmalFlags |= MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY;
    if (Flags & RPI3_MMAL_SUBMIT_EOS)
        MmalFlags |= MMAL_BUFFER_HEADER_FLAG_EOS;

    if (DataSize)
    {
        if (DataSize > MAXUINT - 3)
            return E_INVALIDARG;
        TransferSize = ALIGN_UP(DataSize, 4);
        if (TransferSize == DataSize)
        {
            Transfer = Data;
        }
        else
        {
            AllocatedTransfer = HeapAlloc(GetProcessHeap(),
                                          HEAP_ZERO_MEMORY,
                                          TransferSize);
            if (!AllocatedTransfer)
                return E_OUTOFMEMORY;
            CopyMemory(AllocatedTransfer, Data, DataSize);
            Transfer = AllocatedTransfer;
        }
    }
    else if (Flags & RPI3_MMAL_SUBMIT_EOS)
    {
        Transfer = (BYTE *)EmptyBulk;
        TransferSize = sizeof(EmptyBulk);
    }

    ZeroMemory(&Message, sizeof(Message));
    Message.Header.Magic = MMAL_MAGIC;
    Message.Header.MessageId = DataSize ? MmalWorkerBufferFromHost :
                                         MmalWorkerBufferFromHostZeroLength;
    Message.DriverBuffer.Magic = MMAL_MAGIC;
    Message.DriverBuffer.ComponentHandle = Decoder->ComponentHandle;
    Message.DriverBuffer.PortHandle = Decoder->Input.Handle;
    Token = (UINT32)InterlockedIncrement(&Decoder->NextToken) & ~MMAL_TOKEN_OUTPUT;
    if (!Token)
        Token = 1;
    Message.DriverBuffer.ClientContext = Token;
    Message.BufferHeader.Data = Token;
    Message.BufferHeader.AllocationSize = DataSize;
    Message.BufferHeader.Length = DataSize;
    Message.BufferHeader.Flags = MmalFlags;
    Message.BufferHeader.Pts = Pts;
    Message.BufferHeader.Dts = Dts;
    if (Decoder->Encoder && DataSize)
    {
        Message.TypeSpecific.Video.PlaneCount = 2;
        Message.TypeSpecific.Video.Offset[0] = 0;
        Message.TypeSpecific.Video.Offset[1] =
            Decoder->Pitch * Decoder->StorageHeight;
        Message.TypeSpecific.Video.Pitch[0] = Decoder->Pitch;
        Message.TypeSpecific.Video.Pitch[1] = Decoder->Pitch;
    }

    EnterCriticalSection(&Decoder->InputLock);
    ResetEvent(Decoder->InputReturnedEvent);
    InterlockedExchange(&Decoder->InputResult, (LONG)S_OK);
    InterlockedExchange(&Decoder->PendingInputToken, (LONG)Token);
    if (!Rpi3MmalQueueMessage(Decoder, &Message, sizeof(Message)))
    {
        Result = Rpi3MmalErrorFromLastError();
        goto InputComplete;
    }

    if (TransferSize && !Rpi3MmalBulkTransmit(Decoder, Transfer, TransferSize, Token))
        Result = Rpi3MmalErrorFromLastError();
    else if (TransferSize)
        Decoder->InputBulkSent = TRUE;

    if (SUCCEEDED(Result))
    {
        WaitHandles[0] = Decoder->InputReturnedEvent;
        WaitHandles[1] = Decoder->StopEvent;
        WaitStatus = WaitForMultipleObjects(ARRAYSIZE(WaitHandles),
                                            WaitHandles,
                                            FALSE,
                                            MMAL_CONTROL_TIMEOUT_MS);
        if (WaitStatus == WAIT_OBJECT_0)
        {
            Result = (HRESULT)InterlockedCompareExchange(
                                  &Decoder->InputResult,
                                  S_OK,
                                  S_OK);
        }
        else if (WaitStatus == WAIT_OBJECT_0 + 1)
        {
            Result = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
        }
        else if (WaitStatus == WAIT_TIMEOUT)
        {
            Result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        else
        {
            Result = Rpi3MmalErrorFromLastError();
        }
    }

InputComplete:
    InterlockedCompareExchange(&Decoder->PendingInputToken, 0, (LONG)Token);
    LeaveCriticalSection(&Decoder->InputLock);

    if (AllocatedTransfer)
        HeapFree(GetProcessHeap(), 0, AllocatedTransfer);
    if (FAILED(Result))
    {
        Rpi3MmalSetAsyncResult(Decoder, Result);
        return Result;
    }

    return Result;
}

typedef struct _RPI3_MMAL_FIXED_OUTPUT
{
    BYTE *Buffer;
    UINT BufferSize;
    UINT Pitch;
} RPI3_MMAL_FIXED_OUTPUT;

static HRESULT CALLBACK
Rpi3MmalSelectFixedOutput(void *Context,
                          const RPI3_MMAL_FRAME *Frame,
                          BYTE **Buffer,
                          UINT *BufferSize,
                          UINT *Pitch)
{
    RPI3_MMAL_FIXED_OUTPUT *Output = Context;

    UNREFERENCED_PARAMETER(Frame);
    if (!Output || !Output->Buffer || !Buffer || !BufferSize || !Pitch)
        return E_INVALIDARG;

    *Buffer = Output->Buffer;
    *BufferSize = Output->BufferSize;
    *Pitch = Output->Pitch;
    return S_OK;
}

static BOOL
Rpi3MmalSnapshotOldestReadyOutput(RPI3_MMAL_DECODER *Decoder,
                                  RPI3_MMAL_READY_OUTPUT *Output)
{
    const RPI3_MMAL_OUTPUT_SLOT *Slot;
    UINT Index;

    EnterCriticalSection(&Decoder->StateLock);
    if (!Rpi3MmalFindOldestReadyOutputLocked(Decoder, &Index))
    {
        LeaveCriticalSection(&Decoder->StateLock);
        return FALSE;
    }

    Slot = &Decoder->OutputSlots[Index];
    Output->Index = Index;
    Output->Offset = Slot->Offset;
    Output->Length = Slot->Length;
    Output->Flags = Slot->Flags;
    Output->PayloadInMessage = Slot->PayloadInMessage;
    Output->Pts = Slot->Pts;
    Output->Dts = Slot->Dts;
    Output->Sequence = Slot->Sequence;
    LeaveCriticalSection(&Decoder->StateLock);
    return TRUE;
}

static HRESULT
Rpi3MmalReceiveNV12Internal(RPI3_MMAL_DECODER *Decoder,
                            DWORD TimeoutMilliseconds,
                            RPI3_MMAL_SELECT_NV12_OUTPUT SelectOutput,
                            void *Context,
                            RPI3_MMAL_FRAME *Frame)
{
    HANDLE WaitHandles[3];
    RPI3_MMAL_READY_OUTPUT Output;
    BYTE *Buffer;
    BYTE *ReceiveBuffer;
    UINT BufferSize;
    UINT Pitch;
    UINT ReceiveBufferSize;
    DWORD WaitStatus;
    UINT Index;
    UINT Row;
    UINT RequiredSize;
    UINT SourcePitch;
    UINT SourceHeight;
    ULONGLONG Size;
    UINT FrameFlags = 0;
    BOOL DirectOutput;
    HRESULT Result = S_OK;

    if (!Decoder || Decoder->Encoder || !SelectOutput || !Frame)
        return E_INVALIDARG;

    WaitHandles[0] = Decoder->FormatEvent;
    WaitHandles[1] = Decoder->FrameEvent;
    WaitHandles[2] = Decoder->StopEvent;

    for (;;)
    {
        Result = (HRESULT)InterlockedCompareExchange(&Decoder->AsyncResult, S_OK, S_OK);
        if (FAILED(Result))
            return Result;

        WaitStatus = WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles, FALSE,
                                            TimeoutMilliseconds);
        if (WaitStatus == WAIT_OBJECT_0)
        {
            Result = Rpi3MmalReconfigureOutput(Decoder);
            if (FAILED(Result))
                return Result;
            continue;
        }
        if (WaitStatus == WAIT_OBJECT_0 + 2)
            return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
        if (WaitStatus != WAIT_OBJECT_0 + 1)
        {
            return WaitStatus == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) :
                                                Rpi3MmalErrorFromLastError();
        }

        EnterCriticalSection(&Decoder->StateLock);
        if (Decoder->FormatGeneration != Decoder->AppliedFormatGeneration)
        {
            LeaveCriticalSection(&Decoder->StateLock);
            continue;
        }
        LeaveCriticalSection(&Decoder->StateLock);

        ZeroMemory(&Output, sizeof(Output));
        if (!Rpi3MmalSnapshotOldestReadyOutput(Decoder, &Output))
            continue;

        ZeroMemory(Frame, sizeof(*Frame));
        Frame->Width = Decoder->Width;
        Frame->Height = Decoder->Height;
        Frame->VisibleWidth = Decoder->VisibleWidth;
        Frame->VisibleHeight = Decoder->VisibleHeight;
        Frame->Pitch = Decoder->Pitch;
        Frame->StorageHeight = Decoder->StorageHeight;
        Frame->Size = Decoder->OutputSize;
        Frame->Pts = Output.Pts;
        Frame->Dts = Output.Dts;

        Buffer = NULL;
        BufferSize = 0;
        Pitch = 0;
        Result = SelectOutput(Context, Frame, &Buffer, &BufferSize, &Pitch);
        if (FAILED(Result) || !Buffer)
        {
            HRESULT DiscardResult = Rpi3MmalConsumeReadyOutput(
                                        Decoder,
                                        Decoder->DiscardData,
                                        Decoder->DiscardSize,
                                        TRUE,
                                        &Output,
                                        NULL);
            if (FAILED(DiscardResult))
                Result = DiscardResult;
            Rpi3MmalSetAsyncResult(Decoder, Result);
            return Result;
        }

        SourcePitch = Decoder->Pitch;
        SourceHeight = Decoder->StorageHeight;
        if (Pitch < Decoder->VisibleWidth)
            Result = E_INVALIDARG;
        else
        {
            Size = (ULONGLONG)Pitch * Decoder->VisibleHeight +
                   (ULONGLONG)Pitch * ((Decoder->VisibleHeight + 1) / 2);
            if (Size > MAXUINT || BufferSize < Size)
                Result = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            else
                RequiredSize = (UINT)Size;
        }
        if (FAILED(Result))
        {
            HRESULT DiscardResult = Rpi3MmalConsumeReadyOutput(
                                        Decoder,
                                        Decoder->DiscardData,
                                        Decoder->DiscardSize,
                                        TRUE,
                                        &Output,
                                        NULL);
            if (FAILED(DiscardResult))
                Result = DiscardResult;
            Rpi3MmalSetAsyncResult(Decoder, Result);
            return Result;
        }

        DirectOutput = Output.Offset == 0 &&
                       Pitch == SourcePitch &&
                       BufferSize >= Decoder->OutputSize;
        ReceiveBuffer = DirectOutput ? Buffer : Decoder->DiscardData;
        ReceiveBufferSize = DirectOutput ? BufferSize : Decoder->DiscardSize;
        if (!ReceiveBuffer || !ReceiveBufferSize)
        {
            Result = E_OUTOFMEMORY;
            Rpi3MmalSetAsyncResult(Decoder, Result);
            return Result;
        }

        Result = Rpi3MmalConsumeReadyOutput(
                     Decoder,
                     ReceiveBuffer,
                     ReceiveBufferSize,
                     FALSE,
                     &Output,
                     &Output);
        if (Result == S_FALSE)
            continue;
        if (FAILED(Result))
        {
            Rpi3MmalSetAsyncResult(Decoder, Result);
            return Result;
        }
        Index = Output.Index;
        break;
    }

    Size = (ULONGLONG)SourcePitch * SourceHeight +
           (ULONGLONG)SourcePitch * (SourceHeight / 2);
    if (Output.Length && (Size > MAXUINT || Output.Length < Size))
    {
        Output.Flags |= MMAL_BUFFER_HEADER_FLAG_CORRUPTED;
        Result = E_FAIL;
    }
    else if (Output.Length && !DirectOutput)
    {
        for (Row = 0; Row < Decoder->VisibleHeight; ++Row)
            CopyMemory(Buffer + Row * Pitch,
                       Decoder->DiscardData + Output.Offset +
                           Row * SourcePitch,
                       Decoder->VisibleWidth);
        for (Row = 0; Row < (Decoder->VisibleHeight + 1) / 2; ++Row)
            CopyMemory(Buffer + Pitch * Decoder->VisibleHeight + Row * Pitch,
                       Decoder->DiscardData + Output.Offset +
                           SourcePitch * SourceHeight + Row * SourcePitch,
                       Decoder->VisibleWidth);
    }

    if (Output.Flags & MMAL_BUFFER_HEADER_FLAG_EOS)
        FrameFlags |= RPI3_MMAL_FRAME_EOS;
    if (Output.Flags & (MMAL_BUFFER_HEADER_FLAG_CORRUPTED |
                        MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED))
    {
        FrameFlags |= RPI3_MMAL_FRAME_CORRUPT;
        Result = E_FAIL;
    }
    if (InterlockedExchange(&Decoder->FormatChanged, 0))
        FrameFlags |= RPI3_MMAL_FRAME_FORMAT_CHANGED;

    Frame->Pitch = Pitch;
    Frame->StorageHeight = DirectOutput ? SourceHeight : Decoder->VisibleHeight;
    Frame->Size = RequiredSize;
    Frame->Flags = FrameFlags;
    Frame->Pts = Output.Pts;
    Frame->Dts = Output.Dts;

    if (Output.Flags & MMAL_BUFFER_HEADER_FLAG_EOS)
    {
        EnterCriticalSection(&Decoder->StateLock);
        if (Decoder->OutputSlots[Index].State == Rpi3MmalSlotConsuming)
            Decoder->OutputSlots[Index].State = Rpi3MmalSlotFree;
        Rpi3MmalUpdateOutputEventsLocked(Decoder);
        LeaveCriticalSection(&Decoder->StateLock);
    }
    else if (!Rpi3MmalQueueOutputSlot(Decoder,
                                      Index,
                                      Rpi3MmalSlotConsuming,
                                      TRUE))
    {
        HRESULT QueueResult = Rpi3MmalErrorFromLastError();
        Rpi3MmalSetAsyncResult(Decoder, QueueResult);
        return QueueResult;
    }

    return Result;
}

HRESULT WINAPI
Rpi3MmalReceiveNV12(RPI3_MMAL_DECODER *Decoder,
                    BYTE *Buffer,
                    UINT BufferSize,
                    UINT Pitch,
                    DWORD TimeoutMilliseconds,
                    RPI3_MMAL_FRAME *Frame)
{
    RPI3_MMAL_FIXED_OUTPUT Output;
    HRESULT Result;

    if (!Decoder || !Buffer || !Frame)
        return E_INVALIDARG;

    Output.Buffer = Buffer;
    Output.BufferSize = BufferSize;
    Output.Pitch = Pitch;
    EnterCriticalSection(&Decoder->OutputLock);
    Result = Rpi3MmalReceiveNV12Internal(Decoder,
                                         TimeoutMilliseconds,
                                         Rpi3MmalSelectFixedOutput,
                                         &Output,
                                         Frame);
    LeaveCriticalSection(&Decoder->OutputLock);
    return Result;
}

HRESULT WINAPI
Rpi3MmalReceiveNV12Selected(RPI3_MMAL_DECODER *Decoder,
                            DWORD TimeoutMilliseconds,
                            RPI3_MMAL_SELECT_NV12_OUTPUT SelectOutput,
                            void *Context,
                            RPI3_MMAL_FRAME *Frame)
{
    HRESULT Result;

    if (!Decoder || !SelectOutput || !Frame)
        return E_INVALIDARG;

    EnterCriticalSection(&Decoder->OutputLock);
    Result = Rpi3MmalReceiveNV12Internal(Decoder,
                                         TimeoutMilliseconds,
                                         SelectOutput,
                                         Context,
                                         Frame);
    LeaveCriticalSection(&Decoder->OutputLock);
    return Result;
}

static BOOL
Rpi3MmalMapEncoderProfile(RPI3_MMAL_H264_PROFILE Profile,
                          UINT *MmalProfile)
{
    switch (Profile)
    {
        case Rpi3MmalH264ProfileBaseline:
            *MmalProfile = MMAL_VIDEO_PROFILE_H264_BASELINE;
            return TRUE;
        case Rpi3MmalH264ProfileMain:
            *MmalProfile = MMAL_VIDEO_PROFILE_H264_MAIN;
            return TRUE;
        case Rpi3MmalH264ProfileHigh:
            *MmalProfile = MMAL_VIDEO_PROFILE_H264_HIGH;
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
Rpi3MmalMapEncoderLevel(RPI3_MMAL_H264_LEVEL Level,
                        UINT *MmalLevel)
{
    static const UINT Levels[] =
    {
        MMAL_VIDEO_LEVEL_H264_3,
        MMAL_VIDEO_LEVEL_H264_31,
        MMAL_VIDEO_LEVEL_H264_32,
        MMAL_VIDEO_LEVEL_H264_4,
        MMAL_VIDEO_LEVEL_H264_41,
        MMAL_VIDEO_LEVEL_H264_42
    };

    if ((UINT)Level >= ARRAYSIZE(Levels))
        return FALSE;
    *MmalLevel = Levels[Level];
    return TRUE;
}

HRESULT WINAPI
Rpi3MmalCreateH264Encoder(const RPI3_MMAL_ENCODER_CONFIG *Config,
                          RPI3_MMAL_ENCODER **EncoderOut)
{
    RPI3_MMAL_ENCODER *Encoder;
    ULONGLONG MacroblocksPerSecond;
    UINT Profile;
    UINT Level;
    HRESULT Result;

    if (!Config || !EncoderOut || !Config->Width || !Config->Height ||
        Config->Width > 1920 || Config->Height > 1088 ||
        !Config->FrameRateNumerator || !Config->FrameRateDenominator ||
        !Config->Bitrate || Config->Bitrate > 25000000 ||
        !Rpi3MmalMapEncoderProfile(Config->Profile, &Profile) ||
        !Rpi3MmalMapEncoderLevel(Config->Level, &Level))
    {
        return E_INVALIDARG;
    }
    *EncoderOut = NULL;

    MacroblocksPerSecond =
        (ULONGLONG)(ALIGN_UP(Config->Width, 16) / 16) *
        (ALIGN_UP(Config->Height, 16) / 16) *
        Config->FrameRateNumerator / Config->FrameRateDenominator;
    if (MacroblocksPerSecond > 244800)
        return E_INVALIDARG;

    Encoder = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Encoder));
    if (!Encoder)
        return E_OUTOFMEMORY;

    Encoder->Encoder = TRUE;
    Encoder->Width = Config->Width;
    Encoder->Height = Config->Height;
    Encoder->VisibleWidth = Config->Width;
    Encoder->VisibleHeight = Config->Height;
    Encoder->FrameRateNumerator = Config->FrameRateNumerator;
    Encoder->FrameRateDenominator = Config->FrameRateDenominator;
    Encoder->Bitrate = Config->Bitrate;
    Encoder->IntraPeriod = Config->IntraPeriod;
    Encoder->Profile = Profile;
    Encoder->Level = Level;
    Encoder->InlineHeaders = Config->InlineHeaders;

    Result = Rpi3MmalOpenSession(Encoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalConfigureEncoderPorts(Encoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalAllocateOutputBuffers(Encoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalStart(Encoder);
    if (FAILED(Result))
        goto Failure;

    *EncoderOut = Encoder;
    return S_OK;

Failure:
    Rpi3MmalDestroyEncoder(Encoder);
    return Result;
}

HRESULT WINAPI
Rpi3MmalSubmitNV12(RPI3_MMAL_ENCODER *Encoder,
                   const BYTE *Data,
                   UINT DataSize,
                   UINT Pitch,
                   UINT Flags,
                   LONGLONG Pts,
                   LONGLONG Dts)
{
    ULONGLONG RequiredSize;

    if (!Encoder || !Encoder->Encoder)
        return E_INVALIDARG;
    if (!DataSize && (Flags & RPI3_MMAL_SUBMIT_EOS))
    {
        return Rpi3MmalSubmit(Encoder,
                              NULL,
                              0,
                              RPI3_MMAL_SUBMIT_EOS,
                              Pts,
                              Dts);
    }
    if (!Data || Pitch != Encoder->Pitch)
        return E_INVALIDARG;

    RequiredSize = (ULONGLONG)Pitch * Encoder->StorageHeight * 3 / 2;
    if (RequiredSize > MAXUINT || DataSize < RequiredSize)
        return E_INVALIDARG;

    Flags &= RPI3_MMAL_SUBMIT_KEYFRAME |
             RPI3_MMAL_SUBMIT_DISCONTINUITY;
    Flags |= RPI3_MMAL_SUBMIT_FRAME_START |
             RPI3_MMAL_SUBMIT_FRAME_END;
    return Rpi3MmalSubmit(Encoder,
                          Data,
                          (UINT)RequiredSize,
                          Flags,
                          Pts,
                          Dts);
}

HRESULT WINAPI
Rpi3MmalReceiveH264(RPI3_MMAL_ENCODER *Encoder,
                    BYTE *Buffer,
                    UINT BufferSize,
                    DWORD TimeoutMilliseconds,
                    RPI3_MMAL_PACKET *Packet)
{
    HANDLE WaitHandles[3];
    RPI3_MMAL_READY_OUTPUT Output;
    UINT Index;
    DWORD WaitStatus;
    HRESULT Result;

    if (!Encoder || !Encoder->Encoder || !Buffer || !Packet)
        return E_INVALIDARG;

    WaitHandles[0] = Encoder->FormatEvent;
    WaitHandles[1] = Encoder->FrameEvent;
    WaitHandles[2] = Encoder->StopEvent;
    EnterCriticalSection(&Encoder->OutputLock);
    for (;;)
    {
        Result = (HRESULT)InterlockedCompareExchange(
                              &Encoder->AsyncResult,
                              S_OK,
                              S_OK);
        if (FAILED(Result))
            break;

        WaitStatus = WaitForMultipleObjects(ARRAYSIZE(WaitHandles),
                                            WaitHandles,
                                            FALSE,
                                            TimeoutMilliseconds);
        if (WaitStatus == WAIT_OBJECT_0)
        {
            Result = Rpi3MmalReconfigureOutput(Encoder);
            if (FAILED(Result))
                break;
            continue;
        }
        if (WaitStatus == WAIT_OBJECT_0 + 2)
        {
            Result = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
            break;
        }
        if (WaitStatus != WAIT_OBJECT_0 + 1)
        {
            Result = WaitStatus == WAIT_TIMEOUT ?
                     HRESULT_FROM_WIN32(ERROR_TIMEOUT) :
                     Rpi3MmalErrorFromLastError();
            break;
        }

        ZeroMemory(&Output, sizeof(Output));
        if (!Rpi3MmalSnapshotOldestReadyOutput(Encoder, &Output))
            continue;
        if (Output.Length > BufferSize)
        {
            Result = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            break;
        }

        Result = Rpi3MmalConsumeReadyOutput(Encoder,
                                            Encoder->DiscardData,
                                            Encoder->DiscardSize,
                                            FALSE,
                                            &Output,
                                            &Output);
        if (Result == S_FALSE)
            continue;
        if (FAILED(Result))
            break;

        Index = Output.Index;
        if (Output.Length)
        {
            CopyMemory(Buffer,
                       Encoder->DiscardData + Output.Offset,
                       Output.Length);
        }
        ZeroMemory(Packet, sizeof(*Packet));
        Packet->Size = Output.Length;
        Packet->Pts = Output.Pts;
        Packet->Dts = Output.Dts;
        if (Output.Flags & MMAL_BUFFER_HEADER_FLAG_EOS)
            Packet->Flags |= RPI3_MMAL_PACKET_EOS;
        if (Output.Flags & (MMAL_BUFFER_HEADER_FLAG_CORRUPTED |
                            MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED))
        {
            Packet->Flags |= RPI3_MMAL_PACKET_CORRUPT;
        }
        if (Output.Flags & MMAL_BUFFER_HEADER_FLAG_KEYFRAME)
            Packet->Flags |= RPI3_MMAL_PACKET_KEYFRAME;
        if (Output.Flags & MMAL_BUFFER_HEADER_FLAG_CONFIG)
            Packet->Flags |= RPI3_MMAL_PACKET_CONFIG;
        if (Output.Flags & MMAL_BUFFER_HEADER_FLAG_FRAME_END)
            Packet->Flags |= RPI3_MMAL_PACKET_FRAME_END;

        if (Output.Flags & MMAL_BUFFER_HEADER_FLAG_EOS)
        {
            EnterCriticalSection(&Encoder->StateLock);
            if (Encoder->OutputSlots[Index].State == Rpi3MmalSlotConsuming)
                Encoder->OutputSlots[Index].State = Rpi3MmalSlotFree;
            Rpi3MmalUpdateOutputEventsLocked(Encoder);
            LeaveCriticalSection(&Encoder->StateLock);
        }
        else if (!Rpi3MmalQueueOutputSlot(Encoder,
                                          Index,
                                          Rpi3MmalSlotConsuming,
                                          TRUE))
        {
            Result = Rpi3MmalErrorFromLastError();
            break;
        }
        Result = (Packet->Flags & RPI3_MMAL_PACKET_CORRUPT) ? E_FAIL : S_OK;
        break;
    }
    LeaveCriticalSection(&Encoder->OutputLock);
    if (FAILED(Result) && Result != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) &&
        Result != HRESULT_FROM_WIN32(ERROR_TIMEOUT))
    {
        Rpi3MmalSetAsyncResult(Encoder, Result);
    }
    return Result;
}

HRESULT WINAPI
Rpi3MmalRequestKeyFrame(RPI3_MMAL_ENCODER *Encoder)
{
    if (!Encoder || !Encoder->Encoder)
        return E_INVALIDARG;

    return Rpi3MmalPortParameterSetBoolean(
               Encoder,
               &Encoder->Output,
               MMAL_PARAMETER_VIDEO_REQUEST_I_FRAME,
               TRUE);
}

HRESULT WINAPI
Rpi3MmalSetBitrate(RPI3_MMAL_ENCODER *Encoder, UINT Bitrate)
{
    HRESULT Result;

    if (!Encoder || !Encoder->Encoder || !Bitrate || Bitrate > 25000000)
        return E_INVALIDARG;
    Result = Rpi3MmalPortParameterSetUint32(Encoder,
                                            &Encoder->Output,
                                            MMAL_PARAMETER_VIDEO_BIT_RATE,
                                            Bitrate);
    if (SUCCEEDED(Result))
    {
        Encoder->Bitrate = Bitrate;
        Encoder->Output.Format.Bitrate = Bitrate;
    }
    return Result;
}

HRESULT WINAPI
Rpi3MmalSetIntraPeriod(RPI3_MMAL_ENCODER *Encoder, UINT IntraPeriod)
{
    HRESULT Result;

    if (!Encoder || !Encoder->Encoder)
        return E_INVALIDARG;
    Result = Rpi3MmalPortParameterSetUint32(Encoder,
                                            &Encoder->Output,
                                            MMAL_PARAMETER_INTRAPERIOD,
                                            IntraPeriod);
    if (SUCCEEDED(Result))
        Encoder->IntraPeriod = IntraPeriod;
    return Result;
}

static HRESULT
Rpi3MmalFlushInternal(RPI3_MMAL_DECODER *Decoder)
{
    BOOL FormatPending;
    UINT Index;
    HRESULT Result;

    if (!Decoder)
        return E_INVALIDARG;
    Result = (HRESULT)InterlockedCompareExchange(&Decoder->AsyncResult, S_OK, S_OK);
    if (FAILED(Result))
        return Result;

    InterlockedExchange(&Decoder->Reconfiguring, 1);
    Result = Rpi3MmalDrainReturnedOutputs(Decoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalPortFlush(Decoder, &Decoder->Input, Decoder->InputBulkSent);
    if (FAILED(Result))
        goto Failure;
    Decoder->InputBulkSent = FALSE;
    Result = Rpi3MmalPortFlush(Decoder, &Decoder->Output, FALSE);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalWaitForOutputDrain(Decoder);
    if (FAILED(Result))
        goto Failure;
    Result = Rpi3MmalDrainReturnedOutputs(Decoder);
    if (FAILED(Result))
        goto Failure;

    EnterCriticalSection(&Decoder->StateLock);
    for (Index = 0; Index < Decoder->OutputCount; ++Index)
    {
        Decoder->OutputSlots[Index].Length = 0;
        Decoder->OutputSlots[Index].Offset = 0;
        Decoder->OutputSlots[Index].Flags = 0;
        Decoder->OutputSlots[Index].State = Rpi3MmalSlotFree;
    }
    Rpi3MmalUpdateOutputEventsLocked(Decoder);
    FormatPending = Decoder->FormatGeneration != Decoder->AppliedFormatGeneration;
    LeaveCriticalSection(&Decoder->StateLock);
    InterlockedExchange(&Decoder->Reconfiguring, 0);

    if (FormatPending)
        return Rpi3MmalReconfigureOutput(Decoder);

    for (Index = 0; Index < Decoder->OutputCount; ++Index)
    {
        if (!Rpi3MmalQueueOutputSlot(Decoder,
                                     Index,
                                     Rpi3MmalSlotFree,
                                     FALSE))
            return Rpi3MmalErrorFromLastError();
    }
    return S_OK;

Failure:
    InterlockedExchange(&Decoder->Reconfiguring, 0);
    Rpi3MmalSetAsyncResult(Decoder, Result);
    return Result;
}

HRESULT WINAPI
Rpi3MmalFlush(RPI3_MMAL_DECODER *Decoder)
{
    HRESULT Result;

    if (!Decoder)
        return E_INVALIDARG;

    EnterCriticalSection(&Decoder->OutputLock);
    Result = Rpi3MmalFlushInternal(Decoder);
    LeaveCriticalSection(&Decoder->OutputLock);
    return Result;
}

HRESULT WINAPI
Rpi3MmalFlushEncoder(RPI3_MMAL_ENCODER *Encoder)
{
    if (!Encoder || !Encoder->Encoder)
        return E_INVALIDARG;

    return Rpi3MmalFlush(Encoder);
}

void WINAPI
Rpi3MmalDestroyEncoder(RPI3_MMAL_ENCODER *Encoder)
{
    if (!Encoder)
        return;

    Rpi3MmalDestroyDecoder(Encoder);
}

void WINAPI
Rpi3MmalDestroyDecoder(RPI3_MMAL_DECODER *Decoder)
{
    DWORD WaitStatus;
    HRESULT Result = S_OK;

    if (!Decoder)
        return;

    if (Decoder->ComponentHandle && Decoder->CompletionThread)
    {
        EnterCriticalSection(&Decoder->OutputLock);
        InterlockedExchange(&Decoder->Reconfiguring, 1);
        Result = Rpi3MmalDrainReturnedOutputs(Decoder);
        if (Decoder->InputEnabled && SUCCEEDED(Result))
        {
            Result = Rpi3MmalPortAction(Decoder,
                                        &Decoder->Input,
                                        MmalPortActionDisable);
            Decoder->InputEnabled = FALSE;
        }
        if (Decoder->OutputEnabled && SUCCEEDED(Result))
        {
            Result = Rpi3MmalPortAction(Decoder,
                                        &Decoder->Output,
                                        MmalPortActionDisable);
            Decoder->OutputEnabled = FALSE;
            if (SUCCEEDED(Result))
                Result = Rpi3MmalWaitForOutputDrain(Decoder);
            if (SUCCEEDED(Result))
                Result = Rpi3MmalDrainReturnedOutputs(Decoder);
        }
        if (Decoder->ComponentEnabled && SUCCEEDED(Result))
        {
            Result = Rpi3MmalComponentCommand(
                         Decoder,
                         MmalWorkerComponentDisable);
            Decoder->ComponentEnabled = FALSE;
        }
        if (SUCCEEDED(Result))
            (VOID)Rpi3MmalComponentCommand(Decoder,
                                            MmalWorkerComponentDestroy);
        Decoder->ComponentHandle = 0;
        LeaveCriticalSection(&Decoder->OutputLock);
    }

    InterlockedExchange(&Decoder->StopRequested, 1);
    if (Decoder->StopEvent)
        SetEvent(Decoder->StopEvent);
    if (Decoder->ControlEvent)
        SetEvent(Decoder->ControlEvent);
    if (Decoder->InputReturnedEvent)
        SetEvent(Decoder->InputReturnedEvent);
    if (Decoder->FrameEvent)
        SetEvent(Decoder->FrameEvent);
    if (Decoder->FormatEvent)
        SetEvent(Decoder->FormatEvent);
    if (Decoder->OutputDrainedEvent)
        SetEvent(Decoder->OutputDrainedEvent);
    if (Decoder->Device != INVALID_HANDLE_VALUE)
    {
        (VOID)Rpi3MmalDeviceIoControl(Decoder,
                                      VCHIQ_IOC_SHUTDOWN,
                                      NULL,
                                      0,
                                      NULL,
                                      0,
                                      NULL);
        if (Decoder->Device != INVALID_HANDLE_VALUE)
            CancelIoEx(Decoder->Device, NULL);
    }
    if (Decoder->CompletionThread)
    {
        WaitStatus = WaitForSingleObject(Decoder->CompletionThread,
                                         MMAL_CONTROL_TIMEOUT_MS);
        if (WaitStatus != WAIT_OBJECT_0)
        {
            if (Decoder->Device != INVALID_HANDLE_VALUE)
            {
                (VOID)Rpi3MmalCloseDevice(Decoder, Decoder->Device);
            }
            WaitForSingleObject(Decoder->CompletionThread, INFINITE);
        }
        CloseHandle(Decoder->CompletionThread);
    }
    if (Decoder->Device != INVALID_HANDLE_VALUE)
    {
        (VOID)Rpi3MmalCloseDevice(Decoder, Decoder->Device);
    }

    Rpi3MmalFreeOutputBuffers(Decoder);
    if (Decoder->OutputDrainedEvent)
        CloseHandle(Decoder->OutputDrainedEvent);
    if (Decoder->FormatEvent)
        CloseHandle(Decoder->FormatEvent);
    if (Decoder->FrameEvent)
        CloseHandle(Decoder->FrameEvent);
    if (Decoder->ControlEvent)
        CloseHandle(Decoder->ControlEvent);
    if (Decoder->InputReturnedEvent)
        CloseHandle(Decoder->InputReturnedEvent);
    if (Decoder->StopEvent)
        CloseHandle(Decoder->StopEvent);
    DeleteCriticalSection(&Decoder->StateLock);
    DeleteCriticalSection(&Decoder->OutputBulkLock);
    DeleteCriticalSection(&Decoder->OutputLock);
    DeleteCriticalSection(&Decoder->InputLock);
    DeleteCriticalSection(&Decoder->ControlLock);
    HeapFree(GetProcessHeap(), 0, Decoder);
}
