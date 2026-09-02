/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared scaffolding for the Core Audio session registry tests
 */

#include "sessionharness.h"

#include <strsafe.h>

static const WCHAR TestRegistryName[] =
    L"Local\\ReactOS.CoreAudio.SessionRegistry.v1";

typedef struct _TEST_ENDPOINT
{
    IMMDevice IMMDevice_iface;
    LONG Ref;
    WCHAR Id[MAX_PATH];
} TEST_ENDPOINT;

static TEST_ENDPOINT *TestEndpointFromInterface(IMMDevice *iface)
{
    return CONTAINING_RECORD(iface, TEST_ENDPOINT, IMMDevice_iface);
}

static HRESULT STDMETHODCALLTYPE TestEndpointQueryInterface(
    IMMDevice *iface, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMMDevice))
    {
        *ppv = iface;
        IMMDevice_AddRef(iface);
        return S_OK;
    }

    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE TestEndpointAddRef(IMMDevice *iface)
{
    return InterlockedIncrement(&TestEndpointFromInterface(iface)->Ref);
}

static ULONG STDMETHODCALLTYPE TestEndpointRelease(IMMDevice *iface)
{
    TEST_ENDPOINT *Endpoint = TestEndpointFromInterface(iface);
    LONG Ref = InterlockedDecrement(&Endpoint->Ref);

    if (Ref == 0)
        HeapFree(GetProcessHeap(), 0, Endpoint);

    return Ref;
}

static HRESULT STDMETHODCALLTYPE TestEndpointActivate(
    IMMDevice *iface, REFIID iid, DWORD clsctx, PROPVARIANT *params, void **ppv)
{
    if (ppv)
        *ppv = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE TestEndpointOpenPropertyStore(
    IMMDevice *iface, DWORD access, IPropertyStore **store)
{
    if (store)
        *store = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TestEndpointGetId(IMMDevice *iface, WCHAR **id)
{
    TEST_ENDPOINT *Endpoint = TestEndpointFromInterface(iface);
    SIZE_T Size;

    if (!id)
        return E_POINTER;

    Size = (lstrlenW(Endpoint->Id) + 1) * sizeof(WCHAR);
    *id = CoTaskMemAlloc(Size);
    if (!*id)
        return E_OUTOFMEMORY;

    CopyMemory(*id, Endpoint->Id, Size);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE TestEndpointGetState(IMMDevice *iface, DWORD *state)
{
    if (!state)
        return E_POINTER;

    *state = DEVICE_STATE_ACTIVE;
    return S_OK;
}

static const IMMDeviceVtbl TestEndpointVtbl =
{
    TestEndpointQueryInterface,
    TestEndpointAddRef,
    TestEndpointRelease,
    TestEndpointActivate,
    TestEndpointOpenPropertyStore,
    TestEndpointGetId,
    TestEndpointGetState
};

IMMDevice *TestEndpointCreate(const WCHAR *EndpointId)
{
    TEST_ENDPOINT *Endpoint = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                        sizeof(*Endpoint));

    if (!Endpoint)
        return NULL;

    Endpoint->IMMDevice_iface.lpVtbl = &TestEndpointVtbl;
    Endpoint->Ref = 1;
    StringCchCopyW(Endpoint->Id, ARRAYSIZE(Endpoint->Id), EndpointId);
    return &Endpoint->IMMDevice_iface;
}

void TestEndpointDestroy(IMMDevice *Endpoint)
{
    if (Endpoint)
        IMMDevice_Release(Endpoint);
}

void TestEndpointId(WCHAR *Buffer, SIZE_T Length, const WCHAR *Tag, DWORD ProcessId)
{
    StringCchPrintfW(Buffer, Length, L"{apitest}.mmdevapi.%lu.%s", ProcessId, Tag);
}

TEST_SHARED_REGISTRY *TestRegistryMap(void)
{
    TEST_SHARED_REGISTRY *Registry;
    HANDLE Mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE,
                                      TestRegistryName);

    if (!Mapping)
        return NULL;

    Registry = MapViewOfFile(Mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                             sizeof(*Registry));
    CloseHandle(Mapping);
    return Registry;
}

void TestRegistryUnmap(TEST_SHARED_REGISTRY *Registry)
{
    if (Registry)
        UnmapViewOfFile(Registry);
}

TEST_SHARED_SESSION *TestRegistryFind(TEST_SHARED_REGISTRY *Registry,
                                      const struct reactos_audio_session_id *Id)
{
    TEST_SHARED_SESSION *Session;

    if (!Registry || !Id || Id->slot >= SESSION_REGISTRY_TEST_CAPACITY)
        return NULL;

    Session = &Registry->Sessions[Id->slot];
    if (!Session->Occupied || !IsEqualGUID(&Session->InstanceGuid, &Id->instance_guid))
        return NULL;

    return Session;
}

void TestTupleString(UINT Index, WCHAR Base, WCHAR *Buffer, SIZE_T Length)
{
    SIZE_T i;

    for (i = 0; i + 1 < Length; ++i)
        Buffer[i] = (WCHAR)(Base + (Index % TEST_TUPLE_COUNT));
    Buffer[Length - 1] = UNICODE_NULL;
}

BOOL TestTupleStringValid(const WCHAR *Value, SIZE_T Length, WCHAR Base, UINT *Index)
{
    SIZE_T i;
    UINT Slot;

    if (!Value || (SIZE_T)lstrlenW(Value) != Length - 1)
        return FALSE;

    if (Value[0] < Base || Value[0] >= Base + TEST_TUPLE_COUNT)
        return FALSE;

    Slot = (UINT)(Value[0] - Base);
    for (i = 1; i + 1 < Length; ++i)
    {
        if (Value[i] != Value[0])
            return FALSE;
    }

    if (Index)
        *Index = Slot;
    return TRUE;
}

float TestTupleVolume(UINT Index)
{
    return 0.125f * (float)((Index % TEST_TUPLE_COUNT) + 1);
}

float TestTupleChannel(UINT Index, UINT Channel)
{
    UINT Step = (Index % TEST_TUPLE_COUNT) * TEST_TUPLE_CHANNELS + Channel + 1;

    return (float)Step / 64.0f;
}

BOOL TestTupleChannelsValid(const float *Levels, UINT Count, UINT *Index)
{
    UINT Slot, i;

    if (!Levels || Count != TEST_TUPLE_CHANNELS)
        return FALSE;

    for (Slot = 0; Slot < TEST_TUPLE_COUNT; ++Slot)
    {
        for (i = 0; i < Count; ++i)
        {
            if (Levels[i] != TestTupleChannel(Slot, i))
                break;
        }

        if (i == Count)
        {
            if (Index)
                *Index = Slot;
            return TRUE;
        }
    }

    return FALSE;
}

BOOL TestTupleVolumeValid(float Value, UINT *Index)
{
    UINT i;

    for (i = 0; i < TEST_TUPLE_COUNT; ++i)
    {
        if (Value == TestTupleVolume(i))
        {
            if (Index)
                *Index = i;
            return TRUE;
        }
    }

    return FALSE;
}

void TestEventNames(DWORD ParentProcessId, WCHAR *Ready, WCHAR *Stop, SIZE_T Length)
{
    StringCchPrintfW(Ready, Length, L"Local\\mmdevapi_apitest.%lu.ready", ParentProcessId);
    StringCchPrintfW(Stop, Length, L"Local\\mmdevapi_apitest.%lu.stop", ParentProcessId);
}
