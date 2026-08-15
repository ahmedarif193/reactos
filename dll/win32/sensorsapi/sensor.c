/*
 * Windows Sensor API sensor objects
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "private.h"

struct sensor_kind
{
    const GUID *category;
    const GUID *type;
    const PROPERTYKEY *data_key;
    ULONG unit;
    double scale;
};

struct sensor
{
    ISensor ISensor_iface;
    LONG refs;
    CRITICAL_SECTION critical_section;
    WCHAR *device_path;
    WCHAR *manufacturer;
    WCHAR *model;
    WCHAR *friendly_name;
    REACTOS_SENSOR_CHANNEL_INFORMATION channel;
    ULONG channel_index;
    GUID category;
    GUID type;
    PROPERTYKEY data_key;
    double scale;
    ISensorEvents *event_sink;
};

static inline struct sensor *sensor_from_ISensor(ISensor *iface)
{
    return CONTAINING_RECORD(iface, struct sensor, ISensor_iface);
}

static BOOL sensor_kind_from_channel(const REACTOS_SENSOR_CHANNEL_INFORMATION *channel, struct sensor_kind *kind)
{
    switch (channel->Type)
    {
        case REACTOS_SENSOR_TYPE_VOLTAGE:
            if (channel->Unit != REACTOS_SENSOR_UNIT_MICROVOLTS)
                return FALSE;
            kind->category = &SENSOR_CATEGORY_ELECTRICAL;
            kind->type = &SENSOR_TYPE_VOLTAGE;
            kind->data_key = &SENSOR_DATA_TYPE_VOLTAGE_VOLTS;
            kind->unit = REACTOS_SENSOR_UNIT_MICROVOLTS;
            kind->scale = 0.000001;
            return TRUE;

        case REACTOS_SENSOR_TYPE_TEMPERATURE:
            if (channel->Unit != REACTOS_SENSOR_UNIT_MICRODEGREES_CELSIUS)
                return FALSE;
            kind->category = &SENSOR_CATEGORY_ENVIRONMENTAL;
            kind->type = &SENSOR_TYPE_ENVIRONMENTAL_TEMPERATURE;
            kind->data_key = &SENSOR_DATA_TYPE_TEMPERATURE_CELSIUS;
            kind->unit = REACTOS_SENSOR_UNIT_MICRODEGREES_CELSIUS;
            kind->scale = 0.000001;
            return TRUE;

        case REACTOS_SENSOR_TYPE_FAN_SPEED:
            if (channel->Unit != REACTOS_SENSOR_UNIT_REVOLUTIONS_PER_MINUTE)
                return FALSE;
            kind->category = &SENSOR_CATEGORY_MECHANICAL;
            kind->type = &SENSOR_TYPE_CUSTOM;
            kind->data_key = &SENSOR_DATA_TYPE_CUSTOM_VALUE1;
            kind->unit = REACTOS_SENSOR_UNIT_REVOLUTIONS_PER_MINUTE;
            kind->scale = 1.0;
            return TRUE;

        case REACTOS_SENSOR_TYPE_CURRENT:
            if (channel->Unit != REACTOS_SENSOR_UNIT_MICROAMPS)
                return FALSE;
            kind->category = &SENSOR_CATEGORY_ELECTRICAL;
            kind->type = &SENSOR_TYPE_CURRENT;
            kind->data_key = &SENSOR_DATA_TYPE_CURRENT_AMPS;
            kind->unit = REACTOS_SENSOR_UNIT_MICROAMPS;
            kind->scale = 0.000001;
            return TRUE;

        case REACTOS_SENSOR_TYPE_POWER:
            if (channel->Unit != REACTOS_SENSOR_UNIT_MILLIWATTS)
                return FALSE;
            kind->category = &SENSOR_CATEGORY_ELECTRICAL;
            kind->type = &SENSOR_TYPE_ELECTRICAL_POWER;
            kind->data_key = &SENSOR_DATA_TYPE_ELECTRICAL_POWER_WATTS;
            kind->unit = REACTOS_SENSOR_UNIT_MILLIWATTS;
            kind->scale = 0.001;
            return TRUE;

        default:
            return FALSE;
    }
}

static HRESULT sensor_propvariant_set_guid(PROPVARIANT *value, const GUID *guid)
{
    value->puuid = CoTaskMemAlloc(sizeof(*value->puuid));
    if (!value->puuid)
        return E_OUTOFMEMORY;
    *value->puuid = *guid;
    value->vt = VT_CLSID;
    return S_OK;
}

static HRESULT sensor_propvariant_set_string(PROPVARIANT *value, const WCHAR *string)
{
    SIZE_T size = (lstrlenW(string) + 1) * sizeof(*string);

    value->pwszVal = CoTaskMemAlloc(size);
    if (!value->pwszVal)
        return E_OUTOFMEMORY;
    memcpy(value->pwszVal, string, size);
    value->vt = VT_LPWSTR;
    return S_OK;
}

static HRESULT WINAPI sensor_QueryInterface(ISensor *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_ISensor))
        return E_NOINTERFACE;
    *out = iface;
    ISensor_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI sensor_AddRef(ISensor *iface)
{
    struct sensor *sensor = sensor_from_ISensor(iface);

    return InterlockedIncrement(&sensor->refs);
}

static ULONG WINAPI sensor_Release(ISensor *iface)
{
    struct sensor *sensor = sensor_from_ISensor(iface);
    ISensorEvents *event_sink;
    ULONG refs;

    refs = InterlockedDecrement(&sensor->refs);
    if (!refs)
    {
        EnterCriticalSection(&sensor->critical_section);
        event_sink = sensor->event_sink;
        sensor->event_sink = NULL;
        LeaveCriticalSection(&sensor->critical_section);
        if (event_sink)
            ISensorEvents_Release(event_sink);
        DeleteCriticalSection(&sensor->critical_section);
        HeapFree(GetProcessHeap(), 0, sensor->device_path);
        HeapFree(GetProcessHeap(), 0, sensor->manufacturer);
        HeapFree(GetProcessHeap(), 0, sensor->model);
        HeapFree(GetProcessHeap(), 0, sensor->friendly_name);
        HeapFree(GetProcessHeap(), 0, sensor);
    }
    return refs;
}

static HRESULT WINAPI sensor_GetID(ISensor *iface, SENSOR_ID *id)
{
    struct sensor *sensor = sensor_from_ISensor(iface);

    if (!id)
        return E_POINTER;
    *id = sensor->channel.SensorId;
    return S_OK;
}

static HRESULT WINAPI sensor_GetCategory(ISensor *iface, SENSOR_CATEGORY_ID *category)
{
    struct sensor *sensor = sensor_from_ISensor(iface);

    if (!category)
        return E_POINTER;
    *category = sensor->category;
    return S_OK;
}

static HRESULT WINAPI sensor_GetType(ISensor *iface, SENSOR_TYPE_ID *type)
{
    struct sensor *sensor = sensor_from_ISensor(iface);

    if (!type)
        return E_POINTER;
    *type = sensor->type;
    return S_OK;
}

static HRESULT WINAPI sensor_GetFriendlyName(ISensor *iface, BSTR *friendly_name)
{
    struct sensor *sensor = sensor_from_ISensor(iface);

    if (!friendly_name)
        return E_POINTER;
    *friendly_name = SysAllocString(sensor->friendly_name);
    return *friendly_name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI sensor_GetProperty(ISensor *iface, REFPROPERTYKEY key, PROPVARIANT *value)
{
    struct sensor *sensor = sensor_from_ISensor(iface);

    if (!key || !value)
        return E_POINTER;
    PropVariantInit(value);
    if (IsEqualPropertyKey(*key, SENSOR_PROPERTY_TYPE))
        return sensor_propvariant_set_guid(value, &sensor->type);
    if (IsEqualPropertyKey(*key, SENSOR_PROPERTY_STATE))
    {
        value->vt = VT_UI4;
        value->ulVal = SENSOR_STATE_READY;
        return S_OK;
    }
    if (IsEqualPropertyKey(*key, SENSOR_PROPERTY_PERSISTENT_UNIQUE_ID))
        return sensor_propvariant_set_guid(value, &sensor->channel.SensorId);
    if (IsEqualPropertyKey(*key, SENSOR_PROPERTY_MANUFACTURER))
        return sensor_propvariant_set_string(value, sensor->manufacturer);
    if (IsEqualPropertyKey(*key, SENSOR_PROPERTY_MODEL))
        return sensor_propvariant_set_string(value, sensor->model);
    if (IsEqualPropertyKey(*key, SENSOR_PROPERTY_FRIENDLY_NAME) || IsEqualPropertyKey(*key, SENSOR_PROPERTY_DESCRIPTION))
        return sensor_propvariant_set_string(value, sensor->friendly_name);
    if (IsEqualPropertyKey(*key, SENSOR_PROPERTY_CONNECTION_TYPE))
    {
        value->vt = VT_UI4;
        value->ulVal = SENSOR_CONNECTION_TYPE_PC_INTEGRATED;
        return S_OK;
    }
    if (IsEqualPropertyKey(*key, SENSOR_PROPERTY_MIN_REPORT_INTERVAL) || IsEqualPropertyKey(*key, SENSOR_PROPERTY_CURRENT_REPORT_INTERVAL))
    {
        value->vt = VT_UI4;
        value->ulVal = 0;
        return S_OK;
    }
    if (IsEqualPropertyKey(*key, SENSOR_PROPERTY_DEVICE_PATH))
        return sensor_propvariant_set_string(value, sensor->device_path);
    if (IsEqualPropertyKey(*key, SENSOR_PROPERTY_RESOLUTION))
    {
        value->vt = VT_R8;
        value->dblVal = sensor->channel.Resolution * sensor->scale;
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

static HRESULT WINAPI sensor_GetProperties(ISensor *iface, IPortableDeviceKeyCollection *keys, IPortableDeviceValues **out)
{
    IPortableDeviceValues *values;
    PROPVARIANT value;
    PROPERTYKEY key;
    DWORD count;
    DWORD index;
    HRESULT item_hr;
    HRESULT hr;

    if (!keys || !out)
        return E_POINTER;
    *out = NULL;
    hr = CoCreateInstance(&CLSID_PortableDeviceValues, NULL, CLSCTX_INPROC_SERVER, &IID_IPortableDeviceValues, (void **)&values);
    if (FAILED(hr))
        return hr;
    hr = IPortableDeviceKeyCollection_GetCount(keys, &count);
    if (FAILED(hr))
        goto done;
    for (index = 0; index < count; index++)
    {
        hr = IPortableDeviceKeyCollection_GetAt(keys, index, &key);
        if (FAILED(hr))
            goto done;
        item_hr = sensor_GetProperty(iface, &key, &value);
        if (SUCCEEDED(item_hr))
        {
            hr = IPortableDeviceValues_SetValue(values, &key, &value);
            PropVariantClear(&value);
        }
        else
        {
            hr = IPortableDeviceValues_SetErrorValue(values, &key, item_hr);
        }
        if (FAILED(hr))
            goto done;
    }
    *out = values;
    return S_OK;

done:
    IPortableDeviceValues_Release(values);
    return hr;
}

static HRESULT WINAPI sensor_GetSupportedDataFields(ISensor *iface, IPortableDeviceKeyCollection **out)
{
    struct sensor *sensor = sensor_from_ISensor(iface);
    IPortableDeviceKeyCollection *keys;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    hr = CoCreateInstance(&CLSID_PortableDeviceKeyCollection, NULL, CLSCTX_INPROC_SERVER, &IID_IPortableDeviceKeyCollection, (void **)&keys);
    if (FAILED(hr))
        return hr;
    hr = IPortableDeviceKeyCollection_Add(keys, &SENSOR_DATA_TYPE_TIMESTAMP);
    if (SUCCEEDED(hr))
        hr = IPortableDeviceKeyCollection_Add(keys, &sensor->data_key);
    if (FAILED(hr))
    {
        IPortableDeviceKeyCollection_Release(keys);
        return hr;
    }
    *out = keys;
    return S_OK;
}

static HRESULT WINAPI sensor_SetProperties(ISensor *iface, IPortableDeviceValues *properties, IPortableDeviceValues **out)
{
    IPortableDeviceValues *results;
    PROPVARIANT value;
    PROPERTYKEY key;
    DWORD count;
    DWORD index;
    HRESULT hr;

    if (!properties || !out)
        return E_POINTER;
    *out = NULL;
    hr = CoCreateInstance(&CLSID_PortableDeviceValues, NULL, CLSCTX_INPROC_SERVER, &IID_IPortableDeviceValues, (void **)&results);
    if (FAILED(hr))
        return hr;
    hr = IPortableDeviceValues_GetCount(properties, &count);
    if (FAILED(hr))
        goto done;
    for (index = 0; index < count; index++)
    {
        PropVariantInit(&value);
        hr = IPortableDeviceValues_GetAt(properties, index, &key, &value);
        if (FAILED(hr))
            goto done;
        PropVariantClear(&value);
        hr = IPortableDeviceValues_SetErrorValue(results, &key, E_ACCESSDENIED);
        if (FAILED(hr))
            goto done;
    }
    *out = results;
    return S_OK;

done:
    IPortableDeviceValues_Release(results);
    return hr;
}

static HRESULT WINAPI sensor_SupportsDataField(ISensor *iface, REFPROPERTYKEY key, VARIANT_BOOL *supported)
{
    struct sensor *sensor = sensor_from_ISensor(iface);

    if (!key || !supported)
        return E_POINTER;
    *supported = IsEqualPropertyKey(*key, SENSOR_DATA_TYPE_TIMESTAMP) || IsEqualPropertyKey(*key, sensor->data_key) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT WINAPI sensor_GetState(ISensor *iface, SensorState *state)
{
    if (!state)
        return E_POINTER;
    *state = SENSOR_STATE_READY;
    return S_OK;
}

static HRESULT WINAPI sensor_GetData(ISensor *iface, ISensorDataReport **out)
{
    struct sensor *sensor = sensor_from_ISensor(iface);
    REACTOS_SENSOR_READ_INPUT input;
    REACTOS_SENSOR_READING reading;
    HANDLE device;
    DWORD bytes;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    device = CreateFileW(sensor->device_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (device == INVALID_HANDLE_VALUE)
        return sensor_hresult_from_last_error();
    memset(&input, 0, sizeof(input));
    input.Version = REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION;
    input.Size = sizeof(input);
    input.ChannelIndex = sensor->channel_index;
    memset(&reading, 0, sizeof(reading));
    bytes = 0;
    if (!DeviceIoControl(device, IOCTL_REACTOS_SENSOR_READ_CHANNEL, &input, sizeof(input), &reading, sizeof(reading), &bytes, NULL))
        hr = sensor_hresult_from_last_error();
    else if (bytes < sizeof(reading) || reading.Version != REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION || reading.Size < sizeof(reading) || reading.Size > bytes ||
             !IsEqualGUID(&reading.SensorId, &sensor->channel.SensorId) || reading.Type != sensor->channel.Type || reading.Unit != sensor->channel.Unit)
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    else
        hr = sensor_data_report_create(&sensor->data_key, sensor->scale, &reading, &IID_ISensorDataReport, (void **)out);
    CloseHandle(device);
    return hr;
}

static HRESULT WINAPI sensor_SupportsEvent(ISensor *iface, REFGUID event_guid, VARIANT_BOOL *supported)
{
    if (!event_guid || !supported)
        return E_POINTER;
    *supported = VARIANT_FALSE;
    return S_OK;
}

static HRESULT WINAPI sensor_GetEventInterest(ISensor *iface, GUID **values, ULONG *count)
{
    if (!values || !count)
        return E_POINTER;
    *values = NULL;
    *count = 0;
    return S_OK;
}

static HRESULT WINAPI sensor_SetEventInterest(ISensor *iface, GUID *values, ULONG count)
{
    if (count && !values)
        return E_INVALIDARG;
    return count ? E_INVALIDARG : S_OK;
}

static HRESULT WINAPI sensor_SetEventSink(ISensor *iface, ISensorEvents *event_sink)
{
    struct sensor *sensor = sensor_from_ISensor(iface);
    ISensorEvents *old_sink;

    if (event_sink)
        ISensorEvents_AddRef(event_sink);
    EnterCriticalSection(&sensor->critical_section);
    old_sink = sensor->event_sink;
    sensor->event_sink = event_sink;
    LeaveCriticalSection(&sensor->critical_section);
    if (old_sink)
        ISensorEvents_Release(old_sink);
    return S_OK;
}

static const ISensorVtbl sensor_vtbl =
{
    sensor_QueryInterface,
    sensor_AddRef,
    sensor_Release,
    sensor_GetID,
    sensor_GetCategory,
    sensor_GetType,
    sensor_GetFriendlyName,
    sensor_GetProperty,
    sensor_GetProperties,
    sensor_GetSupportedDataFields,
    sensor_SetProperties,
    sensor_SupportsDataField,
    sensor_GetState,
    sensor_GetData,
    sensor_SupportsEvent,
    sensor_GetEventInterest,
    sensor_SetEventInterest,
    sensor_SetEventSink,
};

HRESULT sensor_create(const WCHAR *device_path, const REACTOS_SENSOR_PROVIDER_INFORMATION *provider, ULONG channel_index, REFIID iid, void **out)
{
    const REACTOS_SENSOR_CHANNEL_INFORMATION *channel;
    struct sensor_kind kind;
    struct sensor *sensor;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!device_path || !provider || channel_index >= provider->ChannelCount)
        return E_INVALIDARG;
    channel = &provider->Channels[channel_index];
    if (!sensor_kind_from_channel(channel, &kind))
        return E_INVALIDARG;
    sensor = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sensor));
    if (!sensor)
        return E_OUTOFMEMORY;
    sensor->device_path = sensor_strdupW(device_path);
    sensor->manufacturer = sensor_strdupW(provider->Manufacturer);
    sensor->model = sensor_strdupW(provider->Model);
    sensor->friendly_name = sensor_strdupW(channel->Name);
    if (!sensor->device_path || !sensor->manufacturer || !sensor->model || !sensor->friendly_name)
    {
        HeapFree(GetProcessHeap(), 0, sensor->device_path);
        HeapFree(GetProcessHeap(), 0, sensor->manufacturer);
        HeapFree(GetProcessHeap(), 0, sensor->model);
        HeapFree(GetProcessHeap(), 0, sensor->friendly_name);
        HeapFree(GetProcessHeap(), 0, sensor);
        return E_OUTOFMEMORY;
    }
    sensor->ISensor_iface.lpVtbl = &sensor_vtbl;
    sensor->refs = 1;
    sensor->channel = *channel;
    sensor->channel_index = channel_index;
    sensor->category = *kind.category;
    sensor->type = *kind.type;
    sensor->data_key = *kind.data_key;
    sensor->scale = kind.scale;
    InitializeCriticalSection(&sensor->critical_section);
    hr = ISensor_QueryInterface(&sensor->ISensor_iface, iid, out);
    ISensor_Release(&sensor->ISensor_iface);
    return hr;
}
