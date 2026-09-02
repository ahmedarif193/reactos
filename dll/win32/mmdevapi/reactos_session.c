/*
 * PROJECT:     ReactOS Core Audio
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Cross-process Core Audio session registry
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "audioclient.h"
#include "audiopolicy.h"
#include "mmdeviceapi.h"

#include "wine/debug.h"

#include "mmdevapi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(mmdevapi);

#define SESSION_REGISTRY_MAGIC 0x53534152 /* RASS */
#define SESSION_REGISTRY_VERSION 2
#define SESSION_REGISTRY_CAPACITY 128

static const WCHAR session_registry_name[] =
    L"Local\\ReactOS.CoreAudio.SessionRegistry.v2";
static const WCHAR session_registry_mutex_name[] =
    L"Local\\ReactOS.CoreAudio.SessionRegistry.v2.Lock";

struct shared_audio_session
{
    volatile LONG occupied;
    volatile LONG generation;
    GUID instance_guid;
    GUID session_guid;
    GUID grouping_param;
    DWORD process_id;
    FILETIME process_start_time;
    DWORD state;
    UINT32 channel_count;
    float master_volume;
    BOOL mute;
    float channel_volumes[REACTOS_AUDIO_SESSION_MAX_CHANNELS];
    WCHAR endpoint_id[MAX_PATH];
    WCHAR process_path[MAX_PATH];
    WCHAR display_name[128];
    WCHAR icon_path[MAX_PATH];
    volatile LONG peak_bits;
    volatile LONG peak_tick;
};

struct shared_audio_session_registry
{
    DWORD magic;
    DWORD version;
    DWORD size;
    DWORD capacity;
    struct shared_audio_session sessions[SESSION_REGISTRY_CAPACITY];
};

static INIT_ONCE registry_init_once = INIT_ONCE_STATIC_INIT;
static HANDLE registry_mapping;
static HANDLE registry_mutex;
static struct shared_audio_session_registry *registry;
static FILETIME current_process_start_time;

static BOOL file_times_equal(const FILETIME *left, const FILETIME *right)
{
    return left->dwLowDateTime == right->dwLowDateTime &&
           left->dwHighDateTime == right->dwHighDateTime;
}

static BOOL get_current_process_start_time(FILETIME *creation_time)
{
    FILETIME exit_time, kernel_time, user_time;

    return GetProcessTimes(GetCurrentProcess(), creation_time, &exit_time,
                           &kernel_time, &user_time);
}

static BOOL lock_registry(void)
{
    DWORD wait;

    if (!registry_mutex)
        return FALSE;

    wait = WaitForSingleObject(registry_mutex, INFINITE);
    return wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
}

static BOOL try_lock_registry(void)
{
    DWORD wait;

    if (!registry_mutex)
        return FALSE;

    wait = WaitForSingleObject(registry_mutex, 0);
    return wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
}

static void unlock_registry(void)
{
    ReleaseMutex(registry_mutex);
}

static BOOL WINAPI initialize_registry(INIT_ONCE *once, void *parameter,
                                       void **context)
{
    BOOL initialized = FALSE;

    UNREFERENCED_PARAMETER(once);
    UNREFERENCED_PARAMETER(parameter);
    UNREFERENCED_PARAMETER(context);

    registry_mutex = CreateMutexW(NULL, FALSE, session_registry_mutex_name);
    if (!registry_mutex)
        goto failed;

    registry_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
                                          PAGE_READWRITE, 0,
                                          sizeof(*registry),
                                          session_registry_name);
    if (!registry_mapping)
        goto failed;

    registry = MapViewOfFile(registry_mapping, FILE_MAP_READ | FILE_MAP_WRITE,
                             0, 0, sizeof(*registry));
    if (!registry)
        goto failed;

    if (!get_current_process_start_time(&current_process_start_time))
        ZeroMemory(&current_process_start_time, sizeof(current_process_start_time));

    if (!lock_registry())
        goto failed;

    if (registry->magic != SESSION_REGISTRY_MAGIC ||
        registry->version != SESSION_REGISTRY_VERSION ||
        registry->size != sizeof(*registry) ||
        registry->capacity != SESSION_REGISTRY_CAPACITY)
    {
        ZeroMemory(registry, sizeof(*registry));
        registry->version = SESSION_REGISTRY_VERSION;
        registry->size = sizeof(*registry);
        registry->capacity = SESSION_REGISTRY_CAPACITY;
        MemoryBarrier();
        registry->magic = SESSION_REGISTRY_MAGIC;
    }

    unlock_registry();
    initialized = TRUE;

failed:
    if (!initialized)
    {
        if (registry)
            UnmapViewOfFile(registry);
        if (registry_mapping)
            CloseHandle(registry_mapping);
        if (registry_mutex)
            CloseHandle(registry_mutex);
        registry = NULL;
        registry_mapping = NULL;
        registry_mutex = NULL;
    }

    return initialized;
}

static HRESULT ensure_registry(void)
{
    DWORD error;

    if (InitOnceExecuteOnce(&registry_init_once, initialize_registry, NULL, NULL))
        return S_OK;

    error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

static BOOL query_process_start_time(DWORD process_id, FILETIME *creation_time,
                                     BOOL *terminated)
{
    FILETIME exit_time, kernel_time, user_time;
    HANDLE process;
    DWORD wait;

    *terminated = FALSE;
    process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                          FALSE, process_id);
    if (!process)
        process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION,
                              FALSE, process_id);
    if (!process)
    {
        process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
        if (!process)
        {
            if (GetLastError() == ERROR_INVALID_PARAMETER)
                *terminated = TRUE;
            return FALSE;
        }
    }

    wait = WaitForSingleObject(process, 0);
    if (wait == WAIT_OBJECT_0)
    {
        *terminated = TRUE;
        CloseHandle(process);
        return FALSE;
    }

    if (!GetProcessTimes(process, creation_time, &exit_time,
                         &kernel_time, &user_time))
    {
        CloseHandle(process);
        return FALSE;
    }

    CloseHandle(process);
    return TRUE;
}

static BOOL record_process_is_alive(const struct shared_audio_session *session)
{
    FILETIME creation_time;
    BOOL terminated;

    if (!session->process_id)
        return FALSE;

    if (session->process_id == GetCurrentProcessId())
        return file_times_equal(&session->process_start_time,
                                &current_process_start_time);

    if (query_process_start_time(session->process_id, &creation_time,
                                 &terminated))
        return file_times_equal(&session->process_start_time, &creation_time);

    return !terminated;
}

static void clear_record(struct shared_audio_session *session)
{
    InterlockedExchange(&session->occupied, FALSE);
    MemoryBarrier();
    ZeroMemory(session, sizeof(*session));
}

static void remove_stale_records(void)
{
    UINT32 i;

    for (i = 0; i < SESSION_REGISTRY_CAPACITY; ++i)
    {
        struct shared_audio_session *session = &registry->sessions[i];

        if (session->occupied && !record_process_is_alive(session))
            clear_record(session);
    }
}

static HRESULT get_endpoint_id(IMMDevice *device, WCHAR endpoint_id[MAX_PATH])
{
    WCHAR *allocated_id;
    HRESULT hr;

    hr = IMMDevice_GetId(device, &allocated_id);
    if (FAILED(hr))
        return hr;

    lstrcpynW(endpoint_id, allocated_id, MAX_PATH);
    CoTaskMemFree(allocated_id);
    return S_OK;
}

static struct shared_audio_session *find_record_locked(
    const struct reactos_audio_session_id *id)
{
    struct shared_audio_session *session;

    if (!id || id->slot >= SESSION_REGISTRY_CAPACITY)
        return NULL;

    session = &registry->sessions[id->slot];
    if (!session->occupied ||
        !IsEqualGUID(&session->instance_guid, &id->instance_guid))
        return NULL;

    return session;
}

static void begin_record_update(struct shared_audio_session *session)
{
    LONG generation;

    generation = InterlockedIncrement(&session->generation);
    if (!(generation & 1))
        InterlockedIncrement(&session->generation);
    MemoryBarrier();
}

static void finish_record_update(struct shared_audio_session *session)
{
    LONG generation;

    MemoryBarrier();
    generation = InterlockedIncrement(&session->generation);
    if (!generation)
        InterlockedExchange(&session->generation, 2);
}

static void copy_record_snapshot(const struct shared_audio_session *session,
                                 struct reactos_audio_session_snapshot *snapshot)
{
    UINT32 i;

    snapshot->session_guid = session->session_guid;
    snapshot->grouping_param = session->grouping_param;
    snapshot->process_id = session->process_id;
    snapshot->state = session->state;
    snapshot->channel_count = session->channel_count;
    snapshot->master_volume = session->master_volume;
    snapshot->mute = session->mute;
    for (i = 0; i < REACTOS_AUDIO_SESSION_MAX_CHANNELS; ++i)
        snapshot->channel_volumes[i] = session->channel_volumes[i];
    lstrcpynW(snapshot->process_path, session->process_path,
             ARRAY_SIZE(snapshot->process_path));
    lstrcpynW(snapshot->display_name, session->display_name,
             ARRAY_SIZE(snapshot->display_name));
    lstrcpynW(snapshot->icon_path, session->icon_path,
             ARRAY_SIZE(snapshot->icon_path));
    snapshot->peak_tick = (DWORD)InterlockedCompareExchange((volatile LONG *)&session->peak_tick, 0, 0);
    {
        LONG bits = InterlockedCompareExchange((volatile LONG *)&session->peak_bits, 0, 0);
        memcpy(&snapshot->peak, &bits, sizeof(float));
    }
}

HRESULT reactos_audio_session_set_peak(
    const struct reactos_audio_session_id *id, float peak)
{
    struct shared_audio_session *session;
    LONG bits;
    HRESULT hr;

    if (!id || id->slot >= SESSION_REGISTRY_CAPACITY)
        return E_INVALIDARG;
    if (FAILED(hr = ensure_registry()))
        return hr;
    session = &registry->sessions[id->slot];
    if (!InterlockedCompareExchange(&session->occupied, TRUE, TRUE) ||
        !IsEqualGUID(&session->instance_guid, &id->instance_guid))
        return AUDCLNT_E_DEVICE_INVALIDATED;
    if (peak < 0.0f) peak = 0.0f;
    if (peak > 1.0f) peak = 1.0f;
    memcpy(&bits, &peak, sizeof(LONG));
    InterlockedExchange((volatile LONG *)&session->peak_bits, bits);
    InterlockedExchange((volatile LONG *)&session->peak_tick, (LONG)GetTickCount());
    return S_OK;
}

HRESULT reactos_audio_session_read_peak(
    const struct reactos_audio_session_id *id, float *peak, DWORD *tick)
{
    struct shared_audio_session *session;
    LONG bits;
    HRESULT hr;

    if (!id || !peak)
        return E_POINTER;
    *peak = 0.0f;
    if (tick)
        *tick = 0;
    if (id->slot >= SESSION_REGISTRY_CAPACITY)
        return E_INVALIDARG;
    if (FAILED(hr = ensure_registry()))
        return hr;
    session = &registry->sessions[id->slot];
    if (!InterlockedCompareExchange(&session->occupied, TRUE, TRUE) ||
        !IsEqualGUID(&session->instance_guid, &id->instance_guid))
        return AUDCLNT_E_DEVICE_INVALIDATED;
    bits = InterlockedCompareExchange((volatile LONG *)&session->peak_bits, 0, 0);
    memcpy(peak, &bits, sizeof(float));
    if (tick)
        *tick = (DWORD)InterlockedCompareExchange((volatile LONG *)&session->peak_tick, 0, 0);
    return S_OK;
}

HRESULT reactos_audio_session_max_peak(IMMDevice *device, float *peak)
{
    WCHAR endpoint_id[MAX_PATH];
    DWORD now = GetTickCount();
    float best = 0.0f;
    UINT32 i;
    HRESULT hr;

    if (!device || !peak)
        return E_POINTER;
    *peak = 0.0f;
    if (FAILED(hr = get_endpoint_id(device, endpoint_id)))
        return hr;
    if (FAILED(hr = ensure_registry()))
        return hr;
    for (i = 0; i < SESSION_REGISTRY_CAPACITY; ++i)
    {
        struct shared_audio_session *session = &registry->sessions[i];
        LONG bits;
        DWORD tick;
        float value;
        if (!InterlockedCompareExchange(&session->occupied, TRUE, TRUE))
            continue;
        if (lstrcmpW(session->endpoint_id, endpoint_id))
            continue;
        tick = (DWORD)InterlockedCompareExchange((volatile LONG *)&session->peak_tick, 0, 0);
        if (now - tick > 400)
            continue;
        bits = InterlockedCompareExchange((volatile LONG *)&session->peak_bits, 0, 0);
        memcpy(&value, &bits, sizeof(float));
        if (value > best)
            best = value;
    }
    *peak = best;
    return S_OK;
}

HRESULT reactos_audio_session_register(
    IMMDevice *device, const GUID *session_guid, UINT32 channels,
    struct reactos_audio_session_id *id,
    struct reactos_audio_session_snapshot *snapshot)
{
    struct shared_audio_session *free_session = NULL;
    struct shared_audio_session *session = NULL;
    WCHAR endpoint_id[MAX_PATH];
    GUID effective_guid;
    DWORD process_id = GetCurrentProcessId();
    UINT32 i, channel_count;
    HRESULT hr;

    if (!device || !id || !snapshot)
        return E_POINTER;

    effective_guid = session_guid ? *session_guid : GUID_NULL;
    if (FAILED(hr = get_endpoint_id(device, endpoint_id)))
        return hr;
    if (FAILED(hr = ensure_registry()))
        return hr;
    if (!lock_registry())
        return HRESULT_FROM_WIN32(GetLastError());

    remove_stale_records();
    for (i = 0; i < SESSION_REGISTRY_CAPACITY; ++i)
    {
        struct shared_audio_session *candidate = &registry->sessions[i];

        if (!candidate->occupied)
        {
            if (!free_session)
                free_session = candidate;
            continue;
        }

        if (candidate->process_id == process_id &&
            file_times_equal(&candidate->process_start_time,
                             &current_process_start_time) &&
            IsEqualGUID(&candidate->session_guid, &effective_guid) &&
            !lstrcmpW(candidate->endpoint_id, endpoint_id))
        {
            session = candidate;
            break;
        }
    }

    if (!session)
    {
        WCHAR module_path[MAX_PATH];

        if (!(session = free_session))
        {
            unlock_registry();
            return HRESULT_FROM_WIN32(ERROR_TOO_MANY_SESS);
        }

        ZeroMemory(session, sizeof(*session));
        if (FAILED(CoCreateGuid(&session->instance_guid)))
        {
            unlock_registry();
            return E_FAIL;
        }
        session->session_guid = effective_guid;
        CoCreateGuid(&session->grouping_param);
        session->process_id = process_id;
        session->process_start_time = current_process_start_time;
        session->state = AudioSessionStateInactive;
        session->master_volume = 1.0f;
        for (i = 0; i < REACTOS_AUDIO_SESSION_MAX_CHANNELS; ++i)
            session->channel_volumes[i] = 1.0f;
        lstrcpynW(session->endpoint_id, endpoint_id,
                 ARRAY_SIZE(session->endpoint_id));
        if (!GetModuleFileNameW(NULL, module_path, ARRAY_SIZE(module_path)))
            module_path[0] = 0;
        lstrcpynW(session->process_path, module_path,
                 ARRAY_SIZE(session->process_path));
        session->generation = 2;
        MemoryBarrier();
        InterlockedExchange(&session->occupied, TRUE);
    }

    channel_count = min(channels, REACTOS_AUDIO_SESSION_MAX_CHANNELS);
    if (session->channel_count < channel_count)
    {
        begin_record_update(session);
        session->channel_count = channel_count;
        finish_record_update(session);
    }

    id->slot = session - registry->sessions;
    id->instance_guid = session->instance_guid;
    ZeroMemory(snapshot, sizeof(*snapshot));
    copy_record_snapshot(session, snapshot);
    snapshot->generation = session->generation;
    unlock_registry();
    return S_OK;
}

HRESULT reactos_audio_session_enumerate(IMMDevice *device,
                                        struct reactos_audio_session_id **ids,
                                        int *count)
{
    WCHAR endpoint_id[MAX_PATH];
    struct reactos_audio_session_id *result = NULL;
    UINT32 i;
    int found = 0;
    HRESULT hr;

    if (!device || !ids || !count)
        return E_POINTER;
    *ids = NULL;
    *count = 0;

    if (FAILED(hr = get_endpoint_id(device, endpoint_id)))
        return hr;
    if (FAILED(hr = ensure_registry()))
        return hr;
    if (!lock_registry())
        return HRESULT_FROM_WIN32(GetLastError());

    remove_stale_records();
    for (i = 0; i < SESSION_REGISTRY_CAPACITY; ++i)
    {
        const struct shared_audio_session *session = &registry->sessions[i];

        if (session->occupied && !lstrcmpW(session->endpoint_id, endpoint_id))
            ++found;
    }

    if (found && !(result = malloc(found * sizeof(*result))))
    {
        unlock_registry();
        return E_OUTOFMEMORY;
    }

    found = 0;
    for (i = 0; i < SESSION_REGISTRY_CAPACITY; ++i)
    {
        const struct shared_audio_session *session = &registry->sessions[i];

        if (!session->occupied || lstrcmpW(session->endpoint_id, endpoint_id))
            continue;
        result[found].slot = i;
        result[found].instance_guid = session->instance_guid;
        ++found;
    }

    unlock_registry();
    *ids = result;
    *count = found;
    return S_OK;
}

HRESULT reactos_audio_session_read(
    const struct reactos_audio_session_id *id,
    LONG known_generation,
    struct reactos_audio_session_snapshot *snapshot)
{
    struct shared_audio_session *session;
    LONG generation_before, generation_after;
    UINT attempt;
    HRESULT hr;

    if (!id || !snapshot)
        return E_POINTER;
    if (FAILED(hr = ensure_registry()))
        return hr;
    if (id->slot >= SESSION_REGISTRY_CAPACITY)
        return AUDCLNT_E_DEVICE_INVALIDATED;

    session = &registry->sessions[id->slot];
    for (attempt = 0; attempt < 4; ++attempt)
    {
        if (!InterlockedCompareExchange(&session->occupied, TRUE, TRUE) ||
            !IsEqualGUID(&session->instance_guid, &id->instance_guid))
            return AUDCLNT_E_DEVICE_INVALIDATED;

        generation_before = InterlockedCompareExchange(&session->generation, 0, 0);
        if (generation_before & 1)
            continue;
        MemoryBarrier();
        if (known_generation && generation_before == known_generation)
        {
            MemoryBarrier();
            generation_after = InterlockedCompareExchange(&session->generation, 0, 0);
            if (generation_before == generation_after &&
                InterlockedCompareExchange(&session->occupied, TRUE, TRUE) &&
                IsEqualGUID(&session->instance_guid, &id->instance_guid))
                return S_FALSE;
            continue;
        }
        ZeroMemory(snapshot, sizeof(*snapshot));
        copy_record_snapshot(session, snapshot);
        MemoryBarrier();
        generation_after = InterlockedCompareExchange(&session->generation, 0, 0);

        if (!(generation_after & 1) &&
            generation_before == generation_after &&
            InterlockedCompareExchange(&session->occupied, TRUE, TRUE) &&
            IsEqualGUID(&session->instance_guid, &id->instance_guid))
        {
            snapshot->generation = generation_after;
            return S_OK;
        }
    }

    return HRESULT_FROM_WIN32(ERROR_RETRY);
}

HRESULT reactos_audio_session_set_master(
    const struct reactos_audio_session_id *id, float level)
{
    struct shared_audio_session *session;
    HRESULT hr;

    if (FAILED(hr = ensure_registry()))
        return hr;
    if (!lock_registry())
        return HRESULT_FROM_WIN32(GetLastError());
    if (!(session = find_record_locked(id)))
    {
        unlock_registry();
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    begin_record_update(session);
    session->master_volume = level;
    finish_record_update(session);
    unlock_registry();
    return S_OK;
}

HRESULT reactos_audio_session_set_mute(
    const struct reactos_audio_session_id *id, BOOL mute)
{
    struct shared_audio_session *session;
    HRESULT hr;

    if (FAILED(hr = ensure_registry()))
        return hr;
    if (!lock_registry())
        return HRESULT_FROM_WIN32(GetLastError());
    if (!(session = find_record_locked(id)))
    {
        unlock_registry();
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    begin_record_update(session);
    session->mute = !!mute;
    finish_record_update(session);
    unlock_registry();
    return S_OK;
}

HRESULT reactos_audio_session_set_strings(
    const struct reactos_audio_session_id *id,
    const WCHAR *display_name, const WCHAR *icon_path)
{
    struct shared_audio_session *session;
    HRESULT hr;

    if (FAILED(hr = ensure_registry()))
        return hr;
    if (!lock_registry())
        return HRESULT_FROM_WIN32(GetLastError());
    if (!(session = find_record_locked(id)))
    {
        unlock_registry();
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    begin_record_update(session);
    if (display_name)
        lstrcpynW(session->display_name, display_name,
                  ARRAY_SIZE(session->display_name));
    if (icon_path)
        lstrcpynW(session->icon_path, icon_path,
                  ARRAY_SIZE(session->icon_path));
    finish_record_update(session);
    unlock_registry();
    return S_OK;
}

HRESULT reactos_audio_session_set_channel(
    const struct reactos_audio_session_id *id, UINT32 channel, float level)
{
    struct shared_audio_session *session;
    HRESULT hr;

    if (FAILED(hr = ensure_registry()))
        return hr;
    if (!lock_registry())
        return HRESULT_FROM_WIN32(GetLastError());
    if (!(session = find_record_locked(id)))
    {
        unlock_registry();
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    if (channel >= session->channel_count ||
        channel >= REACTOS_AUDIO_SESSION_MAX_CHANNELS)
    {
        unlock_registry();
        return E_INVALIDARG;
    }
    begin_record_update(session);
    session->channel_volumes[channel] = level;
    finish_record_update(session);
    unlock_registry();
    return S_OK;
}

HRESULT reactos_audio_session_set_channels(
    const struct reactos_audio_session_id *id, UINT32 count,
    const float *levels)
{
    struct shared_audio_session *session;
    UINT32 i;
    HRESULT hr;

    if (!levels)
        return E_POINTER;
    if (FAILED(hr = ensure_registry()))
        return hr;
    if (!lock_registry())
        return HRESULT_FROM_WIN32(GetLastError());
    if (!(session = find_record_locked(id)))
    {
        unlock_registry();
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    if (count != session->channel_count ||
        count > REACTOS_AUDIO_SESSION_MAX_CHANNELS)
    {
        unlock_registry();
        return E_INVALIDARG;
    }
    begin_record_update(session);
    for (i = 0; i < count; ++i)
        session->channel_volumes[i] = levels[i];
    finish_record_update(session);
    unlock_registry();
    return S_OK;
}

HRESULT reactos_audio_session_set_state(
    const struct reactos_audio_session_id *id, AudioSessionState state)
{
    struct shared_audio_session *session;
    HRESULT hr;

    if (FAILED(hr = ensure_registry()))
        return hr;
    if (!lock_registry())
        return HRESULT_FROM_WIN32(GetLastError());
    if (!(session = find_record_locked(id)))
    {
        unlock_registry();
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    if (session->state != state)
    {
        begin_record_update(session);
        session->state = state;
        finish_record_update(session);
    }
    unlock_registry();
    return S_OK;
}

void reactos_audio_sessions_shutdown(void)
{
    UINT32 i;

    if (!registry)
        return;

    /* DllMain calls this under the loader lock. Never wait for another
     * process to release the registry mutex from that context. Stale records
     * are removed by the next enumerator after this process terminates. */
    if (try_lock_registry())
    {
        for (i = 0; i < SESSION_REGISTRY_CAPACITY; ++i)
        {
            struct shared_audio_session *session = &registry->sessions[i];

            if (session->occupied &&
                session->process_id == GetCurrentProcessId() &&
                file_times_equal(&session->process_start_time,
                                 &current_process_start_time))
                clear_record(session);
        }
        unlock_registry();
    }

    UnmapViewOfFile(registry);
    CloseHandle(registry_mapping);
    CloseHandle(registry_mutex);
    registry = NULL;
    registry_mapping = NULL;
    registry_mutex = NULL;
}
