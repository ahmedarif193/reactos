/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Torture tests for the Core Audio shared session registry
 */

#include <apitest.h>

#include "sessionharness.h"

#include <strsafe.h>

#define TORTURE_MILLISECONDS    2500
#define TORTURE_READERS         4
#define REGISTER_RACERS         6

static IMMDevice *EndpointMain;
static IMMDevice *EndpointAlt;
static struct reactos_audio_session_id MainId;
static struct reactos_audio_session_snapshot MainSnapshot;
static BOOL MainRegistered;

static const GUID SessionGuidMain =
    {0x2c9c1a10, 0x7f31, 0x4a25, {0x9b, 0x11, 0x63, 0x2d, 0x71, 0x05, 0x00, 0x01}};
static const GUID SessionGuidAlt =
    {0x2c9c1a10, 0x7f31, 0x4a25, {0x9b, 0x11, 0x63, 0x2d, 0x71, 0x05, 0x00, 0x02}};
static const GUID SessionGuidRace =
    {0x2c9c1a10, 0x7f31, 0x4a25, {0x9b, 0x11, 0x63, 0x2d, 0x71, 0x05, 0x00, 0x03}};

static ULONG FloatBits(float Value)
{
    ULONG Bits;

    CopyMemory(&Bits, &Value, sizeof(Bits));
    return Bits;
}

static float BitsToFloat(ULONG Bits)
{
    float Value;

    CopyMemory(&Value, &Bits, sizeof(Value));
    return Value;
}

static void CreateEndpoints(void)
{
    WCHAR Id[MAX_PATH];

    TestEndpointId(Id, ARRAYSIZE(Id), L"main", GetCurrentProcessId());
    EndpointMain = TestEndpointCreate(Id);
    TestEndpointId(Id, ARRAYSIZE(Id), L"alt", GetCurrentProcessId());
    EndpointAlt = TestEndpointCreate(Id);
    ok(EndpointMain != NULL && EndpointAlt != NULL, "Unable to create test endpoints\n");
}

static void TestSharedLayout(void)
{
    TEST_SHARED_REGISTRY *Registry;
    ULONG Occupied = 0;
    ULONG i;

    C_ASSERT(sizeof(TEST_SHARED_SESSION) == 2028);
    C_ASSERT(sizeof(TEST_SHARED_REGISTRY) == 16 + 128 * 2028);
    C_ASSERT(FIELD_OFFSET(TEST_SHARED_SESSION, Generation) == 4);
    C_ASSERT(FIELD_OFFSET(TEST_SHARED_SESSION, ChannelVolumes) == 84);
    C_ASSERT(FIELD_OFFSET(TEST_SHARED_REGISTRY, Sessions) == 16);

    Registry = TestRegistryMap();
    ok(Registry != NULL, "The session registry section is absent: %lu\n", GetLastError());
    if (!Registry)
        return;

    ok_eq_hex(Registry->Magic, SESSION_REGISTRY_TEST_MAGIC);
    ok_eq_ulong(Registry->Version, (ULONG)SESSION_REGISTRY_TEST_VERSION);
    ok_eq_ulong(Registry->Size, (ULONG)sizeof(TEST_SHARED_REGISTRY));
    ok_eq_ulong(Registry->Capacity, (ULONG)SESSION_REGISTRY_TEST_CAPACITY);

    for (i = 0; i < SESSION_REGISTRY_TEST_CAPACITY; ++i)
    {
        if (Registry->Sessions[i].Occupied)
            ++Occupied;
    }
    trace("Registry holds %lu of %u records\n", Occupied,
          (unsigned)SESSION_REGISTRY_TEST_CAPACITY);

    TestRegistryUnmap(Registry);
}

static void TestRegisterContract(void)
{
    struct reactos_audio_session_snapshot Snapshot;
    struct reactos_audio_session_id Id, Again;
    WCHAR ModulePath[MAX_PATH];
    HRESULT hr;
    UINT32 i;

    hr = reactos_audio_session_register(NULL, &SessionGuidMain, 2, &Id, &Snapshot);
    ok_hr(hr, E_POINTER);
    hr = reactos_audio_session_register(EndpointMain, &SessionGuidMain, 2, NULL, &Snapshot);
    ok_hr(hr, E_POINTER);
    hr = reactos_audio_session_register(EndpointMain, &SessionGuidMain, 2, &Id, NULL);
    ok_hr(hr, E_POINTER);

    hr = reactos_audio_session_register(EndpointMain, &SessionGuidMain,
                                        TEST_TUPLE_CHANNELS, &MainId, &MainSnapshot);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    MainRegistered = TRUE;
    ok(MainId.slot < SESSION_REGISTRY_TEST_CAPACITY, "slot %u out of range\n", MainId.slot);
    ok(!IsEqualGUID(&MainId.instance_guid, &GUID_NULL), "instance guid is null\n");
    ok_eq_ulong(MainSnapshot.process_id, GetCurrentProcessId());
    ok_eq_uint(MainSnapshot.channel_count, TEST_TUPLE_CHANNELS);
    ok(MainSnapshot.master_volume == 1.0f, "master volume %f, expected 1.0\n",
       MainSnapshot.master_volume);
    ok_eq_bool(MainSnapshot.mute, FALSE);
    ok_eq_ulong(MainSnapshot.state, (ULONG)AudioSessionStateInactive);
    ok((MainSnapshot.generation & 1) == 0, "generation %ld is odd\n", MainSnapshot.generation);
    ok(MainSnapshot.generation != 0, "generation is zero\n");
    ok(IsEqualGUID(&MainSnapshot.session_guid, &SessionGuidMain), "session guid mismatch\n");
    ok(!IsEqualGUID(&MainSnapshot.grouping_param, &GUID_NULL), "grouping param is null\n");
    for (i = 0; i < TEST_TUPLE_CHANNELS; ++i)
        ok(MainSnapshot.channel_volumes[i] == 1.0f,
           "channel %u volume %f, expected 1.0\n", i, MainSnapshot.channel_volumes[i]);

    if (GetModuleFileNameW(NULL, ModulePath, ARRAYSIZE(ModulePath)))
        ok_eq_wstr(MainSnapshot.process_path, ModulePath);

    hr = reactos_audio_session_register(EndpointMain, &SessionGuidMain, 2, &Again, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_uint(Again.slot, MainId.slot);
    ok(IsEqualGUID(&Again.instance_guid, &MainId.instance_guid), "instance guid changed\n");
    ok_eq_uint(Snapshot.channel_count, TEST_TUPLE_CHANNELS);

    hr = reactos_audio_session_register(EndpointAlt, &SessionGuidAlt, 2, &Again, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_uint(Snapshot.channel_count, 2);
    hr = reactos_audio_session_register(EndpointAlt, &SessionGuidAlt, 5, &Again, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_uint(Snapshot.channel_count, 5);
    hr = reactos_audio_session_register(EndpointAlt, &SessionGuidAlt, 3, &Again, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_uint(Snapshot.channel_count, 5);
    hr = reactos_audio_session_register(EndpointAlt, &SessionGuidAlt,
                                        REACTOS_AUDIO_SESSION_MAX_CHANNELS * 4,
                                        &Again, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_uint(Snapshot.channel_count, REACTOS_AUDIO_SESSION_MAX_CHANNELS);

    hr = reactos_audio_session_register(EndpointAlt, &SessionGuidMain, 2, &Again, &Snapshot);
    ok_hr(hr, S_OK);
    ok(Again.slot != MainId.slot, "a distinct endpoint reused slot %u\n", Again.slot);

    hr = reactos_audio_session_register(EndpointMain, &SessionGuidAlt, 2, &Again, &Snapshot);
    ok_hr(hr, S_OK);
    ok(Again.slot != MainId.slot, "a distinct session guid reused slot %u\n", Again.slot);

    hr = reactos_audio_session_register(EndpointMain, NULL, 2, &Again, &Snapshot);
    ok_hr(hr, S_OK);
    ok(IsEqualGUID(&Snapshot.session_guid, &GUID_NULL), "a null guid was not mapped to GUID_NULL\n");
}

static void TestReadContract(void)
{
    struct reactos_audio_session_snapshot Snapshot;
    struct reactos_audio_session_id Bogus;
    HRESULT hr;

    if (!MainRegistered)
        return;

    hr = reactos_audio_session_read(NULL, 0, &Snapshot);
    ok_hr(hr, E_POINTER);
    hr = reactos_audio_session_read(&MainId, 0, NULL);
    ok_hr(hr, E_POINTER);

    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok(Snapshot.generation != 0, "generation is zero\n");
    ok((Snapshot.generation & 1) == 0, "generation %ld is odd\n", Snapshot.generation);

    hr = reactos_audio_session_read(&MainId, Snapshot.generation, &Snapshot);
    ok_hr(hr, S_FALSE);

    hr = reactos_audio_session_read(&MainId, Snapshot.generation ^ 2, &Snapshot);
    ok_hr(hr, S_OK);

    Bogus = MainId;
    Bogus.slot = SESSION_REGISTRY_TEST_CAPACITY;
    hr = reactos_audio_session_read(&Bogus, 0, &Snapshot);
    ok_hr(hr, AUDCLNT_E_DEVICE_INVALIDATED);

    Bogus = MainId;
    Bogus.slot = 0xffffffff;
    hr = reactos_audio_session_read(&Bogus, 0, &Snapshot);
    ok_hr(hr, AUDCLNT_E_DEVICE_INVALIDATED);

    Bogus = MainId;
    CoCreateGuid(&Bogus.instance_guid);
    hr = reactos_audio_session_read(&Bogus, 0, &Snapshot);
    ok_hr(hr, AUDCLNT_E_DEVICE_INVALIDATED);

    hr = reactos_audio_session_set_master(&Bogus, 0.5f);
    ok_hr(hr, AUDCLNT_E_DEVICE_INVALIDATED);
    hr = reactos_audio_session_set_mute(&Bogus, TRUE);
    ok_hr(hr, AUDCLNT_E_DEVICE_INVALIDATED);
    hr = reactos_audio_session_set_state(&Bogus, AudioSessionStateActive);
    ok_hr(hr, AUDCLNT_E_DEVICE_INVALIDATED);
    hr = reactos_audio_session_set_channel(&Bogus, 0, 0.5f);
    ok_hr(hr, AUDCLNT_E_DEVICE_INVALIDATED);
    hr = reactos_audio_session_set_strings(&Bogus, L"x", L"y");
    ok_hr(hr, AUDCLNT_E_DEVICE_INVALIDATED);
}

static void TestVolumeLevels(void)
{
    static const ULONG Levels[] =
    {
        0x00000000, 0x80000000, 0x3f800000, 0x3f000000, 0x3dcccccd,
        0x00800000, 0x00000001, 0x7f7fffff, 0xff7fffff, 0xbf800000,
        0x40000000, 0x7f800000, 0xff800000, 0x7fc00000
    };
    struct reactos_audio_session_snapshot Snapshot;
    float Channels[REACTOS_AUDIO_SESSION_MAX_CHANNELS];
    HRESULT hr;
    UINT32 i;

    if (!MainRegistered)
        return;

    for (i = 0; i < ARRAYSIZE(Levels); ++i)
    {
        hr = reactos_audio_session_set_master(&MainId, BitsToFloat(Levels[i]));
        ok_hr(hr, S_OK);
        hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
        ok_hr(hr, S_OK);
        ok_eq_hex(FloatBits(Snapshot.master_volume), Levels[i]);
    }

    hr = reactos_audio_session_set_master(&MainId, 1.0f);
    ok_hr(hr, S_OK);

    for (i = 0; i < ARRAYSIZE(Levels); ++i)
    {
        UINT32 Channel = i % TEST_TUPLE_CHANNELS;
        ULONG Stored;

        hr = reactos_audio_session_set_channel(&MainId, Channel, BitsToFloat(Levels[i]));
        ok_hr(hr, S_OK);
        hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
        ok_hr(hr, S_OK);
        Stored = FloatBits(Snapshot.channel_volumes[Channel]);
        ok_eq_hex(Stored, Levels[i]);
    }

    hr = reactos_audio_session_set_channel(&MainId, TEST_TUPLE_CHANNELS, 0.5f);
    ok_hr(hr, E_INVALIDARG);
    hr = reactos_audio_session_set_channel(&MainId, REACTOS_AUDIO_SESSION_MAX_CHANNELS, 0.5f);
    ok_hr(hr, E_INVALIDARG);
    hr = reactos_audio_session_set_channel(&MainId, 0xffffffff, 0.5f);
    ok_hr(hr, E_INVALIDARG);

    hr = reactos_audio_session_set_channels(&MainId, TEST_TUPLE_CHANNELS, NULL);
    ok_hr(hr, E_POINTER);

    for (i = 0; i < ARRAYSIZE(Channels); ++i)
        Channels[i] = TestTupleChannel(0, i);

    hr = reactos_audio_session_set_channels(&MainId, TEST_TUPLE_CHANNELS - 1, Channels);
    ok_hr(hr, E_INVALIDARG);
    hr = reactos_audio_session_set_channels(&MainId, TEST_TUPLE_CHANNELS + 1, Channels);
    ok_hr(hr, E_INVALIDARG);
    hr = reactos_audio_session_set_channels(&MainId, REACTOS_AUDIO_SESSION_MAX_CHANNELS + 1,
                                            Channels);
    ok_hr(hr, E_INVALIDARG);

    hr = reactos_audio_session_set_channels(&MainId, TEST_TUPLE_CHANNELS, Channels);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    for (i = 0; i < TEST_TUPLE_CHANNELS; ++i)
        ok_eq_hex(FloatBits(Snapshot.channel_volumes[i]), FloatBits(TestTupleChannel(0, i)));

    hr = reactos_audio_session_set_mute(&MainId, 12345);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_bool(Snapshot.mute, TRUE);

    hr = reactos_audio_session_set_mute(&MainId, FALSE);
    ok_hr(hr, S_OK);

    hr = reactos_audio_session_set_state(&MainId, AudioSessionStateExpired);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_ulong(Snapshot.state, (ULONG)AudioSessionStateExpired);
}

static void TestStringLevels(void)
{
    struct reactos_audio_session_snapshot Snapshot;
    WCHAR Long[TEST_ICON_PATH_CCH * 2];
    HRESULT hr;
    UINT32 i;

    if (!MainRegistered)
        return;

    for (i = 0; i < ARRAYSIZE(Long) - 1; ++i)
        Long[i] = L'Z';
    Long[ARRAYSIZE(Long) - 1] = UNICODE_NULL;

    hr = reactos_audio_session_set_strings(&MainId, Long, Long);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_size((SIZE_T)lstrlenW(Snapshot.display_name), (SIZE_T)(TEST_DISPLAY_NAME_CCH - 1));
    ok_eq_size((SIZE_T)lstrlenW(Snapshot.icon_path), (SIZE_T)(TEST_ICON_PATH_CCH - 1));

    hr = reactos_audio_session_set_strings(&MainId, L"display", NULL);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_wstr(Snapshot.display_name, L"display");
    ok_eq_size((SIZE_T)lstrlenW(Snapshot.icon_path), (SIZE_T)(TEST_ICON_PATH_CCH - 1));

    hr = reactos_audio_session_set_strings(&MainId, NULL, L"icon");
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_wstr(Snapshot.display_name, L"display");
    ok_eq_wstr(Snapshot.icon_path, L"icon");

    hr = reactos_audio_session_set_strings(&MainId, NULL, NULL);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_wstr(Snapshot.display_name, L"display");
    ok_eq_wstr(Snapshot.icon_path, L"icon");

    hr = reactos_audio_session_set_strings(&MainId, L"", L"");
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_wstr(Snapshot.display_name, L"");
    ok_eq_wstr(Snapshot.icon_path, L"");
}

static void TestGenerationParity(void)
{
    static const LONG Seeds[] = {0, 1, -1, -2, 0x7ffffffe, 0x7fffffff};
    TEST_SHARED_REGISTRY *Registry;
    TEST_SHARED_SESSION *Session;
    struct reactos_audio_session_snapshot Snapshot;
    HRESULT hr;
    UINT32 i;

    if (!MainRegistered)
        return;

    Registry = TestRegistryMap();
    ok(Registry != NULL, "The session registry section is absent: %lu\n", GetLastError());
    if (!Registry)
        return;

    Session = TestRegistryFind(Registry, &MainId);
    ok(Session != NULL, "The registered record is not visible through the section\n");
    if (!Session)
    {
        TestRegistryUnmap(Registry);
        return;
    }

    for (i = 0; i < ARRAYSIZE(Seeds); ++i)
    {
        LONG Generation;

        InterlockedExchange(&Session->Generation, Seeds[i]);
        hr = reactos_audio_session_set_master(&MainId, 0.25f);
        ok_hr(hr, S_OK);

        Generation = InterlockedCompareExchange(&Session->Generation, 0, 0);
        ok((Generation & 1) == 0, "seed %ld left an odd generation %ld\n",
           Seeds[i], Generation);
        ok(Generation != 0, "seed %ld left a zero generation\n", Seeds[i]);

        hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
        ok_hr(hr, S_OK);
        ok(Snapshot.master_volume == 0.25f, "seed %ld lost the write: %f\n",
           Seeds[i], Snapshot.master_volume);
        ok_eq_long(Snapshot.generation, Generation);
    }

    InterlockedExchange(&Session->Generation, 3);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, HRESULT_FROM_WIN32(ERROR_RETRY));

    InterlockedExchange(&Session->Generation, 3);
    hr = reactos_audio_session_read(&MainId, 3, &Snapshot);
    ok_hr(hr, HRESULT_FROM_WIN32(ERROR_RETRY));

    InterlockedExchange(&Session->Generation, 4);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_long(Snapshot.generation, 4);

    hr = reactos_audio_session_read(&MainId, 4, &Snapshot);
    ok_hr(hr, S_FALSE);

    InterlockedExchange(&Session->Occupied, FALSE);
    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, AUDCLNT_E_DEVICE_INVALIDATED);
    InterlockedExchange(&Session->Occupied, TRUE);

    ok(TestRegistryFind(Registry, &MainId) == Session,
       "The record was taken over while it was marked free\n");

    hr = reactos_audio_session_read(&MainId, 0, &Snapshot);
    ok_hr(hr, S_OK);

    TestRegistryUnmap(Registry);
}

typedef struct _TORTURE_CONTEXT
{
    struct reactos_audio_session_id Id;
    volatile LONG Stop;
    volatile LONG Writes;
    LONG Reads;
    LONG Retries;
    LONG Invalidated;
    LONG Unexpected;
    LONG TornDisplayName;
    LONG TornIconPath;
    LONG TornChannels;
    LONG TornMaster;
    LONG OddGeneration;
    LONG BadState;
    LONG BadMute;
} TORTURE_CONTEXT;

static DWORD WINAPI TortureWriteStrings(LPVOID Parameter)
{
    TORTURE_CONTEXT *Context = Parameter;
    WCHAR Display[TEST_DISPLAY_NAME_CCH];
    WCHAR Icon[TEST_ICON_PATH_CCH];
    UINT Index = 0;

    while (!Context->Stop)
    {
        TestTupleString(Index, TEST_DISPLAY_BASE, Display, ARRAYSIZE(Display));
        TestTupleString(Index, TEST_ICON_BASE, Icon, ARRAYSIZE(Icon));
        if (SUCCEEDED(reactos_audio_session_set_strings(&Context->Id, Display, Icon)))
            InterlockedIncrement(&Context->Writes);
        Index = (Index + 1) % TEST_TUPLE_COUNT;
    }

    return 0;
}

static DWORD WINAPI TortureWriteChannels(LPVOID Parameter)
{
    TORTURE_CONTEXT *Context = Parameter;
    float Levels[TEST_TUPLE_CHANNELS];
    UINT Index = 0;
    UINT i;

    while (!Context->Stop)
    {
        for (i = 0; i < ARRAYSIZE(Levels); ++i)
            Levels[i] = TestTupleChannel(Index, i);
        if (SUCCEEDED(reactos_audio_session_set_channels(&Context->Id,
                                                         ARRAYSIZE(Levels), Levels)))
            InterlockedIncrement(&Context->Writes);
        Index = (Index + 1) % TEST_TUPLE_COUNT;
    }

    return 0;
}

static DWORD WINAPI TortureWriteScalars(LPVOID Parameter)
{
    TORTURE_CONTEXT *Context = Parameter;
    UINT Index = 0;

    while (!Context->Stop)
    {
        reactos_audio_session_set_master(&Context->Id, TestTupleVolume(Index));
        reactos_audio_session_set_mute(&Context->Id, (Index & 1) != 0);
        reactos_audio_session_set_state(&Context->Id, (AudioSessionState)(Index % 3));
        InterlockedIncrement(&Context->Writes);
        Index = (Index + 1) % TEST_TUPLE_COUNT;
    }

    return 0;
}

static DWORD WINAPI TortureRead(LPVOID Parameter)
{
    TORTURE_CONTEXT *Context = Parameter;
    struct reactos_audio_session_snapshot Snapshot;
    LONG Reads = 0, Retries = 0, Invalidated = 0, Unexpected = 0;
    LONG TornDisplayName = 0, TornIconPath = 0, TornChannels = 0, TornMaster = 0;
    LONG OddGeneration = 0, BadState = 0, BadMute = 0;

    while (!Context->Stop)
    {
        HRESULT hr = reactos_audio_session_read(&Context->Id, 0, &Snapshot);
        UINT Index;

        if (hr == HRESULT_FROM_WIN32(ERROR_RETRY))
        {
            ++Retries;
            continue;
        }
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
        {
            ++Invalidated;
            continue;
        }
        if (hr != S_OK)
        {
            ++Unexpected;
            continue;
        }

        ++Reads;

        if ((Snapshot.generation & 1) != 0 || Snapshot.generation == 0)
            ++OddGeneration;
        if (!TestTupleStringValid(Snapshot.display_name, TEST_DISPLAY_NAME_CCH,
                                  TEST_DISPLAY_BASE, NULL))
            ++TornDisplayName;
        if (!TestTupleStringValid(Snapshot.icon_path, TEST_ICON_PATH_CCH,
                                  TEST_ICON_BASE, NULL))
            ++TornIconPath;
        if (!TestTupleVolumeValid(Snapshot.master_volume, NULL))
            ++TornMaster;
        if (Snapshot.state > AudioSessionStateExpired)
            ++BadState;
        if (Snapshot.mute != FALSE && Snapshot.mute != TRUE)
            ++BadMute;

        if (!TestTupleChannelsValid(Snapshot.channel_volumes, Snapshot.channel_count, &Index))
            ++TornChannels;
    }

    InterlockedExchangeAdd(&Context->Reads, Reads);
    InterlockedExchangeAdd(&Context->Retries, Retries);
    InterlockedExchangeAdd(&Context->Invalidated, Invalidated);
    InterlockedExchangeAdd(&Context->Unexpected, Unexpected);
    InterlockedExchangeAdd(&Context->TornDisplayName, TornDisplayName);
    InterlockedExchangeAdd(&Context->TornIconPath, TornIconPath);
    InterlockedExchangeAdd(&Context->TornChannels, TornChannels);
    InterlockedExchangeAdd(&Context->TornMaster, TornMaster);
    InterlockedExchangeAdd(&Context->OddGeneration, OddGeneration);
    InterlockedExchangeAdd(&Context->BadState, BadState);
    InterlockedExchangeAdd(&Context->BadMute, BadMute);
    return 0;
}

static void TestSeqlockTorture(void)
{
    TORTURE_CONTEXT Context;
    HANDLE Threads[TORTURE_READERS + 3];
    WCHAR Display[TEST_DISPLAY_NAME_CCH];
    WCHAR Icon[TEST_ICON_PATH_CCH];
    float Levels[TEST_TUPLE_CHANNELS];
    DWORD Count = 0;
    HRESULT hr;
    UINT i;

    if (!MainRegistered)
        return;

    ZeroMemory(&Context, sizeof(Context));
    Context.Id = MainId;

    TestTupleString(0, TEST_DISPLAY_BASE, Display, ARRAYSIZE(Display));
    TestTupleString(0, TEST_ICON_BASE, Icon, ARRAYSIZE(Icon));
    for (i = 0; i < ARRAYSIZE(Levels); ++i)
        Levels[i] = TestTupleChannel(0, i);

    hr = reactos_audio_session_set_strings(&Context.Id, Display, Icon);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_set_channels(&Context.Id, ARRAYSIZE(Levels), Levels);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_set_master(&Context.Id, TestTupleVolume(0));
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_set_mute(&Context.Id, FALSE);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_set_state(&Context.Id, AudioSessionStateInactive);
    ok_hr(hr, S_OK);

    Threads[Count++] = CreateThread(NULL, 0, TortureWriteStrings, &Context, 0, NULL);
    Threads[Count++] = CreateThread(NULL, 0, TortureWriteChannels, &Context, 0, NULL);
    Threads[Count++] = CreateThread(NULL, 0, TortureWriteScalars, &Context, 0, NULL);
    for (i = 0; i < TORTURE_READERS; ++i)
        Threads[Count++] = CreateThread(NULL, 0, TortureRead, &Context, 0, NULL);

    for (i = 0; i < Count; ++i)
        ok(Threads[i] != NULL, "CreateThread %u failed: %lu\n", i, GetLastError());

    Sleep(TORTURE_MILLISECONDS);
    InterlockedExchange(&Context.Stop, TRUE);

    for (i = 0; i < Count; ++i)
    {
        if (!Threads[i])
            continue;
        ok_eq_ulong(WaitForSingleObject(Threads[i], 30000), (ULONG)WAIT_OBJECT_0);
        CloseHandle(Threads[i]);
    }

    trace("Torture: %ld reads, %ld writes, %ld retries\n",
          Context.Reads, Context.Writes, Context.Retries);

    ok(Context.Reads > 0, "The readers never completed a snapshot\n");
    ok(Context.Writes > 0, "The writers never completed an update\n");
    ok_eq_long(Context.Invalidated, 0);
    ok_eq_long(Context.Unexpected, 0);
    ok_eq_long(Context.OddGeneration, 0);
    ok_eq_long(Context.TornDisplayName, 0);
    ok_eq_long(Context.TornIconPath, 0);
    ok_eq_long(Context.TornChannels, 0);
    ok_eq_long(Context.TornMaster, 0);
    ok_eq_long(Context.BadState, 0);
    ok_eq_long(Context.BadMute, 0);
}

typedef struct _RACE_CONTEXT
{
    HANDLE Start;
    GUID Guid;
    struct reactos_audio_session_id Id;
    HRESULT Result;
} RACE_CONTEXT;

static DWORD WINAPI RaceRegister(LPVOID Parameter)
{
    RACE_CONTEXT *Context = Parameter;
    struct reactos_audio_session_snapshot Snapshot;

    WaitForSingleObject(Context->Start, INFINITE);
    Context->Result = reactos_audio_session_register(EndpointAlt, &Context->Guid,
                                                     TEST_TUPLE_CHANNELS,
                                                     &Context->Id, &Snapshot);
    return 0;
}

static void TestConcurrentRegister(void)
{
    RACE_CONTEXT Contexts[REGISTER_RACERS];
    HANDLE Threads[REGISTER_RACERS];
    HANDLE Start;
    UINT i, j;

    Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Start != NULL, "CreateEvent failed: %lu\n", GetLastError());
    if (!Start)
        return;

    for (i = 0; i < REGISTER_RACERS; ++i)
    {
        ZeroMemory(&Contexts[i], sizeof(Contexts[i]));
        Contexts[i].Start = Start;
        Contexts[i].Guid = SessionGuidRace;
        Contexts[i].Result = E_FAIL;
        Threads[i] = CreateThread(NULL, 0, RaceRegister, &Contexts[i], 0, NULL);
        ok(Threads[i] != NULL, "CreateThread %u failed: %lu\n", i, GetLastError());
    }

    SetEvent(Start);
    for (i = 0; i < REGISTER_RACERS; ++i)
    {
        if (!Threads[i])
            continue;
        ok_eq_ulong(WaitForSingleObject(Threads[i], 30000), (ULONG)WAIT_OBJECT_0);
        CloseHandle(Threads[i]);
    }

    for (i = 0; i < REGISTER_RACERS; ++i)
    {
        ok_hr(Contexts[i].Result, S_OK);
        if (FAILED(Contexts[i].Result) || FAILED(Contexts[0].Result))
            continue;
        ok_eq_uint(Contexts[i].Id.slot, Contexts[0].Id.slot);
        ok(IsEqualGUID(&Contexts[i].Id.instance_guid, &Contexts[0].Id.instance_guid),
           "racer %u got a different instance guid\n", i);
    }

    ResetEvent(Start);
    for (i = 0; i < REGISTER_RACERS; ++i)
    {
        ZeroMemory(&Contexts[i], sizeof(Contexts[i]));
        Contexts[i].Start = Start;
        Contexts[i].Result = E_FAIL;
        CoCreateGuid(&Contexts[i].Guid);
        Threads[i] = CreateThread(NULL, 0, RaceRegister, &Contexts[i], 0, NULL);
        ok(Threads[i] != NULL, "CreateThread %u failed: %lu\n", i, GetLastError());
    }

    SetEvent(Start);
    for (i = 0; i < REGISTER_RACERS; ++i)
    {
        if (!Threads[i])
            continue;
        ok_eq_ulong(WaitForSingleObject(Threads[i], 30000), (ULONG)WAIT_OBJECT_0);
        CloseHandle(Threads[i]);
    }

    for (i = 0; i < REGISTER_RACERS; ++i)
    {
        ok_hr(Contexts[i].Result, S_OK);
        if (FAILED(Contexts[i].Result))
            continue;
        for (j = i + 1; j < REGISTER_RACERS; ++j)
        {
            if (FAILED(Contexts[j].Result))
                continue;
            ok(Contexts[i].Id.slot != Contexts[j].Id.slot,
               "racers %u and %u share slot %u\n", i, j, Contexts[i].Id.slot);
        }
    }

    CloseHandle(Start);
}

START_TEST(sessionregistry)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    ok(SUCCEEDED(hr), "CoInitializeEx failed: 0x%08lx\n", hr);

    CreateEndpoints();
    if (!EndpointMain || !EndpointAlt)
        goto done;

    TestRegisterContract();
    TestSharedLayout();
    TestReadContract();
    TestVolumeLevels();
    TestStringLevels();
    TestGenerationParity();
    TestSeqlockTorture();
    TestConcurrentRegister();

done:
    TestEndpointDestroy(EndpointMain);
    TestEndpointDestroy(EndpointAlt);
    if (SUCCEEDED(hr))
        CoUninitialize();
}
