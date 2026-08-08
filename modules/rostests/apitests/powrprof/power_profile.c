/*
 * PROJECT:     ReactOS powrprof API tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Modern processor power-scheme and telemetry parity tests
 */

#include <apitest.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <powrprof.h>
#include <powersetting.h>

static const GUID balanced_scheme = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
static const GUID power_saver_scheme = {0xa1841308, 0x3541, 0x4fab, {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};
static const GUID high_performance_scheme = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
static const GUID processor_subgroup = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
static const GUID processor_minimum = {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}};
static const GUID processor_maximum = {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}};
static const GUID processor_autonomous = {0x8baa4a8a, 0x14c6, 0x4451, {0x8e, 0x8b, 0x14, 0xbd, 0xbd, 0x19, 0x75, 0x37}};
static const GUID processor_epp = {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63}};
static const GUID unknown_guid = {0xdeadbeef, 0x1234, 0x5678, {0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78}};

typedef struct
{
    DWORD ac_minimum;
    DWORD dc_minimum;
    DWORD ac_maximum;
    DWORD dc_maximum;
    DWORD ac_autonomous;
    DWORD dc_autonomous;
    DWORD ac_epp;
    DWORD dc_epp;
} SCHEME_EXPECTED_VALUES;

static const SCHEME_EXPECTED_VALUES balanced_expected = {5, 5, 100, 100, 0, 0, 33, 50};
static const SCHEME_EXPECTED_VALUES power_saver_expected = {5, 5, 100, 100, 0, 0, 60, 60};
static const SCHEME_EXPECTED_VALUES high_performance_expected = {100, 5, 100, 100, 0, 0, 0, 0};

static void trace_guid(const char *name, const GUID *guid)
{
    trace("%s=%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n", name, guid->Data1, guid->Data2, guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
}

static void query_scheme_settings(const char *name, const GUID *scheme, const SCHEME_EXPECTED_VALUES *expected)
{
    DWORD ac_min = 0xdeadbeef, dc_min = 0xdeadbeef;
    DWORD ac_max = 0xdeadbeef, dc_max = 0xdeadbeef;
    DWORD ac_autonomous = 0xdeadbeef, dc_autonomous = 0xdeadbeef;
    DWORD ac_epp = 0xdeadbeef, dc_epp = 0xdeadbeef;
    DWORD ret_ac_min, ret_dc_min, ret_ac_max, ret_dc_max, ret_ac_autonomous, ret_dc_autonomous, ret_ac_epp, ret_dc_epp;

    ret_ac_min = PowerReadACValueIndex(NULL, scheme, &processor_subgroup, &processor_minimum, &ac_min);
    ret_dc_min = PowerReadDCValueIndex(NULL, scheme, &processor_subgroup, &processor_minimum, &dc_min);
    ret_ac_max = PowerReadACValueIndex(NULL, scheme, &processor_subgroup, &processor_maximum, &ac_max);
    ret_dc_max = PowerReadDCValueIndex(NULL, scheme, &processor_subgroup, &processor_maximum, &dc_max);
    ret_ac_autonomous = PowerReadACValueIndex(NULL, scheme, &processor_subgroup, &processor_autonomous, &ac_autonomous);
    ret_dc_autonomous = PowerReadDCValueIndex(NULL, scheme, &processor_subgroup, &processor_autonomous, &dc_autonomous);
    ret_ac_epp = PowerReadACValueIndex(NULL, scheme, &processor_subgroup, &processor_epp, &ac_epp);
    ret_dc_epp = PowerReadDCValueIndex(NULL, scheme, &processor_subgroup, &processor_epp, &dc_epp);

    ok(ret_ac_min == ERROR_SUCCESS, "%s AC minimum returned %lu\n", name, ret_ac_min);
    ok(ret_dc_min == ERROR_SUCCESS, "%s DC minimum returned %lu\n", name, ret_dc_min);
    ok(ret_ac_max == ERROR_SUCCESS, "%s AC maximum returned %lu\n", name, ret_ac_max);
    ok(ret_dc_max == ERROR_SUCCESS, "%s DC maximum returned %lu\n", name, ret_dc_max);
    ok(ret_ac_autonomous == ERROR_SUCCESS, "%s AC autonomous mode returned %lu\n", name, ret_ac_autonomous);
    ok(ret_dc_autonomous == ERROR_SUCCESS, "%s DC autonomous mode returned %lu\n", name, ret_dc_autonomous);
    ok(ret_ac_epp == ERROR_SUCCESS, "%s AC EPP returned %lu\n", name, ret_ac_epp);
    ok(ret_dc_epp == ERROR_SUCCESS, "%s DC EPP returned %lu\n", name, ret_dc_epp);
    trace("%s settings ac_min=%lu dc_min=%lu ac_max=%lu dc_max=%lu ac_auto=%lu dc_auto=%lu ac_epp=%lu dc_epp=%lu\n", name, ac_min, dc_min, ac_max, dc_max, ac_autonomous, dc_autonomous, ac_epp, dc_epp);

    if (ret_ac_min == ERROR_SUCCESS && ret_ac_max == ERROR_SUCCESS)
    {
        ok(ac_min <= 100, "%s AC minimum %lu exceeds 100\n", name, ac_min);
        ok(ac_max <= 100, "%s AC maximum %lu exceeds 100\n", name, ac_max);
        ok(ac_min <= ac_max, "%s AC minimum %lu exceeds maximum %lu\n", name, ac_min, ac_max);
    }
    if (ret_dc_min == ERROR_SUCCESS && ret_dc_max == ERROR_SUCCESS)
    {
        ok(dc_min <= 100, "%s DC minimum %lu exceeds 100\n", name, dc_min);
        ok(dc_max <= 100, "%s DC maximum %lu exceeds 100\n", name, dc_max);
        ok(dc_min <= dc_max, "%s DC minimum %lu exceeds maximum %lu\n", name, dc_min, dc_max);
    }
    if (expected)
    {
        if (ret_ac_min == ERROR_SUCCESS)
            ok(ac_min == expected->ac_minimum, "%s AC minimum is %lu, expected %lu\n", name, ac_min, expected->ac_minimum);
        if (ret_dc_min == ERROR_SUCCESS)
            ok(dc_min == expected->dc_minimum, "%s DC minimum is %lu, expected %lu\n", name, dc_min, expected->dc_minimum);
        if (ret_ac_max == ERROR_SUCCESS)
            ok(ac_max == expected->ac_maximum, "%s AC maximum is %lu, expected %lu\n", name, ac_max, expected->ac_maximum);
        if (ret_dc_max == ERROR_SUCCESS)
            ok(dc_max == expected->dc_maximum, "%s DC maximum is %lu, expected %lu\n", name, dc_max, expected->dc_maximum);
        if (ret_ac_autonomous == ERROR_SUCCESS)
            ok(ac_autonomous == expected->ac_autonomous, "%s AC autonomous mode is %lu, expected %lu\n", name, ac_autonomous, expected->ac_autonomous);
        if (ret_dc_autonomous == ERROR_SUCCESS)
            ok(dc_autonomous == expected->dc_autonomous, "%s DC autonomous mode is %lu, expected %lu\n", name, dc_autonomous, expected->dc_autonomous);
        if (ret_ac_epp == ERROR_SUCCESS)
            ok(ac_epp == expected->ac_epp, "%s AC EPP is %lu, expected %lu\n", name, ac_epp, expected->ac_epp);
        if (ret_dc_epp == ERROR_SUCCESS)
            ok(dc_epp == expected->dc_epp, "%s DC EPP is %lu, expected %lu\n", name, dc_epp, expected->dc_epp);
    }
}

static void query_processor_information(const char *name)
{
    PROCESSOR_POWER_INFORMATION *info;
    SYSTEM_INFO system_info;
    NTSTATUS status;
    SIZE_T size;
    DWORD i;

    GetSystemInfo(&system_info);
    size = system_info.dwNumberOfProcessors * sizeof(*info);
    info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    ok(info != NULL, "%s processor information allocation failed\n", name);
    if (!info)
        return;

    status = CallNtPowerInformation(ProcessorInformation, NULL, 0, info, (ULONG)size);
    ok(status == STATUS_SUCCESS, "%s ProcessorInformation returned 0x%08lx\n", name, (ULONG)status);
    if (status == STATUS_SUCCESS)
    {
        for (i = 0; i < system_info.dwNumberOfProcessors; ++i)
        {
            trace("%s cpu=%lu max_mhz=%lu current_mhz=%lu limit_mhz=%lu max_idle=%lu current_idle=%lu\n", name, info[i].Number, info[i].MaxMhz, info[i].CurrentMhz, info[i].MhzLimit, info[i].MaxIdleState, info[i].CurrentIdleState);
            ok(info[i].Number == i, "%s processor %lu reported number %lu\n", name, i, info[i].Number);
            ok(info[i].MaxMhz != 0, "%s processor %lu reported zero MaxMhz\n", name, i);
            ok(info[i].MhzLimit != 0, "%s processor %lu reported zero MhzLimit\n", name, i);
        }
    }
    HeapFree(GetProcessHeap(), 0, info);
}

static BOOL activate_and_query(const char *name, const GUID *scheme, const SCHEME_EXPECTED_VALUES *expected)
{
    GUID *active = NULL;
    DWORD ret;

    ret = PowerSetActiveScheme(NULL, scheme);
    if (ret == ERROR_FILE_NOT_FOUND || ret == ERROR_NOT_SUPPORTED)
    {
        win_skip("%s scheme is unavailable, error %lu\n", name, ret);
        return FALSE;
    }
    ok(ret == ERROR_SUCCESS, "PowerSetActiveScheme(%s) returned %lu\n", name, ret);
    if (ret != ERROR_SUCCESS)
        return FALSE;

    ret = PowerGetActiveScheme(NULL, &active);
    ok(ret == ERROR_SUCCESS, "PowerGetActiveScheme after %s returned %lu\n", name, ret);
    ok(active != NULL, "PowerGetActiveScheme after %s returned NULL\n", name);
    if (ret == ERROR_SUCCESS && active)
    {
        trace_guid(name, active);
        ok(IsEqualGUID(active, scheme), "%s did not become the active scheme\n", name);
    }
    LocalFree(active);
    query_scheme_settings(name, scheme, expected);
    Sleep(500);
    query_processor_information(name);
    return TRUE;
}

static void test_argument_contract(void)
{
    GUID *active = (GUID *)0xdeadbeef;
    DWORD value = 0xdeadbeef;
    DWORD ret;

    ret = PowerSetActiveScheme(NULL, NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerSetActiveScheme(NULL scheme) returned %lu\n", ret);
    ret = PowerReadACValueIndex(NULL, NULL, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerReadACValueIndex(NULL scheme) returned %lu\n", ret);
    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, NULL, &processor_minimum, &value);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerReadACValueIndex(NULL subgroup) returned %lu\n", ret);
    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &processor_subgroup, NULL, &value);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerReadACValueIndex(NULL setting) returned %lu\n", ret);
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, NULL);
    ok(ret == ERROR_SUCCESS, "PowerReadACValueIndex(NULL output) returned %lu\n", ret);

    ret = PowerGetActiveScheme(HKEY_CURRENT_USER, &active);
    ok(ret == ERROR_SUCCESS, "PowerGetActiveScheme(non-NULL root) returned %lu\n", ret);
    ok(active != NULL, "PowerGetActiveScheme(non-NULL root) returned NULL\n");
    if (ret == ERROR_SUCCESS)
        LocalFree(active);
    ret = PowerSetActiveScheme(HKEY_CURRENT_USER, &balanced_scheme);
    ok(ret == ERROR_SUCCESS, "PowerSetActiveScheme(non-NULL root) returned %lu\n", ret);
    ret = PowerSetActiveScheme(NULL, &unknown_guid);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerSetActiveScheme(unknown scheme) returned %lu\n", ret);

    ret = PowerReadACValueIndex(HKEY_CURRENT_USER, &balanced_scheme, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS, "PowerReadACValueIndex(non-NULL root) returned %lu\n", ret);
    ret = PowerReadACValueIndex(NULL, &unknown_guid, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerReadACValueIndex(unknown scheme) returned %lu\n", ret);
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &unknown_guid, &processor_minimum, &value);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerReadACValueIndex(unknown subgroup) returned %lu\n", ret);
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &unknown_guid, &value);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerReadACValueIndex(unknown setting) returned %lu\n", ret);
}

static void test_write_read_contract(void)
{
    DWORD original_ac, original_dc, value;
    DWORD test_ac, test_dc;
    DWORD ret;
    BOOL restore_ac = FALSE, restore_dc = FALSE;

    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, &original_ac);
    ok(ret == ERROR_SUCCESS, "reading original balanced AC minimum returned %lu\n", ret);
    if (ret != ERROR_SUCCESS)
        return;
    ret = PowerReadDCValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, &original_dc);
    ok(ret == ERROR_SUCCESS, "reading original balanced DC minimum returned %lu\n", ret);
    if (ret != ERROR_SUCCESS)
        return;

    test_ac = original_ac == 17 ? 18 : 17;
    test_dc = original_dc == 23 ? 24 : 23;
    ret = PowerWriteACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, test_ac);
    ok(ret == ERROR_SUCCESS, "PowerWriteACValueIndex(valid) returned %lu\n", ret);
    if (ret == ERROR_SUCCESS)
        restore_ac = TRUE;
    ret = PowerWriteDCValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, test_dc);
    ok(ret == ERROR_SUCCESS, "PowerWriteDCValueIndex(valid) returned %lu\n", ret);
    if (ret == ERROR_SUCCESS)
        restore_dc = TRUE;

    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS, "PowerReadACValueIndex(after write) returned %lu\n", ret);
    ok(value == test_ac, "PowerReadACValueIndex(after write) returned %lu, expected %lu\n", value, test_ac);
    value = 0xdeadbeef;
    ret = PowerReadDCValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS, "PowerReadDCValueIndex(after write) returned %lu\n", ret);
    ok(value == test_dc, "PowerReadDCValueIndex(after write) returned %lu, expected %lu\n", value, test_dc);

    ret = PowerWriteACValueIndex(HKEY_CURRENT_USER, &balanced_scheme, &processor_subgroup, &processor_minimum, 10);
    ok(ret == ERROR_SUCCESS, "PowerWriteACValueIndex(non-NULL root) returned %lu\n", ret);
    ret = PowerWriteACValueIndex(NULL, &unknown_guid, &processor_subgroup, &processor_minimum, 10);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerWriteACValueIndex(unknown scheme) returned %lu\n", ret);
    ret = PowerWriteACValueIndex(NULL, &balanced_scheme, &unknown_guid, &processor_minimum, 10);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerWriteACValueIndex(unknown subgroup) returned %lu\n", ret);
    ret = PowerWriteACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &unknown_guid, 10);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerWriteACValueIndex(unknown setting) returned %lu\n", ret);
    ret = PowerWriteACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, 101);
    ok(ret == ERROR_INVALID_DATA, "PowerWriteACValueIndex(value 101) returned %lu\n", ret);

    if (restore_ac)
        ok(PowerWriteACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, original_ac) == ERROR_SUCCESS, "restoring balanced AC minimum failed\n");
    if (restore_dc)
        ok(PowerWriteDCValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, original_dc) == ERROR_SUCCESS, "restoring balanced DC minimum failed\n");
}

START_TEST(power_profile)
{
    GUID *original = NULL;
    DWORD ret;

    ret = PowerGetActiveScheme(NULL, &original);
    ok(ret == ERROR_SUCCESS, "PowerGetActiveScheme returned %lu\n", ret);
    ok(original != NULL, "PowerGetActiveScheme returned NULL\n");
    if (ret != ERROR_SUCCESS || !original)
        return;

    trace_guid("original", original);
    test_argument_contract();
    test_write_read_contract();
    query_scheme_settings("original", original, NULL);
    query_processor_information("original-prime");
    Sleep(500);
    query_processor_information("original");

    activate_and_query("balanced", &balanced_scheme, &balanced_expected);
    activate_and_query("power-saver", &power_saver_scheme, &power_saver_expected);
    activate_and_query("high-performance", &high_performance_scheme, &high_performance_expected);

    ret = PowerSetActiveScheme(NULL, original);
    ok(ret == ERROR_SUCCESS, "restoring the original power scheme returned %lu\n", ret);
    LocalFree(original);
}
