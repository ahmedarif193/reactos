/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared scaffolding for the Core Audio session registry tests
 */

#ifndef _MMDEVAPI_APITEST_SESSIONHARNESS_H
#define _MMDEVAPI_APITEST_SESSIONHARNESS_H

#define COBJMACROS

#include <windows.h>
#include <objbase.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

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

C_ASSERT(sizeof(struct reactos_audio_session_id) == 20);
C_ASSERT(sizeof(struct reactos_audio_session_snapshot) == 1480);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, master_volume) == 48);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, channel_volumes) == 56);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, display_name) == 704);

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

#define SESSION_REGISTRY_TEST_MAGIC     0x53534152
#define SESSION_REGISTRY_TEST_VERSION   1
#define SESSION_REGISTRY_TEST_CAPACITY  128

typedef struct _TEST_SHARED_SESSION
{
    volatile LONG Occupied;
    volatile LONG Generation;
    GUID InstanceGuid;
    GUID SessionGuid;
    GUID GroupingParam;
    DWORD ProcessId;
    FILETIME ProcessStartTime;
    DWORD State;
    UINT32 ChannelCount;
    float MasterVolume;
    BOOL Mute;
    float ChannelVolumes[REACTOS_AUDIO_SESSION_MAX_CHANNELS];
    WCHAR EndpointId[MAX_PATH];
    WCHAR ProcessPath[MAX_PATH];
    WCHAR DisplayName[128];
    WCHAR IconPath[MAX_PATH];
} TEST_SHARED_SESSION;

typedef struct _TEST_SHARED_REGISTRY
{
    DWORD Magic;
    DWORD Version;
    DWORD Size;
    DWORD Capacity;
    TEST_SHARED_SESSION Sessions[SESSION_REGISTRY_TEST_CAPACITY];
} TEST_SHARED_REGISTRY;

#define TEST_DISPLAY_NAME_CCH   128
#define TEST_ICON_PATH_CCH      MAX_PATH
#define TEST_DISPLAY_BASE       L'A'
#define TEST_ICON_BASE          L'a'
#define TEST_TUPLE_COUNT        7
#define TEST_TUPLE_CHANNELS     8

IMMDevice *TestEndpointCreate(const WCHAR *EndpointId);
void TestEndpointDestroy(IMMDevice *Endpoint);
void TestEndpointId(WCHAR *Buffer, SIZE_T Length, const WCHAR *Tag, DWORD ProcessId);

TEST_SHARED_REGISTRY *TestRegistryMap(void);
void TestRegistryUnmap(TEST_SHARED_REGISTRY *Registry);
TEST_SHARED_SESSION *TestRegistryFind(TEST_SHARED_REGISTRY *Registry,
                                      const struct reactos_audio_session_id *Id);

void TestTupleString(UINT Index, WCHAR Base, WCHAR *Buffer, SIZE_T Length);
BOOL TestTupleStringValid(const WCHAR *Value, SIZE_T Length, WCHAR Base, UINT *Index);
float TestTupleVolume(UINT Index);
float TestTupleChannel(UINT Index, UINT Channel);
BOOL TestTupleVolumeValid(float Value, UINT *Index);
BOOL TestTupleChannelsValid(const float *Levels, UINT Count, UINT *Index);

void TestEventNames(DWORD ParentProcessId, WCHAR *Ready, WCHAR *Stop, SIZE_T Length);

#endif /* _MMDEVAPI_APITEST_SESSIONHARNESS_H */
