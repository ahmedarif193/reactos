/*
 * PROJECT:     ReactOS Core Audio
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Cross-process shared-mode render engine
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "mmreg.h"
#include "audioclient.h"

#include "reactos_render_engine.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#define RENDER_REGION_MAGIC 0x52415352 /* RSAR */
#define RENDER_REGION_VERSION 2
#define RENDER_CLIENT_CAPACITY 16
#define RENDER_RING_FRAMES 8192
#define RENDER_MAX_FRAME_SIZE 32
#define RENDER_PENDING_CHUNK_CAPACITY 64
#define RENDER_WORKER_INTERVAL_MS 5
#define RENDER_OWNER_RETRY_MS 100

struct shared_render_slot
{
    LONG occupied;
    DWORD token;
    DWORD process_id;
    FILETIME process_start_time;
    LONG started;
    UINT32 capacity_frames;
    UINT32 read_frame;
    UINT32 write_frame;
    UINT32 queued_frames;
    UINT32 pending_frames;
    UINT64 position;
    UINT64 qpc_time;
    BYTE data[RENDER_RING_FRAMES * RENDER_MAX_FRAME_SIZE];
};

struct shared_render_region
{
    DWORD magic;
    DWORD version;
    DWORD size;
    DWORD endpoint_index;
    WAVEFORMATEXTENSIBLE format;
    DWORD token_seed;
    DWORD owner_process_id;
    FILETIME owner_process_start_time;
    LONG owner_ready;
    struct shared_render_slot clients[RENDER_CLIENT_CAPACITY];
};

struct local_render_endpoint
{
    struct local_render_endpoint *next;
    DWORD endpoint_index;
    WAVEFORMATEXTENSIBLE format;
    DWORD reference_count;
    FILETIME process_start_time;
    HANDLE mapping;
    HANDLE mutex;
    HANDLE wake_event;
    HANDLE stop_event;
    HANDLE worker_thread;
    struct shared_render_region *region;
    struct reactos_render_transport_ops transport_ops;
    void *transport;
    BOOL transport_started;
    BYTE *mix_buffer;
    UINT32 mix_buffer_frames;
    UINT64 completed_frames;
    UINT32 pending_chunk_head;
    UINT32 pending_chunk_count;
    struct pending_render_chunk *pending_chunks;
};

struct reactos_shared_render_client
{
    struct local_render_endpoint *endpoint;
    DWORD slot_index;
    DWORD token;
};

struct pending_consumption
{
    DWORD token;
    UINT32 read_frame;
    UINT32 frames;
};

struct pending_render_chunk
{
    UINT32 frames;
    UINT32 played_frames;
    struct pending_consumption clients[RENDER_CLIENT_CAPACITY];
};

static INIT_ONCE endpoint_list_init_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION endpoint_list_lock;
static struct local_render_endpoint *endpoint_list;

static BOOL WINAPI
initialize_endpoint_list(INIT_ONCE *once, void *parameter, void **context)
{
    UNREFERENCED_PARAMETER(once);
    UNREFERENCED_PARAMETER(parameter);
    UNREFERENCED_PARAMETER(context);

    InitializeCriticalSection(&endpoint_list_lock);
    return TRUE;
}

static HRESULT
ensure_endpoint_list(void)
{
    DWORD error;

    if (InitOnceExecuteOnce(&endpoint_list_init_once, initialize_endpoint_list, NULL, NULL))
        return S_OK;

    error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

static BOOL
file_times_equal(const FILETIME *left, const FILETIME *right)
{
    return left->dwLowDateTime == right->dwLowDateTime && left->dwHighDateTime == right->dwHighDateTime;
}

static BOOL
get_current_process_start_time(FILETIME *creation_time)
{
    FILETIME exit_time, kernel_time, user_time;

    return GetProcessTimes(GetCurrentProcess(), creation_time, &exit_time, &kernel_time, &user_time);
}

static BOOL
query_process_start_time(DWORD process_id, FILETIME *creation_time, BOOL *terminated)
{
    FILETIME exit_time, kernel_time, user_time;
    HANDLE process;
    DWORD wait;

    *terminated = FALSE;
    process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process)
        process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, process_id);
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

    if (!GetProcessTimes(process, creation_time, &exit_time, &kernel_time, &user_time))
    {
        CloseHandle(process);
        return FALSE;
    }

    CloseHandle(process);
    return TRUE;
}

static BOOL
process_record_is_alive(
    DWORD process_id,
    const FILETIME *process_start_time,
    const FILETIME *current_process_start_time)
{
    FILETIME creation_time;
    BOOL terminated;

    if (!process_id)
        return FALSE;

    if (process_id == GetCurrentProcessId())
        return file_times_equal(process_start_time, current_process_start_time);

    if (query_process_start_time(process_id, &creation_time, &terminated))
        return file_times_equal(process_start_time, &creation_time);

    return !terminated;
}

static BOOL
lock_endpoint(struct local_render_endpoint *endpoint)
{
    DWORD wait;

    wait = WaitForSingleObject(endpoint->mutex, INFINITE);
    return wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
}

static void
unlock_endpoint(struct local_render_endpoint *endpoint)
{
    ReleaseMutex(endpoint->mutex);
}

static UINT64
query_performance_time_100ns(void)
{
    LARGE_INTEGER counter, frequency;
    ULONGLONG quotient, remainder;

    if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        return GetTickCount64() * 10000ULL;

    quotient = (ULONGLONG)counter.QuadPart / (ULONGLONG)frequency.QuadPart;
    remainder = (ULONGLONG)counter.QuadPart % (ULONGLONG)frequency.QuadPart;
    return quotient * 10000000ULL + remainder * 10000000ULL / (ULONGLONG)frequency.QuadPart;
}

static BOOL
formats_equal(const WAVEFORMATEXTENSIBLE *left, const WAVEFORMATEXTENSIBLE *right)
{
    return left->Format.wFormatTag == right->Format.wFormatTag && left->Format.nChannels == right->Format.nChannels &&
           left->Format.nSamplesPerSec == right->Format.nSamplesPerSec &&
           left->Format.nAvgBytesPerSec == right->Format.nAvgBytesPerSec &&
           left->Format.nBlockAlign == right->Format.nBlockAlign &&
           left->Format.wBitsPerSample == right->Format.wBitsPerSample &&
           left->Samples.wValidBitsPerSample == right->Samples.wValidBitsPerSample &&
           left->dwChannelMask == right->dwChannelMask && IsEqualGUID(&left->SubFormat, &right->SubFormat);
}

static BOOL
region_has_clients(const struct shared_render_region *region)
{
    UINT32 i;

    for (i = 0; i < RENDER_CLIENT_CAPACITY; ++i)
    {
        if (region->clients[i].occupied)
            return TRUE;
    }
    return FALSE;
}

static void
clear_slot(struct shared_render_slot *slot)
{
    ZeroMemory(slot, sizeof(*slot));
}

static void
discard_pending_chunks_locked(struct local_render_endpoint *endpoint)
{
    UINT32 i;

    endpoint->pending_chunk_head = 0;
    endpoint->pending_chunk_count = 0;
    ZeroMemory(endpoint->pending_chunks,
               RENDER_PENDING_CHUNK_CAPACITY * sizeof(*endpoint->pending_chunks));

    for (i = 0; i < RENDER_CLIENT_CAPACITY; ++i)
        endpoint->region->clients[i].pending_frames = 0;
}

static void
remove_stale_clients_locked(struct local_render_endpoint *endpoint)
{
    UINT32 i;

    for (i = 0; i < RENDER_CLIENT_CAPACITY; ++i)
    {
        struct shared_render_slot *slot = &endpoint->region->clients[i];

        if (slot->occupied &&
            !process_record_is_alive(slot->process_id, &slot->process_start_time, &endpoint->process_start_time))
            clear_slot(slot);
    }
}

static BOOL
owner_is_current_process_locked(const struct local_render_endpoint *endpoint)
{
    return endpoint->region->owner_process_id == GetCurrentProcessId() &&
           file_times_equal(&endpoint->region->owner_process_start_time, &endpoint->process_start_time);
}

static BOOL
owner_is_alive_locked(const struct local_render_endpoint *endpoint)
{
    return process_record_is_alive(
        endpoint->region->owner_process_id, &endpoint->region->owner_process_start_time, &endpoint->process_start_time);
}

static void
clear_owner_locked(struct local_render_endpoint *endpoint)
{
    endpoint->region->owner_ready = FALSE;
    endpoint->region->owner_process_id = 0;
    ZeroMemory(&endpoint->region->owner_process_start_time, sizeof(endpoint->region->owner_process_start_time));
}

static void
initialize_region_locked(struct local_render_endpoint *endpoint)
{
    ZeroMemory(endpoint->region, sizeof(*endpoint->region));
    endpoint->region->version = RENDER_REGION_VERSION;
    endpoint->region->size = sizeof(*endpoint->region);
    endpoint->region->endpoint_index = endpoint->endpoint_index;
    endpoint->region->format = endpoint->format;
    MemoryBarrier();
    endpoint->region->magic = RENDER_REGION_MAGIC;
}

static HRESULT
validate_region_locked(struct local_render_endpoint *endpoint)
{
    struct shared_render_region *region = endpoint->region;

    if (region->magic != RENDER_REGION_MAGIC || region->version != RENDER_REGION_VERSION ||
        region->size != sizeof(*region) || region->endpoint_index != endpoint->endpoint_index)
    {
        initialize_region_locked(endpoint);
        return S_OK;
    }

    remove_stale_clients_locked(endpoint);
    if (region->owner_process_id && !owner_is_alive_locked(endpoint))
        clear_owner_locked(endpoint);

    if (!formats_equal(&region->format, &endpoint->format))
    {
        if (region_has_clients(region) || region->owner_process_id)
            return AUDCLNT_E_UNSUPPORTED_FORMAT;
        initialize_region_locked(endpoint);
    }

    return S_OK;
}

static struct shared_render_slot *
find_client_slot_locked(const struct reactos_shared_render_client *client)
{
    struct shared_render_slot *slot;

    if (!client || client->slot_index >= RENDER_CLIENT_CAPACITY)
        return NULL;

    slot = &client->endpoint->region->clients[client->slot_index];
    if (!slot->occupied || slot->token != client->token)
        return NULL;

    return slot;
}

static HRESULT
allocate_client_slot_locked(
    struct local_render_endpoint *endpoint,
    UINT32 capacity_frames,
    DWORD *slot_index,
    DWORD *token)
{
    struct shared_render_slot *slot;
    UINT32 i;

    remove_stale_clients_locked(endpoint);
    for (i = 0; i < RENDER_CLIENT_CAPACITY; ++i)
    {
        if (!endpoint->region->clients[i].occupied)
            break;
    }
    if (i == RENDER_CLIENT_CAPACITY)
        return AUDCLNT_E_ENDPOINT_CREATE_FAILED;

    slot = &endpoint->region->clients[i];
    clear_slot(slot);
    if (!++endpoint->region->token_seed)
        ++endpoint->region->token_seed;
    slot->token = endpoint->region->token_seed;
    slot->process_id = GetCurrentProcessId();
    slot->process_start_time = endpoint->process_start_time;
    slot->capacity_frames = capacity_frames;
    MemoryBarrier();
    slot->occupied = TRUE;

    *slot_index = i;
    *token = slot->token;
    return S_OK;
}

static HRESULT
create_transport(struct local_render_endpoint *endpoint, BOOL require_success)
{
    void *transport = NULL;
    BOOL create_here = FALSE;
    HRESULT hr;

    if (!lock_endpoint(endpoint))
        return E_FAIL;

    if (FAILED(hr = validate_region_locked(endpoint)))
    {
        unlock_endpoint(endpoint);
        return hr;
    }

    if (!endpoint->region->owner_process_id)
    {
        endpoint->region->owner_process_id = GetCurrentProcessId();
        endpoint->region->owner_process_start_time = endpoint->process_start_time;
        endpoint->region->owner_ready = FALSE;
        discard_pending_chunks_locked(endpoint);
        create_here = TRUE;
    }
    else if (owner_is_current_process_locked(endpoint) && !endpoint->transport)
    {
        discard_pending_chunks_locked(endpoint);
        create_here = TRUE;
    }
    unlock_endpoint(endpoint);

    if (!create_here)
        return S_OK;

    hr = endpoint->transport_ops.create(endpoint->endpoint_index, &endpoint->format, &transport);
    if (SUCCEEDED(hr) && !endpoint->transport_ops.completed_frames(transport, &endpoint->completed_frames, NULL))
    {
        endpoint->transport_ops.destroy(transport);
        transport = NULL;
        hr = E_FAIL;
    }

    if (!lock_endpoint(endpoint))
    {
        if (transport)
            endpoint->transport_ops.destroy(transport);
        return E_FAIL;
    }

    if (!owner_is_current_process_locked(endpoint))
    {
        unlock_endpoint(endpoint);
        if (transport)
            endpoint->transport_ops.destroy(transport);
        return S_OK;
    }

    if (FAILED(hr))
    {
        clear_owner_locked(endpoint);
        unlock_endpoint(endpoint);
        SetEvent(endpoint->wake_event);
        return require_success ? hr : S_OK;
    }

    endpoint->transport = transport;
    endpoint->region->owner_ready = TRUE;
    unlock_endpoint(endpoint);
    SetEvent(endpoint->wake_event);
    return S_OK;
}

static BOOL
any_started_clients_locked(const struct local_render_endpoint *endpoint)
{
    UINT32 i;

    for (i = 0; i < RENDER_CLIENT_CAPACITY; ++i)
    {
        const struct shared_render_slot *slot = &endpoint->region->clients[i];

        if (slot->occupied && slot->started)
            return TRUE;
    }
    return FALSE;
}

static INT32
read_pcm_sample(const BYTE *data, WORD bits, WORD valid_bits)
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
            raw = data[0] | ((UINT32)data[1] << 8) | ((UINT32)data[2] << 16);
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

static void
write_pcm_sample(BYTE *data, WORD bits, WORD valid_bits, INT32 value)
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

static INT32
saturate_sample(INT64 sample)
{
    if (sample > 0x7fffffffLL)
        return 0x7fffffff;
    if (sample < (-0x7fffffffLL - 1))
        return (-0x7fffffff - 1);
    return (INT32)sample;
}

static BOOL
ensure_mix_buffer(struct local_render_endpoint *endpoint, UINT32 frames)
{
    BYTE *buffer;

    if (endpoint->mix_buffer_frames >= frames)
        return TRUE;
    if ((SIZE_T)frames > ~(SIZE_T)0 / endpoint->format.Format.nBlockAlign)
        return FALSE;

    buffer = realloc(endpoint->mix_buffer, (SIZE_T)frames * endpoint->format.Format.nBlockAlign);
    if (!buffer)
        return FALSE;

    endpoint->mix_buffer = buffer;
    endpoint->mix_buffer_frames = frames;
    return TRUE;
}

static void
retire_completed_chunks(struct local_render_endpoint *endpoint)
{
    UINT64 completed_frames, completed_qpc, remaining;

    if (!endpoint->transport ||
        !endpoint->transport_ops.completed_frames(endpoint->transport, &completed_frames, &completed_qpc))
    {
        return;
    }

    if (completed_frames <= endpoint->completed_frames)
    {
        if (completed_frames < endpoint->completed_frames)
        {
            if (lock_endpoint(endpoint))
            {
                discard_pending_chunks_locked(endpoint);
                endpoint->completed_frames = completed_frames;
                unlock_endpoint(endpoint);
            }
        }
        return;
    }

    remaining = completed_frames - endpoint->completed_frames;
    if (!lock_endpoint(endpoint))
        return;

    while (remaining && endpoint->pending_chunk_count)
    {
        struct pending_render_chunk *chunk =
            &endpoint->pending_chunks[endpoint->pending_chunk_head];
        UINT32 old_played = chunk->played_frames;
        UINT32 advance = (UINT32)min(remaining, chunk->frames - old_played);
        UINT32 new_played = old_played + advance;
        UINT32 client_index;

        for (client_index = 0; client_index < RENDER_CLIENT_CAPACITY; ++client_index)
        {
            const struct pending_consumption *entry = &chunk->clients[client_index];
            struct shared_render_slot *slot = &endpoint->region->clients[client_index];
            UINT32 old_client_played, new_client_played, client_advance;

            if (!entry->frames || !slot->occupied || slot->token != entry->token)
                continue;

            old_client_played = min(old_played, entry->frames);
            new_client_played = min(new_played, entry->frames);
            client_advance = new_client_played - old_client_played;
            if (!client_advance)
                continue;

            slot->pending_frames = slot->pending_frames > client_advance ?
                                       slot->pending_frames - client_advance : 0;
            slot->position += client_advance;
            slot->qpc_time = completed_qpc;
        }

        chunk->played_frames = new_played;
        remaining -= advance;
        if (new_played == chunk->frames)
        {
            ZeroMemory(chunk, sizeof(*chunk));
            endpoint->pending_chunk_head =
                (endpoint->pending_chunk_head + 1) % RENDER_PENDING_CHUNK_CAPACITY;
            --endpoint->pending_chunk_count;
        }
    }

    endpoint->completed_frames = completed_frames;
    unlock_endpoint(endpoint);
}

static BOOL
mix_one_chunk(struct local_render_endpoint *endpoint)
{
    struct pending_consumption consumed[RENDER_CLIENT_CAPACITY];
    UINT32 max_queued = 0, writable, period, frames;
    UINT32 frame, client_index;
    WORD sample_size, channel;
    BOOL queued;

    writable = endpoint->transport_ops.writable_frames(endpoint->transport);
    if (!writable)
        return FALSE;
    period = endpoint->transport_ops.period_frames(endpoint->transport);
    if (!period)
        period = 1;

    if (!lock_endpoint(endpoint))
        return FALSE;
    if (!owner_is_current_process_locked(endpoint) || !endpoint->region->owner_ready ||
        endpoint->pending_chunk_count == RENDER_PENDING_CHUNK_CAPACITY)
    {
        unlock_endpoint(endpoint);
        return FALSE;
    }

    for (client_index = 0; client_index < RENDER_CLIENT_CAPACITY; ++client_index)
    {
        const struct shared_render_slot *slot = &endpoint->region->clients[client_index];

        if (slot->occupied && slot->started && slot->queued_frames > max_queued)
            max_queued = slot->queued_frames;
    }

    frames = min(max_queued, min(writable, period));
    if (!frames || !ensure_mix_buffer(endpoint, frames))
    {
        unlock_endpoint(endpoint);
        return FALSE;
    }

    ZeroMemory(consumed, sizeof(consumed));
    sample_size = endpoint->format.Format.wBitsPerSample / 8;
    for (frame = 0; frame < frames; ++frame)
    {
        BYTE *destination = endpoint->mix_buffer + (SIZE_T)frame * endpoint->format.Format.nBlockAlign;

        for (channel = 0; channel < endpoint->format.Format.nChannels; ++channel)
        {
            INT64 mixed_sample = 0;

            for (client_index = 0; client_index < RENDER_CLIENT_CAPACITY; ++client_index)
            {
                const struct shared_render_slot *slot = &endpoint->region->clients[client_index];
                const BYTE *source;
                UINT32 source_frame;

                if (!slot->occupied || !slot->started || frame >= slot->queued_frames)
                    continue;

                source_frame = (slot->read_frame + frame) % slot->capacity_frames;
                source =
                    slot->data + (SIZE_T)source_frame * endpoint->format.Format.nBlockAlign + channel * sample_size;
                mixed_sample += read_pcm_sample(
                    source, endpoint->format.Format.wBitsPerSample, endpoint->format.Samples.wValidBitsPerSample);
            }

            write_pcm_sample(
                destination + channel * sample_size, endpoint->format.Format.wBitsPerSample,
                endpoint->format.Samples.wValidBitsPerSample, saturate_sample(mixed_sample));
        }
    }

    for (client_index = 0; client_index < RENDER_CLIENT_CAPACITY; ++client_index)
    {
        const struct shared_render_slot *slot = &endpoint->region->clients[client_index];

        if (!slot->occupied || !slot->started || !slot->queued_frames)
            continue;
        consumed[client_index].token = slot->token;
        consumed[client_index].read_frame = slot->read_frame;
        consumed[client_index].frames = min(frames, slot->queued_frames);
    }
    queued = endpoint->transport_ops.queue_frames(endpoint->transport, endpoint->mix_buffer, frames);
    if (!queued)
    {
        unlock_endpoint(endpoint);
        return FALSE;
    }

    {
        UINT32 pending_index =
            (endpoint->pending_chunk_head + endpoint->pending_chunk_count) % RENDER_PENDING_CHUNK_CAPACITY;
        struct pending_render_chunk *chunk = &endpoint->pending_chunks[pending_index];

        ZeroMemory(chunk, sizeof(*chunk));
        chunk->frames = frames;
        CopyMemory(chunk->clients, consumed, sizeof(consumed));
        ++endpoint->pending_chunk_count;
    }

    for (client_index = 0; client_index < RENDER_CLIENT_CAPACITY; ++client_index)
    {
        struct shared_render_slot *slot = &endpoint->region->clients[client_index];
        const struct pending_consumption *entry = &consumed[client_index];

        if (!entry->frames || !slot->occupied || slot->token != entry->token || slot->read_frame != entry->read_frame)
            continue;

        slot->read_frame = (slot->read_frame + entry->frames) % slot->capacity_frames;
        slot->queued_frames -= entry->frames;
        slot->pending_frames += entry->frames;
    }
    unlock_endpoint(endpoint);
    SetEvent(endpoint->wake_event);
    return TRUE;
}

static void
stop_and_destroy_transport(struct local_render_endpoint *endpoint)
{
    void *transport = endpoint->transport;

    if (!transport)
        return;

    retire_completed_chunks(endpoint);
    if (endpoint->transport_started)
    {
        endpoint->transport_ops.stop(transport);
        endpoint->transport_started = FALSE;
        retire_completed_chunks(endpoint);
    }

    endpoint->transport = NULL;
    endpoint->transport_ops.destroy(transport);
    if (lock_endpoint(endpoint))
    {
        discard_pending_chunks_locked(endpoint);
        unlock_endpoint(endpoint);
    }
}

static DWORD WINAPI
render_worker(void *parameter)
{
    struct local_render_endpoint *endpoint = parameter;
    HANDLE wait_handles[2] = {endpoint->stop_event, endpoint->wake_event};
    DWORD wait, owner_retry = 0;

    SetThreadDescription(GetCurrentThread(), L"shared_audio_engine");
    for (;;)
    {
        BOOL active = FALSE;
        UINT32 submits;

        wait = WaitForMultipleObjects(2, wait_handles, FALSE, RENDER_WORKER_INTERVAL_MS);
        if (wait == WAIT_OBJECT_0)
            break;

        if (!endpoint->transport && (!owner_retry || GetTickCount() - owner_retry >= RENDER_OWNER_RETRY_MS))
        {
            create_transport(endpoint, FALSE);
            owner_retry = GetTickCount();
        }

        if (!endpoint->transport)
            continue;

        retire_completed_chunks(endpoint);

        if (lock_endpoint(endpoint))
        {
            active = owner_is_current_process_locked(endpoint) && endpoint->region->owner_ready &&
                     (any_started_clients_locked(endpoint) || endpoint->pending_chunk_count);
            unlock_endpoint(endpoint);
        }

        if (active && !endpoint->transport_started)
        {
            if (FAILED(endpoint->transport_ops.start(endpoint->transport)))
            {
                if (lock_endpoint(endpoint))
                {
                    if (owner_is_current_process_locked(endpoint))
                        clear_owner_locked(endpoint);
                    unlock_endpoint(endpoint);
                }
                stop_and_destroy_transport(endpoint);
                continue;
            }
            endpoint->transport_started = TRUE;
        }
        else if (!active && endpoint->transport_started)
        {
            endpoint->transport_ops.stop(endpoint->transport);
            endpoint->transport_started = FALSE;
            retire_completed_chunks(endpoint);
        }

        if (!endpoint->transport_started)
            continue;

        for (submits = 0; submits < 8; ++submits)
        {
            if (!mix_one_chunk(endpoint))
                break;
        }
    }

    stop_and_destroy_transport(endpoint);
    return 0;
}

static void
destroy_local_endpoint(struct local_render_endpoint *endpoint)
{
    if (!endpoint)
        return;

    if (endpoint->stop_event)
        SetEvent(endpoint->stop_event);
    if (endpoint->worker_thread)
    {
        WaitForSingleObject(endpoint->worker_thread, INFINITE);
        CloseHandle(endpoint->worker_thread);
    }
    else
    {
        stop_and_destroy_transport(endpoint);
    }

    if (endpoint->region && endpoint->mutex && lock_endpoint(endpoint))
    {
        if (owner_is_current_process_locked(endpoint))
            clear_owner_locked(endpoint);
        unlock_endpoint(endpoint);
    }
    if (endpoint->wake_event)
        SetEvent(endpoint->wake_event);
    if (endpoint->region)
        UnmapViewOfFile(endpoint->region);
    if (endpoint->mapping)
        CloseHandle(endpoint->mapping);
    if (endpoint->mutex)
        CloseHandle(endpoint->mutex);
    if (endpoint->wake_event)
        CloseHandle(endpoint->wake_event);
    if (endpoint->stop_event)
        CloseHandle(endpoint->stop_event);
    free(endpoint->mix_buffer);
    free(endpoint->pending_chunks);
    free(endpoint);
}

static HRESULT
create_local_endpoint(
    DWORD endpoint_index,
    const WAVEFORMATEXTENSIBLE *format,
    const struct reactos_render_transport_ops *transport_ops,
    struct local_render_endpoint **created)
{
    WCHAR mapping_name[96], mutex_name[104], wake_name[104];
    struct local_render_endpoint *endpoint;
    HRESULT hr = E_FAIL;

    endpoint = calloc(1, sizeof(*endpoint));
    if (!endpoint)
        return E_OUTOFMEMORY;
    endpoint->pending_chunks = calloc(RENDER_PENDING_CHUNK_CAPACITY, sizeof(*endpoint->pending_chunks));
    if (!endpoint->pending_chunks)
    {
        free(endpoint);
        return E_OUTOFMEMORY;
    }

    endpoint->endpoint_index = endpoint_index;
    endpoint->format = *format;
    endpoint->transport_ops = *transport_ops;
    if (!get_current_process_start_time(&endpoint->process_start_time))
        ZeroMemory(&endpoint->process_start_time, sizeof(endpoint->process_start_time));

    swprintf(mapping_name, ARRAY_SIZE(mapping_name), L"Local\\ReactOS.CoreAudio.Render.%lu.v2", endpoint_index);
    swprintf(mutex_name, ARRAY_SIZE(mutex_name), L"%s.Lock", mapping_name);
    swprintf(wake_name, ARRAY_SIZE(wake_name), L"%s.Wake", mapping_name);

    endpoint->mutex = CreateMutexW(NULL, FALSE, mutex_name);
    endpoint->wake_event = CreateEventW(NULL, FALSE, FALSE, wake_name);
    endpoint->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    endpoint->mapping =
        CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*endpoint->region), mapping_name);
    if (!endpoint->mutex || !endpoint->wake_event || !endpoint->stop_event || !endpoint->mapping)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto failed;
    }

    endpoint->region =
        MapViewOfFile(endpoint->mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(*endpoint->region));
    if (!endpoint->region)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto failed;
    }

    if (!lock_endpoint(endpoint))
        goto failed;
    hr = validate_region_locked(endpoint);
    unlock_endpoint(endpoint);
    if (FAILED(hr))
        goto failed;

    *created = endpoint;
    return S_OK;

failed:
    destroy_local_endpoint(endpoint);
    return hr;
}

static struct local_render_endpoint *
find_local_endpoint(DWORD endpoint_index, const WAVEFORMATEXTENSIBLE *format)
{
    struct local_render_endpoint *endpoint;

    for (endpoint = endpoint_list; endpoint; endpoint = endpoint->next)
    {
        if (endpoint->endpoint_index == endpoint_index && formats_equal(&endpoint->format, format))
            return endpoint;
    }
    return NULL;
}

HRESULT
reactos_shared_render_create(
    DWORD endpoint_index,
    const WAVEFORMATEXTENSIBLE *format,
    UINT32 requested_buffer_frames,
    const struct reactos_render_transport_ops *transport_ops,
    struct reactos_shared_render_client **client,
    UINT32 *buffer_frames)
{
    struct reactos_shared_render_client *new_client = NULL;
    struct local_render_endpoint *endpoint;
    DWORD slot_index = 0, token = 0;
    UINT32 capacity_frames;
    BOOL new_endpoint = FALSE;
    HRESULT hr;

    if (!format || !transport_ops || !transport_ops->create || !transport_ops->destroy || !transport_ops->start ||
        !transport_ops->stop || !transport_ops->writable_frames || !transport_ops->period_frames ||
        !transport_ops->completed_frames || !transport_ops->queue_frames || !client || !buffer_frames ||
        !format->Format.nBlockAlign ||
        format->Format.nBlockAlign > RENDER_MAX_FRAME_SIZE || !requested_buffer_frames)
        return E_INVALIDARG;

    if (FAILED(hr = ensure_endpoint_list()))
        return hr;

    capacity_frames = min(requested_buffer_frames, (UINT32)RENDER_RING_FRAMES);
    new_client = calloc(1, sizeof(*new_client));
    if (!new_client)
        return E_OUTOFMEMORY;

    EnterCriticalSection(&endpoint_list_lock);
    endpoint = find_local_endpoint(endpoint_index, format);
    if (!endpoint)
    {
        hr = create_local_endpoint(endpoint_index, format, transport_ops, &endpoint);
        if (FAILED(hr))
            goto failed_locked;
        endpoint->next = endpoint_list;
        endpoint_list = endpoint;
        new_endpoint = TRUE;
    }

    if (!lock_endpoint(endpoint))
    {
        hr = E_FAIL;
        goto failed_locked;
    }
    hr = validate_region_locked(endpoint);
    if (SUCCEEDED(hr))
        hr = allocate_client_slot_locked(endpoint, capacity_frames, &slot_index, &token);
    unlock_endpoint(endpoint);
    if (FAILED(hr))
        goto failed_locked;

    ++endpoint->reference_count;
    new_client->endpoint = endpoint;
    new_client->slot_index = slot_index;
    new_client->token = token;

    if (new_endpoint)
    {
        hr = create_transport(endpoint, TRUE);
        if (FAILED(hr))
            goto failed_client_locked;

        endpoint->worker_thread = CreateThread(NULL, 0, render_worker, endpoint, 0, NULL);
        if (!endpoint->worker_thread)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            goto failed_client_locked;
        }
        SetThreadPriority(endpoint->worker_thread, THREAD_PRIORITY_TIME_CRITICAL);
    }

    LeaveCriticalSection(&endpoint_list_lock);
    *client = new_client;
    *buffer_frames = capacity_frames;
    SetEvent(endpoint->wake_event);
    return S_OK;

failed_client_locked:
    if (lock_endpoint(endpoint))
    {
        struct shared_render_slot *slot = &endpoint->region->clients[slot_index];

        if (slot->occupied && slot->token == token)
            clear_slot(slot);
        unlock_endpoint(endpoint);
    }
    --endpoint->reference_count;

failed_locked:
    if (new_endpoint && endpoint && !endpoint->reference_count)
    {
        struct local_render_endpoint **link = &endpoint_list;

        while (*link && *link != endpoint)
            link = &(*link)->next;
        if (*link)
            *link = endpoint->next;
        destroy_local_endpoint(endpoint);
    }
    LeaveCriticalSection(&endpoint_list_lock);
    free(new_client);
    return hr;
}

void
reactos_shared_render_release(struct reactos_shared_render_client *client)
{
    struct local_render_endpoint *endpoint;
    BOOL destroy = FALSE;

    if (!client)
        return;
    endpoint = client->endpoint;

    if (lock_endpoint(endpoint))
    {
        struct shared_render_slot *slot = find_client_slot_locked(client);

        if (slot)
            clear_slot(slot);
        unlock_endpoint(endpoint);
    }
    SetEvent(endpoint->wake_event);

    if (SUCCEEDED(ensure_endpoint_list()))
    {
        EnterCriticalSection(&endpoint_list_lock);
        if (endpoint->reference_count && !--endpoint->reference_count)
        {
            struct local_render_endpoint **link = &endpoint_list;

            while (*link && *link != endpoint)
                link = &(*link)->next;
            if (*link)
                *link = endpoint->next;
            destroy = TRUE;
        }
        LeaveCriticalSection(&endpoint_list_lock);
    }

    free(client);
    if (destroy)
        destroy_local_endpoint(endpoint);
}

HRESULT
reactos_shared_render_start(struct reactos_shared_render_client *client)
{
    struct shared_render_slot *slot;

    if (!client || !lock_endpoint(client->endpoint))
        return AUDCLNT_E_DEVICE_INVALIDATED;
    slot = find_client_slot_locked(client);
    if (!slot)
    {
        unlock_endpoint(client->endpoint);
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    if (slot->started)
    {
        unlock_endpoint(client->endpoint);
        return AUDCLNT_E_NOT_STOPPED;
    }
    slot->started = TRUE;
    unlock_endpoint(client->endpoint);
    SetEvent(client->endpoint->wake_event);
    return S_OK;
}

HRESULT
reactos_shared_render_stop(struct reactos_shared_render_client *client)
{
    struct shared_render_slot *slot;

    if (!client || !lock_endpoint(client->endpoint))
        return AUDCLNT_E_DEVICE_INVALIDATED;
    slot = find_client_slot_locked(client);
    if (!slot)
    {
        unlock_endpoint(client->endpoint);
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    if (!slot->started)
    {
        unlock_endpoint(client->endpoint);
        return S_FALSE;
    }
    slot->started = FALSE;
    unlock_endpoint(client->endpoint);
    SetEvent(client->endpoint->wake_event);
    return S_OK;
}

HRESULT
reactos_shared_render_reset(struct reactos_shared_render_client *client)
{
    struct shared_render_slot *slot;

    if (!client || !lock_endpoint(client->endpoint))
        return AUDCLNT_E_DEVICE_INVALIDATED;
    slot = find_client_slot_locked(client);
    if (!slot)
    {
        unlock_endpoint(client->endpoint);
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    if (slot->started)
    {
        unlock_endpoint(client->endpoint);
        return AUDCLNT_E_NOT_STOPPED;
    }

    slot->read_frame = 0;
    slot->write_frame = 0;
    slot->queued_frames = 0;
    slot->pending_frames = 0;
    slot->position = 0;
    slot->qpc_time = query_performance_time_100ns();
    if (!++client->endpoint->region->token_seed)
        ++client->endpoint->region->token_seed;
    slot->token = client->endpoint->region->token_seed;
    client->token = slot->token;
    unlock_endpoint(client->endpoint);
    SetEvent(client->endpoint->wake_event);
    return S_OK;
}

HRESULT
reactos_shared_render_write(struct reactos_shared_render_client *client, const BYTE *data, UINT32 frames)
{
    struct shared_render_slot *slot;
    UINT32 first_frames;
    WORD frame_size;

    if (!client || (!data && frames) || !lock_endpoint(client->endpoint))
        return E_INVALIDARG;
    slot = find_client_slot_locked(client);
    if (!slot)
    {
        unlock_endpoint(client->endpoint);
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    if (slot->queued_frames > slot->capacity_frames ||
        slot->pending_frames > slot->capacity_frames - slot->queued_frames ||
        frames > slot->capacity_frames - slot->queued_frames - slot->pending_frames)
    {
        unlock_endpoint(client->endpoint);
        return AUDCLNT_E_BUFFER_TOO_LARGE;
    }

    frame_size = client->endpoint->format.Format.nBlockAlign;
    first_frames = min(frames, slot->capacity_frames - slot->write_frame);
    if (first_frames)
    {
        CopyMemory(slot->data + (SIZE_T)slot->write_frame * frame_size, data, (SIZE_T)first_frames * frame_size);
    }
    if (frames > first_frames)
    {
        CopyMemory(slot->data, data + (SIZE_T)first_frames * frame_size, (SIZE_T)(frames - first_frames) * frame_size);
    }
    if (frames)
    {
        slot->write_frame = (slot->write_frame + frames) % slot->capacity_frames;
        slot->queued_frames += frames;
    }
    unlock_endpoint(client->endpoint);
    if (frames)
        SetEvent(client->endpoint->wake_event);
    return S_OK;
}

HRESULT
reactos_shared_render_get_padding(struct reactos_shared_render_client *client, UINT32 *padding)
{
    struct shared_render_slot *slot;

    if (!client || !padding || !lock_endpoint(client->endpoint))
        return E_INVALIDARG;
    slot = find_client_slot_locked(client);
    if (!slot)
    {
        unlock_endpoint(client->endpoint);
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    *padding = slot->queued_frames + slot->pending_frames;
    unlock_endpoint(client->endpoint);
    return S_OK;
}

HRESULT
reactos_shared_render_get_position(struct reactos_shared_render_client *client, UINT64 *position, UINT64 *qpc_time)
{
    struct shared_render_slot *slot;

    if (!client || !position || !lock_endpoint(client->endpoint))
        return E_INVALIDARG;
    slot = find_client_slot_locked(client);
    if (!slot)
    {
        unlock_endpoint(client->endpoint);
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    *position = slot->position;
    if (qpc_time)
        *qpc_time = slot->qpc_time ? slot->qpc_time : query_performance_time_100ns();
    unlock_endpoint(client->endpoint);
    return S_OK;
}
