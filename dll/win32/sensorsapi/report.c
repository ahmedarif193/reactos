/*
 * Windows Sensor API data reports
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "private.h"

struct sensor_data_report
{
    ISensorDataReport ISensorDataReport_iface;
    LONG refs;
    PROPERTYKEY data_key;
    double scale;
    REACTOS_SENSOR_READING reading;
    FILETIME timestamp;
};

static inline struct sensor_data_report *report_from_ISensorDataReport(ISensorDataReport *iface)
{
    return CONTAINING_RECORD(iface, struct sensor_data_report, ISensorDataReport_iface);
}

static HRESULT WINAPI sensor_data_report_QueryInterface(ISensorDataReport *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_ISensorDataReport))
        return E_NOINTERFACE;
    *out = iface;
    ISensorDataReport_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI sensor_data_report_AddRef(ISensorDataReport *iface)
{
    struct sensor_data_report *report = report_from_ISensorDataReport(iface);

    return InterlockedIncrement(&report->refs);
}

static ULONG WINAPI sensor_data_report_Release(ISensorDataReport *iface)
{
    struct sensor_data_report *report = report_from_ISensorDataReport(iface);
    ULONG refs;

    refs = InterlockedDecrement(&report->refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, report);
    return refs;
}

static HRESULT WINAPI sensor_data_report_GetTimestamp(ISensorDataReport *iface, SYSTEMTIME *timestamp)
{
    struct sensor_data_report *report = report_from_ISensorDataReport(iface);

    if (!timestamp)
        return E_POINTER;
    if (!FileTimeToSystemTime(&report->timestamp, timestamp))
        return sensor_hresult_from_last_error();
    return S_OK;
}

static HRESULT WINAPI sensor_data_report_GetSensorValue(ISensorDataReport *iface, REFPROPERTYKEY key, PROPVARIANT *value)
{
    struct sensor_data_report *report = report_from_ISensorDataReport(iface);

    if (!key || !value)
        return E_POINTER;
    PropVariantInit(value);
    if (IsEqualPropertyKey(*key, SENSOR_DATA_TYPE_TIMESTAMP))
    {
        value->vt = VT_FILETIME;
        value->filetime = report->timestamp;
        return S_OK;
    }
    if (!IsEqualPropertyKey(*key, report->data_key))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    value->vt = VT_R8;
    value->dblVal = report->reading.Value * report->scale;
    return S_OK;
}

static HRESULT WINAPI sensor_data_report_GetSensorValues(ISensorDataReport *iface, IPortableDeviceKeyCollection *keys, IPortableDeviceValues **out)
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
        item_hr = sensor_data_report_GetSensorValue(iface, &key, &value);
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

static const ISensorDataReportVtbl sensor_data_report_vtbl =
{
    sensor_data_report_QueryInterface,
    sensor_data_report_AddRef,
    sensor_data_report_Release,
    sensor_data_report_GetTimestamp,
    sensor_data_report_GetSensorValue,
    sensor_data_report_GetSensorValues,
};

HRESULT sensor_data_report_create(const PROPERTYKEY *key, double scale, const REACTOS_SENSOR_READING *reading, REFIID iid, void **out)
{
    struct sensor_data_report *report;
    ULARGE_INTEGER timestamp;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!key || !reading)
        return E_INVALIDARG;
    report = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*report));
    if (!report)
        return E_OUTOFMEMORY;
    report->ISensorDataReport_iface.lpVtbl = &sensor_data_report_vtbl;
    report->refs = 1;
    report->data_key = *key;
    report->scale = scale;
    report->reading = *reading;
    timestamp.QuadPart = reading->Timestamp;
    report->timestamp.dwLowDateTime = timestamp.LowPart;
    report->timestamp.dwHighDateTime = timestamp.HighPart;
    hr = ISensorDataReport_QueryInterface(&report->ISensorDataReport_iface, iid, out);
    ISensorDataReport_Release(&report->ISensorDataReport_iface);
    return hr;
}
