/*
 * Windows Sensor API manager and provider discovery
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "private.h"

struct sensor_manager
{
    ISensorManager ISensorManager_iface;
    LONG refs;
    CRITICAL_SECTION critical_section;
    ISensorManagerEvents *event_sink;
};

enum sensor_filter_kind
{
    SENSOR_FILTER_CATEGORY,
    SENSOR_FILTER_TYPE,
    SENSOR_FILTER_ID,
};

static inline struct sensor_manager *manager_from_ISensorManager(ISensorManager *iface)
{
    return CONTAINING_RECORD(iface, struct sensor_manager, ISensorManager_iface);
}

static BOOL sensor_matches_filter(ISensor *sensor, enum sensor_filter_kind filter_kind, const GUID *filter)
{
    GUID value;
    HRESULT hr;

    if (filter_kind == SENSOR_FILTER_CATEGORY && IsEqualGUID(filter, &SENSOR_CATEGORY_ALL))
        return TRUE;
    switch (filter_kind)
    {
        case SENSOR_FILTER_CATEGORY:
            hr = ISensor_GetCategory(sensor, &value);
            break;
        case SENSOR_FILTER_TYPE:
            hr = ISensor_GetType(sensor, &value);
            break;
        case SENSOR_FILTER_ID:
            hr = ISensor_GetID(sensor, &value);
            break;
        default:
            return FALSE;
    }
    return SUCCEEDED(hr) && IsEqualGUID(&value, filter);
}

static BOOL sensor_provider_information_valid(const REACTOS_SENSOR_PROVIDER_INFORMATION *provider, DWORD bytes)
{
    SIZE_T required_size;
    ULONG index;

    if (bytes < FIELD_OFFSET(REACTOS_SENSOR_PROVIDER_INFORMATION, Channels) ||
        provider->Version != REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION ||
        provider->Size > bytes || provider->Size < FIELD_OFFSET(REACTOS_SENSOR_PROVIDER_INFORMATION, Channels) ||
        provider->ChannelCount > REACTOS_SENSOR_PROVIDER_MAX_CHANNELS)
        return FALSE;
    required_size = FIELD_OFFSET(REACTOS_SENSOR_PROVIDER_INFORMATION, Channels) + provider->ChannelCount * sizeof(provider->Channels[0]);
    if (provider->Size < required_size)
        return FALSE;
    for (index = 0; index < provider->ChannelCount; index++)
    {
        if (!provider->Channels[index].Type || !provider->Channels[index].Unit || IsEqualGUID(&provider->Channels[index].SensorId, &GUID_NULL))
            return FALSE;
    }
    return TRUE;
}

static HRESULT sensor_manager_collect(enum sensor_filter_kind filter_kind, const GUID *filter, ISensorCollection **out)
{
    REACTOS_SENSOR_PROVIDER_INFORMATION provider;
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
    SP_DEVICE_INTERFACE_DATA interface_data;
    ISensorCollection *collection;
    ISensor *sensor;
    HDEVINFO devices;
    HANDLE device;
    DWORD required_size;
    DWORD bytes;
    ULONG count;
    ULONG device_index;
    ULONG channel_index;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!filter)
        return E_INVALIDARG;
    hr = sensor_collection_create(&IID_ISensorCollection, (void **)&collection);
    if (FAILED(hr))
        return hr;
    devices = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_REACTOS_SENSOR_PROVIDER, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE)
    {
        ISensorCollection_Release(collection);
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    for (device_index = 0;; device_index++)
    {
        memset(&interface_data, 0, sizeof(interface_data));
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(devices, NULL, &GUID_DEVINTERFACE_REACTOS_SENSOR_PROVIDER, device_index, &interface_data))
            break;
        required_size = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, NULL, 0, &required_size, NULL);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required_size < sizeof(*detail))
            continue;
        detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required_size);
        if (!detail)
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, detail, required_size, NULL, NULL))
        {
            HeapFree(GetProcessHeap(), 0, detail);
            continue;
        }
        device = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (device == INVALID_HANDLE_VALUE)
        {
            HeapFree(GetProcessHeap(), 0, detail);
            continue;
        }
        memset(&provider, 0, sizeof(provider));
        bytes = 0;
        if (!DeviceIoControl(device, IOCTL_REACTOS_SENSOR_QUERY_PROVIDER, NULL, 0, &provider, sizeof(provider), &bytes, NULL) || !sensor_provider_information_valid(&provider, bytes))
        {
            CloseHandle(device);
            HeapFree(GetProcessHeap(), 0, detail);
            continue;
        }
        CloseHandle(device);
        provider.Manufacturer[REACTOS_SENSOR_PROVIDER_STRING_LENGTH - 1] = 0;
        provider.Model[REACTOS_SENSOR_PROVIDER_STRING_LENGTH - 1] = 0;
        for (channel_index = 0; channel_index < provider.ChannelCount; channel_index++)
        {
            provider.Channels[channel_index].Name[REACTOS_SENSOR_PROVIDER_STRING_LENGTH - 1] = 0;
            hr = sensor_create(detail->DevicePath, &provider, channel_index, &IID_ISensor, (void **)&sensor);
            if (FAILED(hr))
            {
                if (hr == E_INVALIDARG)
                    continue;
                HeapFree(GetProcessHeap(), 0, detail);
                goto done;
            }
            if (sensor_matches_filter(sensor, filter_kind, filter))
            {
                hr = ISensorCollection_Add(collection, sensor);
                if (FAILED(hr))
                {
                    ISensor_Release(sensor);
                    HeapFree(GetProcessHeap(), 0, detail);
                    goto done;
                }
            }
            ISensor_Release(sensor);
        }
        HeapFree(GetProcessHeap(), 0, detail);
    }
    hr = ISensorCollection_GetCount(collection, &count);
    if (FAILED(hr))
        goto done;
    if (!count)
    {
        hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        goto done;
    }
    SetupDiDestroyDeviceInfoList(devices);
    *out = collection;
    return S_OK;

done:
    SetupDiDestroyDeviceInfoList(devices);
    ISensorCollection_Release(collection);
    return hr;
}

static HRESULT WINAPI sensor_manager_QueryInterface(ISensorManager *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_ISensorManager))
        return E_NOINTERFACE;
    *out = iface;
    ISensorManager_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI sensor_manager_AddRef(ISensorManager *iface)
{
    struct sensor_manager *manager = manager_from_ISensorManager(iface);

    return InterlockedIncrement(&manager->refs);
}

static ULONG WINAPI sensor_manager_Release(ISensorManager *iface)
{
    struct sensor_manager *manager = manager_from_ISensorManager(iface);
    ISensorManagerEvents *event_sink;
    ULONG refs;

    refs = InterlockedDecrement(&manager->refs);
    if (!refs)
    {
        EnterCriticalSection(&manager->critical_section);
        event_sink = manager->event_sink;
        manager->event_sink = NULL;
        LeaveCriticalSection(&manager->critical_section);
        if (event_sink)
            ISensorManagerEvents_Release(event_sink);
        DeleteCriticalSection(&manager->critical_section);
        HeapFree(GetProcessHeap(), 0, manager);
    }
    return refs;
}

static HRESULT WINAPI sensor_manager_GetSensorsByCategory(ISensorManager *iface, REFSENSOR_CATEGORY_ID category, ISensorCollection **out)
{
    return sensor_manager_collect(SENSOR_FILTER_CATEGORY, category, out);
}

static HRESULT WINAPI sensor_manager_GetSensorsByType(ISensorManager *iface, REFSENSOR_TYPE_ID type, ISensorCollection **out)
{
    return sensor_manager_collect(SENSOR_FILTER_TYPE, type, out);
}

static HRESULT WINAPI sensor_manager_GetSensorByID(ISensorManager *iface, REFSENSOR_ID sensor_id, ISensor **out)
{
    ISensorCollection *collection;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    hr = sensor_manager_collect(SENSOR_FILTER_ID, sensor_id, &collection);
    if (FAILED(hr))
        return hr;
    hr = ISensorCollection_GetAt(collection, 0, out);
    ISensorCollection_Release(collection);
    return hr;
}

static HRESULT WINAPI sensor_manager_SetEventSink(ISensorManager *iface, ISensorManagerEvents *event_sink)
{
    struct sensor_manager *manager = manager_from_ISensorManager(iface);
    ISensorManagerEvents *old_sink;

    if (event_sink)
        ISensorManagerEvents_AddRef(event_sink);
    EnterCriticalSection(&manager->critical_section);
    old_sink = manager->event_sink;
    manager->event_sink = event_sink;
    LeaveCriticalSection(&manager->critical_section);
    if (old_sink)
        ISensorManagerEvents_Release(old_sink);
    return S_OK;
}

static HRESULT WINAPI sensor_manager_RequestPermissions(ISensorManager *iface, HWND parent, ISensorCollection *sensors, BOOL modal)
{
    return E_ACCESSDENIED;
}

static const ISensorManagerVtbl sensor_manager_vtbl =
{
    sensor_manager_QueryInterface,
    sensor_manager_AddRef,
    sensor_manager_Release,
    sensor_manager_GetSensorsByCategory,
    sensor_manager_GetSensorsByType,
    sensor_manager_GetSensorByID,
    sensor_manager_SetEventSink,
    sensor_manager_RequestPermissions,
};

HRESULT sensor_manager_create(REFIID iid, void **out)
{
    struct sensor_manager *manager;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    manager = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*manager));
    if (!manager)
        return E_OUTOFMEMORY;
    manager->ISensorManager_iface.lpVtbl = &sensor_manager_vtbl;
    manager->refs = 1;
    InitializeCriticalSection(&manager->critical_section);
    hr = ISensorManager_QueryInterface(&manager->ISensorManager_iface, iid, out);
    ISensorManager_Release(&manager->ISensorManager_iface);
    return hr;
}
