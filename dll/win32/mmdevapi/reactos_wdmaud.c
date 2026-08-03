/*
 * ReactOS WDMAUD backend for Wine mmdevapi.
 *
 * Wine 10 delegates mmdevapi to wine*.drv Unix audio backends. ReactOS does
 * not have that backend ABI, so this file maps the mmdevapi backend contract
 * onto the existing WDMAUD/sysaudio stack.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#define COBJMACROS
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winternl.h"
#include "winioctl.h"
#include "winreg.h"
#include "mmsystem.h"
#include "mmreg.h"
#include "audiopolicy.h"
#include "setupapi.h"
#include "ks.h"
#include "ksmedia.h"
#include "interface.h"
#include "wavefmt.h"

#include "wine/debug.h"

#include "mmdevapi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(mmdevapi);

#define REACTOS_DEVICE_PREFIX "wdmaud:"
#define REACTOS_DEVICE_RENDER "render"
#define REACTOS_DEVICE_CAPTURE "capture"
#define REACTOS_MMDEV_MAGIC 0x524d4d44
#define REACTOS_DEFAULT_PERIOD 100000
#define REACTOS_MIN_PERIOD 30000
#define REACTOS_DEFAULT_BUFFER_FRAMES 4096

struct reactos_stream
{
    DWORD magic;
    volatile LONG closing;
    EDataFlow flow;
    SOUND_DEVICE_TYPE type;
    DWORD index;
    HANDLE pin;
    HANDLE io_handle;
    HANDLE event;
    HANDLE stop_event;
    HANDLE timer_thread;
    CRITICAL_SECTION lock;
    WAVEFORMATEX format;
    UINT32 frame_size;
    UINT32 buffer_frames;
    UINT32 period_frames;
    UINT32 padding;
    UINT32 capture_frames;
    UINT64 position;
    BYTE *buffer;
    UINT32 buffer_alloc_frames;
    BOOL capture_locked;
    BOOL started;
};

static HANDLE wdmaud_handle = INVALID_HANDLE_VALUE;
static WCHAR wdmaud_path[MAX_PATH];

static DWORD WINAPI reactos_timer_thread(void *param);

static const GUID wdmaud_category = {STATIC_KSCATEGORY_WDMAUD};

static const GUID reactos_mmdevapi_guid_seed =
{
    0x524f5357, 0x444d, 0x4155, {0x44, 0x2d, 0x4d, 0x4d, 0x44, 0x45, 0x56, 0x00}
};

static struct reactos_stream *stream_from_handle(stream_handle handle)
{
    struct reactos_stream *stream = (struct reactos_stream *)(ULONG_PTR)handle;

    if (!stream || stream->magic != REACTOS_MMDEV_MAGIC)
        return NULL;

    return stream;
}

static BOOL wdmaud_ioctl(DWORD ioctl, WDMAUD_DEVICE_INFO *info)
{
    DWORD returned;

    if (wdmaud_handle == INVALID_HANDLE_VALUE)
        return FALSE;

    return DeviceIoControl(wdmaud_handle, ioctl, info, sizeof(*info), info,
                           sizeof(*info), &returned, NULL);
}

static BOOL open_wdmaud(void)
{
    HDEVINFO devinfo;
    SP_DEVICE_INTERFACE_DATA iface;
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
    WCHAR *path;
    DWORD detail_size;
    BOOL ret = FALSE;

    if (wdmaud_handle != INVALID_HANDLE_VALUE)
        return TRUE;

    devinfo = SetupDiGetClassDevsW(&wdmaud_category, NULL, NULL,
                                   DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (devinfo == INVALID_HANDLE_VALUE)
    {
        WARN("SetupDiGetClassDevsW(KSCATEGORY_WDMAUD) failed, error %lu.\n", GetLastError());
        return FALSE;
    }

    iface.cbSize = sizeof(iface);
    if (!SetupDiEnumDeviceInterfaces(devinfo, NULL, &wdmaud_category, 0, &iface))
    {
        WARN("SetupDiEnumDeviceInterfaces(KSCATEGORY_WDMAUD) failed, error %lu.\n", GetLastError());
        goto done;
    }

    detail_size = sizeof(*detail) + MAX_PATH * sizeof(WCHAR);
    detail = malloc(detail_size);
    if (!detail)
        goto done;

    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(devinfo, &iface, detail, detail_size, NULL, NULL))
    {
        WARN("SetupDiGetDeviceInterfaceDetailW(KSCATEGORY_WDMAUD) failed, error %lu.\n", GetLastError());
        free(detail);
        goto done;
    }

    path = detail->DevicePath;
    if (path[0] == L'\\' && path[1] == L'?')
        path[1] = L'\\';

    wdmaud_handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                                0, NULL, OPEN_EXISTING, 0, NULL);
    if (wdmaud_handle == INVALID_HANDLE_VALUE)
    {
        WARN("Failed to open WDMAUD interface %s, error %lu.\n",
             wine_dbgstr_w(path), GetLastError());
    }
    else
    {
        lstrcpynW(wdmaud_path, path, sizeof(wdmaud_path) / sizeof(wdmaud_path[0]));
        ret = TRUE;
    }

    free(detail);

done:
    SetupDiDestroyDeviceInfoList(devinfo);
    return ret;
}

static void close_wdmaud(void)
{
    if (wdmaud_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(wdmaud_handle);
        wdmaud_handle = INVALID_HANDLE_VALUE;
    }
}

static SOUND_DEVICE_TYPE device_type_from_flow(EDataFlow flow)
{
    return flow == eCapture ? WAVE_IN_DEVICE_TYPE : WAVE_OUT_DEVICE_TYPE;
}

static BOOL parse_device_name(const char *device, EDataFlow *flow, DWORD *index)
{
    const char *kind, *index_string;
    char *end;
    unsigned long value;

    if (!device || strncmp(device, REACTOS_DEVICE_PREFIX, strlen(REACTOS_DEVICE_PREFIX)))
        return FALSE;

    kind = device + strlen(REACTOS_DEVICE_PREFIX);
    if (!strncmp(kind, REACTOS_DEVICE_RENDER ":", strlen(REACTOS_DEVICE_RENDER) + 1))
    {
        *flow = eRender;
        index_string = kind + strlen(REACTOS_DEVICE_RENDER) + 1;
    }
    else if (!strncmp(kind, REACTOS_DEVICE_CAPTURE ":", strlen(REACTOS_DEVICE_CAPTURE) + 1))
    {
        *flow = eCapture;
        index_string = kind + strlen(REACTOS_DEVICE_CAPTURE) + 1;
    }
    else
    {
        return FALSE;
    }

    value = strtoul(index_string, &end, 10);
    if (*index_string == '\0' || *end != '\0')
        return FALSE;

    *index = value;
    return TRUE;
}

static void format_device_name(EDataFlow flow, DWORD index, char *buffer, size_t size)
{
    const char *kind = flow == eCapture ? REACTOS_DEVICE_CAPTURE : REACTOS_DEVICE_RENDER;

    snprintf(buffer, size, REACTOS_DEVICE_PREFIX "%s:%lu", kind, index);
}

static void WINAPI reactos_get_device_guid(EDataFlow flow, const char *name, GUID *guid)
{
    DWORD index = 0;
    EDataFlow parsed_flow = flow;

    *guid = reactos_mmdevapi_guid_seed;

    if (!parse_device_name(name, &parsed_flow, &index))
        parsed_flow = flow;

    guid->Data3 = parsed_flow == eCapture ? 0x4341 : 0x5245;
    guid->Data4[5] = (BYTE)((index >> 8) & 0xff);
    guid->Data4[6] = (BYTE)(index & 0xff);
    guid->Data4[7] = parsed_flow == eCapture ? 1 : 0;
}

static BOOL WINAPI reactos_get_device_name_from_guid(GUID *guid, char **name, EDataFlow *flow)
{
    DWORD index;

    if (guid->Data1 != reactos_mmdevapi_guid_seed.Data1 ||
        guid->Data2 != reactos_mmdevapi_guid_seed.Data2 ||
        guid->Data4[0] != reactos_mmdevapi_guid_seed.Data4[0] ||
        guid->Data4[1] != reactos_mmdevapi_guid_seed.Data4[1] ||
        guid->Data4[2] != reactos_mmdevapi_guid_seed.Data4[2] ||
        guid->Data4[3] != reactos_mmdevapi_guid_seed.Data4[3] ||
        guid->Data4[4] != reactos_mmdevapi_guid_seed.Data4[4])
        return FALSE;

    if (guid->Data3 == 0x4341 || guid->Data4[7] == 1)
        *flow = eCapture;
    else if (guid->Data3 == 0x5245 || guid->Data4[7] == 0)
        *flow = eRender;
    else
        return FALSE;

    index = ((DWORD)guid->Data4[5] << 8) | guid->Data4[6];

    *name = malloc(32);
    if (!*name)
        return FALSE;

    format_device_name(*flow, index, *name, 32);
    return TRUE;
}

static UINT query_device_count(EDataFlow flow)
{
    WDMAUD_DEVICE_INFO info;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = device_type_from_flow(flow);

    if (!wdmaud_ioctl(IOCTL_GETNUMDEVS_TYPE, &info))
    {
        WARN("IOCTL_GETNUMDEVS_TYPE failed for flow %u, error %lu.\n", flow, GetLastError());
        return 0;
    }

    return info.DeviceCount;
}

static BOOL query_device_caps(EDataFlow flow, DWORD index, WDMAUD_DEVICE_INFO *info)
{
    ZeroMemory(info, sizeof(*info));
    info->DeviceType = device_type_from_flow(flow);
    info->DeviceIndex = index;

    if (!wdmaud_ioctl(IOCTL_GETCAPABILITIES, info))
    {
        WARN("IOCTL_GETCAPABILITIES failed for flow %u index %lu, error %lu.\n",
             flow, index, GetLastError());
        return FALSE;
    }

    return TRUE;
}

static void get_device_display_name(EDataFlow flow, DWORD index, WCHAR *name, UINT name_len)
{
    WDMAUD_DEVICE_INFO caps;
    const WCHAR *caps_name = NULL;

    if (query_device_caps(flow, index, &caps))
    {
        if (flow == eCapture)
            caps_name = caps.u.WaveInCaps.szPname;
        else
            caps_name = caps.u.WaveOutCaps.szPname;
    }

    if (caps_name && caps_name[0])
        lstrcpynW(name, caps_name, name_len);
    else if (flow == eCapture)
        swprintf(name, name_len, L"ReactOS WDMAUD Capture %lu", index);
    else
        swprintf(name, name_len, L"ReactOS WDMAUD Render %lu", index);
}

static unsigned int align_uint(unsigned int value, unsigned int align)
{
    return (value + align - 1) & ~(align - 1);
}

static void fill_endpoint_ids(struct get_endpoint_ids_params *params)
{
    static const unsigned int name_chars = MAXPNAMELEN + 40;
    UINT count = query_device_count(params->flow);
    unsigned int needed, i;
    BYTE *base;

    needed = count * sizeof(*params->endpoints);
    for (i = 0; i < count; ++i)
    {
        needed = align_uint(needed, sizeof(WCHAR));
        needed += name_chars * sizeof(WCHAR);
        needed += 32;
    }

    if (params->size < needed)
    {
        params->size = needed;
        params->num = count;
        params->default_idx = 0;
        params->result = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        return;
    }

    params->num = count;
    params->default_idx = 0;
    params->result = S_OK;

    if (!count)
        return;

    base = (BYTE *)params->endpoints;
    needed = count * sizeof(*params->endpoints);

    for (i = 0; i < count; ++i)
    {
        WCHAR *wide_name;
        char *device_name;

        needed = align_uint(needed, sizeof(WCHAR));
        params->endpoints[i].name = needed;
        wide_name = (WCHAR *)(base + needed);
        get_device_display_name(params->flow, i, wide_name, name_chars);
        needed += name_chars * sizeof(WCHAR);

        params->endpoints[i].device = needed;
        device_name = (char *)(base + needed);
        format_device_name(params->flow, i, device_name, 32);
        needed += 32;
    }
}

static DWORD channel_mask_from_count(WORD channels)
{
    switch (channels)
    {
        case 1:
            return KSAUDIO_SPEAKER_MONO;
        case 2:
            return KSAUDIO_SPEAKER_STEREO;
        case 4:
            return KSAUDIO_SPEAKER_QUAD;
        case 6:
            return KSAUDIO_SPEAKER_5POINT1;
        case 8:
            return KSAUDIO_SPEAKER_7POINT1;
        default:
            return KSAUDIO_SPEAKER_DIRECTOUT;
    }
}

static BOOL fill_mix_format(const char *device, EDataFlow flow, WAVEFORMATEXTENSIBLE *fmt)
{
    WDMAUD_DEVICE_INFO caps;
    DWORD index = 0, formats = 0, preferred;
    DWORD samples_per_sec = 48000;
    WORD bits_per_sample = 16, channels = 2;
    EDataFlow parsed_flow = flow;

    if (device)
        parse_device_name(device, &parsed_flow, &index);

    if (query_device_caps(parsed_flow, index, &caps))
    {
        if (parsed_flow == eCapture && caps.u.WaveInCaps.wChannels)
        {
            channels = caps.u.WaveInCaps.wChannels;
            formats = caps.u.WaveInCaps.dwFormats;
        }
        else if (parsed_flow == eRender && caps.u.WaveOutCaps.wChannels)
        {
            channels = caps.u.WaveOutCaps.wChannels;
            formats = caps.u.WaveOutCaps.dwFormats;
        }
    }

    preferred = channels <= 2 ? RosSoundChooseBestLegacyFormat(formats) : 0;
    if (preferred)
        RosSoundLegacyFlagToWaveFormatFields(preferred, &samples_per_sec, &bits_per_sample, &channels);

    ZeroMemory(fmt, sizeof(*fmt));
    fmt->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    fmt->Format.nChannels = (WORD)channels;
    fmt->Format.nSamplesPerSec = samples_per_sec;
    fmt->Format.wBitsPerSample = bits_per_sample;
    fmt->Format.nBlockAlign = (fmt->Format.nChannels * fmt->Format.wBitsPerSample) / 8;
    fmt->Format.nAvgBytesPerSec = fmt->Format.nSamplesPerSec * fmt->Format.nBlockAlign;
    fmt->Format.cbSize = sizeof(*fmt) - sizeof(fmt->Format);
    fmt->Samples.wValidBitsPerSample = fmt->Format.wBitsPerSample;
    fmt->dwChannelMask = channel_mask_from_count(channels);
    fmt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    return TRUE;
}

static WORD valid_bits_from_format(const WAVEFORMATEX *fmt)
{
    const WAVEFORMATEXTENSIBLE *ext;

    if (fmt->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        return fmt->wBitsPerSample;

    ext = (const WAVEFORMATEXTENSIBLE *)fmt;
    return ext->Samples.wValidBitsPerSample;
}

static BOOL format_fields_match(const WAVEFORMATEX *left, const WAVEFORMATEX *right)
{
    return left->nChannels == right->nChannels &&
           left->nSamplesPerSec == right->nSamplesPerSec &&
           left->wBitsPerSample == right->wBitsPerSample &&
           left->nBlockAlign == right->nBlockAlign &&
           left->nAvgBytesPerSec == right->nAvgBytesPerSec &&
           valid_bits_from_format(left) == valid_bits_from_format(right);
}

static BOOL is_pcm_format(const WAVEFORMATEX *fmt)
{
    const WAVEFORMATEXTENSIBLE *ext;

    if (!fmt)
        return FALSE;

    if (fmt->wFormatTag == WAVE_FORMAT_PCM)
        return TRUE;

    if (fmt->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        fmt->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        return FALSE;

    ext = (const WAVEFORMATEXTENSIBLE *)fmt;
    return IsEqualGUID(&ext->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM);
}

static HRESULT validate_format(const char *device, EDataFlow flow, const WAVEFORMATEX *fmt)
{
    WDMAUD_DEVICE_INFO caps;
    WAVEFORMATEXTENSIBLE mix;
    DWORD index = 0, formats, flag;
    EDataFlow parsed_flow = flow;
    UINT block_align;
    WORD cap_channels;

    if (!is_pcm_format(fmt))
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    if (!fmt->nChannels ||
        !fmt->nSamplesPerSec || fmt->nSamplesPerSec > 192000 ||
        (fmt->wBitsPerSample != 8 && fmt->wBitsPerSample != 16 &&
         fmt->wBitsPerSample != 24 && fmt->wBitsPerSample != 32))
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    block_align = (fmt->nChannels * fmt->wBitsPerSample) / 8;
    if (!block_align || fmt->nBlockAlign != block_align ||
        fmt->nAvgBytesPerSec != fmt->nSamplesPerSec * fmt->nBlockAlign)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    flag = RosSoundWaveFormatFieldsToLegacyFlag(fmt->nSamplesPerSec,
                                                fmt->wBitsPerSample,
                                                fmt->nChannels);

    if (device)
        parse_device_name(device, &parsed_flow, &index);

    if (!query_device_caps(parsed_flow, index, &caps))
        return AUDCLNT_E_DEVICE_INVALIDATED;

    formats = parsed_flow == eCapture ? caps.u.WaveInCaps.dwFormats :
                                        caps.u.WaveOutCaps.dwFormats;
    cap_channels = parsed_flow == eCapture ? caps.u.WaveInCaps.wChannels :
                                             caps.u.WaveOutCaps.wChannels;
    if (!cap_channels)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    if (cap_channels <= 2 && flag && (formats & flag))
        return S_OK;

    if (!fill_mix_format(device, parsed_flow, &mix) ||
        !format_fields_match(fmt, &mix.Format))
    {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    return S_OK;
}

static void normalize_wdmaud_format(WAVEFORMATEX *dst, const WAVEFORMATEX *src)
{
    *dst = *src;

    if (src->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        dst->wFormatTag = WAVE_FORMAT_PCM;

    dst->cbSize = 0;
}

static void close_stream_pin(struct reactos_stream *stream);

static HRESULT open_stream_pin(struct reactos_stream *stream)
{
    WDMAUD_DEVICE_INFO info;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = stream->type;
    info.DeviceIndex = stream->index;
    info.u.WaveFormatEx = stream->format;

    if (!wdmaud_ioctl(IOCTL_OPEN_WDMAUD, &info))
    {
        WARN("IOCTL_OPEN_WDMAUD failed for flow %u index %lu, error %lu.\n",
             stream->flow, stream->index, GetLastError());
        return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    }

    stream->pin = info.hDevice;
    stream->io_handle = CreateFileW(wdmaud_path, GENERIC_READ | GENERIC_WRITE,
                                    0, NULL, OPEN_EXISTING,
                                    FILE_FLAG_OVERLAPPED, NULL);
    if (stream->io_handle == INVALID_HANDLE_VALUE)
    {
        WARN("Failed to open WDMAUD stream I/O handle %s, error %lu.\n",
             wine_dbgstr_w(wdmaud_path), GetLastError());
        close_stream_pin(stream);
        return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    }

    return S_OK;
}

static void close_stream_pin(struct reactos_stream *stream)
{
    WDMAUD_DEVICE_INFO info;

    if (stream->io_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(stream->io_handle);
        stream->io_handle = INVALID_HANDLE_VALUE;
    }

    if (!stream->pin)
        return;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = stream->type;
    info.DeviceIndex = stream->index;
    info.hDevice = stream->pin;
    wdmaud_ioctl(IOCTL_CLOSE_WDMAUD, &info);
    stream->pin = NULL;
}

static HRESULT set_stream_state(struct reactos_stream *stream, KSSTATE state)
{
    WDMAUD_DEVICE_INFO info;

    if (!stream->pin)
        return S_OK;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = stream->type;
    info.DeviceIndex = stream->index;
    info.hDevice = stream->pin;
    info.u.State = state;

    if (!wdmaud_ioctl(IOCTL_SETDEVICE_STATE, &info))
    {
        WARN("IOCTL_SETDEVICE_STATE(%u) failed for flow %u index %lu, error %lu.\n",
             state, stream->flow, stream->index, GetLastError());
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }

    return S_OK;
}

static void reset_stream_pin(struct reactos_stream *stream)
{
    WDMAUD_DEVICE_INFO info;

    if (!stream->pin)
        return;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = stream->type;
    info.DeviceIndex = stream->index;
    info.hDevice = stream->pin;
    info.u.ResetStream = KSRESET_BEGIN;
    wdmaud_ioctl(IOCTL_RESET_STREAM, &info);
    info.u.ResetStream = KSRESET_END;
    wdmaud_ioctl(IOCTL_RESET_STREAM, &info);
}

static UINT64 query_stream_position(struct reactos_stream *stream)
{
    WDMAUD_DEVICE_INFO info;

    if (!stream->pin)
        return stream->position;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = stream->type;
    info.DeviceIndex = stream->index;
    info.hDevice = stream->pin;

    if (!wdmaud_ioctl(IOCTL_GETPOS, &info))
        return stream->position;

    return info.u.Position;
}

static BOOL stream_io_wait(struct reactos_stream *stream, BOOL read, BYTE *buffer, UINT32 bytes, UINT32 *used)
{
    WDMAUD_DEVICE_INFO info;
    OVERLAPPED overlapped;
    HANDLE wait_handles[2];
    DWORD transferred;
    DWORD wait;
    BOOL ret;

    if (used)
        *used = 0;

    ZeroMemory(&info, sizeof(info));
    info.Header.Size = sizeof(info);
    info.Header.FrameExtent = bytes;
    info.Header.Data = buffer;
    info.Header.PresentationTime.Numerator = 1;
    info.Header.PresentationTime.Denominator = 1;
    info.DeviceType = stream->type;
    info.DeviceIndex = stream->index;
    info.hDevice = stream->pin;

    if (!read)
        info.Header.DataUsed = bytes;

    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!overlapped.hEvent)
        return FALSE;

    ret = read ? ReadFile(stream->io_handle, &info, sizeof(info), &transferred, &overlapped) :
                 WriteFile(stream->io_handle, &info, sizeof(info), &transferred, &overlapped);

    if (!ret && GetLastError() == ERROR_IO_PENDING)
    {
        wait_handles[0] = overlapped.hEvent;
        wait_handles[1] = stream->stop_event;
        wait = WaitForMultipleObjects(stream->stop_event ? 2 : 1, wait_handles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0 + 1)
        {
            CancelIoEx(stream->io_handle, &overlapped);
            GetOverlappedResult(stream->io_handle, &overlapped, &transferred, TRUE);
            CloseHandle(overlapped.hEvent);
            SetLastError(ERROR_OPERATION_ABORTED);
            return FALSE;
        }

        ret = GetOverlappedResult(stream->io_handle, &overlapped, &transferred, FALSE);
    }

    if (used)
        *used = info.Header.DataUsed;

    CloseHandle(overlapped.hEvent);
    return ret;
}

BOOL reactos_audio_driver_init(DriverFuncs *driver)
{
    if (!open_wdmaud())
        return FALSE;

    ZeroMemory(driver, sizeof(*driver));
    driver->module = GetModuleHandleW(L"mmdevapi.dll");
    driver->module_unixlib = 1;
    lstrcpyW(driver->module_name, L"wdmaud.drv");
    driver->priority = Priority_Preferred;

    return TRUE;
}

void reactos_audio_driver_deinit(void)
{
    close_wdmaud();
}

static NTSTATUS reactos_create_stream(struct create_stream_params *params)
{
    struct reactos_stream *stream;
    EDataFlow flow;
    DWORD index;
    HRESULT hr;

    if (!parse_device_name(params->device, &flow, &index) || flow != params->flow)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    if (FAILED(hr = validate_format(params->device, params->flow, params->fmt)))
    {
        params->result = hr;
        return STATUS_SUCCESS;
    }

    stream = calloc(1, sizeof(*stream));
    if (!stream)
    {
        params->result = E_OUTOFMEMORY;
        return STATUS_SUCCESS;
    }

    stream->magic = REACTOS_MMDEV_MAGIC;
    stream->flow = flow;
    stream->type = device_type_from_flow(flow);
    stream->index = index;
    stream->pin = NULL;
    stream->io_handle = INVALID_HANDLE_VALUE;
    stream->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!stream->stop_event)
    {
        free(stream);
        params->result = E_OUTOFMEMORY;
        return STATUS_SUCCESS;
    }
    InitializeCriticalSection(&stream->lock);
    normalize_wdmaud_format(&stream->format, params->fmt);
    stream->frame_size = params->fmt->nBlockAlign;
    stream->period_frames = (params->fmt->nSamplesPerSec * REACTOS_DEFAULT_PERIOD) / 10000000;
    if (!stream->period_frames)
        stream->period_frames = 1;
    stream->buffer_frames = (UINT32)max(REACTOS_DEFAULT_BUFFER_FRAMES,
                                        (params->duration * params->fmt->nSamplesPerSec) / 10000000);
    if (!stream->buffer_frames)
        stream->buffer_frames = REACTOS_DEFAULT_BUFFER_FRAMES;

    if (FAILED(hr = open_stream_pin(stream)))
    {
        DeleteCriticalSection(&stream->lock);
        CloseHandle(stream->stop_event);
        free(stream);
        params->result = hr;
        return STATUS_SUCCESS;
    }

    *params->channel_count = params->fmt->nChannels;
    *params->stream = (stream_handle)(ULONG_PTR)stream;
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_release_stream(struct release_stream_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    InterlockedExchange(&stream->closing, 1);
    stream->started = FALSE;
    if (stream->stop_event)
        SetEvent(stream->stop_event);
    if (stream->event)
        SetEvent(stream->event);
    if (stream->timer_thread)
    {
        WaitForSingleObject(stream->timer_thread, 1000);
        CloseHandle(stream->timer_thread);
        stream->timer_thread = NULL;
    }

    set_stream_state(stream, KSSTATE_STOP);
    close_stream_pin(stream);
    stream->magic = 0;
    if (stream->stop_event)
        CloseHandle(stream->stop_event);
    DeleteCriticalSection(&stream->lock);
    free(stream->buffer);
    free(stream);
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_start(struct start_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    params->result = set_stream_state(stream, KSSTATE_RUN);
    if (SUCCEEDED(params->result))
    {
        ResetEvent(stream->stop_event);
        EnterCriticalSection(&stream->lock);
        stream->capture_frames = 0;
        stream->capture_locked = FALSE;
        stream->started = TRUE;
        LeaveCriticalSection(&stream->lock);
        stream->timer_thread = CreateThread(NULL, 0, reactos_timer_thread, stream, 0, NULL);
        if (!stream->timer_thread)
        {
            EnterCriticalSection(&stream->lock);
            stream->started = FALSE;
            LeaveCriticalSection(&stream->lock);
            set_stream_state(stream, KSSTATE_PAUSE);
            params->result = E_FAIL;
            return STATUS_SUCCESS;
        }
        SetThreadPriority(stream->timer_thread, THREAD_PRIORITY_TIME_CRITICAL);
        if (stream->event)
            SetEvent(stream->event);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_stop(struct stop_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    EnterCriticalSection(&stream->lock);
    stream->started = FALSE;
    LeaveCriticalSection(&stream->lock);
    if (stream->stop_event)
        SetEvent(stream->stop_event);
    if (stream->timer_thread)
    {
        WaitForSingleObject(stream->timer_thread, 1000);
        CloseHandle(stream->timer_thread);
        stream->timer_thread = NULL;
    }
    params->result = set_stream_state(stream, KSSTATE_PAUSE);
    if (stream->event)
        SetEvent(stream->event);
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_reset(struct reset_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    reset_stream_pin(stream);
    EnterCriticalSection(&stream->lock);
    stream->padding = 0;
    stream->capture_frames = 0;
    stream->capture_locked = FALSE;
    stream->position = 0;
    LeaveCriticalSection(&stream->lock);
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_render_buffer(struct get_render_buffer_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);
    size_t bytes;

    if (!stream || stream->flow != eRender)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    if (!params->frames || params->frames > stream->buffer_frames)
    {
        params->result = AUDCLNT_E_BUFFER_TOO_LARGE;
        return STATUS_SUCCESS;
    }

    bytes = params->frames * stream->frame_size;
    if (stream->buffer_alloc_frames < params->frames)
    {
        BYTE *buffer = realloc(stream->buffer, bytes);
        if (!buffer)
        {
            params->result = E_OUTOFMEMORY;
            return STATUS_SUCCESS;
        }
        stream->buffer = buffer;
        stream->buffer_alloc_frames = params->frames;
    }

    *params->data = stream->buffer;
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_release_render_buffer(struct release_render_buffer_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream || stream->flow != eRender)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    if (params->flags & AUDCLNT_BUFFERFLAGS_SILENT)
        ZeroMemory(stream->buffer, params->written_frames * stream->frame_size);

    if (params->written_frames)
    {
        UINT32 used;
        UINT32 bytes = params->written_frames * stream->frame_size;

        if (!stream_io_wait(stream, FALSE, stream->buffer, bytes, &used))
        {
            WARN("WriteFile(WDMAUD) failed for render stream, error %lu.\n", GetLastError());
            params->result = AUDCLNT_E_DEVICE_INVALIDATED;
            return STATUS_SUCCESS;
        }

        stream->position += used / stream->frame_size;
    }

    stream->padding = 0;
    if (stream->event)
        SetEvent(stream->event);
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_capture_buffer(struct get_capture_buffer_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream || stream->flow != eCapture)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    *params->data = NULL;
    *params->frames = 0;
    *params->flags = 0;
    if (params->devpos)
        *params->devpos = stream->position;
    if (params->qpcpos)
        *params->qpcpos = GetTickCount() * 10000ULL;

    EnterCriticalSection(&stream->lock);
    if (stream->capture_locked)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_E_OUT_OF_ORDER;
        return STATUS_SUCCESS;
    }

    if (!stream->capture_frames)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_S_BUFFER_EMPTY;
        return STATUS_SUCCESS;
    }

    stream->capture_locked = TRUE;
    stream->padding = stream->capture_frames;
    *params->data = stream->buffer;
    *params->frames = stream->capture_frames;
    LeaveCriticalSection(&stream->lock);
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_release_capture_buffer(struct release_capture_buffer_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream || stream->flow != eCapture)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    EnterCriticalSection(&stream->lock);
    if (!stream->capture_locked)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = params->done ? AUDCLNT_E_OUT_OF_ORDER : S_OK;
        return STATUS_SUCCESS;
    }

    if (params->done != stream->capture_frames)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_E_INVALID_SIZE;
        return STATUS_SUCCESS;
    }

    stream->position += params->done;
    stream->padding = 0;
    stream->capture_frames = 0;
    stream->capture_locked = FALSE;
    LeaveCriticalSection(&stream->lock);
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static HRESULT ensure_stream_buffer(struct reactos_stream *stream, UINT32 frames)
{
    size_t bytes = frames * stream->frame_size;

    if (stream->buffer_alloc_frames < frames)
    {
        BYTE *buffer = realloc(stream->buffer, bytes);
        if (!buffer)
            return E_OUTOFMEMORY;

        stream->buffer = buffer;
        stream->buffer_alloc_frames = frames;
    }

    return S_OK;
}

static void reactos_capture_timer_loop(struct reactos_stream *stream)
{
    UINT32 bytes, frames, used;
    DWORD error;

    while (stream_from_handle((stream_handle)(ULONG_PTR)stream))
    {
        EnterCriticalSection(&stream->lock);
        if (stream->closing || !stream->started)
        {
            LeaveCriticalSection(&stream->lock);
            break;
        }

        if (stream->capture_locked || stream->capture_frames)
        {
            if (stream->event)
                SetEvent(stream->event);
            LeaveCriticalSection(&stream->lock);
            Sleep(10);
            continue;
        }

        if (FAILED(ensure_stream_buffer(stream, stream->period_frames)))
        {
            LeaveCriticalSection(&stream->lock);
            Sleep(10);
            continue;
        }

        bytes = stream->period_frames * stream->frame_size;
        LeaveCriticalSection(&stream->lock);

        used = 0;
        if (!stream_io_wait(stream, TRUE, stream->buffer, bytes, &used))
        {
            error = GetLastError();
            if (error == ERROR_OPERATION_ABORTED)
                break;

            WARN("ReadFile(WDMAUD) failed for capture stream, error %lu.\n", error);
            Sleep(10);
            continue;
        }

        frames = used / stream->frame_size;
        EnterCriticalSection(&stream->lock);
        if (stream->closing || !stream->started)
        {
            LeaveCriticalSection(&stream->lock);
            break;
        }

        stream->capture_frames = frames;
        stream->padding = frames;
        if (frames && stream->event)
            SetEvent(stream->event);
        LeaveCriticalSection(&stream->lock);
    }
}

static DWORD WINAPI reactos_timer_thread(void *param)
{
    struct reactos_stream *stream = param;

    SetThreadDescription(GetCurrentThread(), L"audio_client_timer");

    if (stream->flow == eCapture)
    {
        reactos_capture_timer_loop(stream);
        return 0;
    }

    while (!stream->closing && stream->started)
    {
        if (stream->event)
            SetEvent(stream->event);
        Sleep(10);
    }
    return 0;
}

static NTSTATUS reactos_is_format_supported(struct is_format_supported_params *params)
{
    HRESULT hr = validate_format(params->device, params->flow, params->fmt_in);

    if (SUCCEEDED(hr))
    {
        params->result = S_OK;
        return STATUS_SUCCESS;
    }

    params->result = hr;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_mix_format(struct get_mix_format_params *params)
{
    fill_mix_format(params->device, params->flow, params->fmt);
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_device_period(struct get_device_period_params *params)
{
    if (params->def_period)
        *params->def_period = REACTOS_DEFAULT_PERIOD;
    if (params->min_period)
        *params->min_period = REACTOS_MIN_PERIOD;
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_buffer_size(struct get_buffer_size_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    *params->frames = stream->buffer_frames;
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_latency(struct get_latency_params *params)
{
    *params->latency = REACTOS_DEFAULT_PERIOD;
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_current_padding(struct get_current_padding_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    *params->padding = stream->padding;
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_next_packet_size(struct get_next_packet_size_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    EnterCriticalSection(&stream->lock);
    *params->frames = stream->capture_frames;
    LeaveCriticalSection(&stream->lock);
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_frequency(struct get_frequency_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    *params->freq = stream->format.nSamplesPerSec;
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_position(struct get_position_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    *params->pos = query_stream_position(stream);
    if (params->qpctime)
        *params->qpctime = GetTickCount() * 10000ULL;
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_set_event_handle(struct set_event_handle_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    stream->event = params->event;
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_set_sample_rate(struct set_sample_rate_params *params)
{
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_is_started(struct is_started_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    params->result = stream->started ? S_OK : S_FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS reactos_mmdevapi_call(unsigned int code, void *args)
{
    switch (code)
    {
        case process_attach:
        case process_detach:
            return STATUS_SUCCESS;

        case main_loop_start:
        case main_loop_stop:
            return STATUS_SUCCESS;

        case get_endpoint_ids:
            fill_endpoint_ids(args);
            return STATUS_SUCCESS;

        case create_stream:
            return reactos_create_stream(args);

        case release_stream:
            return reactos_release_stream(args);

        case start:
            return reactos_start(args);

        case stop:
            return reactos_stop(args);

        case reset:
            return reactos_reset(args);

        case midi_get_driver:
            ((WCHAR *)args)[0] = 0;
            return STATUS_SUCCESS;

        case get_render_buffer:
            return reactos_get_render_buffer(args);

        case release_render_buffer:
            return reactos_release_render_buffer(args);

        case get_capture_buffer:
            return reactos_get_capture_buffer(args);

        case release_capture_buffer:
            return reactos_release_capture_buffer(args);

        case is_format_supported:
            return reactos_is_format_supported(args);

        case get_loopback_capture_device:
        {
            struct get_loopback_capture_device_params *params = args;
            params->result = E_NOTIMPL;
            return STATUS_SUCCESS;
        }

        case get_mix_format:
            return reactos_get_mix_format(args);

        case get_device_period:
            return reactos_get_device_period(args);

        case get_buffer_size:
            return reactos_get_buffer_size(args);

        case get_latency:
            return reactos_get_latency(args);

        case get_current_padding:
            return reactos_get_current_padding(args);

        case get_next_packet_size:
            return reactos_get_next_packet_size(args);

        case get_frequency:
            return reactos_get_frequency(args);

        case get_position:
            return reactos_get_position(args);

        case set_volumes:
            return STATUS_SUCCESS;

        case set_event_handle:
            return reactos_set_event_handle(args);

        case set_sample_rate:
            return reactos_set_sample_rate(args);

        case test_connect:
        {
            struct test_connect_params *params = args;
            params->priority = Priority_Preferred;
            return STATUS_SUCCESS;
        }

        case is_started:
            return reactos_is_started(args);

        case get_prop_value:
        {
            struct get_prop_value_params *params = args;
            params->result = E_NOTIMPL;
            return STATUS_SUCCESS;
        }

        case midi_init:
        {
            struct midi_init_params *params = args;
            *params->err = MMSYSERR_NODRIVER;
            return STATUS_SUCCESS;
        }

        case midi_release:
            return STATUS_SUCCESS;

        case midi_out_message:
        {
            struct midi_out_message_params *params = args;
            *params->err = MMSYSERR_NODRIVER;
            params->notify->send_notify = FALSE;
            return STATUS_SUCCESS;
        }

        case midi_in_message:
        {
            struct midi_in_message_params *params = args;
            *params->err = MMSYSERR_NODRIVER;
            params->notify->send_notify = FALSE;
            return STATUS_SUCCESS;
        }

        case midi_notify_wait:
        {
            struct midi_notify_wait_params *params = args;
            *params->quit = TRUE;
            params->notify->send_notify = FALSE;
            return STATUS_SUCCESS;
        }

        case aux_message:
        {
            struct aux_message_params *params = args;
            *params->err = MMSYSERR_NODRIVER;
            return STATUS_SUCCESS;
        }

        default:
            FIXME("Unhandled ReactOS mmdevapi backend call %u.\n", code);
            return STATUS_NOT_IMPLEMENTED;
    }
}
