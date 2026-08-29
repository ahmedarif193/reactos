/*
 * ReactOS WDMAUD backend for Wine mmdevapi.
 *
 * Wine 10 delegates mmdevapi to wine*.drv Unix audio backends. ReactOS does
 * not have that backend ABI, so this file maps the mmdevapi backend contract
 * onto the existing WDMAUD/sysaudio stack.
 */

#include <stdarg.h>
#include <errno.h>
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
#define REACTOS_RT_NOTIFICATION_COUNT 2
#define REACTOS_RENDER_PACKET_COUNT 16

struct reactos_render_packet
{
    WDMAUD_DEVICE_INFO info;
    OVERLAPPED overlapped;
    BYTE *data;
    UINT32 capacity_frames;
    UINT32 client_frames;
    UINT32 device_frames;
    BOOL pending;
};

struct reactos_stream
{
    DWORD magic;
    volatile LONG closing;
    EDataFlow flow;
    SOUND_DEVICE_TYPE type;
    DWORD index;
    HANDLE pin;
    HANDLE user_pin;
    HANDLE io_handle;
    HANDLE event;
    HANDLE rt_event;
    HANDLE stop_event;
    HANDLE render_wake_event;
    HANDLE timer_thread;
    CRITICAL_SECTION lock;
    WAVEFORMATEXTENSIBLE format;
    WAVEFORMATEXTENSIBLE device_format;
    UINT32 frame_size;
    UINT32 device_frame_size;
    UINT32 buffer_frames;
    UINT32 period_frames;
    UINT32 device_period_frames;
    UINT32 padding;
    UINT32 capture_frames;
    UINT64 position;
    UINT64 resample_offset;
    BYTE *rt_buffer;
    UINT32 rt_buffer_frames;
    UINT32 rt_period_frames;
    UINT32 rt_period_index;
    UINT32 rt_period_queued_frames[REACTOS_RT_NOTIFICATION_COUNT];
    UINT64 position_qpc_100ns;
    BYTE *render_ring;
    UINT32 render_ring_read_frame;
    UINT32 render_ring_write_frame;
    UINT32 render_ring_frames;
    BYTE *render_conversion_buffer;
    UINT32 render_conversion_buffer_alloc_frames;
    DWORD render_error;
    struct reactos_render_packet render_packets[REACTOS_RENDER_PACKET_COUNT];
    BYTE *buffer;
    BYTE *device_buffer;
    UINT32 buffer_alloc_frames;
    UINT32 device_buffer_alloc_frames;
    BOOL capture_conversion;
    BOOL render_conversion;
    BOOL client_float;
    BOOL capture_locked;
    BOOL render_locked;
    UINT32 render_locked_frames;
    float *channel_volumes;
    BOOL volume_passthrough;
    BOOL rt_enabled;
    BOOL started;
};

static HANDLE wdmaud_handle = INVALID_HANDLE_VALUE;
static WCHAR wdmaud_path[MAX_PATH];

static DWORD WINAPI reactos_timer_thread(void *param);

static const GUID wdmaud_category = {STATIC_KSCATEGORY_WDMAUD};
static const GUID rt_audio_property_set = {STATIC_KSPROPSETID_RtAudio};

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

    errno = 0;
    value = strtoul(index_string, &end, 10);
    if (*index_string == '\0' || *end != '\0' || errno == ERANGE ||
        value > MAXDWORD)
        return FALSE;

    *index = value;
    return TRUE;
}

static HRESULT wdmaud_error_hresult(void)
{
    DWORD error = GetLastError();

    return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
}

static BOOL query_wave_mixer_id(EDataFlow flow, DWORD index, DWORD *mixer_index)
{
    WDMAUD_DEVICE_INFO info;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = device_type_from_flow(flow);
    info.DeviceIndex = index;
    if (!wdmaud_ioctl(IOCTL_GETWAVEMIXERID, &info))
        return FALSE;

    *mixer_index = info.u.MixerId;
    return TRUE;
}

static BOOL query_mixer_line(DWORD mixer_index, DWORD component_type,
                             MIXERLINEW *line)
{
    WDMAUD_DEVICE_INFO info;

    ZeroMemory(line, sizeof(*line));
    line->cbStruct = sizeof(*line);
    line->dwComponentType = component_type;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = MIXER_DEVICE_TYPE;
    info.DeviceIndex = mixer_index;
    info.Flags = MIXER_OBJECTF_MIXER | MIXER_GETLINEINFOF_COMPONENTTYPE;
    info.u.MixLine = *line;
    if (!wdmaud_ioctl(IOCTL_GETLINEINFO, &info))
        return FALSE;

    *line = info.u.MixLine;
    return TRUE;
}

static BOOL query_render_mixer_line(DWORD mixer_index, MIXERLINEW *line)
{
    static const DWORD component_types[] =
    {
        MIXERLINE_COMPONENTTYPE_DST_SPEAKERS,
        MIXERLINE_COMPONENTTYPE_DST_HEADPHONES,
        MIXERLINE_COMPONENTTYPE_DST_DIGITAL,
        MIXERLINE_COMPONENTTYPE_DST_LINE
    };
    UINT i;

    for (i = 0; i < ARRAY_SIZE(component_types); ++i)
    {
        if (query_mixer_line(mixer_index, component_types[i], line))
            return TRUE;
    }

    return FALSE;
}

static BOOL query_mixer_control(DWORD mixer_index, DWORD line_id,
                                DWORD control_type, MIXERCONTROLW *control)
{
    MIXERLINECONTROLSW controls;
    WDMAUD_DEVICE_INFO info;

    ZeroMemory(control, sizeof(*control));
    control->cbStruct = sizeof(*control);

    ZeroMemory(&controls, sizeof(controls));
    controls.cbStruct = sizeof(controls);
    controls.dwLineID = line_id;
    controls.dwControlType = control_type;
    controls.cControls = 1;
    controls.cbmxctrl = sizeof(*control);
    controls.pamxctrl = control;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = MIXER_DEVICE_TYPE;
    info.DeviceIndex = mixer_index;
    info.Flags = MIXER_OBJECTF_MIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE;
    info.u.MixControls = controls;
    return wdmaud_ioctl(IOCTL_GETLINECONTROLS, &info);
}

static HRESULT endpoint_volume_details(
    const struct reactos_endpoint_volume_state *state, BOOL set,
    MIXERCONTROLDETAILS_UNSIGNED *values)
{
    MIXERCONTROLDETAILS details;
    WDMAUD_DEVICE_INFO info;

    ZeroMemory(&details, sizeof(details));
    details.cbStruct = sizeof(details);
    details.dwControlID = state->volume_control_id;
    details.cChannels = state->volume_control_channels;
    details.cbDetails = sizeof(*values);
    details.paDetails = values;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = MIXER_DEVICE_TYPE;
    info.DeviceIndex = state->mixer_index;
    info.Flags = MIXER_OBJECTF_MIXER;
    info.u.MixDetails = details;
    if (!wdmaud_ioctl(set ? IOCTL_SETCONTROLDETAILS : IOCTL_GETCONTROLDETAILS,
                      &info))
    {
        return wdmaud_error_hresult();
    }

    return S_OK;
}

static HRESULT endpoint_mute_details(
    const struct reactos_endpoint_volume_state *state, BOOL set,
    MIXERCONTROLDETAILS_BOOLEAN *values)
{
    MIXERCONTROLDETAILS details;
    WDMAUD_DEVICE_INFO info;

    ZeroMemory(&details, sizeof(details));
    details.cbStruct = sizeof(details);
    details.dwControlID = state->mute_control_id;
    details.cChannels = state->mute_control_channels;
    details.cbDetails = sizeof(*values);
    details.paDetails = values;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = MIXER_DEVICE_TYPE;
    info.DeviceIndex = state->mixer_index;
    info.Flags = MIXER_OBJECTF_MIXER;
    info.u.MixDetails = details;
    if (!wdmaud_ioctl(set ? IOCTL_SETCONTROLDETAILS : IOCTL_GETCONTROLDETAILS,
                      &info))
    {
        return wdmaud_error_hresult();
    }

    return S_OK;
}

HRESULT reactos_endpoint_volume_initialize(
    const GUID *guid, struct reactos_endpoint_volume_state *state)
{
    MIXERCONTROLW volume_control;
    MIXERCONTROLW mute_control;
    MIXERLINEW line;
    EDataFlow flow;
    DWORD wave_index;
    char *device;

    if (!guid || !state)
        return E_POINTER;

    ZeroMemory(state, sizeof(*state));
    if (!get_device_name_from_guid(guid, &device, &flow))
        return AUDCLNT_E_DEVICE_INVALIDATED;

    if (!parse_device_name(device, &flow, &wave_index) || flow != eRender)
    {
        free(device);
        return E_NOINTERFACE;
    }
    free(device);

    if (!open_wdmaud())
        return AUDCLNT_E_DEVICE_INVALIDATED;
    if (!query_wave_mixer_id(flow, wave_index, &state->mixer_index))
        return wdmaud_error_hresult();
    if (!query_render_mixer_line(state->mixer_index, &line))
        return wdmaud_error_hresult();
    if (!query_mixer_control(state->mixer_index, line.dwLineID,
                             MIXERCONTROL_CONTROLTYPE_VOLUME,
                             &volume_control))
    {
        return wdmaud_error_hresult();
    }

    state->channel_count = max(line.cChannels, 1);
    state->volume_control_channels =
        (volume_control.fdwControl & MIXERCONTROL_CONTROLF_UNIFORM) ?
        1 : state->channel_count;
    state->volume_control_id = volume_control.dwControlID;
    state->step_count = max(volume_control.Metrics.cSteps, 2);

    if (query_mixer_control(state->mixer_index, line.dwLineID,
                            MIXERCONTROL_CONTROLTYPE_MUTE,
                            &mute_control))
    {
        state->mute_control_id = mute_control.dwControlID;
        state->mute_control_channels =
            (mute_control.fdwControl & MIXERCONTROL_CONTROLF_UNIFORM) ?
            1 : state->channel_count;
        state->mute_supported = TRUE;
    }

    return S_OK;
}

HRESULT reactos_endpoint_volume_get(
    const struct reactos_endpoint_volume_state *state, float *levels,
    UINT count)
{
    MIXERCONTROLDETAILS_UNSIGNED *values;
    HRESULT hr;
    UINT i;

    if (!state || !levels)
        return E_POINTER;
    if (count != state->channel_count)
        return E_INVALIDARG;

    values = calloc(state->volume_control_channels, sizeof(*values));
    if (!values)
        return E_OUTOFMEMORY;

    hr = endpoint_volume_details(state, FALSE, values);
    if (SUCCEEDED(hr))
    {
        for (i = 0; i < count; ++i)
        {
            levels[i] = min(values[state->volume_control_channels == 1 ? 0 : i].dwValue,
                            0xffff) / 65535.0f;
        }
    }

    free(values);
    return hr;
}

HRESULT reactos_endpoint_volume_set(
    const struct reactos_endpoint_volume_state *state, const float *levels,
    UINT count)
{
    MIXERCONTROLDETAILS_UNSIGNED *values;
    HRESULT hr;
    UINT i;

    if (!state || !levels)
        return E_POINTER;
    if (count != state->channel_count)
        return E_INVALIDARG;

    for (i = 0; i < count; ++i)
    {
        if (levels[i] < 0.0f || levels[i] > 1.0f)
            return E_INVALIDARG;
    }

    values = calloc(state->volume_control_channels, sizeof(*values));
    if (!values)
        return E_OUTOFMEMORY;

    for (i = 0; i < state->volume_control_channels; ++i)
        values[i].dwValue = (DWORD)(levels[i] * 65535.0f + 0.5f);

    hr = endpoint_volume_details(state, TRUE, values);
    free(values);
    return hr;
}

HRESULT reactos_endpoint_mute_get(
    const struct reactos_endpoint_volume_state *state, BOOL *mute)
{
    MIXERCONTROLDETAILS_BOOLEAN *values;
    HRESULT hr;
    UINT i;

    if (!state || !mute)
        return E_POINTER;
    if (!state->mute_supported)
        return E_NOTIMPL;

    values = calloc(state->mute_control_channels, sizeof(*values));
    if (!values)
        return E_OUTOFMEMORY;

    hr = endpoint_mute_details(state, FALSE, values);
    if (SUCCEEDED(hr))
    {
        *mute = FALSE;
        for (i = 0; i < state->mute_control_channels; ++i)
            *mute = *mute || values[i].fValue;
    }

    free(values);
    return hr;
}

HRESULT reactos_endpoint_mute_set(
    const struct reactos_endpoint_volume_state *state, BOOL mute)
{
    MIXERCONTROLDETAILS_BOOLEAN *values;
    HRESULT hr;
    UINT i;

    if (!state)
        return E_POINTER;
    if (!state->mute_supported)
        return E_NOTIMPL;

    values = calloc(state->mute_control_channels, sizeof(*values));
    if (!values)
        return E_OUTOFMEMORY;

    for (i = 0; i < state->mute_control_channels; ++i)
        values[i].fValue = mute;

    hr = endpoint_mute_details(state, TRUE, values);
    free(values);
    return hr;
}

static void format_device_name(EDataFlow flow, DWORD index, char *buffer, size_t size)
{
    const char *kind = flow == eCapture ? REACTOS_DEVICE_CAPTURE : REACTOS_DEVICE_RENDER;

    snprintf(buffer, size, REACTOS_DEVICE_PREFIX "%s:%lu", kind, index);
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

static BOOL query_preferred_wave_format(EDataFlow flow, DWORD index,
                                        WAVEFORMATEXTENSIBLE *format)
{
    WDMAUD_DEVICE_INFO info;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = device_type_from_flow(flow);
    info.DeviceIndex = index;

    if (!wdmaud_ioctl(IOCTL_GETPREFERRED_WAVE_FORMAT, &info))
        return FALSE;

    *format = info.u.WaveFormatExtensible;
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
    else if (query_preferred_wave_format(parsed_flow, index, fmt))
        return TRUE;

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

static BOOL is_float_format(const WAVEFORMATEX *fmt)
{
    const WAVEFORMATEXTENSIBLE *ext;

    if (!fmt)
        return FALSE;

    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return TRUE;

    if (fmt->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        fmt->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        return FALSE;

    ext = (const WAVEFORMATEXTENSIBLE *)fmt;
    return IsEqualGUID(&ext->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
}

static HRESULT validate_pcm_format(const WAVEFORMATEX *fmt)
{
    WORD valid_bits;
    UINT block_align;

    if (!is_pcm_format(fmt))
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    valid_bits = valid_bits_from_format(fmt);
    if (!fmt->nChannels || !fmt->nSamplesPerSec ||
        fmt->nSamplesPerSec > 192000 || !valid_bits ||
        valid_bits > fmt->wBitsPerSample ||
        (fmt->wBitsPerSample != 8 && fmt->wBitsPerSample != 16 &&
         fmt->wBitsPerSample != 24 && fmt->wBitsPerSample != 32))
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    block_align = (fmt->nChannels * fmt->wBitsPerSample) / 8;
    if (!block_align || fmt->nBlockAlign != block_align ||
        fmt->nAvgBytesPerSec !=
            (UINT64)fmt->nSamplesPerSec * fmt->nBlockAlign)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    return S_OK;
}

static HRESULT validate_render_client_format(const WAVEFORMATEX *fmt)
{
    UINT block_align;

    if (is_pcm_format(fmt))
        return validate_pcm_format(fmt);

    if (!is_float_format(fmt) || fmt->wBitsPerSample != 32 ||
        valid_bits_from_format(fmt) != 32 || !fmt->nChannels ||
        !fmt->nSamplesPerSec || fmt->nSamplesPerSec > 192000)
    {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    block_align = fmt->nChannels * sizeof(float);
    if (!block_align || fmt->nBlockAlign != block_align ||
        fmt->nAvgBytesPerSec !=
            (UINT64)fmt->nSamplesPerSec * fmt->nBlockAlign)
    {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    return S_OK;
}

static BOOL shared_render_conversion_supported(const WAVEFORMATEX *client,
                                               const WAVEFORMATEX *device)
{
    return SUCCEEDED(validate_render_client_format(client)) &&
           SUCCEEDED(validate_pcm_format(device)) &&
           client->nSamplesPerSec == device->nSamplesPerSec &&
           client->nChannels == device->nChannels;
}

static HRESULT validate_format(const char *device, EDataFlow flow, const WAVEFORMATEX *fmt)
{
    WDMAUD_DEVICE_INFO caps;
    WAVEFORMATEXTENSIBLE mix;
    DWORD index = 0, formats, flag;
    EDataFlow parsed_flow = flow;
    HRESULT hr;
    WORD cap_channels;

    if (FAILED(hr = validate_pcm_format(fmt)))
        return hr;

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

static void copy_wdmaud_format(WAVEFORMATEXTENSIBLE *dst,
                               const WAVEFORMATEX *src)
{
    ZeroMemory(dst, sizeof(*dst));
    dst->Format = *src;
    dst->Samples.wValidBitsPerSample = src->wBitsPerSample;
    dst->dwChannelMask = channel_mask_from_count(src->nChannels);
    if (src->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        dst->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    else
        dst->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    if (src->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        src->cbSize >= sizeof(*dst) - sizeof(dst->Format))
    {
        const WAVEFORMATEXTENSIBLE *src_ext =
            (const WAVEFORMATEXTENSIBLE *)src;

        dst->Samples = src_ext->Samples;
        dst->dwChannelMask = src_ext->dwChannelMask;
        dst->SubFormat = src_ext->SubFormat;
    }
}

static void close_stream_pin(struct reactos_stream *stream);

static BOOL pin_ioctl(HANDLE pin, DWORD ioctl, void *input, DWORD input_size,
                      void *output, DWORD output_size, DWORD *bytes_returned)
{
    OVERLAPPED overlapped;
    DWORD error = ERROR_SUCCESS, transferred = 0;
    BOOL ret;

    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!overlapped.hEvent)
        return FALSE;

    ret = DeviceIoControl(pin, ioctl, input, input_size, output, output_size,
                          &transferred, &overlapped);
    if (!ret)
    {
        error = GetLastError();
        if (error == ERROR_IO_PENDING)
        {
            if (WaitForSingleObject(overlapped.hEvent, INFINITE) == WAIT_OBJECT_0)
                ret = GetOverlappedResult(pin, &overlapped, &transferred, FALSE);
            if (!ret)
                error = GetLastError();
        }
    }

    CloseHandle(overlapped.hEvent);
    if (bytes_returned)
        *bytes_returned = transferred;
    if (!ret)
        SetLastError(error);
    return ret;
}

static BOOL duplicate_stream_pin(struct reactos_stream *stream)
{
    WDMAUD_DEVICE_INFO info;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = stream->type;
    info.DeviceIndex = stream->index;
    info.hDevice = stream->pin;
    if (!wdmaud_ioctl(IOCTL_DUPLICATE_WDMAUD_PIN, &info))
        return FALSE;

    stream->user_pin = info.u.hUserDevice;
    return stream->user_pin && stream->user_pin != INVALID_HANDLE_VALUE;
}

static BOOL initialize_wavert(struct reactos_stream *stream)
{
    KSRTAUDIO_BUFFER_PROPERTY_WITH_NOTIFICATION property;
    KSRTAUDIO_NOTIFICATION_EVENT_PROPERTY event_property;
    KSRTAUDIO_BUFFER buffer;
    UINT64 requested_size;
    DWORD returned;

    requested_size = (UINT64)stream->device_period_frames *
                     stream->device_frame_size *
                     REACTOS_RT_NOTIFICATION_COUNT;
    if (!requested_size || requested_size > MAXDWORD)
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }

    ZeroMemory(&property, sizeof(property));
    property.Property.Set = rt_audio_property_set;
    property.Property.Id = KSPROPERTY_RTAUDIO_BUFFER_WITH_NOTIFICATION;
    property.Property.Flags = KSPROPERTY_TYPE_GET;
    property.RequestedBufferSize = (ULONG)requested_size;
    property.NotificationCount = REACTOS_RT_NOTIFICATION_COUNT;
    ZeroMemory(&buffer, sizeof(buffer));

    if (!pin_ioctl(stream->user_pin, IOCTL_KS_PROPERTY,
                   &property, sizeof(property), &buffer, sizeof(buffer), &returned))
    {
        return FALSE;
    }
    if (returned < sizeof(buffer) || !buffer.BufferAddress ||
        !buffer.ActualBufferSize ||
        buffer.ActualBufferSize %
            (stream->device_frame_size * REACTOS_RT_NOTIFICATION_COUNT))
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    stream->rt_buffer = buffer.BufferAddress;
    stream->rt_buffer_frames = buffer.ActualBufferSize / stream->device_frame_size;
    stream->rt_period_frames = stream->rt_buffer_frames / REACTOS_RT_NOTIFICATION_COUNT;
    ZeroMemory(stream->rt_period_queued_frames, sizeof(stream->rt_period_queued_frames));
    ZeroMemory(stream->rt_buffer, buffer.ActualBufferSize);
    MemoryBarrier();

    stream->rt_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!stream->rt_event)
        return FALSE;

    ZeroMemory(&event_property, sizeof(event_property));
    event_property.Property.Set = rt_audio_property_set;
    event_property.Property.Id = KSPROPERTY_RTAUDIO_REGISTER_NOTIFICATION_EVENT;
    event_property.Property.Flags = KSPROPERTY_TYPE_GET;
    event_property.NotificationEvent = stream->rt_event;
    if (!pin_ioctl(stream->user_pin, IOCTL_KS_PROPERTY,
                   &event_property, sizeof(event_property), NULL, 0, &returned))
    {
        return FALSE;
    }

    stream->rt_enabled = TRUE;
    return TRUE;
}

static void cleanup_wavert(struct reactos_stream *stream)
{
    KSRTAUDIO_NOTIFICATION_EVENT_PROPERTY property;
    DWORD returned;

    if (stream->rt_enabled && stream->user_pin != INVALID_HANDLE_VALUE)
    {
        ZeroMemory(&property, sizeof(property));
        property.Property.Set = rt_audio_property_set;
        property.Property.Id = KSPROPERTY_RTAUDIO_UNREGISTER_NOTIFICATION_EVENT;
        property.Property.Flags = KSPROPERTY_TYPE_GET;
        property.NotificationEvent = stream->rt_event;
        pin_ioctl(stream->user_pin, IOCTL_KS_PROPERTY,
                  &property, sizeof(property), NULL, 0, &returned);
    }

    stream->rt_enabled = FALSE;
    stream->rt_buffer = NULL;
    stream->rt_buffer_frames = 0;
    stream->rt_period_frames = 0;
    stream->rt_period_index = 0;
    ZeroMemory(stream->rt_period_queued_frames, sizeof(stream->rt_period_queued_frames));
    if (stream->rt_event)
    {
        CloseHandle(stream->rt_event);
        stream->rt_event = NULL;
    }
    if (stream->user_pin != INVALID_HANDLE_VALUE)
    {
        CloseHandle(stream->user_pin);
        stream->user_pin = INVALID_HANDLE_VALUE;
    }
}

static HRESULT open_stream_pin(struct reactos_stream *stream)
{
    WDMAUD_DEVICE_INFO info;

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = stream->type;
    info.DeviceIndex = stream->index;
    info.u.WaveFormatExtensible = stream->device_format;

    if (!wdmaud_ioctl(IOCTL_OPEN_WDMAUD, &info))
    {
        WARN("IOCTL_OPEN_WDMAUD failed for flow %u index %lu, error %lu.\n",
             stream->flow, stream->index, GetLastError());
        return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    }

    stream->pin = info.hDevice;
    if (stream->flow == eRender && duplicate_stream_pin(stream))
    {
        if (initialize_wavert(stream))
            return S_OK;

        if (!stream->rt_buffer)
        {
            CloseHandle(stream->user_pin);
            stream->user_pin = INVALID_HANDLE_VALUE;
        }
        else
        {
            WARN("Failed to initialize WaveRT stream for flow %u index %lu, error %lu.\n",
                 stream->flow, stream->index, GetLastError());
            close_stream_pin(stream);
            return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
        }
    }

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

    cleanup_wavert(stream);

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

    if (stream->rt_enabled || stream->capture_conversion ||
        stream->render_conversion || !stream->pin)
    {
        return stream->position;
    }

    ZeroMemory(&info, sizeof(info));
    info.DeviceType = stream->type;
    info.DeviceIndex = stream->index;
    info.hDevice = stream->pin;

    if (!wdmaud_ioctl(IOCTL_GETPOS, &info))
        return stream->position;

    return info.u.Position;
}

static UINT64 query_performance_time_100ns(void)
{
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    ULONGLONG quotient;
    ULONGLONG remainder;

    if (!NT_SUCCESS(NtQueryPerformanceCounter(&counter, &frequency)) ||
        frequency.QuadPart <= 0)
        return GetTickCount64() * 10000ULL;

    quotient = (ULONGLONG)counter.QuadPart / (ULONGLONG)frequency.QuadPart;
    remainder = (ULONGLONG)counter.QuadPart % (ULONGLONG)frequency.QuadPart;
    return quotient * 10000000ULL +
           remainder * 10000000ULL / (ULONGLONG)frequency.QuadPart;
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

static void cancel_render_packets(struct reactos_stream *stream)
{
    UINT i;

    if (stream->io_handle == INVALID_HANDLE_VALUE)
        return;

    CancelIoEx(stream->io_handle, NULL);
    for (i = 0; i < REACTOS_RENDER_PACKET_COUNT; ++i)
    {
        struct reactos_render_packet *packet = &stream->render_packets[i];
        DWORD transferred;

        if (!packet->pending)
            continue;

        GetOverlappedResult(stream->io_handle, &packet->overlapped,
                            &transferred, TRUE);
        packet->pending = FALSE;
        packet->client_frames = 0;
        packet->device_frames = 0;
        ResetEvent(packet->overlapped.hEvent);
    }
}

static void cleanup_render_queue(struct reactos_stream *stream)
{
    UINT i;

    cancel_render_packets(stream);
    for (i = 0; i < REACTOS_RENDER_PACKET_COUNT; ++i)
    {
        struct reactos_render_packet *packet = &stream->render_packets[i];

        free(packet->data);
        packet->data = NULL;
        packet->capacity_frames = 0;
        if (packet->overlapped.hEvent)
        {
            CloseHandle(packet->overlapped.hEvent);
            packet->overlapped.hEvent = NULL;
        }
    }

    free(stream->render_ring);
    stream->render_ring = NULL;
    free(stream->render_conversion_buffer);
    stream->render_conversion_buffer = NULL;
    stream->render_conversion_buffer_alloc_frames = 0;
    if (stream->render_wake_event)
    {
        CloseHandle(stream->render_wake_event);
        stream->render_wake_event = NULL;
    }
}

static BOOL initialize_render_queue(struct reactos_stream *stream)
{
    SIZE_T ring_bytes;
    UINT i;

    if (!stream->frame_size ||
        (SIZE_T)stream->buffer_frames > ~(SIZE_T)0 / stream->frame_size)
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }

    ring_bytes = (SIZE_T)stream->buffer_frames * stream->frame_size;
    stream->render_ring = malloc(ring_bytes);
    if (!stream->render_ring)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    if (stream->rt_enabled)
        return TRUE;

    stream->render_wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!stream->render_wake_event)
        goto failed;

    for (i = 0; i < REACTOS_RENDER_PACKET_COUNT; ++i)
    {
        stream->render_packets[i].overlapped.hEvent =
            CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!stream->render_packets[i].overlapped.hEvent)
            goto failed;
    }

    return TRUE;

failed:
    cleanup_render_queue(stream);
    return FALSE;
}

static INT32 read_pcm_sample(const BYTE *data, WORD bits, WORD valid_bits);
static void write_pcm_sample(BYTE *data, WORD bits, WORD valid_bits,
                             INT32 value);

/* Grows one of the stream's frame buffers in place. Shared by every buffer the
   stream owns so the overflow guard and growth policy exist in one place. */
static BOOL ensure_frame_buffer(BYTE **buffer, UINT32 *alloc_frames, UINT32 frames, UINT stride)
{
    BYTE *grown;

    if (*alloc_frames >= frames)
        return TRUE;

    if (!stride || (SIZE_T)frames > ~(SIZE_T)0 / stride)
        return FALSE;

    grown = realloc(*buffer, (SIZE_T)frames * stride);
    if (!grown)
        return FALSE;

    *buffer = grown;
    *alloc_frames = frames;
    return TRUE;
}

static BOOL ensure_render_conversion_buffer(struct reactos_stream *stream, UINT32 frames)
{
    return ensure_frame_buffer(&stream->render_conversion_buffer, &stream->render_conversion_buffer_alloc_frames, frames, stream->frame_size);
}

static INT32 read_render_sample(const struct reactos_stream *stream,
                                const BYTE *data)
{
    float sample;

    if (!stream->client_float)
        return read_pcm_sample(data, stream->format.Format.wBitsPerSample,
                               stream->format.Samples.wValidBitsPerSample);

    memcpy(&sample, data, sizeof(sample));
    if (sample != sample)
        return 0;
    if (sample >= 1.0f)
        return 0x7fffffff;
    if (sample <= -1.0f)
        return (-2147483647 - 1);
    return (INT32)(sample * 2147483647.0f);
}

static void apply_render_volume(struct reactos_stream *stream, BYTE *data,
                                UINT32 frames)
{
    UINT sample_size = stream->format.Format.wBitsPerSample / 8;
    UINT32 frame;
    WORD channel;

    if (stream->volume_passthrough)
        return;

    for (frame = 0; frame < frames; ++frame)
    {
        BYTE *frame_data = data + (SIZE_T)frame * stream->frame_size;

        for (channel = 0; channel < stream->format.Format.nChannels; ++channel)
        {
            BYTE *sample_data = frame_data + channel * sample_size;
            float gain = stream->channel_volumes[channel];

            if (stream->client_float)
            {
                float sample;

                memcpy(&sample, sample_data, sizeof(sample));
                sample *= gain;
                memcpy(sample_data, &sample, sizeof(sample));
            }
            else
            {
                INT32 sample = read_pcm_sample(sample_data,
                                               stream->format.Format.wBitsPerSample,
                                               stream->format.Samples.wValidBitsPerSample);

                sample = (INT32)((double)sample * gain);
                write_pcm_sample(sample_data,
                                 stream->format.Format.wBitsPerSample,
                                 stream->format.Samples.wValidBitsPerSample,
                                 sample);
            }
        }
    }
}

static void convert_render_frames(const struct reactos_stream *stream,
                                  const BYTE *source, UINT32 frames,
                                  BYTE *destination)
{
    UINT source_sample_size = stream->format.Format.wBitsPerSample / 8;
    UINT destination_sample_size =
        stream->device_format.Format.wBitsPerSample / 8;
    UINT32 frame;
    WORD channel;

    for (frame = 0; frame < frames; ++frame)
    {
        const BYTE *source_frame =
            source + (SIZE_T)frame * stream->frame_size;
        BYTE *destination_frame =
            destination + (SIZE_T)frame * stream->device_frame_size;

        for (channel = 0;
             channel < stream->device_format.Format.nChannels;
             ++channel)
        {
            INT32 sample = read_render_sample(
                stream, source_frame + channel * source_sample_size);

            write_pcm_sample(destination_frame + channel * destination_sample_size,
                             stream->device_format.Format.wBitsPerSample,
                             stream->device_format.Samples.wValidBitsPerSample,
                             sample);
        }
    }
}

static BOOL fill_wavert_period(struct reactos_stream *stream, UINT32 period)
{
    BYTE *destination;
    BYTE *source;
    UINT32 first_frames;
    UINT32 frames;

    if (!stream->rt_buffer || !stream->render_ring ||
        period >= REACTOS_RT_NOTIFICATION_COUNT)
    {
        return FALSE;
    }

    frames = min(stream->rt_period_frames, stream->render_ring_frames);
    destination = stream->rt_buffer +
                  (SIZE_T)period * stream->rt_period_frames *
                      stream->device_frame_size;
    source = destination;

    if (frames && stream->render_conversion)
    {
        if (!ensure_render_conversion_buffer(stream, frames))
            return FALSE;
        source = stream->render_conversion_buffer;
    }

    first_frames = min(frames,
                       stream->buffer_frames -
                           stream->render_ring_read_frame);
    if (first_frames)
    {
        CopyMemory(source,
                   stream->render_ring +
                       (SIZE_T)stream->render_ring_read_frame *
                           stream->frame_size,
                   (SIZE_T)first_frames * stream->frame_size);
    }
    if (frames > first_frames)
    {
        CopyMemory(source + (SIZE_T)first_frames * stream->frame_size,
                   stream->render_ring,
                   (SIZE_T)(frames - first_frames) * stream->frame_size);
    }

    if (frames)
        apply_render_volume(stream, source, frames);

    if (frames && stream->render_conversion)
        convert_render_frames(stream, source, frames, destination);
    if (frames < stream->rt_period_frames)
    {
        ZeroMemory(destination +
                       (SIZE_T)frames * stream->device_frame_size,
                   (SIZE_T)(stream->rt_period_frames - frames) *
                       stream->device_frame_size);
    }

    stream->render_ring_read_frame =
        (stream->render_ring_read_frame + frames) % stream->buffer_frames;
    stream->render_ring_frames -= frames;
    stream->rt_period_queued_frames[period] = frames;
    MemoryBarrier();
    return TRUE;
}

static BOOL prime_wavert_buffer(struct reactos_stream *stream)
{
    UINT32 period;

    if (!stream->rt_enabled)
        return FALSE;

    for (period = 0; period < REACTOS_RT_NOTIFICATION_COUNT; ++period)
    {
        if (!fill_wavert_period(stream, period))
            return FALSE;
    }

    stream->rt_period_index = 0;
    return TRUE;
}

static void complete_render_packet(struct reactos_stream *stream,
                                   struct reactos_render_packet *packet)
{
    DWORD error = ERROR_SUCCESS;
    DWORD transferred;
    UINT32 completed_client_frames = 0;
    BOOL ret;

    ret = GetOverlappedResult(stream->io_handle, &packet->overlapped,
                              &transferred, FALSE);
    if (!ret)
        error = GetLastError();
    else if (packet->device_frames)
    {
        UINT32 completed_device_frames =
            min(packet->device_frames,
                packet->info.Header.DataUsed / stream->device_frame_size);

        completed_client_frames = (UINT32)(
            (UINT64)completed_device_frames * packet->client_frames /
            packet->device_frames);
    }

    EnterCriticalSection(&stream->lock);
    if (ret)
        stream->position += completed_client_frames;
    else if (error != ERROR_OPERATION_ABORTED)
    {
        ERR("Render completion failed, stream %p, packet %p, event %p, error %lu, transferred %lu, data used %lu, client frames %u, device frames %u.\n",
            stream, packet, packet->overlapped.hEvent, error, transferred,
            packet->info.Header.DataUsed,
            packet->client_frames, packet->device_frames);
        stream->render_error = error;
    }

    if (stream->padding > packet->client_frames)
        stream->padding -= packet->client_frames;
    else
        stream->padding = 0;
    LeaveCriticalSection(&stream->lock);

    packet->pending = FALSE;
    packet->client_frames = 0;
    packet->device_frames = 0;
    ResetEvent(packet->overlapped.hEvent);
    if (stream->event)
        SetEvent(stream->event);
}

static BOOL submit_render_packet(struct reactos_stream *stream,
                                 struct reactos_render_packet *packet)
{
    HANDLE completion_event = packet->overlapped.hEvent;
    BYTE *staging;
    UINT32 first_frames;
    UINT32 client_frames;
    UINT32 device_frames;
    UINT32 bytes;
    DWORD transferred;
    DWORD error;
    BOOL ret;

    EnterCriticalSection(&stream->lock);
    if (!stream->started || stream->render_error || !stream->render_ring_frames)
    {
        LeaveCriticalSection(&stream->lock);
        return FALSE;
    }

    client_frames = min(stream->period_frames, stream->render_ring_frames);
    device_frames = client_frames;
    if (packet->capacity_frames < device_frames)
    {
        BYTE *data = realloc(packet->data,
                             (SIZE_T)device_frames *
                                 stream->device_frame_size);

        if (!data)
        {
            stream->render_error = ERROR_NOT_ENOUGH_MEMORY;
            LeaveCriticalSection(&stream->lock);
            if (stream->event)
                SetEvent(stream->event);
            return FALSE;
        }
        packet->data = data;
        packet->capacity_frames = device_frames;
    }

    if (stream->render_conversion &&
        !ensure_render_conversion_buffer(stream, client_frames))
    {
        stream->render_error = ERROR_NOT_ENOUGH_MEMORY;
        LeaveCriticalSection(&stream->lock);
        if (stream->event)
            SetEvent(stream->event);
        return FALSE;
    }

    /* Client-rate frames land in the conversion buffer when a rate/format
       conversion follows, otherwise straight into the packet. */
    staging = stream->render_conversion ? stream->render_conversion_buffer : packet->data;

    first_frames = min(client_frames, stream->buffer_frames - stream->render_ring_read_frame);
    CopyMemory(staging,
               stream->render_ring + (SIZE_T)stream->render_ring_read_frame * stream->frame_size,
               (SIZE_T)first_frames * stream->frame_size);
    if (client_frames > first_frames)
    {
        CopyMemory(staging + (SIZE_T)first_frames * stream->frame_size,
                   stream->render_ring,
                   (SIZE_T)(client_frames - first_frames) * stream->frame_size);
    }

    apply_render_volume(stream, staging, client_frames);

    stream->render_ring_read_frame =
        (stream->render_ring_read_frame + client_frames) %
            stream->buffer_frames;
    stream->render_ring_frames -= client_frames;
    LeaveCriticalSection(&stream->lock);

    if (stream->render_conversion)
        convert_render_frames(stream, staging, client_frames, packet->data);

    bytes = device_frames * stream->device_frame_size;
    ZeroMemory(&packet->info, sizeof(packet->info));
    packet->info.Header.Size = sizeof(packet->info);
    packet->info.Header.FrameExtent = bytes;
    packet->info.Header.DataUsed = bytes;
    packet->info.Header.Data = packet->data;
    packet->info.Header.PresentationTime.Numerator = 1;
    packet->info.Header.PresentationTime.Denominator = 1;
    packet->info.DeviceType = stream->type;
    packet->info.DeviceIndex = stream->index;
    packet->info.hDevice = stream->pin;

    ZeroMemory(&packet->overlapped, sizeof(packet->overlapped));
    packet->overlapped.hEvent = completion_event;
    ResetEvent(completion_event);
    packet->client_frames = client_frames;
    packet->device_frames = device_frames;
    packet->pending = TRUE;

    ret = WriteFile(stream->io_handle, &packet->info, sizeof(packet->info),
                    &transferred, &packet->overlapped);
    error = ret ? ERROR_SUCCESS : GetLastError();
    if (!ret)
    {
        if (error == ERROR_IO_PENDING)
            return TRUE;

        packet->pending = FALSE;
        packet->client_frames = 0;
        packet->device_frames = 0;
        EnterCriticalSection(&stream->lock);
        ERR("WriteFile(WDMAUD) failed for render stream, error %lu.\n",
            error);
        stream->render_error = error;
        if (stream->padding > client_frames)
            stream->padding -= client_frames;
        else
            stream->padding = 0;
        LeaveCriticalSection(&stream->lock);
        if (stream->event)
            SetEvent(stream->event);
        return FALSE;
    }

    SetEvent(completion_event);
    return TRUE;
}

static void reactos_render_timer_loop(struct reactos_stream *stream)
{
    struct reactos_render_packet *wait_packets[REACTOS_RENDER_PACKET_COUNT];
    HANDLE wait_handles[REACTOS_RENDER_PACKET_COUNT + 2];
    DWORD count, wait;
    UINT i;

    while (!stream->closing)
    {
        for (i = 0; i < REACTOS_RENDER_PACKET_COUNT; ++i)
        {
            if (!stream->render_packets[i].pending)
                submit_render_packet(stream, &stream->render_packets[i]);
        }

        EnterCriticalSection(&stream->lock);
        if (stream->render_error)
        {
            LeaveCriticalSection(&stream->lock);
            break;
        }
        LeaveCriticalSection(&stream->lock);

        count = 0;
        wait_handles[count++] = stream->stop_event;
        wait_handles[count++] = stream->render_wake_event;
        for (i = 0; i < REACTOS_RENDER_PACKET_COUNT; ++i)
        {
            if (!stream->render_packets[i].pending)
                continue;
            wait_handles[count] = stream->render_packets[i].overlapped.hEvent;
            wait_packets[count - 2] = &stream->render_packets[i];
            ++count;
        }

        wait = WaitForMultipleObjects(count, wait_handles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0)
        {
            if (stream->closing)
                cancel_render_packets(stream);
            break;
        }
        if (wait == WAIT_OBJECT_0 + 1)
            continue;
        if (wait >= WAIT_OBJECT_0 + 2 && wait < WAIT_OBJECT_0 + count)
        {
            complete_render_packet(stream,
                                   wait_packets[wait - WAIT_OBJECT_0 - 2]);
            continue;
        }

        EnterCriticalSection(&stream->lock);
        ERR("Render wait failed, result %lu, error %lu.\n", wait,
            GetLastError());
        stream->render_error = GetLastError();
        LeaveCriticalSection(&stream->lock);
        break;
    }
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
    WAVEFORMATEXTENSIBLE mix;
    UINT64 duration_frames;
    BOOL capture_conversion = FALSE;
    BOOL render_conversion = FALSE;
    EDataFlow flow;
    DWORD index;
    UINT32 i;
    HRESULT hr;

    if (!parse_device_name(params->device, &flow, &index) || flow != params->flow)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    hr = validate_format(params->device, params->flow, params->fmt);
    if (FAILED(hr) && hr == AUDCLNT_E_UNSUPPORTED_FORMAT &&
        params->share == AUDCLNT_SHAREMODE_SHARED)
    {
        if (flow == eCapture &&
            (params->flags & AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM) &&
            SUCCEEDED(validate_pcm_format(params->fmt)))
        {
            capture_conversion = TRUE;
        }
        else if (flow == eRender &&
                 fill_mix_format(params->device, params->flow, &mix) &&
                 shared_render_conversion_supported(params->fmt,
                                                    &mix.Format))
        {
            render_conversion = TRUE;
        }
    }
    if (FAILED(hr) && !capture_conversion && !render_conversion)
    {
        params->result = hr;
        return STATUS_SUCCESS;
    }

    stream = calloc(1, sizeof(*stream) +
                       params->fmt->nChannels * sizeof(*stream->channel_volumes));
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
    stream->user_pin = INVALID_HANDLE_VALUE;
    stream->io_handle = INVALID_HANDLE_VALUE;
    InitializeCriticalSection(&stream->lock);
    stream->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!stream->stop_event)
    {
        params->result = E_OUTOFMEMORY;
        goto failed;
    }
    copy_wdmaud_format(&stream->format, params->fmt);
    stream->channel_volumes = (float *)(stream + 1);
    stream->volume_passthrough = TRUE;
    for (i = 0; i < params->fmt->nChannels; ++i)
        stream->channel_volumes[i] = 1.0f;
    stream->device_format = stream->format;
    stream->frame_size = params->fmt->nBlockAlign;
    stream->device_frame_size = stream->frame_size;
    stream->client_float = is_float_format(params->fmt);
    if (capture_conversion || render_conversion)
    {
        if (capture_conversion &&
            !fill_mix_format(params->device, params->flow, &mix))
        {
            params->result = AUDCLNT_E_UNSUPPORTED_FORMAT;
            goto failed;
        }
        copy_wdmaud_format(&stream->device_format, &mix.Format);
        stream->device_frame_size = stream->device_format.Format.nBlockAlign;
        stream->capture_conversion = capture_conversion;
        stream->render_conversion = render_conversion;
    }
    stream->period_frames = (UINT32)(((UINT64)params->fmt->nSamplesPerSec *
                                      REACTOS_DEFAULT_PERIOD) / 10000000);
    if (!stream->period_frames)
        stream->period_frames = 1;
    stream->device_period_frames = (UINT32)(((UINT64)stream->device_format.Format.nSamplesPerSec *
                                             REACTOS_DEFAULT_PERIOD) / 10000000);
    if (!stream->device_period_frames)
        stream->device_period_frames = 1;
    if (params->duration <= 0 ||
        (UINT64)params->duration >
            (UINT64)MAXDWORD * 10000000ULL /
                params->fmt->nSamplesPerSec)
    {
        params->result = AUDCLNT_E_BUFFER_SIZE_ERROR;
        goto failed;
    }
    duration_frames = (UINT64)params->duration *
                      params->fmt->nSamplesPerSec / 10000000ULL;
    stream->buffer_frames = duration_frames > REACTOS_DEFAULT_BUFFER_FRAMES ?
                                (UINT32)duration_frames :
                                REACTOS_DEFAULT_BUFFER_FRAMES;

    if (FAILED(hr = open_stream_pin(stream)))
    {
        params->result = hr;
        goto failed;
    }

    if (stream->flow == eRender && !initialize_render_queue(stream))
    {
        params->result = E_OUTOFMEMORY;
        goto failed;
    }

    if (stream->rt_enabled && stream->render_conversion &&
        !ensure_render_conversion_buffer(stream, stream->rt_period_frames))
    {
        params->result = E_OUTOFMEMORY;
        goto failed;
    }

    if (stream->rt_enabled)
    {
        if (FAILED(hr = set_stream_state(stream, KSSTATE_ACQUIRE)) ||
            FAILED(hr = set_stream_state(stream, KSSTATE_PAUSE)))
        {
            params->result = hr;
            goto failed;
        }
    }

    *params->channel_count = params->fmt->nChannels;
    *params->stream = (stream_handle)(ULONG_PTR)stream;
    params->result = S_OK;
    return STATUS_SUCCESS;

failed:
    cleanup_render_queue(stream);
    close_stream_pin(stream);
    DeleteCriticalSection(&stream->lock);
    if (stream->stop_event)
        CloseHandle(stream->stop_event);
    free(stream);
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
        WaitForSingleObject(stream->timer_thread, INFINITE);
        CloseHandle(stream->timer_thread);
        stream->timer_thread = NULL;
    }

    set_stream_state(stream, KSSTATE_STOP);
    cleanup_render_queue(stream);
    close_stream_pin(stream);
    stream->magic = 0;
    if (stream->stop_event)
        CloseHandle(stream->stop_event);
    DeleteCriticalSection(&stream->lock);
    free(stream->buffer);
    free(stream->device_buffer);
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

    EnterCriticalSection(&stream->lock);
    if (stream->started)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_E_NOT_STOPPED;
        return STATUS_SUCCESS;
    }
    stream->started = TRUE;
    LeaveCriticalSection(&stream->lock);

    ResetEvent(stream->stop_event);
    if (stream->rt_event)
        ResetEvent(stream->rt_event);

    if (stream->rt_enabled)
    {
        EnterCriticalSection(&stream->lock);
        if (!prime_wavert_buffer(stream))
        {
            stream->started = FALSE;
            LeaveCriticalSection(&stream->lock);
            params->result = E_OUTOFMEMORY;
            return STATUS_SUCCESS;
        }
        stream->position_qpc_100ns = query_performance_time_100ns();
        LeaveCriticalSection(&stream->lock);

        stream->timer_thread = CreateThread(NULL, 0, reactos_timer_thread, stream, 0, NULL);
        if (!stream->timer_thread)
        {
            EnterCriticalSection(&stream->lock);
            stream->started = FALSE;
            LeaveCriticalSection(&stream->lock);
            params->result = E_FAIL;
            return STATUS_SUCCESS;
        }
        SetThreadPriority(stream->timer_thread, THREAD_PRIORITY_TIME_CRITICAL);
        params->result = set_stream_state(stream, KSSTATE_RUN);
        if (FAILED(params->result))
        {
            EnterCriticalSection(&stream->lock);
            stream->started = FALSE;
            LeaveCriticalSection(&stream->lock);
            SetEvent(stream->stop_event);
            WaitForSingleObject(stream->timer_thread, INFINITE);
            CloseHandle(stream->timer_thread);
            stream->timer_thread = NULL;
            return STATUS_SUCCESS;
        }
        if (stream->event && stream->flow != eCapture)
            SetEvent(stream->event);
        return STATUS_SUCCESS;
    }

    params->result = set_stream_state(stream, KSSTATE_RUN);
    if (SUCCEEDED(params->result))
    {
        EnterCriticalSection(&stream->lock);
        stream->capture_frames = 0;
        stream->capture_locked = FALSE;
        stream->resample_offset = 0;
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
        if (stream->event && stream->flow != eCapture)
            SetEvent(stream->event);
    }
    else
    {
        EnterCriticalSection(&stream->lock);
        stream->started = FALSE;
        LeaveCriticalSection(&stream->lock);
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
    if (!stream->started)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = S_FALSE;
        return STATUS_SUCCESS;
    }
    stream->started = FALSE;
    LeaveCriticalSection(&stream->lock);
    if (stream->stop_event)
        SetEvent(stream->stop_event);
    if (stream->timer_thread)
    {
        WaitForSingleObject(stream->timer_thread, INFINITE);
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

    EnterCriticalSection(&stream->lock);
    if (stream->started)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_E_NOT_STOPPED;
        return STATUS_SUCCESS;
    }
    LeaveCriticalSection(&stream->lock);

    reset_stream_pin(stream);
    if (stream->render_ring)
        cancel_render_packets(stream);
    EnterCriticalSection(&stream->lock);
    stream->padding = 0;
    stream->capture_frames = 0;
    stream->capture_locked = FALSE;
    stream->position = 0;
    stream->position_qpc_100ns = query_performance_time_100ns();
    stream->resample_offset = 0;
    stream->render_locked = FALSE;
    stream->render_locked_frames = 0;
    stream->rt_period_index = 0;
    ZeroMemory(stream->rt_period_queued_frames, sizeof(stream->rt_period_queued_frames));
    if (stream->rt_buffer)
    {
        ZeroMemory(stream->rt_buffer,
                   stream->rt_buffer_frames * stream->device_frame_size);
        MemoryBarrier();
    }
    if (stream->render_ring)
    {
        stream->render_ring_read_frame = 0;
        stream->render_ring_write_frame = 0;
        stream->render_ring_frames = 0;
        stream->render_error = ERROR_SUCCESS;
    }
    LeaveCriticalSection(&stream->lock);
    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS reactos_get_render_buffer(struct get_render_buffer_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);

    if (!stream || stream->flow != eRender)
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }

    EnterCriticalSection(&stream->lock);
    if (stream->render_locked)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_E_OUT_OF_ORDER;
        return STATUS_SUCCESS;
    }
    if (stream->render_error)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }
    if (!params->frames)
    {
        *params->data = NULL;
        LeaveCriticalSection(&stream->lock);
        params->result = S_OK;
        return STATUS_SUCCESS;
    }
    if (params->frames > stream->buffer_frames - stream->padding)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_E_BUFFER_TOO_LARGE;
        return STATUS_SUCCESS;
    }

    if (!ensure_frame_buffer(&stream->buffer, &stream->buffer_alloc_frames, params->frames, stream->frame_size))
    {
        LeaveCriticalSection(&stream->lock);
        params->result = E_OUTOFMEMORY;
        return STATUS_SUCCESS;
    }

    stream->render_locked = TRUE;
    stream->render_locked_frames = params->frames;
    *params->data = stream->buffer;
    LeaveCriticalSection(&stream->lock);
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

    EnterCriticalSection(&stream->lock);
    if (!stream->render_locked)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_E_OUT_OF_ORDER;
        return STATUS_SUCCESS;
    }
    if (params->written_frames > stream->render_locked_frames)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_E_INVALID_SIZE;
        return STATUS_SUCCESS;
    }

    if (params->flags & AUDCLNT_BUFFERFLAGS_SILENT)
        ZeroMemory(stream->buffer, params->written_frames * stream->frame_size);

    stream->render_locked = FALSE;
    stream->render_locked_frames = 0;
    if (stream->render_error)
    {
        LeaveCriticalSection(&stream->lock);
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }
    if (params->written_frames)
    {
        UINT32 first_frames;

        first_frames = min(params->written_frames,
                           stream->buffer_frames -
                               stream->render_ring_write_frame);
        CopyMemory(stream->render_ring +
                       (SIZE_T)stream->render_ring_write_frame *
                           stream->frame_size,
                   stream->buffer,
                   (SIZE_T)first_frames * stream->frame_size);
        if (params->written_frames > first_frames)
        {
            CopyMemory(stream->render_ring,
                       stream->buffer +
                           (SIZE_T)first_frames * stream->frame_size,
                       (SIZE_T)(params->written_frames - first_frames) *
                           stream->frame_size);
        }

        stream->render_ring_write_frame =
            (stream->render_ring_write_frame + params->written_frames) %
                stream->buffer_frames;
        stream->render_ring_frames += params->written_frames;
        stream->padding += params->written_frames;
    }
    LeaveCriticalSection(&stream->lock);
    if (params->written_frames && stream->render_wake_event)
        SetEvent(stream->render_wake_event);
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
        *params->qpcpos = query_performance_time_100ns();

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
    return ensure_frame_buffer(&stream->buffer, &stream->buffer_alloc_frames, frames, stream->frame_size) ? S_OK : E_OUTOFMEMORY;
}

static HRESULT ensure_device_buffer(struct reactos_stream *stream, UINT32 frames)
{
    return ensure_frame_buffer(&stream->device_buffer, &stream->device_buffer_alloc_frames, frames, stream->device_frame_size) ? S_OK : E_OUTOFMEMORY;
}

static INT32 read_pcm_sample(const BYTE *data, WORD bits, WORD valid_bits)
{
    UINT32 raw;
    WORD unused_bits;

    if (!valid_bits || valid_bits > bits)
        valid_bits = bits;
    unused_bits = bits - valid_bits;

    switch (bits)
    {
        case 8:
            raw = data[0] & (0xffu << unused_bits);
            return ((INT32)raw - 128) * 0x1000000;
        case 16:
            raw = data[0] | ((UINT32)data[1] << 8);
            raw &= 0xffffu << unused_bits;
            return (INT32)(raw << 16);
        case 24:
            raw = data[0] | ((UINT32)data[1] << 8) |
                  ((UINT32)data[2] << 16);
            raw &= 0xffffffu << unused_bits;
            return (INT32)(raw << 8);
        case 32:
            memcpy(&raw, data, sizeof(raw));
            raw &= 0xffffffffu << unused_bits;
            return (INT32)raw;
        default:
            return 0;
    }
}

static void write_pcm_sample(BYTE *data, WORD bits, WORD valid_bits,
                             INT32 value)
{
    UINT32 raw = (UINT32)value;
    WORD byte_index = 0;
    WORD unused_bits;

    if (!valid_bits || valid_bits > bits)
        valid_bits = bits;
    unused_bits = bits - valid_bits;

    switch (bits)
    {
        case 8:
            data[0] = (BYTE)(raw >> 24) ^ 0x80;
            break;
        case 16:
            data[0] = (BYTE)(raw >> 16);
            data[1] = (BYTE)(raw >> 24);
            break;
        case 24:
            data[0] = (BYTE)(raw >> 8);
            data[1] = (BYTE)(raw >> 16);
            data[2] = (BYTE)(raw >> 24);
            break;
        case 32:
            memcpy(data, &raw, sizeof(raw));
            break;
        default:
            return;
    }

    while (unused_bits >= 8)
    {
        data[byte_index++] = 0;
        unused_bits -= 8;
    }
    if (unused_bits)
        data[byte_index] &= (BYTE)(0xffu << unused_bits);
}

static UINT32 convert_capture_frames(struct reactos_stream *stream, const BYTE *source, UINT32 source_frames, BYTE *destination, UINT32 destination_capacity)
{
    const WAVEFORMATEX *source_format = &stream->device_format.Format;
    const WAVEFORMATEX *destination_format = &stream->format.Format;
    WORD source_valid_bits =
        stream->device_format.Samples.wValidBitsPerSample;
    WORD destination_valid_bits =
        stream->format.Samples.wValidBitsPerSample;
    UINT source_sample_size = source_format->wBitsPerSample / 8;
    UINT destination_sample_size = destination_format->wBitsPerSample / 8;
    UINT64 offset = stream->resample_offset;
    UINT32 source_index, destination_frames = 0;
    WORD channel;

    while (offset / destination_format->nSamplesPerSec < source_frames && destination_frames < destination_capacity)
    {
        const BYTE *source_frame;
        BYTE *destination_frame;

        source_index = (UINT32)(offset / destination_format->nSamplesPerSec);
        source_frame = source + source_index * source_format->nBlockAlign;
        destination_frame = destination + destination_frames * destination_format->nBlockAlign;

        for (channel = 0; channel < destination_format->nChannels; ++channel)
        {
            INT32 sample;

            if (destination_format->nChannels == 1 && source_format->nChannels > 1)
            {
                LONGLONG total = 0;
                WORD source_channel;

                for (source_channel = 0; source_channel < source_format->nChannels; ++source_channel)
                    total += read_pcm_sample(source_frame + source_channel * source_sample_size, source_format->wBitsPerSample, source_valid_bits);
                sample = (INT32)(total / source_format->nChannels);
            }
            else
            {
                WORD source_channel = source_format->nChannels == 1 ? 0 : min(channel, source_format->nChannels - 1);
                sample = read_pcm_sample(source_frame + source_channel * source_sample_size, source_format->wBitsPerSample, source_valid_bits);
            }

            write_pcm_sample(destination_frame + channel * destination_sample_size, destination_format->wBitsPerSample, destination_valid_bits, sample);
        }

        ++destination_frames;
        offset += source_format->nSamplesPerSec;
    }

    stream->resample_offset = offset - (UINT64)source_frames * destination_format->nSamplesPerSec;
    return destination_frames;
}

static void reactos_capture_timer_loop(struct reactos_stream *stream)
{
    BYTE *read_buffer;
    UINT32 bytes, frames, output_capacity, source_frames, used;
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

        output_capacity = stream->capture_conversion ? (UINT32)(((UINT64)stream->device_period_frames * stream->format.Format.nSamplesPerSec + stream->device_format.Format.nSamplesPerSec - 1) / stream->device_format.Format.nSamplesPerSec + 1) : stream->device_period_frames;
        if (FAILED(ensure_stream_buffer(stream, output_capacity)) || (stream->capture_conversion && FAILED(ensure_device_buffer(stream, stream->device_period_frames))))
        {
            LeaveCriticalSection(&stream->lock);
            Sleep(10);
            continue;
        }

        read_buffer = stream->capture_conversion ? stream->device_buffer : stream->buffer;
        bytes = stream->device_period_frames * stream->device_frame_size;
        LeaveCriticalSection(&stream->lock);

        used = 0;
        if (!stream_io_wait(stream, TRUE, read_buffer, bytes, &used))
        {
            error = GetLastError();
            if (error == ERROR_OPERATION_ABORTED)
                break;

            WARN("ReadFile(WDMAUD) failed for capture stream, error %lu.\n", error);
            Sleep(10);
            continue;
        }

        source_frames = used / stream->device_frame_size;
        frames = stream->capture_conversion ? convert_capture_frames(stream, stream->device_buffer, source_frames, stream->buffer, stream->buffer_alloc_frames) : source_frames;
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

static void reactos_render_rt_timer_loop(struct reactos_stream *stream)
{
    HANDLE wait_handles[2];
    DWORD wait;

    wait_handles[0] = stream->stop_event;
    wait_handles[1] = stream->rt_event;
    while (!stream->closing)
    {
        wait = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0 + 1)
            break;

        EnterCriticalSection(&stream->lock);
        if (!stream->started)
        {
            LeaveCriticalSection(&stream->lock);
            break;
        }

        {
            UINT32 completed_frames =
                stream->rt_period_queued_frames[stream->rt_period_index];

            stream->position += stream->rt_period_frames;
            stream->position_qpc_100ns = query_performance_time_100ns();
            if (stream->padding > completed_frames)
                stream->padding -= completed_frames;
            else
                stream->padding = 0;

            if (!fill_wavert_period(stream, stream->rt_period_index))
            {
                stream->render_error = ERROR_NOT_ENOUGH_MEMORY;
                ZeroMemory(stream->rt_buffer +
                               (SIZE_T)stream->rt_period_index *
                                   stream->rt_period_frames *
                                   stream->device_frame_size,
                           (SIZE_T)stream->rt_period_frames *
                               stream->device_frame_size);
                MemoryBarrier();
            }

            stream->rt_period_index =
                (stream->rt_period_index + 1) %
                    REACTOS_RT_NOTIFICATION_COUNT;
        }
        LeaveCriticalSection(&stream->lock);

        if (stream->event)
            SetEvent(stream->event);
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

    if (stream->rt_enabled)
    {
        reactos_render_rt_timer_loop(stream);
        return 0;
    }

    reactos_render_timer_loop(stream);
    return 0;
}

static NTSTATUS reactos_is_format_supported(struct is_format_supported_params *params)
{
    HRESULT hr = validate_format(params->device, params->flow, params->fmt_in);

    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT && params->flow == eRender &&
        params->share == AUDCLNT_SHAREMODE_SHARED)
    {
        WAVEFORMATEXTENSIBLE mix;

        if (fill_mix_format(params->device, params->flow, &mix) &&
            shared_render_conversion_supported(params->fmt_in, &mix.Format))
        {
            hr = S_OK;
        }
    }

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

    EnterCriticalSection(&stream->lock);
    *params->padding = stream->padding;
    LeaveCriticalSection(&stream->lock);
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

    *params->freq = stream->format.Format.nSamplesPerSec;
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

    if (stream->rt_enabled)
    {
        EnterCriticalSection(&stream->lock);
        *params->pos = stream->position;
        if (params->qpctime)
        {
            *params->qpctime = stream->position_qpc_100ns ?
                stream->position_qpc_100ns :
                query_performance_time_100ns();
        }
        LeaveCriticalSection(&stream->lock);
    }
    else
    {
        *params->pos = query_stream_position(stream);
        if (params->qpctime)
            *params->qpctime = query_performance_time_100ns();
    }
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

static NTSTATUS reactos_set_volumes(struct set_volumes_params *params)
{
    struct reactos_stream *stream = stream_from_handle(params->stream);
    float master_volume;
    BOOL passthrough = TRUE;
    WORD channel;

    if (!stream)
        return STATUS_SUCCESS;

    master_volume = params->master_volume;
    if (!(master_volume >= 0.0f))
        master_volume = 0.0f;
    else if (master_volume > 1.0f)
        master_volume = 1.0f;

    EnterCriticalSection(&stream->lock);
    for (channel = 0; channel < stream->format.Format.nChannels; ++channel)
    {
        float stream_volume = params->volumes ? params->volumes[channel] : 1.0f;
        float session_volume = params->session_volumes ? params->session_volumes[channel] : 1.0f;
        float volume;

        if (!(stream_volume >= 0.0f))
            stream_volume = 0.0f;
        else if (stream_volume > 1.0f)
            stream_volume = 1.0f;
        if (!(session_volume >= 0.0f))
            session_volume = 0.0f;
        else if (session_volume > 1.0f)
            session_volume = 1.0f;

        volume = master_volume * stream_volume * session_volume;
        stream->channel_volumes[channel] = volume;
        if (volume != 1.0f)
            passthrough = FALSE;
    }
    stream->volume_passthrough = passthrough;
    LeaveCriticalSection(&stream->lock);
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
            return reactos_set_volumes(args);

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
