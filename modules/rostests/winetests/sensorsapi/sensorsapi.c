/*
 * Windows Sensor API tests
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS

#include <windows.h>
#include <initguid.h>
#include <sensorsapi.h>
#include <sensors.h>

#include <wine/test.h>

struct mock_sensor
{
    ISensor ISensor_iface;
    LONG refs;
    GUID id;
};

static ISensorVtbl mock_sensor_vtbl;
static const GUID missing_sensor_id = {0xd6634264, 0x5a17, 0x4b41, {0xa7, 0x20, 0xe2, 0x17, 0x44, 0xe5, 0x5c, 0xb4}};
static const GUID test_clsid_sensor_collection = {0x79c43adb, 0xa429, 0x469f, {0xaa, 0x39, 0x2f, 0x2b, 0x74, 0xb7, 0x59, 0x37}};
static const GUID test_clsid_sensor = {0xe97ced00, 0x523a, 0x4133, {0xbf, 0x6f, 0xd3, 0xa2, 0xda, 0xe7, 0xf6, 0xba}};
static const GUID test_clsid_sensor_data_report = {0x4ea9d6ef, 0x694b, 0x4218, {0x88, 0x16, 0xcc, 0xda, 0x8d, 0xa7, 0x4b, 0xba}};
static const GUID it8613_sensor_ids[] =
{
    {0x00f01c21, 0xf5e6, 0x40fc, {0xa5, 0x9b, 0xe8, 0xf3, 0xf0, 0x09, 0x7e, 0x66}},
    {0xd0240722, 0xe0af, 0x467e, {0x85, 0x41, 0x7a, 0x52, 0x98, 0x67, 0xc5, 0xc7}},
    {0xe936d182, 0x0e34, 0x491d, {0x9b, 0xa1, 0x26, 0x52, 0xb5, 0x6e, 0x54, 0x04}},
    {0x1e59e8e8, 0x4d20, 0x4e03, {0x97, 0x22, 0x6c, 0xb1, 0x33, 0xdb, 0xb4, 0x86}},
    {0x4b2211b5, 0xaf5a, 0x473a, {0x90, 0xd7, 0xa6, 0x34, 0x45, 0x86, 0x1f, 0x74}},
    {0xabbfd503, 0xcc00, 0x4781, {0x90, 0xdd, 0x7b, 0xc6, 0x41, 0x3f, 0x24, 0xa2}},
};
static const ULONG it8613_vin_numbers[] = {0, 1, 2, 4, 5, 7};
static const WCHAR *it8613_sensor_names[] = {L"VCCCORE", L"VCCMEM", L"3.3V", L"VDC", L"VCCGT", L"VSB3V"};
static const double it8613_voltage_minimums[] = {0.3, 0.8, 3.0, 9.0, 0.3, 3.0};
static const double it8613_voltage_maximums[] = {1.6, 1.5, 3.6, 20.0, 1.6, 3.6};

static inline struct mock_sensor *mock_from_ISensor(ISensor *iface)
{
    return CONTAINING_RECORD(iface, struct mock_sensor, ISensor_iface);
}

static HRESULT WINAPI mock_QueryInterface(ISensor *iface, REFIID iid, void **out)
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

static ULONG WINAPI mock_AddRef(ISensor *iface)
{
    return InterlockedIncrement(&mock_from_ISensor(iface)->refs);
}

static ULONG WINAPI mock_Release(ISensor *iface)
{
    return InterlockedDecrement(&mock_from_ISensor(iface)->refs);
}

static HRESULT WINAPI mock_GetID(ISensor *iface, SENSOR_ID *id)
{
    if (!id)
        return E_POINTER;
    *id = mock_from_ISensor(iface)->id;
    return S_OK;
}

static void init_mock_sensor(struct mock_sensor *sensor, const GUID *id)
{
    if (!mock_sensor_vtbl.QueryInterface)
    {
        mock_sensor_vtbl.QueryInterface = mock_QueryInterface;
        mock_sensor_vtbl.AddRef = mock_AddRef;
        mock_sensor_vtbl.Release = mock_Release;
        mock_sensor_vtbl.GetID = mock_GetID;
    }
    memset(sensor, 0, sizeof(*sensor));
    sensor->ISensor_iface.lpVtbl = &mock_sensor_vtbl;
    sensor->refs = 1;
    sensor->id = *id;
}

static void test_collection(void)
{
    struct mock_sensor first;
    struct mock_sensor same_id;
    ISensorCollection *collection = NULL;
    ISensor *sensor;
    ULONG count;
    HRESULT hr;

    init_mock_sensor(&first, &missing_sensor_id);
    init_mock_sensor(&same_id, &missing_sensor_id);
    hr = CoCreateInstance(&test_clsid_sensor_collection, NULL, CLSCTX_INPROC_SERVER, &IID_ISensorCollection, (void **)&collection);
    ok(hr == S_OK, "CoCreateInstance returned %#x.\n", (unsigned int)hr);
    if (FAILED(hr))
        return;
    count = 0xdeadbeef;
    hr = ISensorCollection_GetCount(collection, &count);
    ok(hr == S_OK && !count, "GetCount returned %#x, count %lu.\n", (unsigned int)hr, count);
    sensor = (ISensor *)0xdeadbeef;
    hr = ISensorCollection_GetAt(collection, 0, &sensor);
    ok(hr == E_INVALIDARG && !sensor, "GetAt returned %#x, sensor %p.\n", (unsigned int)hr, sensor);
    hr = ISensorCollection_Add(collection, NULL);
    ok(hr == E_INVALIDARG, "Add(NULL) returned %#x.\n", (unsigned int)hr);
    hr = ISensorCollection_Remove(collection, NULL);
    ok(hr == E_INVALIDARG, "Remove(NULL) returned %#x.\n", (unsigned int)hr);
    hr = ISensorCollection_RemoveByID(collection, &missing_sensor_id);
    ok(hr == E_INVALIDARG, "RemoveByID on an empty collection returned %#x.\n", (unsigned int)hr);
    hr = ISensorCollection_Clear(collection);
    ok(hr == S_OK, "Clear on an empty collection returned %#x.\n", (unsigned int)hr);
    hr = ISensorCollection_Add(collection, &first.ISensor_iface);
    ok(hr == S_OK && first.refs == 2, "Add returned %#x, refs %ld.\n", (unsigned int)hr, first.refs);
    hr = ISensorCollection_Add(collection, &first.ISensor_iface);
    ok(hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) && first.refs == 2, "Duplicate Add returned %#x, refs %ld.\n", (unsigned int)hr, first.refs);
    count = 0xdeadbeef;
    hr = ISensorCollection_GetCount(collection, &count);
    ok(hr == S_OK && count == 1, "GetCount returned %#x, count %lu.\n", (unsigned int)hr, count);
    sensor = NULL;
    hr = ISensorCollection_GetAt(collection, 0, &sensor);
    ok(hr == S_OK && sensor == &first.ISensor_iface && first.refs == 3, "GetAt returned %#x, sensor %p, refs %ld.\n", (unsigned int)hr, sensor, first.refs);
    if (sensor)
        ISensor_Release(sensor);
    hr = ISensorCollection_Remove(collection, &same_id.ISensor_iface);
    ok(hr == E_INVALIDARG && first.refs == 2 && same_id.refs == 1, "Identity Remove returned %#x, refs %ld/%ld.\n", (unsigned int)hr, first.refs, same_id.refs);
    hr = ISensorCollection_RemoveByID(collection, &missing_sensor_id);
    ok(hr == S_OK && first.refs == 1, "RemoveByID returned %#x, refs %ld.\n", (unsigned int)hr, first.refs);
    hr = ISensorCollection_GetCount(collection, NULL);
    ok(hr == E_POINTER, "GetCount(NULL) returned %#x.\n", (unsigned int)hr);
    hr = ISensorCollection_GetAt(collection, 0, NULL);
    ok(hr == E_POINTER, "GetAt(NULL) returned %#x.\n", (unsigned int)hr);
    ISensorCollection_Release(collection);
}

static void check_manager_collection(ISensorManager *manager, const GUID *filter, BOOL category)
{
    ISensorCollection *collection = (ISensorCollection *)0xdeadbeef;
    ULONG count = 0;
    HRESULT hr;

    if (category)
        hr = ISensorManager_GetSensorsByCategory(manager, filter, &collection);
    else
        hr = ISensorManager_GetSensorsByType(manager, filter, &collection);
    ok(hr == S_OK || hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "Sensor query returned %#x.\n", (unsigned int)hr);
    if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
    {
        ok(!collection, "Not-found query returned collection %p.\n", collection);
        return;
    }
    ok(!!collection, "Successful query returned no collection.\n");
    if (!collection)
        return;
    hr = ISensorCollection_GetCount(collection, &count);
    ok(hr == S_OK && count, "GetCount returned %#x, count %lu.\n", (unsigned int)hr, count);
    ISensorCollection_Release(collection);
}

static void test_it8613_sensor(ISensorManager *manager)
{
    BOOL required = GetEnvironmentVariableA("ROS_REQUIRE_IT8613", NULL, 0) != 0;
    ULONG found = 0;
    ULONG index;

    for (index = 0; index < ARRAY_SIZE(it8613_sensor_ids); index++)
    {
        IPortableDeviceKeyCollection *fields = NULL;
        ISensorDataReport *report = NULL;
        VARIANT_BOOL supported = VARIANT_FALSE;
        ISensor *sensor = NULL;
        SYSTEMTIME timestamp;
        PROPVARIANT value;
        GUID category;
        GUID type;
        GUID id;
        BSTR name = NULL;
        DWORD count;
        HRESULT hr;

        hr = ISensorManager_GetSensorByID(manager, &it8613_sensor_ids[index], &sensor);
        if (hr != S_OK)
        {
            if (required)
                ok(0, "Required IT8613E VIN%lu sensor is missing, hr %#x.\n", it8613_vin_numbers[index], (unsigned int)hr);
            else
                ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !sensor, "GetSensorByID returned %#x, sensor %p.\n", (unsigned int)hr, sensor);
            continue;
        }
        found++;
        hr = ISensor_GetID(sensor, &id);
        ok(hr == S_OK && IsEqualGUID(&id, &it8613_sensor_ids[index]), "VIN%lu GetID returned %#x.\n", it8613_vin_numbers[index], (unsigned int)hr);
        hr = ISensor_GetCategory(sensor, &category);
        ok(hr == S_OK && IsEqualGUID(&category, &SENSOR_CATEGORY_ELECTRICAL), "VIN%lu GetCategory returned %#x.\n", it8613_vin_numbers[index], (unsigned int)hr);
        hr = ISensor_GetType(sensor, &type);
        ok(hr == S_OK && IsEqualGUID(&type, &SENSOR_TYPE_VOLTAGE), "VIN%lu GetType returned %#x.\n", it8613_vin_numbers[index], (unsigned int)hr);
        hr = ISensor_GetFriendlyName(sensor, &name);
        ok(hr == S_OK && name && name[0], "VIN%lu GetFriendlyName returned %#x, name %ls.\n", it8613_vin_numbers[index], (unsigned int)hr, name ? name : L"(null)");
        if (hr == S_OK && name)
            ok(!lstrcmpW(name, it8613_sensor_names[index]), "VIN%lu name is %ls, expected %ls.\n", it8613_vin_numbers[index], name, it8613_sensor_names[index]);
        hr = ISensor_GetSupportedDataFields(sensor, &fields);
        ok(hr == S_OK && fields, "VIN%lu GetSupportedDataFields returned %#x, fields %p.\n", it8613_vin_numbers[index], (unsigned int)hr, fields);
        if (fields)
        {
            count = 0;
            hr = IPortableDeviceKeyCollection_GetCount(fields, &count);
            ok(hr == S_OK && count == 2, "VIN%lu data field count returned %#x, count %lu.\n", it8613_vin_numbers[index], (unsigned int)hr, count);
            IPortableDeviceKeyCollection_Release(fields);
        }
        hr = ISensor_SupportsDataField(sensor, &SENSOR_DATA_TYPE_VOLTAGE_VOLTS, &supported);
        ok(hr == S_OK && supported == VARIANT_TRUE, "VIN%lu SupportsDataField returned %#x, supported %d.\n", it8613_vin_numbers[index], (unsigned int)hr, supported);
        hr = ISensor_GetData(sensor, &report);
        ok(hr == S_OK && report, "VIN%lu GetData returned %#x, report %p.\n", it8613_vin_numbers[index], (unsigned int)hr, report);
        if (report)
        {
            memset(&timestamp, 0, sizeof(timestamp));
            hr = ISensorDataReport_GetTimestamp(report, &timestamp);
            ok(hr == S_OK && timestamp.wYear >= 2026, "VIN%lu GetTimestamp returned %#x, year %u.\n", it8613_vin_numbers[index], (unsigned int)hr, timestamp.wYear);
            PropVariantInit(&value);
            hr = ISensorDataReport_GetSensorValue(report, &SENSOR_DATA_TYPE_VOLTAGE_VOLTS, &value);
            ok(hr == S_OK && value.vt == VT_R8, "VIN%lu GetSensorValue returned %#x, type %#x.\n", it8613_vin_numbers[index], (unsigned int)hr, value.vt);
            if (hr == S_OK && value.vt == VT_R8)
            {
                ok(value.dblVal >= it8613_voltage_minimums[index] && value.dblVal <= it8613_voltage_maximums[index], "VIN%lu voltage %.6f is outside the expected %.3f-%.3f V range.\n", it8613_vin_numbers[index], value.dblVal, it8613_voltage_minimums[index], it8613_voltage_maximums[index]);
                trace("IT8613_ADC VIN%lu name %ls voltage %.6f V\n", it8613_vin_numbers[index], name ? name : L"(null)", value.dblVal);
            }
            PropVariantClear(&value);
            ISensorDataReport_Release(report);
        }
        SysFreeString(name);
        ISensor_Release(sensor);
    }
    if (!found && !required)
        win_skip("The ITE IT8613E provider is not present.\n");
    if (required)
        ok(found == ARRAY_SIZE(it8613_sensor_ids), "Found %lu of %Iu required IT8613E sensors.\n", found, ARRAY_SIZE(it8613_sensor_ids));
}

static void test_manager(void)
{
    ISensorManager *manager = NULL;
    ISensor *sensor = (ISensor *)0xdeadbeef;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_SensorManager, NULL, CLSCTX_INPROC_SERVER, &IID_ISensorManager, (void **)&manager);
    ok(hr == S_OK, "CoCreateInstance returned %#x.\n", (unsigned int)hr);
    if (FAILED(hr))
        return;
    check_manager_collection(manager, &SENSOR_CATEGORY_ALL, TRUE);
    check_manager_collection(manager, &SENSOR_CATEGORY_ELECTRICAL, TRUE);
    check_manager_collection(manager, &SENSOR_TYPE_VOLTAGE, FALSE);
    hr = ISensorManager_GetSensorByID(manager, &missing_sensor_id, &sensor);
    ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !sensor, "Missing GetSensorByID returned %#x, sensor %p.\n", (unsigned int)hr, sensor);
    hr = ISensorManager_SetEventSink(manager, NULL);
    ok(hr == S_OK, "SetEventSink(NULL) returned %#x.\n", (unsigned int)hr);
    hr = ISensorManager_RequestPermissions(manager, NULL, NULL, FALSE);
    ok(hr == E_ACCESSDENIED, "RequestPermissions returned %#x.\n", (unsigned int)hr);
    test_it8613_sensor(manager);
    ISensorManager_Release(manager);
}

static void test_private_classes(void)
{
    ISensorDataReport *report = (ISensorDataReport *)0xdeadbeef;
    ISensor *sensor = (ISensor *)0xdeadbeef;
    HRESULT hr;

    hr = CoCreateInstance(&test_clsid_sensor, NULL, CLSCTX_INPROC_SERVER, &IID_ISensor, (void **)&sensor);
    ok(hr == REGDB_E_CLASSNOTREG && !sensor, "Sensor creation returned %#x, sensor %p.\n", (unsigned int)hr, sensor);
    hr = CoCreateInstance(&test_clsid_sensor_data_report, NULL, CLSCTX_INPROC_SERVER, &IID_ISensorDataReport, (void **)&report);
    ok(hr == REGDB_E_CLASSNOTREG && !report, "Report creation returned %#x, report %p.\n", (unsigned int)hr, report);
}

START_TEST(sensorsapi)
{
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx returned %#x.\n", (unsigned int)hr);
    if (FAILED(hr))
        return;
    test_collection();
    test_manager();
    test_private_classes();
    CoUninitialize();
}
