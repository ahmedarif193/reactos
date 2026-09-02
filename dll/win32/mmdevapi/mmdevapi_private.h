/*
 * Copyright 2009 Maarten Lankhorst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <assert.h>

#ifdef __REACTOS__
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif
#endif

#include <endpointvolume.h>
#include <spatialaudioclient.h>
#include <winternl.h>

#include <wine/list.h>
#ifndef __REACTOS__
#include <wine/unixlib.h>
#else
typedef HMODULE unixlib_module_t;
typedef ULONG_PTR unixlib_handle_t;
#endif

#include "unixlib.h"

#ifdef __REACTOS__
#define REACTOS_AUDIO_SESSION_MAX_CHANNELS 32

struct reactos_audio_session_id
{
    UINT32 slot;
    GUID instance_guid;
};

struct reactos_audio_session_snapshot
{
    GUID session_guid;
    GUID grouping_param;
    DWORD process_id;
    AudioSessionState state;
    UINT32 channel_count;
    LONG generation;
    float master_volume;
    BOOL mute;
    float channel_volumes[REACTOS_AUDIO_SESSION_MAX_CHANNELS];
    WCHAR process_path[MAX_PATH];
    WCHAR display_name[128];
    WCHAR icon_path[MAX_PATH];
};
#endif

struct audio_session {
    GUID guid;
    struct list clients;

    IMMDevice *device;

    float master_vol;
    UINT32 channel_count;
    float *channel_vols;
    BOOL mute;

    WCHAR *display_name;
    WCHAR *icon_path;
    GUID grouping_param;

#ifdef __REACTOS__
    struct reactos_audio_session_id shared_id;
    LONG shared_generation;
    DWORD process_id;
    AudioSessionState shared_state;
    WCHAR *process_path;
    LONG proxy_wrapper_count;
    BOOL shared_valid;
    BOOL shared_proxy;
#endif

    struct list entry;
};

typedef struct audio_session_wrapper {
    IAudioSessionControl2 IAudioSessionControl2_iface;
    IChannelAudioVolume IChannelAudioVolume_iface;
    ISimpleAudioVolume ISimpleAudioVolume_iface;

    LONG ref;

    struct audio_client *client;
    struct audio_session *session;
} AudioSessionWrapper;

struct audio_client {
    IAudioClient3 IAudioClient3_iface;
    IAudioRenderClient IAudioRenderClient_iface;
    IAudioCaptureClient IAudioCaptureClient_iface;
    IAudioClock IAudioClock_iface;
    IAudioClock2 IAudioClock2_iface;
    IAudioClockAdjustment IAudioClockAdjustment_iface;
    IAudioStreamVolume IAudioStreamVolume_iface;

    LONG ref;

    IMMDevice *parent;
    IUnknown *marshal;

    EDataFlow dataflow;
    float *vols;
    UINT32 channel_count;
    stream_handle stream;

    struct audio_session *session;
    struct audio_session_wrapper *session_wrapper;

    struct list entry;
    char *device_name;
};

extern HRESULT MMDevEnum_Create(REFIID riid, void **ppv);
extern void MMDevEnum_Free(void);

typedef struct _DriverFuncs {
    unixlib_module_t module;
    unixlib_handle_t module_unixlib;
    WCHAR module_name[64];

    /* Highest priority wins.
     * If multiple drivers think they are valid, they will return a
     * priority value reflecting the likelihood that they are actually
     * valid. See enum _DriverPriority. */
    int priority;
} DriverFuncs;

extern DriverFuncs drvs;

extern BOOL reactos_ensure_driver(void);

typedef struct MMDevice {
    IMMDevice IMMDevice_iface;
    IMMEndpoint IMMEndpoint_iface;
    LONG ref;

    EDataFlow flow;
    DWORD state;
    GUID devguid;
    WCHAR *drv_id;

    struct list entry;
} MMDevice;

#ifdef __REACTOS__
struct reactos_endpoint_volume_state
{
    DWORD mixer_index;
    DWORD volume_control_id;
    DWORD mute_control_id;
    UINT channel_count;
    UINT volume_control_channels;
    UINT mute_control_channels;
    UINT step_count;
    BOOL mute_supported;
};

extern BOOL reactos_audio_driver_init(DriverFuncs *driver);
extern void reactos_audio_driver_deinit(void);
extern NTSTATUS reactos_mmdevapi_call(unsigned int code, void *args);
extern HRESULT reactos_endpoint_volume_initialize(const GUID *guid,
                                                  struct reactos_endpoint_volume_state *state);
extern HRESULT reactos_endpoint_volume_get(const struct reactos_endpoint_volume_state *state,
                                           float *levels, UINT count);
extern HRESULT reactos_endpoint_volume_set(const struct reactos_endpoint_volume_state *state,
                                           const float *levels, UINT count);
extern HRESULT reactos_endpoint_mute_get(const struct reactos_endpoint_volume_state *state,
                                         BOOL *mute);
extern HRESULT reactos_endpoint_mute_set(const struct reactos_endpoint_volume_state *state,
                                         BOOL mute);
extern HRESULT reactos_audio_session_register(IMMDevice *device, const GUID *session_guid,
                                              UINT32 channels,
                                              struct reactos_audio_session_id *id,
                                              struct reactos_audio_session_snapshot *snapshot);
extern HRESULT reactos_audio_session_enumerate(IMMDevice *device,
                                               struct reactos_audio_session_id **ids,
                                               int *count);
extern HRESULT reactos_audio_session_read(const struct reactos_audio_session_id *id,
                                          LONG known_generation,
                                          struct reactos_audio_session_snapshot *snapshot);
extern HRESULT reactos_audio_session_set_strings(const struct reactos_audio_session_id *id,
                                                 const WCHAR *display_name,
                                                 const WCHAR *icon_path);
extern HRESULT reactos_audio_session_set_master(const struct reactos_audio_session_id *id,
                                                float level);
extern HRESULT reactos_audio_session_set_mute(const struct reactos_audio_session_id *id,
                                              BOOL mute);
extern HRESULT reactos_audio_session_set_channel(const struct reactos_audio_session_id *id,
                                                 UINT32 channel, float level);
extern HRESULT reactos_audio_session_set_channels(const struct reactos_audio_session_id *id,
                                                  UINT32 count, const float *levels);
extern HRESULT reactos_audio_session_set_state(const struct reactos_audio_session_id *id,
                                               AudioSessionState state);
extern void reactos_audio_sessions_shutdown(void);
#endif

static inline void wine_unix_call(const unsigned int code, void *args)
{
#ifdef __REACTOS__
    const NTSTATUS status = reactos_mmdevapi_call(code, args);
#else
    const NTSTATUS status = __wine_unix_call(drvs.module_unixlib, code, args);
#endif
    assert(!status);
}

extern HRESULT AudioClient_Create(GUID *guid, IMMDevice *device, IAudioClient **out);
extern HRESULT AudioEndpointVolume_Create(MMDevice *parent, IAudioEndpointVolumeEx **ppv);
extern HRESULT AudioSessionManager_Create(IMMDevice *device, IAudioSessionManager2 **ppv);
extern HRESULT SpatialAudioClient_Create(IMMDevice *device, ISpatialAudioClient **out);

extern BOOL get_device_name_from_guid( const GUID *guid, char **name, EDataFlow *flow );
extern HRESULT load_devices_from_reg(void);
extern HRESULT load_driver_devices(EDataFlow flow);

extern const WCHAR drv_keyW[];

extern HRESULT get_audio_session(const GUID *sessionguid, IMMDevice *device, UINT channels,
                                 struct audio_session **out);
extern HRESULT get_audio_session_wrapper(const GUID *guid, IMMDevice *device,
                                         struct audio_session_wrapper **out);
extern HRESULT get_audio_sessions(IMMDevice *device, GUID **ret, int *ret_count);
#ifdef __REACTOS__
extern HRESULT get_audio_session_wrapper_by_id(const struct reactos_audio_session_id *id,
                                               IMMDevice *device,
                                               struct audio_session_wrapper **out);
extern BOOL sync_audio_session(struct audio_session *session);
extern void publish_audio_session_state(struct audio_session *session);
#endif

extern struct audio_session_wrapper *session_wrapper_create(struct audio_client *client);

extern void sessions_lock(void);
extern void sessions_unlock(void);
