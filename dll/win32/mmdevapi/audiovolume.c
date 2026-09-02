/*
 * Copyright 2010 Maarten Lankhorst for CodeWeavers
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

#define COBJMACROS

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "wine/debug.h"

#include "ole2.h"
#include "mmdeviceapi.h"
#include "mmsystem.h"
#include "dsound.h"
#include "audioclient.h"
#include "endpointvolume.h"
#include "audiopolicy.h"
#include "spatialaudioclient.h"

#include "mmdevapi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(mmdevapi);

typedef struct AEVImpl {
    IAudioEndpointVolumeEx IAudioEndpointVolumeEx_iface;
    LONG ref;
#ifdef __REACTOS__
    struct reactos_endpoint_volume_state endpoint;
    CRITICAL_SECTION callback_lock;
    struct list callbacks;
#else
    float master_vol;
    BOOL mute;
#endif
} AEVImpl;

#ifdef __REACTOS__

#define ENDPOINT_VOLUME_MIN_DB (-96.0f)
#define ENDPOINT_VOLUME_MAX_DB 0.0f

struct endpoint_volume_callback
{
    IAudioEndpointVolumeCallback *callback;
    struct list entry;
};

static HRESULT endpoint_volume_get_levels(AEVImpl *This, float **levels)
{
    HRESULT hr;

    *levels = calloc(This->endpoint.channel_count, sizeof(**levels));
    if (!*levels)
        return E_OUTOFMEMORY;

    hr = reactos_endpoint_volume_get(&This->endpoint, *levels,
                                     This->endpoint.channel_count);
    if (FAILED(hr))
    {
        free(*levels);
        *levels = NULL;
    }
    return hr;
}

static float endpoint_volume_master(const float *levels, UINT count)
{
    float master = 0.0f;
    UINT i;

    for (i = 0; i < count; ++i)
        master = max(master, levels[i]);
    return master;
}

static float endpoint_volume_db_from_scalar(float level)
{
    return ENDPOINT_VOLUME_MIN_DB +
           level * (ENDPOINT_VOLUME_MAX_DB - ENDPOINT_VOLUME_MIN_DB);
}

static float endpoint_volume_scalar_from_db(float level)
{
    return (level - ENDPOINT_VOLUME_MIN_DB) /
           (ENDPOINT_VOLUME_MAX_DB - ENDPOINT_VOLUME_MIN_DB);
}

static void endpoint_volume_notify(AEVImpl *This, const GUID *ctx)
{
    struct endpoint_volume_callback *entry;
    IAudioEndpointVolumeCallback **callbacks = NULL;
    AUDIO_VOLUME_NOTIFICATION_DATA *notification = NULL;
    float *levels = NULL;
    BOOL mute = FALSE;
    unsigned int callback_count, i = 0;
    SIZE_T notification_size;

    if (FAILED(endpoint_volume_get_levels(This, &levels)))
        return;
    if (This->endpoint.mute_supported)
        reactos_endpoint_mute_get(&This->endpoint, &mute);

    notification_size = offsetof(AUDIO_VOLUME_NOTIFICATION_DATA,
                                 afChannelVolumes) +
                        This->endpoint.channel_count * sizeof(*levels);
    notification = malloc(notification_size);
    if (!notification)
        goto done;

    if (ctx)
        notification->guidEventContext = *ctx;
    else
        ZeroMemory(&notification->guidEventContext,
                   sizeof(notification->guidEventContext));
    notification->bMuted = mute;
    notification->fMasterVolume =
        endpoint_volume_master(levels, This->endpoint.channel_count);
    notification->nChannels = This->endpoint.channel_count;
    memcpy(notification->afChannelVolumes, levels,
           This->endpoint.channel_count * sizeof(*levels));

    EnterCriticalSection(&This->callback_lock);
    callback_count = list_count(&This->callbacks);
    if (callback_count)
    {
        callbacks = malloc(callback_count * sizeof(*callbacks));
        if (callbacks)
        {
            LIST_FOR_EACH_ENTRY(entry, &This->callbacks,
                                struct endpoint_volume_callback, entry)
            {
                callbacks[i] = entry->callback;
                IAudioEndpointVolumeCallback_AddRef(callbacks[i]);
                ++i;
            }
        }
    }
    LeaveCriticalSection(&This->callback_lock);

    while (i)
    {
        --i;
        IAudioEndpointVolumeCallback_OnNotify(callbacks[i], notification);
        IAudioEndpointVolumeCallback_Release(callbacks[i]);
    }

done:
    free(callbacks);
    free(notification);
    free(levels);
}

static HRESULT endpoint_volume_set_master(AEVImpl *This, float level,
                                          const GUID *ctx)
{
    float *levels;
    HRESULT hr;
    UINT i;

    if (level < 0.0f || level > 1.0f)
        return E_INVALIDARG;

    levels = malloc(This->endpoint.channel_count * sizeof(*levels));
    if (!levels)
        return E_OUTOFMEMORY;

    for (i = 0; i < This->endpoint.channel_count; ++i)
        levels[i] = level;
    hr = reactos_endpoint_volume_set(&This->endpoint, levels,
                                     This->endpoint.channel_count);
    free(levels);
    if (SUCCEEDED(hr))
        endpoint_volume_notify(This, ctx);
    return hr;
}

#endif

static inline AEVImpl *impl_from_IAudioEndpointVolumeEx(IAudioEndpointVolumeEx *iface)
{
    return CONTAINING_RECORD(iface, AEVImpl, IAudioEndpointVolumeEx_iface);
}

static void AudioEndpointVolume_Destroy(AEVImpl *This)
{
#ifdef __REACTOS__
    struct endpoint_volume_callback *entry, *next;

    LIST_FOR_EACH_ENTRY_SAFE(entry, next, &This->callbacks,
                             struct endpoint_volume_callback, entry)
    {
        list_remove(&entry->entry);
        IAudioEndpointVolumeCallback_Release(entry->callback);
        free(entry);
    }
    DeleteCriticalSection(&This->callback_lock);
#endif
    free(This);
}

static HRESULT WINAPI AEV_QueryInterface(IAudioEndpointVolumeEx *iface, REFIID riid, void **ppv)
{
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    TRACE("(%p)->(%s,%p)\n", This, debugstr_guid(riid), ppv);
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IAudioEndpointVolume) ||
        IsEqualIID(riid, &IID_IAudioEndpointVolumeEx)) {
        *ppv = &This->IAudioEndpointVolumeEx_iface;
    }
    else
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown *)*ppv);
    return S_OK;
}

static ULONG WINAPI AEV_AddRef(IAudioEndpointVolumeEx *iface)
{
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) new ref %lu\n", This, ref);
    return ref;
}

static ULONG WINAPI AEV_Release(IAudioEndpointVolumeEx *iface)
{
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) new ref %lu\n", This, ref);
    if (!ref)
        AudioEndpointVolume_Destroy(This);
    return ref;
}

static HRESULT WINAPI AEV_RegisterControlChangeNotify(IAudioEndpointVolumeEx *iface, IAudioEndpointVolumeCallback *notify)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    struct endpoint_volume_callback *entry;
#endif

    TRACE("(%p)->(%p)\n", iface, notify);
    if (!notify)
        return E_POINTER;
#ifdef __REACTOS__
    EnterCriticalSection(&This->callback_lock);
    LIST_FOR_EACH_ENTRY(entry, &This->callbacks,
                        struct endpoint_volume_callback, entry)
    {
        if (entry->callback == notify)
        {
            LeaveCriticalSection(&This->callback_lock);
            return S_OK;
        }
    }

    entry = malloc(sizeof(*entry));
    if (!entry)
    {
        LeaveCriticalSection(&This->callback_lock);
        return E_OUTOFMEMORY;
    }

    entry->callback = notify;
    IAudioEndpointVolumeCallback_AddRef(notify);
    list_add_tail(&This->callbacks, &entry->entry);
    LeaveCriticalSection(&This->callback_lock);
#else
    FIXME("stub\n");
#endif
    return S_OK;
}

static HRESULT WINAPI AEV_UnregisterControlChangeNotify(IAudioEndpointVolumeEx *iface, IAudioEndpointVolumeCallback *notify)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    struct endpoint_volume_callback *entry;
#endif

    TRACE("(%p)->(%p)\n", iface, notify);
    if (!notify)
        return E_POINTER;
#ifdef __REACTOS__
    EnterCriticalSection(&This->callback_lock);
    LIST_FOR_EACH_ENTRY(entry, &This->callbacks,
                        struct endpoint_volume_callback, entry)
    {
        if (entry->callback == notify)
        {
            list_remove(&entry->entry);
            LeaveCriticalSection(&This->callback_lock);
            IAudioEndpointVolumeCallback_Release(entry->callback);
            free(entry);
            return S_OK;
        }
    }
    LeaveCriticalSection(&This->callback_lock);
    return E_INVALIDARG;
#else
    FIXME("stub\n");
    return S_OK;
#endif
}

static HRESULT WINAPI AEV_GetChannelCount(IAudioEndpointVolumeEx *iface, UINT *count)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
#endif

    TRACE("(%p)->(%p)\n", iface, count);
    if (!count)
        return E_POINTER;
#ifdef __REACTOS__
    *count = This->endpoint.channel_count;
    return S_OK;
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI AEV_SetMasterVolumeLevel(IAudioEndpointVolumeEx *iface, float leveldb, const GUID *ctx)
{
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);

    TRACE("(%p)->(%f,%s)\n", iface, leveldb, debugstr_guid(ctx));

#ifdef __REACTOS__
    if (leveldb < ENDPOINT_VOLUME_MIN_DB || leveldb > ENDPOINT_VOLUME_MAX_DB)
        return E_INVALIDARG;

    return endpoint_volume_set_master(This,
                                      endpoint_volume_scalar_from_db(leveldb),
                                      ctx);
#else
    if(leveldb < -100.f || leveldb > 0.f)
        return E_INVALIDARG;

    This->master_vol = leveldb;

    return S_OK;
#endif
}

static HRESULT WINAPI AEV_SetMasterVolumeLevelScalar(IAudioEndpointVolumeEx *iface, float level, const GUID *ctx)
{
    TRACE("(%p)->(%f,%s)\n", iface, level, debugstr_guid(ctx));
#ifdef __REACTOS__
    return endpoint_volume_set_master(impl_from_IAudioEndpointVolumeEx(iface),
                                      level, ctx);
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI AEV_GetMasterVolumeLevel(IAudioEndpointVolumeEx *iface, float *leveldb)
{
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);

    TRACE("(%p)->(%p)\n", iface, leveldb);

    if (!leveldb)
        return E_POINTER;

#ifdef __REACTOS__
    {
        float *levels;
        HRESULT hr;

        if (FAILED(hr = endpoint_volume_get_levels(This, &levels)))
            return hr;
        *leveldb = endpoint_volume_db_from_scalar(
            endpoint_volume_master(levels, This->endpoint.channel_count));
        free(levels);
    }
#else
    *leveldb = This->master_vol;
#endif

    return S_OK;
}

static HRESULT WINAPI AEV_GetMasterVolumeLevelScalar(IAudioEndpointVolumeEx *iface, float *level)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    float *levels;
    HRESULT hr;
#endif

    TRACE("(%p)->(%p)\n", iface, level);
    if (!level)
        return E_POINTER;
#ifdef __REACTOS__
    if (FAILED(hr = endpoint_volume_get_levels(This, &levels)))
        return hr;
    *level = endpoint_volume_master(levels, This->endpoint.channel_count);
    free(levels);
#else
    FIXME("stub\n");
    *level = 1.0;
#endif
    return S_OK;
}

static HRESULT WINAPI AEV_SetChannelVolumeLevelScalar(IAudioEndpointVolumeEx *iface,
                                                       UINT chan, float level,
                                                       const GUID *ctx);
static HRESULT WINAPI AEV_GetChannelVolumeLevelScalar(IAudioEndpointVolumeEx *iface,
                                                       UINT chan, float *level);

static HRESULT WINAPI AEV_SetChannelVolumeLevel(IAudioEndpointVolumeEx *iface, UINT chan, float leveldb, const GUID *ctx)
{
    TRACE("(%p)->(%u,%f,%s)\n", iface, chan, leveldb, debugstr_guid(ctx));
#ifdef __REACTOS__
    if (leveldb < ENDPOINT_VOLUME_MIN_DB || leveldb > ENDPOINT_VOLUME_MAX_DB)
        return E_INVALIDARG;
    return AEV_SetChannelVolumeLevelScalar(iface, chan,
                                           endpoint_volume_scalar_from_db(leveldb),
                                           ctx);
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI AEV_SetChannelVolumeLevelScalar(IAudioEndpointVolumeEx *iface, UINT chan, float level, const GUID *ctx)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    float *levels;
    HRESULT hr;
#endif

    TRACE("(%p)->(%u,%f,%s)\n", iface, chan, level, debugstr_guid(ctx));
#ifdef __REACTOS__
    if (chan >= This->endpoint.channel_count || level < 0.0f || level > 1.0f)
        return E_INVALIDARG;
    if (FAILED(hr = endpoint_volume_get_levels(This, &levels)))
        return hr;
    if (This->endpoint.volume_control_channels == 1)
    {
        UINT i;

        for (i = 0; i < This->endpoint.channel_count; ++i)
            levels[i] = level;
    }
    else
    {
        levels[chan] = level;
    }
    hr = reactos_endpoint_volume_set(&This->endpoint, levels,
                                     This->endpoint.channel_count);
    free(levels);
    if (SUCCEEDED(hr))
        endpoint_volume_notify(This, ctx);
    return hr;
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI AEV_GetChannelVolumeLevel(IAudioEndpointVolumeEx *iface, UINT chan, float *leveldb)
{
    TRACE("(%p)->(%u,%p)\n", iface, chan, leveldb);
    if (!leveldb)
        return E_POINTER;
#ifdef __REACTOS__
    {
        float level;
        HRESULT hr = AEV_GetChannelVolumeLevelScalar(iface, chan, &level);

        if (FAILED(hr))
            return hr;
        *leveldb = endpoint_volume_db_from_scalar(level);
        return S_OK;
    }
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI AEV_GetChannelVolumeLevelScalar(IAudioEndpointVolumeEx *iface, UINT chan, float *level)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    float *levels;
    HRESULT hr;
#endif

    TRACE("(%p)->(%u,%p)\n", iface, chan, level);
    if (!level)
        return E_POINTER;
#ifdef __REACTOS__
    if (chan >= This->endpoint.channel_count)
        return E_INVALIDARG;
    if (FAILED(hr = endpoint_volume_get_levels(This, &levels)))
        return hr;
    *level = levels[chan];
    free(levels);
    return S_OK;
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI AEV_SetMute(IAudioEndpointVolumeEx *iface, BOOL mute, const GUID *ctx)
{
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    HRESULT ret;

    TRACE("(%p)->(%u,%s)\n", iface, mute, debugstr_guid(ctx));

#ifdef __REACTOS__
    {
        BOOL current;

        if (!This->endpoint.mute_supported)
            return E_NOTIMPL;
        if (FAILED(ret = reactos_endpoint_mute_get(&This->endpoint, &current)))
            return ret;
        if (current == mute)
            return S_FALSE;
        ret = reactos_endpoint_mute_set(&This->endpoint, mute);
        if (SUCCEEDED(ret))
            endpoint_volume_notify(This, ctx);
        return ret;
    }
#else
    ret = This->mute == mute ? S_FALSE : S_OK;
    This->mute = mute;
    return ret;
#endif
}

static HRESULT WINAPI AEV_GetMute(IAudioEndpointVolumeEx *iface, BOOL *mute)
{
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);

    TRACE("(%p)->(%p)\n", iface, mute);

    if (!mute)
        return E_POINTER;

#ifdef __REACTOS__
    return reactos_endpoint_mute_get(&This->endpoint, mute);
#else
    *mute = This->mute;

    return S_OK;
#endif
}

static HRESULT WINAPI AEV_GetVolumeStepInfo(IAudioEndpointVolumeEx *iface, UINT *stepsize, UINT *stepcount)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    float level;
    HRESULT hr;
#endif

    TRACE("(%p)->(%p,%p)\n", iface, stepsize, stepcount);
    if (!stepsize || !stepcount)
        return E_POINTER;
#ifdef __REACTOS__
    if (FAILED(hr = AEV_GetMasterVolumeLevelScalar(iface, &level)))
        return hr;
    *stepcount = This->endpoint.step_count;
    *stepsize = (UINT)(level * (*stepcount - 1) + 0.5f);
    return S_OK;
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI AEV_VolumeStepUp(IAudioEndpointVolumeEx *iface, const GUID *ctx)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    UINT step, count;
    HRESULT hr;
#endif

    TRACE("(%p)->(%s)\n", iface, debugstr_guid(ctx));
#ifdef __REACTOS__
    if (FAILED(hr = AEV_GetVolumeStepInfo(iface, &step, &count)))
        return hr;
    if (step + 1 < count)
        ++step;
    return endpoint_volume_set_master(This, (float)step / (count - 1), ctx);
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI AEV_VolumeStepDown(IAudioEndpointVolumeEx *iface, const GUID *ctx)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
    UINT step, count;
    HRESULT hr;
#endif

    TRACE("(%p)->(%s)\n", iface, debugstr_guid(ctx));
#ifdef __REACTOS__
    if (FAILED(hr = AEV_GetVolumeStepInfo(iface, &step, &count)))
        return hr;
    if (step)
        --step;
    return endpoint_volume_set_master(This, (float)step / (count - 1), ctx);
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI AEV_QueryHardwareSupport(IAudioEndpointVolumeEx *iface, DWORD *mask)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
#endif

    TRACE("(%p)->(%p)\n", iface, mask);
    if (!mask)
        return E_POINTER;
#ifdef __REACTOS__
    *mask = ENDPOINT_HARDWARE_SUPPORT_VOLUME;
    if (This->endpoint.mute_supported)
        *mask |= ENDPOINT_HARDWARE_SUPPORT_MUTE;
    return S_OK;
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI AEV_GetVolumeRange(IAudioEndpointVolumeEx *iface, float *mindb, float *maxdb, float *inc)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
#endif

    TRACE("(%p)->(%p,%p,%p)\n", iface, mindb, maxdb, inc);

    if (!mindb || !maxdb || !inc)
        return E_POINTER;

#ifdef __REACTOS__
    *mindb = ENDPOINT_VOLUME_MIN_DB;
    *maxdb = ENDPOINT_VOLUME_MAX_DB;
    *inc = (ENDPOINT_VOLUME_MAX_DB - ENDPOINT_VOLUME_MIN_DB) /
           (This->endpoint.step_count - 1);
#else
    *mindb = -100.f;
    *maxdb = 0.f;
    *inc = 1.f;
#endif

    return S_OK;
}

static HRESULT WINAPI AEV_GetVolumeRangeChannel(IAudioEndpointVolumeEx *iface, UINT chan, float *mindb, float *maxdb, float *inc)
{
#ifdef __REACTOS__
    AEVImpl *This = impl_from_IAudioEndpointVolumeEx(iface);
#endif

    TRACE("(%p)->(%p,%p,%p)\n", iface, mindb, maxdb, inc);
    if (!mindb || !maxdb || !inc)
        return E_POINTER;
#ifdef __REACTOS__
    if (chan >= This->endpoint.channel_count)
        return E_INVALIDARG;
    return AEV_GetVolumeRange(iface, mindb, maxdb, inc);
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

static const IAudioEndpointVolumeExVtbl AEVImpl_Vtbl = {
    AEV_QueryInterface,
    AEV_AddRef,
    AEV_Release,
    AEV_RegisterControlChangeNotify,
    AEV_UnregisterControlChangeNotify,
    AEV_GetChannelCount,
    AEV_SetMasterVolumeLevel,
    AEV_SetMasterVolumeLevelScalar,
    AEV_GetMasterVolumeLevel,
    AEV_GetMasterVolumeLevelScalar,
    AEV_SetChannelVolumeLevel,
    AEV_SetChannelVolumeLevelScalar,
    AEV_GetChannelVolumeLevel,
    AEV_GetChannelVolumeLevelScalar,
    AEV_SetMute,
    AEV_GetMute,
    AEV_GetVolumeStepInfo,
    AEV_VolumeStepUp,
    AEV_VolumeStepDown,
    AEV_QueryHardwareSupport,
    AEV_GetVolumeRange,
    AEV_GetVolumeRangeChannel
};

HRESULT AudioEndpointVolume_Create(MMDevice *parent, IAudioEndpointVolumeEx **ppv)
{
    AEVImpl *This;
#ifdef __REACTOS__
    HRESULT hr;
#endif

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    This = calloc(1, sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;
    This->IAudioEndpointVolumeEx_iface.lpVtbl = &AEVImpl_Vtbl;
    This->ref = 1;
#ifdef __REACTOS__
    if (FAILED(hr = reactos_endpoint_volume_initialize(&parent->devguid,
                                                        &This->endpoint)))
    {
        free(This);
        return hr;
    }
    InitializeCriticalSection(&This->callback_lock);
    list_init(&This->callbacks);
#endif

    *ppv = &This->IAudioEndpointVolumeEx_iface;
    return S_OK;
}

struct endpoint_meter
{
    IAudioMeterInformation IAudioMeterInformation_iface;
    LONG ref;
    IMMDevice *device;
};

static inline struct endpoint_meter *impl_from_meter(IAudioMeterInformation *iface)
{
    return CONTAINING_RECORD(iface, struct endpoint_meter, IAudioMeterInformation_iface);
}

static HRESULT WINAPI endpointmeter_QueryInterface(IAudioMeterInformation *iface, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAudioMeterInformation))
        *ppv = iface;
    else
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown *)*ppv);
    return S_OK;
}

static ULONG WINAPI endpointmeter_AddRef(IAudioMeterInformation *iface)
{
    struct endpoint_meter *This = impl_from_meter(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI endpointmeter_Release(IAudioMeterInformation *iface)
{
    struct endpoint_meter *This = impl_from_meter(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (!ref)
    {
        IMMDevice_Release(This->device);
        free(This);
    }
    return ref;
}

static HRESULT WINAPI endpointmeter_GetPeakValue(IAudioMeterInformation *iface, float *peak)
{
    struct endpoint_meter *This = impl_from_meter(iface);
    if (!peak)
        return E_POINTER;
    return reactos_audio_session_max_peak(This->device, peak);
}

static HRESULT WINAPI endpointmeter_GetMeteringChannelCount(IAudioMeterInformation *iface, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI endpointmeter_GetChannelsPeakValues(IAudioMeterInformation *iface, UINT32 count, float *peaks)
{
    struct endpoint_meter *This = impl_from_meter(iface);
    float peak = 0.0f;
    UINT32 i;
    if (!peaks)
        return E_POINTER;
    reactos_audio_session_max_peak(This->device, &peak);
    for (i = 0; i < count; i++)
        peaks[i] = peak;
    return S_OK;
}

static HRESULT WINAPI endpointmeter_QueryHardwareSupport(IAudioMeterInformation *iface, DWORD *mask)
{
    if (!mask)
        return E_POINTER;
    *mask = 0;
    return S_OK;
}

static const IAudioMeterInformationVtbl EndpointMeterInformation_Vtbl =
{
    endpointmeter_QueryInterface,
    endpointmeter_AddRef,
    endpointmeter_Release,
    endpointmeter_GetPeakValue,
    endpointmeter_GetMeteringChannelCount,
    endpointmeter_GetChannelsPeakValues,
    endpointmeter_QueryHardwareSupport
};

HRESULT AudioEndpointMeter_Create(IMMDevice *parent, void **ppv)
{
    struct endpoint_meter *This;
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    This = calloc(1, sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;
    This->IAudioMeterInformation_iface.lpVtbl = &EndpointMeterInformation_Vtbl;
    This->ref = 1;
    This->device = parent;
    IMMDevice_AddRef(parent);
    *ppv = &This->IAudioMeterInformation_iface;
    return S_OK;
}
