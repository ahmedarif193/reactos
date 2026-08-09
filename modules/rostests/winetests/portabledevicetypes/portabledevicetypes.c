/*
 * Portable Device Types tests
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS

#include <stdarg.h>

#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <initguid.h>
#include <portabledevicetypes.h>

#include <wine/test.h>

static const PROPERTYKEY test_key_1 = {{0x5e4ce45a, 0xd8cb, 0x45ce, {0x9d, 0x06, 0x5b, 0xd5, 0x71, 0x28, 0xa3, 0x1a}}, 1};
static const PROPERTYKEY test_key_2 = {{0x5e4ce45a, 0xd8cb, 0x45ce, {0x9d, 0x06, 0x5b, 0xd5, 0x71, 0x28, 0xa3, 0x1a}}, 2};
static const PROPERTYKEY test_key_3 = {{0x5e4ce45a, 0xd8cb, 0x45ce, {0x9d, 0x06, 0x5b, 0xd5, 0x71, 0x28, 0xa3, 0x1a}}, 3};
static const PROPERTYKEY test_key_4 = {{0x5e4ce45a, 0xd8cb, 0x45ce, {0x9d, 0x06, 0x5b, 0xd5, 0x71, 0x28, 0xa3, 0x1a}}, 4};
static const PROPERTYKEY test_key_value = {{0xf23644f6, 0x14f1, 0x49cf, {0x85, 0xa6, 0x5f, 0xa1, 0x64, 0x95, 0x24, 0x4c}}, 17};
static const GUID test_guid = {0xd6634264, 0x5a17, 0x4b41, {0xa7, 0x20, 0xe2, 0x17, 0x44, 0xe5, 0x5c, 0xb4}};

#ifdef PORTABLEDEVICETYPES_DIRECT
HRESULT portable_device_values_create(REFIID iid, void **out);
HRESULT portable_device_key_collection_create(REFIID iid, void **out);
HRESULT portable_device_propvariant_collection_create(REFIID iid, void **out);
HRESULT portable_device_values_collection_create(REFIID iid, void **out);

static HRESULT create_test_instance(REFCLSID clsid, REFIID iid, void **out)
{
    if (IsEqualCLSID(clsid, &CLSID_PortableDeviceValues))
        return portable_device_values_create(iid, out);
    if (IsEqualCLSID(clsid, &CLSID_PortableDeviceKeyCollection))
        return portable_device_key_collection_create(iid, out);
    if (IsEqualCLSID(clsid, &CLSID_PortableDevicePropVariantCollection))
        return portable_device_propvariant_collection_create(iid, out);
    if (IsEqualCLSID(clsid, &CLSID_PortableDeviceValuesCollection))
        return portable_device_values_collection_create(iid, out);
    return CLASS_E_CLASSNOTAVAILABLE;
}
#else
static HRESULT create_test_instance(REFCLSID clsid, REFIID iid, void **out)
{
    return CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, iid, out);
}
#endif

static BOOL property_key_equal(const PROPERTYKEY *left, const PROPERTYKEY *right)
{
    return left->pid == right->pid && IsEqualGUID(&left->fmtid, &right->fmtid);
}

static void test_values(void)
{
    IPortableDeviceValues *values;
    PROPVARIANT input, variant;
    PROPERTYKEY key;
    WCHAR string[] = L"voltage";
    WCHAR *string_result = NULL;
    BYTE buffer[] = {1, 2, 3, 4};
    BYTE *buffer_result = NULL;
    DWORD count, buffer_size;
    ULONG unsigned_value;
    LONG signed_value;
    FLOAT float_value;
    BOOL bool_value;
    WCHAR *converted_string;
    GUID guid;
    HRESULT hr;

    hr = create_test_instance(&CLSID_PortableDeviceValues, &IID_IPortableDeviceValues, (void **)&values);
    ok(hr == S_OK, "CoCreateInstance returned %#lx.\n", hr);
    if (FAILED(hr))
        return;

    count = 0xdeadbeef;
    hr = IPortableDeviceValues_GetCount(values, &count);
    ok(hr == S_OK, "GetCount returned %#lx.\n", hr);
    ok(count == 0, "Expected 0 values, got %lu.\n", count);

    unsigned_value = 0xdeadbeef;
    hr = IPortableDeviceValues_GetUnsignedIntegerValue(values, &test_key_1, &unsigned_value);
    ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "Missing value returned %#lx.\n", hr);
    ok(unsigned_value == 0xdeadbeef, "Missing value changed the output to %lu.\n", unsigned_value);

    hr = IPortableDeviceValues_SetUnsignedIntegerValue(values, &test_key_1, 42);
    ok(hr == S_OK, "SetUnsignedIntegerValue returned %#lx.\n", hr);
    hr = IPortableDeviceValues_GetUnsignedIntegerValue(values, &test_key_1, &unsigned_value);
    ok(hr == S_OK, "GetUnsignedIntegerValue returned %#lx.\n", hr);
    ok(unsigned_value == 42, "Expected 42, got %lu.\n", unsigned_value);

    hr = IPortableDeviceValues_GetSignedIntegerValue(values, &test_key_1, &signed_value);
    ok(hr == S_OK && signed_value == 42, "Compatible typed getter returned %#lx, value %ld.\n", hr, signed_value);
    float_value = -1.0f;
    hr = IPortableDeviceValues_GetFloatValue(values, &test_key_1, &float_value);
    ok(hr == S_OK && float_value == 42.0f, "UI4 to float returned %#lx, value %.1f.\n", hr, float_value);
    bool_value = FALSE;
    hr = IPortableDeviceValues_GetBoolValue(values, &test_key_1, &bool_value);
    ok(hr == S_OK && bool_value, "UI4 to bool returned %#lx, value %d.\n", hr, bool_value);
    converted_string = NULL;
    hr = IPortableDeviceValues_GetStringValue(values, &test_key_1, &converted_string);
    ok(hr == S_OK && converted_string && !lstrcmpW(converted_string, L"42"), "UI4 to string returned %#lx with unexpected value.\n", hr);
    CoTaskMemFree(converted_string);

    hr = IPortableDeviceValues_SetStringValue(values, &test_key_2, string);
    ok(hr == S_OK, "SetStringValue returned %#lx.\n", hr);
    string[0] = 'X';
    hr = IPortableDeviceValues_GetStringValue(values, &test_key_2, &string_result);
    ok(hr == S_OK, "GetStringValue returned %#lx.\n", hr);
    ok(string_result && !lstrcmpW(string_result, L"voltage"), "Unexpected copied string.\n");
    CoTaskMemFree(string_result);

    hr = IPortableDeviceValues_SetBufferValue(values, &test_key_3, buffer, sizeof(buffer));
    ok(hr == S_OK, "SetBufferValue returned %#lx.\n", hr);
    buffer[0] = 0xff;
    hr = IPortableDeviceValues_GetBufferValue(values, &test_key_3, &buffer_result, &buffer_size);
    ok(hr == S_OK, "GetBufferValue returned %#lx.\n", hr);
    ok(buffer_size == 4, "Expected 4 bytes, got %lu.\n", buffer_size);
    ok(buffer_result && buffer_result[0] == 1 && buffer_result[3] == 4, "Unexpected copied buffer.\n");
    CoTaskMemFree(buffer_result);

    hr = IPortableDeviceValues_SetBufferValue(values, &test_key_3, buffer, 0);
    ok(hr == S_OK, "Empty buffer returned %#lx.\n", hr);
    buffer_result = (BYTE *)0xdeadbeef;
    buffer_size = 0xdeadbeef;
    hr = IPortableDeviceValues_GetBufferValue(values, &test_key_3, &buffer_result, &buffer_size);
    ok(hr == S_OK && !buffer_size, "Empty GetBufferValue returned %#lx, size %lu.\n", hr, buffer_size);
    ok(buffer_result != NULL, "Expected an allocated empty buffer.\n");
    CoTaskMemFree(buffer_result);

    hr = IPortableDeviceValues_SetBufferValue(values, &test_key_3, NULL, 0);
    ok(hr == E_POINTER, "NULL empty SetBufferValue returned %#lx.\n", hr);
    hr = IPortableDeviceValues_SetBufferValue(values, &test_key_3, NULL, 1);
    ok(hr == E_POINTER, "NULL SetBufferValue returned %#lx.\n", hr);

    PropVariantInit(&input);
    input.vt = VT_VECTOR | VT_UI1;
    input.caub.cElems = 0;
    input.caub.pElems = buffer;
    hr = IPortableDeviceValues_SetValue(values, &test_key_3, &input);
    ok(hr == S_OK, "Empty generic vector returned %#lx.\n", hr);
    PropVariantInit(&variant);
    hr = IPortableDeviceValues_GetValue(values, &test_key_3, &variant);
    ok(hr == S_OK && variant.vt == (VT_VECTOR | VT_UI1), "Empty generic vector GetValue returned %#lx, type %#x.\n", hr, variant.vt);
    ok(!variant.caub.cElems && variant.caub.pElems, "Expected an allocated empty generic vector.\n");
    PropVariantClear(&variant);

    input.caub.cElems = 1;
    input.caub.pElems = NULL;
    hr = IPortableDeviceValues_SetValue(values, &test_key_3, &input);
    ok(hr == E_INVALIDARG, "Invalid vector overwrite returned %#lx.\n", hr);
    PropVariantInit(&variant);
    hr = IPortableDeviceValues_GetValue(values, &test_key_3, &variant);
    ok(hr == S_OK && variant.vt == VT_EMPTY, "Failed overwrite left type %#x, hr %#lx.\n", variant.vt, hr);
    PropVariantClear(&variant);
    hr = IPortableDeviceValues_SetValue(values, &test_key_4, &input);
    ok(hr == E_INVALIDARG, "Invalid new vector returned %#lx.\n", hr);
    hr = IPortableDeviceValues_GetCount(values, &count);
    ok(hr == S_OK && count == 3, "Invalid insert changed count, hr %#lx count %lu.\n", hr, count);

    hr = IPortableDeviceValues_SetStringValue(values, &test_key_4, L"invalid");
    ok(hr == S_OK, "SetStringValue returned %#lx.\n", hr);
    unsigned_value = 0xdeadbeef;
    hr = IPortableDeviceValues_GetUnsignedIntegerValue(values, &test_key_4, &unsigned_value);
    ok(hr == TYPE_E_TYPEMISMATCH, "Invalid numeric conversion returned %#lx.\n", hr);
    ok(!unsigned_value, "Invalid numeric conversion returned %lu.\n", unsigned_value);
    hr = IPortableDeviceValues_RemoveValue(values, &test_key_4);
    ok(hr == S_OK, "RemoveValue returned %#lx.\n", hr);
    unsigned_value = 0xdeadbeef;
    hr = IPortableDeviceValues_GetUnsignedIntegerValue(values, &test_key_4, &unsigned_value);
    ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "Missing converted value returned %#lx.\n", hr);
    ok(unsigned_value == 0xdeadbeef, "Missing converted value changed output to %lu.\n", unsigned_value);

    hr = IPortableDeviceValues_SetGuidValue(values, &test_key_3, &test_guid);
    ok(hr == S_OK, "SetGuidValue returned %#lx.\n", hr);
    hr = IPortableDeviceValues_GetGuidValue(values, &test_key_3, &guid);
    ok(hr == S_OK, "GetGuidValue returned %#lx.\n", hr);
    ok(IsEqualGUID(&guid, &test_guid), "Unexpected GUID.\n");

    hr = IPortableDeviceValues_SetKeyValue(values, &test_key_3, &test_key_value);
    ok(hr == S_OK, "SetKeyValue returned %#lx.\n", hr);
    PropVariantInit(&variant);
    hr = IPortableDeviceValues_GetValue(values, &test_key_3, &variant);
    ok(hr == S_OK, "GetValue returned %#lx.\n", hr);
    ok(variant.vt == VT_UNKNOWN, "Expected VT_UNKNOWN, got %#x.\n", variant.vt);
    PropVariantClear(&variant);
    hr = IPortableDeviceValues_GetKeyValue(values, &test_key_3, &key);
    ok(hr == S_OK, "GetKeyValue returned %#lx.\n", hr);
    ok(property_key_equal(&key, &test_key_value), "Unexpected property key.\n");

    hr = IPortableDeviceValues_GetCount(values, &count);
    ok(hr == S_OK, "GetCount returned %#lx.\n", hr);
    ok(count == 3, "Expected 3 values, got %lu.\n", count);
    PropVariantInit(&variant);
    hr = IPortableDeviceValues_GetAt(values, 0, &key, &variant);
    ok(hr == S_OK, "GetAt returned %#lx.\n", hr);
    ok(property_key_equal(&key, &test_key_1), "Unexpected first key.\n");
    ok(variant.vt == VT_UI4 && variant.ulVal == 42, "Unexpected first value.\n");
    PropVariantClear(&variant);

    hr = IPortableDeviceValues_RemoveValue(values, &test_key_1);
    ok(hr == S_OK, "RemoveValue returned %#lx.\n", hr);
    hr = IPortableDeviceValues_RemoveValue(values, &test_key_1);
    ok(hr == S_OK, "Missing RemoveValue returned %#lx.\n", hr);
    hr = IPortableDeviceValues_Clear(values);
    ok(hr == S_OK, "Clear returned %#lx.\n", hr);
    hr = IPortableDeviceValues_GetCount(values, &count);
    ok(hr == S_OK && !count, "Expected empty values, hr %#lx count %lu.\n", hr, count);

    IPortableDeviceValues_Release(values);
}

static void test_key_collection(void)
{
    IPortableDeviceKeyCollection *collection;
    PROPERTYKEY key;
    DWORD count;
    HRESULT hr;

    hr = create_test_instance(&CLSID_PortableDeviceKeyCollection, &IID_IPortableDeviceKeyCollection, (void **)&collection);
    ok(hr == S_OK, "CoCreateInstance returned %#lx.\n", hr);
    if (FAILED(hr))
        return;

    hr = IPortableDeviceKeyCollection_Add(collection, &test_key_1);
    ok(hr == S_OK, "Add returned %#lx.\n", hr);
    hr = IPortableDeviceKeyCollection_Add(collection, &test_key_2);
    ok(hr == S_OK, "Add returned %#lx.\n", hr);
    hr = IPortableDeviceKeyCollection_GetCount(collection, &count);
    ok(hr == S_OK && count == 2, "Expected 2 keys, hr %#lx count %lu.\n", hr, count);
    hr = IPortableDeviceKeyCollection_GetAt(collection, 1, &key);
    ok(hr == S_OK, "GetAt returned %#lx.\n", hr);
    ok(property_key_equal(&key, &test_key_2), "Unexpected second key.\n");
    hr = IPortableDeviceKeyCollection_RemoveAt(collection, 0);
    ok(hr == S_OK, "RemoveAt returned %#lx.\n", hr);
    hr = IPortableDeviceKeyCollection_GetAt(collection, 0, &key);
    ok(hr == S_OK && property_key_equal(&key, &test_key_2), "Unexpected remaining key.\n");
    hr = IPortableDeviceKeyCollection_Clear(collection);
    ok(hr == S_OK, "Clear returned %#lx.\n", hr);
    hr = IPortableDeviceKeyCollection_GetCount(collection, &count);
    ok(hr == S_OK && !count, "Expected empty collection, hr %#lx count %lu.\n", hr, count);
    IPortableDeviceKeyCollection_Release(collection);
}

static void test_propvariant_collection(void)
{
    IPortableDevicePropVariantCollection *collection;
    PROPVARIANT input, output;
    BYTE byte = 1;
    VARTYPE type;
    DWORD count;
    HRESULT hr;

    hr = create_test_instance(&CLSID_PortableDevicePropVariantCollection, &IID_IPortableDevicePropVariantCollection, (void **)&collection);
    ok(hr == S_OK, "CoCreateInstance returned %#lx.\n", hr);
    if (FAILED(hr))
        return;

    type = 0xdead;
    hr = IPortableDevicePropVariantCollection_GetType(collection, &type);
    ok(hr == E_UNEXPECTED && type == 0xdead, "Expected E_UNEXPECTED, hr %#lx type %#x.\n", hr, type);
    hr = IPortableDevicePropVariantCollection_ChangeType(collection, VT_UI4);
    ok(hr == S_OK, "Empty ChangeType returned %#lx.\n", hr);
    type = 0xdead;
    hr = IPortableDevicePropVariantCollection_GetType(collection, &type);
    ok(hr == E_UNEXPECTED && type == 0xdead, "Empty ChangeType set type %#x, hr %#lx.\n", type, hr);

    PropVariantInit(&input);
    input.vt = VT_VECTOR | VT_UI1;
    input.caub.cElems = 0;
    input.caub.pElems = &byte;
    hr = IPortableDevicePropVariantCollection_Add(collection, &input);
    ok(hr == S_OK, "Empty vector Add returned %#lx.\n", hr);
    PropVariantInit(&output);
    hr = IPortableDevicePropVariantCollection_GetAt(collection, 0, &output);
    ok(hr == S_OK && output.vt == VT_EMPTY, "Empty vector GetAt returned %#lx, type %#x.\n", hr, output.vt);
    PropVariantClear(&output);
    input.caub.cElems = 1;
    input.caub.pElems = NULL;
    hr = IPortableDevicePropVariantCollection_Add(collection, &input);
    ok(hr == E_INVALIDARG, "NULL vector Add returned %#lx.\n", hr);
    hr = IPortableDevicePropVariantCollection_GetCount(collection, &count);
    ok(hr == S_OK && count == 1, "Invalid Add changed count, hr %#lx count %lu.\n", hr, count);
    hr = IPortableDevicePropVariantCollection_RemoveAt(collection, 0);
    ok(hr == S_OK, "RemoveAt returned %#lx.\n", hr);
    type = 0xdead;
    hr = IPortableDevicePropVariantCollection_GetType(collection, &type);
    ok(hr == E_UNEXPECTED && type == 0xdead, "Removing the last item left type %#x, hr %#lx.\n", type, hr);

    PropVariantInit(&input);
    input.vt = VT_UI4;
    input.ulVal = 42;
    hr = IPortableDevicePropVariantCollection_Add(collection, &input);
    ok(hr == S_OK, "Add returned %#lx.\n", hr);
    input.ulVal = 84;
    hr = IPortableDevicePropVariantCollection_Add(collection, &input);
    ok(hr == S_OK, "Add returned %#lx.\n", hr);
    hr = IPortableDevicePropVariantCollection_GetCount(collection, &count);
    ok(hr == S_OK && count == 2, "Expected 2 values, hr %#lx count %lu.\n", hr, count);
    hr = IPortableDevicePropVariantCollection_GetType(collection, &type);
    ok(hr == S_OK && type == VT_UI4, "Expected VT_UI4, hr %#lx type %#x.\n", hr, type);
    PropVariantInit(&output);
    hr = IPortableDevicePropVariantCollection_GetAt(collection, 0, &output);
    ok(hr == S_OK && output.vt == VT_UI4 && output.ulVal == 42, "Unexpected first value, hr %#lx.\n", hr);
    PropVariantClear(&output);

    input.vt = VT_LPWSTR;
    input.pwszVal = L"17";
    hr = IPortableDevicePropVariantCollection_Add(collection, &input);
    ok(hr == S_OK, "Convertible Add returned %#lx.\n", hr);
    PropVariantInit(&output);
    hr = IPortableDevicePropVariantCollection_GetAt(collection, 2, &output);
    ok(hr == S_OK && output.vt == VT_UI4 && output.ulVal == 17, "Converted Add returned %#lx, type %#x, value %lu.\n", hr, output.vt, output.ulVal);
    PropVariantClear(&output);
    input.vt = VT_CLSID;
    input.puuid = (GUID *)&test_guid;
    hr = IPortableDevicePropVariantCollection_Add(collection, &input);
    ok(hr == TYPE_E_TYPEMISMATCH, "Unconvertible Add returned %#lx.\n", hr);
    hr = IPortableDevicePropVariantCollection_GetCount(collection, &count);
    ok(hr == S_OK && count == 3, "Unconvertible Add changed count, hr %#lx count %lu.\n", hr, count);

    hr = IPortableDevicePropVariantCollection_ChangeType(collection, VT_LPWSTR);
    ok(hr == S_OK, "ChangeType returned %#lx.\n", hr);
    PropVariantInit(&output);
    hr = IPortableDevicePropVariantCollection_GetAt(collection, 0, &output);
    ok(hr == S_OK && output.vt == VT_LPWSTR, "Unexpected converted type, hr %#lx type %#x.\n", hr, output.vt);
    ok(output.pwszVal && !lstrcmpW(output.pwszVal, L"42"), "Unexpected converted value.\n");
    PropVariantClear(&output);

    hr = IPortableDevicePropVariantCollection_RemoveAt(collection, 0);
    ok(hr == S_OK, "RemoveAt returned %#lx.\n", hr);
    hr = IPortableDevicePropVariantCollection_Clear(collection);
    ok(hr == S_OK, "Clear returned %#lx.\n", hr);
    type = 0xdead;
    hr = IPortableDevicePropVariantCollection_GetType(collection, &type);
    ok(hr == E_UNEXPECTED && type == 0xdead, "Expected E_UNEXPECTED after Clear, hr %#lx type %#x.\n", hr, type);

    input.vt = VT_LPWSTR;
    input.pwszVal = L"invalid";
    hr = IPortableDevicePropVariantCollection_Add(collection, &input);
    ok(hr == S_OK, "String Add returned %#lx.\n", hr);
    input.pwszVal = L"42";
    hr = IPortableDevicePropVariantCollection_Add(collection, &input);
    ok(hr == S_OK, "String Add returned %#lx.\n", hr);
    hr = IPortableDevicePropVariantCollection_ChangeType(collection, VT_UI4);
    ok(hr == TYPE_E_TYPEMISMATCH, "Failed ChangeType returned %#lx.\n", hr);
    hr = IPortableDevicePropVariantCollection_GetCount(collection, &count);
    ok(hr == S_OK && count == 2, "Failed ChangeType changed count, hr %#lx count %lu.\n", hr, count);
    hr = IPortableDevicePropVariantCollection_GetType(collection, &type);
    ok(hr == S_OK && type == VT_EMPTY, "Failed ChangeType left collection type %#x, hr %#lx.\n", type, hr);
    PropVariantInit(&output);
    hr = IPortableDevicePropVariantCollection_GetAt(collection, 0, &output);
    ok(hr == S_OK && output.vt == VT_EMPTY, "Failed item conversion returned %#lx, type %#x.\n", hr, output.vt);
    PropVariantClear(&output);
    PropVariantInit(&output);
    hr = IPortableDevicePropVariantCollection_GetAt(collection, 1, &output);
    ok(hr == S_OK && output.vt == VT_LPWSTR && output.pwszVal && !lstrcmpW(output.pwszVal, L"42"), "Unvisited item changed, hr %#lx type %#x.\n", hr, output.vt);
    PropVariantClear(&output);
    input.vt = VT_UI4;
    input.ulVal = 7;
    hr = IPortableDevicePropVariantCollection_Add(collection, &input);
    ok(hr == S_OK, "Add after failed ChangeType returned %#lx.\n", hr);
    PropVariantInit(&output);
    hr = IPortableDevicePropVariantCollection_GetAt(collection, 2, &output);
    ok(hr == S_OK && output.vt == VT_EMPTY, "Add after failed ChangeType returned %#lx, type %#x.\n", hr, output.vt);
    PropVariantClear(&output);
    hr = IPortableDevicePropVariantCollection_Clear(collection);
    ok(hr == S_OK, "Clear returned %#lx.\n", hr);

    PropVariantInit(&input);
    hr = IPortableDevicePropVariantCollection_Add(collection, &input);
    ok(hr == S_OK, "VT_EMPTY Add returned %#lx.\n", hr);
    hr = IPortableDevicePropVariantCollection_GetType(collection, &type);
    ok(hr == S_OK && type == VT_EMPTY, "VT_EMPTY collection type is %#x, hr %#lx.\n", type, hr);
    hr = IPortableDevicePropVariantCollection_RemoveAt(collection, 0);
    ok(hr == S_OK, "RemoveAt returned %#lx.\n", hr);
    type = 0xdead;
    hr = IPortableDevicePropVariantCollection_GetType(collection, &type);
    ok(hr == E_UNEXPECTED && type == 0xdead, "Removing VT_EMPTY left type %#x, hr %#lx.\n", type, hr);
    IPortableDevicePropVariantCollection_Release(collection);
}

static void test_values_collection(void)
{
    IPortableDeviceValuesCollection *collection;
    IPortableDeviceValues *values, *result;
    DWORD count;
    HRESULT hr;

    hr = create_test_instance(&CLSID_PortableDeviceValuesCollection, &IID_IPortableDeviceValuesCollection, (void **)&collection);
    ok(hr == S_OK, "CoCreateInstance returned %#lx.\n", hr);
    if (FAILED(hr))
        return;
    hr = create_test_instance(&CLSID_PortableDeviceValues, &IID_IPortableDeviceValues, (void **)&values);
    ok(hr == S_OK, "CoCreateInstance returned %#lx.\n", hr);
    if (FAILED(hr))
    {
        IPortableDeviceValuesCollection_Release(collection);
        return;
    }

    hr = IPortableDeviceValuesCollection_Add(collection, values);
    ok(hr == S_OK, "Add returned %#lx.\n", hr);
    hr = IPortableDeviceValuesCollection_GetCount(collection, &count);
    ok(hr == S_OK && count == 1, "Expected 1 value set, hr %#lx count %lu.\n", hr, count);
    result = NULL;
    hr = IPortableDeviceValuesCollection_GetAt(collection, 0, &result);
    ok(hr == S_OK, "GetAt returned %#lx.\n", hr);
    ok(result == values, "Expected the inserted interface pointer.\n");
    if (result)
        IPortableDeviceValues_Release(result);
    hr = IPortableDeviceValuesCollection_RemoveAt(collection, 0);
    ok(hr == S_OK, "RemoveAt returned %#lx.\n", hr);
    IPortableDeviceValues_Release(values);
    IPortableDeviceValuesCollection_Release(collection);
}

START_TEST(portabledevicetypes)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    ok(hr == S_OK || hr == S_FALSE, "CoInitializeEx returned %#lx.\n", hr);
    if (FAILED(hr))
        return;

    test_values();
    test_key_collection();
    test_propvariant_collection();
    test_values_collection();
    CoUninitialize();
}
