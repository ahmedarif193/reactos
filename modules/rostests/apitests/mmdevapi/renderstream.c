/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared render conversion, notification, padding and reset tests
 */
#define COBJMACROS
#include <apitest.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "reactos_wavert.h"

static void test_cyclic_notifications(void)
{
    static const struct
    {
        const char *name;
        UINT64 play, write;
        UINT32 playing;
        BOOL advanced, writable;
    } samples[] =
    {
        { "before first completion", 0, 128, 0, FALSE, TRUE },
        { "first completion", 1920, 2048, 1, TRUE, TRUE },
        { "duplicate wake", 1920, 2048, 1, FALSE, TRUE },
        { "two coalesced completions", 2048, 2176, 1, FALSE, TRUE },
        { "recovery after coalescing", 0, 128, 0, TRUE, TRUE },
        { "timeout without progress", 0, 128, 0, FALSE, TRUE },
        { "prefetch into next half", 1856, 1984, 0, FALSE, FALSE },
        { "completion after prefetch", 1920, 2048, 1, TRUE, TRUE },
        { "prefetch around buffer end", 3776, 64, 1, FALSE, FALSE },
        { "write cursor behind play", 2048, 1984, 1, FALSE, FALSE },
        { "recovery around buffer end", 64, 192, 0, TRUE, TRUE },
    };
    struct reactos_wavert_position position;
    UINT32 previous = 0;
    unsigned i;
    BOOL valid;

    for (i = 0; i < ARRAY_SIZE(samples); ++i)
    {
        valid = reactos_wavert_cyclic_position(480, 4, previous,
                                              samples[i].play, samples[i].write, &position);
        ok(valid, "%s: invalid cursor sample\n", samples[i].name);
        if (!valid) continue;
        ok(position.playing == samples[i].playing, "%s: playing half %u\n",
           samples[i].name, position.playing);
        ok(position.advanced == samples[i].advanced, "%s: advanced %d\n",
           samples[i].name, position.advanced);
        ok(position.writable == samples[i].writable, "%s: writable %d\n",
           samples[i].name, position.writable);
        ok(position.released != position.playing, "%s: selected live DMA half\n",
           samples[i].name);
        previous = position.playing;
    }
    ok(!reactos_wavert_cyclic_position(0, 4, 0, 0, 0, &position), "accepted empty period\n");
    ok(!reactos_wavert_cyclic_position(480, 0, 0, 0, 0, &position), "accepted empty frame\n");
    ok(!reactos_wavert_cyclic_position(480, 4, 2, 0, 0, &position), "accepted invalid previous half\n");
    ok(!reactos_wavert_cyclic_position(480, 4, 0, 3840, 0, &position), "accepted invalid play cursor\n");
    ok(!reactos_wavert_cyclic_position(480, 4, 0, 0, 3840, &position), "accepted invalid write cursor\n");
}

static void drain_stream(IAudioClient *client, IAudioClock *clock, HANDLE event,
                         UINT32 capacity, DWORD rate, UINT64 initial_position)
{
    UINT32 padding = capacity;
    UINT64 position = initial_position, previous = initial_position, frequency;
    DWORD deadline = GetTickCount() + 1000, wait;
    HRESULT hr;
    unsigned events = 0;

    hr = IAudioClock_GetFrequency(clock, &frequency);
    ok(hr == S_OK && frequency != 0, "rate %lu: clock frequency %#lx, %I64u\n", rate, hr, frequency);
    do
    {
        wait = WaitForSingleObject(event, 20);
        ok(wait == WAIT_OBJECT_0 || wait == WAIT_TIMEOUT, "rate %lu: wait %lu\n", rate, wait);
        events += wait == WAIT_OBJECT_0;
        hr = IAudioClient_GetCurrentPadding(client, &padding);
        ok(hr == S_OK, "rate %lu: padding %#lx\n", rate, hr);
        if (FAILED(hr)) break;
        ok(padding <= capacity, "rate %lu: padding %u exceeds capacity %u\n", rate, padding, capacity);
        hr = IAudioClock_GetPosition(clock, &position, NULL);
        ok(hr == S_OK, "rate %lu: clock %#lx\n", rate, hr);
        ok(position >= previous, "rate %lu: clock moved backwards\n", rate);
        previous = position;
        if (!padding && position > initial_position) break;
        Sleep(1);
    } while ((LONG)(deadline - GetTickCount()) > 0);
    ok(events != 0, "rate %lu: no render notifications\n", rate);
    ok(padding == 0, "rate %lu: audio did not drain, %u frames remain\n", rate, padding);
    ok(position > initial_position, "rate %lu: audio clock did not advance\n", rate);
}

static void test_format(IMMDevice *device, DWORD rate, WORD bits)
{
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    IAudioClock *clock = NULL;
    WAVEFORMATEX format = { WAVE_FORMAT_PCM, 2, 0, 0, 0, 0, 0 };
    HANDLE event = NULL;
    BYTE *data;
    UINT32 capacity, padding;
    UINT64 initial_position;
    HRESULT hr;
    unsigned i, pass;
    static const UINT32 pieces[] = { 1, 7, 31, 127 };

    format.nSamplesPerSec = rate;
    format.wBitsPerSample = bits;
    format.nBlockAlign = format.nChannels * bits / 8;
    format.nAvgBytesPerSec = rate * format.nBlockAlign;
    trace("render format %lu Hz, %u bits\n", rate, bits);
    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    if (FAILED(hr)) return;
    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 0, 0, &format, NULL);
    ok(hr == S_OK, "rate %lu bits %u: Initialize %#lx\n", rate, bits, hr);
    if (FAILED(hr)) goto done;
    hr = IAudioClient_GetBufferSize(client, &capacity);
    ok(hr == S_OK && capacity > 166, "rate %lu: buffer %#lx, %u\n", rate, hr, capacity);
    if (FAILED(hr) || capacity <= 166) goto done;
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(event != NULL, "CreateEvent failed %lu\n", GetLastError());
    if (!event) goto done;
    hr = IAudioClient_SetEventHandle(client, event);
    ok(hr == S_OK, "SetEventHandle %#lx\n", hr);
    hr = IAudioClient_GetService(client, &IID_IAudioRenderClient, (void **)&render);
    ok(hr == S_OK, "Render service %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IAudioClient_GetService(client, &IID_IAudioClock, (void **)&clock);
    ok(hr == S_OK, "Clock service %#lx\n", hr);
    if (FAILED(hr)) goto done;

    for (pass = 0; pass < 3; ++pass)
    {
        hr = IAudioClock_GetPosition(clock, &initial_position, NULL);
        ok(hr == S_OK, "GetPosition before rendering %#lx\n", hr);
        if (FAILED(hr)) goto done;
        /* Refill a drained full buffer without Reset to preserve fractional
         * phase. Then test uneven small writes after resetting the stream. */
        for (i = 0; i < (pass == 2 ? ARRAY_SIZE(pieces) : 1); ++i)
        {
            UINT32 frames = pass == 2 ? pieces[i] : capacity;
            hr = IAudioRenderClient_GetBuffer(render, frames, &data);
            ok(hr == S_OK, "rate %lu: GetBuffer(%u) %#lx\n", rate, frames, hr);
            if (FAILED(hr)) goto done;
            memset(data, bits == 8 ? 0x80 : 0, frames * format.nBlockAlign);
            hr = IAudioRenderClient_ReleaseBuffer(render, frames, 0);
            ok(hr == S_OK, "rate %lu: ReleaseBuffer(%u) %#lx\n", rate, frames, hr);
            if (FAILED(hr)) goto done;
        }
        hr = IAudioClient_GetCurrentPadding(client, &padding);
        ok(hr == S_OK && padding != 0 && padding <= capacity, "rate %lu: queued padding %#lx, %u\n", rate, hr, padding);
        hr = IAudioClient_Start(client);
        ok(hr == S_OK, "rate %lu: Start %#lx\n", rate, hr);
        if (FAILED(hr)) goto done;
        drain_stream(client, clock, event, capacity, rate, initial_position);
        if (rate == 48000 && pass == 0)
        {
            /* Let the DMA ring underrun while the client stays started, then
             * submit again. Playback, notifications and the clock must recover
             * without a Stop/Start resetting the notification phase. */
            Sleep(120);
            hr = IAudioClock_GetPosition(clock, &initial_position, NULL);
            ok(hr == S_OK, "GetPosition after underrun %#lx\n", hr);
            if (FAILED(hr)) goto done;
            hr = IAudioRenderClient_GetBuffer(render, capacity, &data);
            ok(hr == S_OK, "GetBuffer after underrun %#lx\n", hr);
            if (FAILED(hr)) goto done;
            memset(data, 0, capacity * format.nBlockAlign);
            hr = IAudioRenderClient_ReleaseBuffer(render, capacity, 0);
            ok(hr == S_OK, "ReleaseBuffer after underrun %#lx\n", hr);
            if (FAILED(hr)) goto done;
            drain_stream(client, clock, event, capacity, rate, initial_position);
        }
        hr = IAudioClient_Stop(client);
        ok(hr == S_OK, "rate %lu: Stop %#lx\n", rate, hr);
        if (pass != 0)
        {
            hr = IAudioClient_Reset(client);
            ok(hr == S_OK, "rate %lu: Reset %#lx\n", rate, hr);
        }
        hr = IAudioClient_GetCurrentPadding(client, &padding);
        ok(hr == S_OK && padding == 0, "rate %lu: reset padding %#lx, %u\n", rate, hr, padding);
    }
done:
    if (clock) IAudioClock_Release(clock);
    if (render) IAudioRenderClient_Release(render);
    IAudioClient_Release(client);
    if (event) CloseHandle(event);
}

START_TEST(renderstream)
{
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    HRESULT hr;
    unsigned i;
    static const DWORD rates[] = { 22050, 44100, 48000, 96000 };

    test_cyclic_notifications();
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "CoInitializeEx %#lx\n", hr);
    if (FAILED(hr)) return;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator);
    ok(hr == S_OK, "Create enumerator %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
    ok(hr == S_OK, "An active render endpoint is required, %#lx\n", hr);
    if (FAILED(hr)) goto done;
    for (i = 0; i < ARRAY_SIZE(rates); ++i)
        test_format(device, rates[i], 16);
    test_format(device, 44100, 8);
    test_format(device, 44100, 24);
done:
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    CoUninitialize();
}
