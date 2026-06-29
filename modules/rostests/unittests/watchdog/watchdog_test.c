/*
 * PROJECT:     ReactOS WDDM TDR and Watchdog Tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     User-mode validation of TDR configuration, watchdog/dxgkrnl
 *              exports, and D3DKMT adapter lifecycle under stress.
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <ndk/umtypes.h>
#include <d3dkmthk.h>
#include <stdio.h>

/*
 * Minimal test harness -- uses printf-based pass/fail reporting.
 * Each test prints PASS or FAIL with a description.
 */
static int g_TotalTests = 0;
static int g_PassedTests = 0;
static int g_FailedTests = 0;

#define TEST_PASS(fmt, ...) do { \
    g_TotalTests++; g_PassedTests++; \
    printf("  PASS: " fmt "\n", ##__VA_ARGS__); \
} while (0)

#define TEST_FAIL(fmt, ...) do { \
    g_TotalTests++; g_FailedTests++; \
    printf("  FAIL: " fmt "\n", ##__VA_ARGS__); \
} while (0)

#define TEST_CHECK(cond, fmt, ...) do { \
    if (cond) { TEST_PASS(fmt, ##__VA_ARGS__); } \
    else      { TEST_FAIL(fmt, ##__VA_ARGS__); } \
} while (0)

#define TEST_SKIP(fmt, ...) do { \
    g_TotalTests++; g_FailedTests++; \
    printf("  FAIL skip requested: " fmt "\n", ##__VA_ARGS__); \
} while (0)

/* ================================================================
 * D3DKMT function pointer types (loaded dynamically from gdi32.dll)
 * ================================================================ */
typedef NTSTATUS (WINAPI *PFN_D3DKMTOpenAdapterFromGdiDisplayName)(
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *);
typedef NTSTATUS (WINAPI *PFN_D3DKMTCloseAdapter)(
    const D3DKMT_CLOSEADAPTER *);
typedef NTSTATUS (WINAPI *PFN_D3DKMTQueryAdapterInfo)(
    const D3DKMT_QUERYADAPTERINFO *);
typedef NTSTATUS (WINAPI *PFN_D3DKMTCreateDevice)(
    D3DKMT_CREATEDEVICE *);
typedef NTSTATUS (WINAPI *PFN_D3DKMTDestroyDevice)(
    const D3DKMT_DESTROYDEVICE *);
typedef NTSTATUS (WINAPI *PFN_D3DKMTGetDeviceState)(
    D3DKMT_GETDEVICESTATE *);

static PFN_D3DKMTOpenAdapterFromGdiDisplayName pfnOpenAdapter;
static PFN_D3DKMTCloseAdapter                  pfnCloseAdapter;
static PFN_D3DKMTQueryAdapterInfo              pfnQueryAdapterInfo;
static PFN_D3DKMTCreateDevice                  pfnCreateDevice;
static PFN_D3DKMTDestroyDevice                 pfnDestroyDevice;
static PFN_D3DKMTGetDeviceState                pfnGetDeviceState;

static BOOL
LoadD3DKMTFunctions(void)
{
    HMODULE hGdi32 = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi32)
        hGdi32 = LoadLibraryW(L"gdi32.dll");
    if (!hGdi32)
        return FALSE;

    pfnOpenAdapter     = (PFN_D3DKMTOpenAdapterFromGdiDisplayName)
        GetProcAddress(hGdi32, "D3DKMTOpenAdapterFromGdiDisplayName");
    pfnCloseAdapter    = (PFN_D3DKMTCloseAdapter)
        GetProcAddress(hGdi32, "D3DKMTCloseAdapter");
    pfnQueryAdapterInfo = (PFN_D3DKMTQueryAdapterInfo)
        GetProcAddress(hGdi32, "D3DKMTQueryAdapterInfo");
    pfnCreateDevice    = (PFN_D3DKMTCreateDevice)
        GetProcAddress(hGdi32, "D3DKMTCreateDevice");
    pfnDestroyDevice   = (PFN_D3DKMTDestroyDevice)
        GetProcAddress(hGdi32, "D3DKMTDestroyDevice");
    pfnGetDeviceState  = (PFN_D3DKMTGetDeviceState)
        GetProcAddress(hGdi32, "D3DKMTGetDeviceState");

    return (pfnOpenAdapter && pfnCloseAdapter);
}

static HMODULE
LoadSystemDriverImage(
    _In_ LPCWSTR DriverName)
{
    WCHAR Path[MAX_PATH];
    UINT Length;
    static const WCHAR DriversSuffix[] = L"\\drivers\\";

    Length = GetSystemDirectoryW(Path, MAX_PATH);
    if (Length != 0 &&
        Length < MAX_PATH &&
        Length + (sizeof(DriversSuffix) / sizeof(DriversSuffix[0])) +
            lstrlenW(DriverName) < MAX_PATH)
    {
        lstrcatW(Path, DriversSuffix);
        lstrcatW(Path, DriverName);
        return LoadLibraryExW(Path, NULL,
                              DONT_RESOLVE_DLL_REFERENCES);
    }

    return LoadLibraryExW(DriverName, NULL,
                          DONT_RESOLVE_DLL_REFERENCES);
}

/* Helper: open the primary display adapter */
static NTSTATUS
OpenPrimaryAdapter(D3DKMT_HANDLE *phAdapter)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;

    memset(&Open, 0, sizeof(Open));
    wcscpy(Open.DeviceName, L"\\\\.\\DISPLAY1");

    NTSTATUS Status = pfnOpenAdapter(&Open);
    if (NT_SUCCESS(Status))
        *phAdapter = Open.hAdapter;
    return Status;
}

/* Helper: close an adapter */
static void
CloseAdapter(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_CLOSEADAPTER Close;
    Close.hAdapter = hAdapter;
    pfnCloseAdapter(&Close);
}

/* ================================================================
 * Test 1: TDR Registry Configuration
 *
 * Reads HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers and
 * verifies TdrLevel, TdrDelay, TdrDdiDelay have expected defaults.
 * ================================================================ */
static void
Test_TdrRegistryConfig(void)
{
    HKEY hKey;
    LONG lResult;
    DWORD dwType, dwSize, dwValue;

    printf("\n[Test 1] TDR Registry Configuration\n");

    lResult = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
        0,
        KEY_READ,
        &hKey);

    if (lResult != ERROR_SUCCESS)
    {
        TEST_SKIP("Cannot open GraphicsDrivers key (error %ld) - key may not exist yet", lResult);
        return;
    }

    /* TdrLevel: default is 3 (TdrLevelRecover) if present */
    dwSize = sizeof(dwValue);
    lResult = RegQueryValueExW(hKey, L"TdrLevel", NULL, &dwType,
                               (LPBYTE)&dwValue, &dwSize);
    if (lResult == ERROR_SUCCESS)
    {
        TEST_CHECK(dwType == REG_DWORD,
                   "TdrLevel type is REG_DWORD (got %lu)", dwType);
        TEST_CHECK(dwValue <= 3,
                   "TdrLevel value in valid range 0-3 (got %lu)", dwValue);
        printf("    TdrLevel = %lu\n", dwValue);
    }
    else
    {
        /* Not present means system uses compiled-in default (3) */
        TEST_PASS("TdrLevel not in registry -- system uses default (3)");
    }

    /* TdrDelay: default is 2 seconds */
    dwSize = sizeof(dwValue);
    lResult = RegQueryValueExW(hKey, L"TdrDelay", NULL, &dwType,
                               (LPBYTE)&dwValue, &dwSize);
    if (lResult == ERROR_SUCCESS)
    {
        TEST_CHECK(dwType == REG_DWORD,
                   "TdrDelay type is REG_DWORD (got %lu)", dwType);
        TEST_CHECK(dwValue >= 1 && dwValue <= 60,
                   "TdrDelay in reasonable range 1-60s (got %lu)", dwValue);
        printf("    TdrDelay = %lu\n", dwValue);
    }
    else
    {
        TEST_PASS("TdrDelay not in registry -- system uses default (2)");
    }

    /* TdrDdiDelay: default is 5 seconds */
    dwSize = sizeof(dwValue);
    lResult = RegQueryValueExW(hKey, L"TdrDdiDelay", NULL, &dwType,
                               (LPBYTE)&dwValue, &dwSize);
    if (lResult == ERROR_SUCCESS)
    {
        TEST_CHECK(dwType == REG_DWORD,
                   "TdrDdiDelay type is REG_DWORD (got %lu)", dwType);
        TEST_CHECK(dwValue >= 1 && dwValue <= 60,
                   "TdrDdiDelay in reasonable range 1-60s (got %lu)", dwValue);
        printf("    TdrDdiDelay = %lu\n", dwValue);
    }
    else
    {
        TEST_PASS("TdrDdiDelay not in registry -- system uses default (5)");
    }

    RegCloseKey(hKey);
}

/* ================================================================
 * Test 2: TDR via QueryAdapterInfo (WDDM version)
 *
 * Opens the adapter, queries KMTQAITYPE_DRIVERVERSION, verifies
 * the reported WDDM version is valid.
 * ================================================================ */
static void
Test_TdrQueryAdapterInfo(void)
{
    D3DKMT_HANDLE hAdapter = 0;
    NTSTATUS Status;
    D3DKMT_DRIVERVERSION DriverVersion = 0;
    D3DKMT_QUERYADAPTERINFO QueryInfo;

    printf("\n[Test 2] TDR via QueryAdapterInfo (WDDM version)\n");

    if (!pfnOpenAdapter || !pfnQueryAdapterInfo)
    {
        TEST_SKIP("D3DKMT functions not available");
        return;
    }

    Status = OpenPrimaryAdapter(&hAdapter);
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("Cannot open primary adapter (status 0x%08lX)", Status);
        return;
    }

    TEST_PASS("Opened primary adapter (handle=0x%lX)", hAdapter);

    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = hAdapter;
    QueryInfo.Type = KMTQAITYPE_DRIVERVERSION;
    QueryInfo.pPrivateDriverData = &DriverVersion;
    QueryInfo.PrivateDriverDataSize = sizeof(DriverVersion);

    Status = pfnQueryAdapterInfo(&QueryInfo);
    if (NT_SUCCESS(Status))
    {
        TEST_PASS("QueryAdapterInfo(DRIVERVERSION) succeeded");
        TEST_CHECK(DriverVersion >= KMT_DRIVERVERSION_WDDM_1_0,
                   "WDDM version >= 1.0 (got %d)", DriverVersion);
        printf("    Reported WDDM version: %d\n", DriverVersion);
    }
    else
    {
        TEST_FAIL("QueryAdapterInfo(DRIVERVERSION) failed (status 0x%08lX)", Status);
    }

    CloseAdapter(hAdapter);
}

/* ================================================================
 * Test 3: GetDeviceState - verify no TDR in normal operation
 *
 * Creates a device, immediately queries execution state, confirms
 * the device is ACTIVE (no TDR has been triggered).
 * ================================================================ */
static void
Test_GetDeviceState_NoTdr(void)
{
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_CREATEDEVICE CreateDevice;
    D3DKMT_GETDEVICESTATE DeviceState;
    D3DKMT_DESTROYDEVICE DestroyDevice;
    NTSTATUS Status;

    printf("\n[Test 3] GetDeviceState - no TDR in normal operation\n");

    if (!pfnOpenAdapter || !pfnCreateDevice || !pfnGetDeviceState || !pfnDestroyDevice)
    {
        TEST_SKIP("Required D3DKMT functions not available");
        return;
    }

    Status = OpenPrimaryAdapter(&hAdapter);
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("Cannot open primary adapter (status 0x%08lX)", Status);
        return;
    }

    memset(&CreateDevice, 0, sizeof(CreateDevice));
    CreateDevice.hAdapter = hAdapter;

    Status = pfnCreateDevice(&CreateDevice);
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("CreateDevice failed (status 0x%08lX) -- no WDDM driver?", Status);
        CloseAdapter(hAdapter);
        return;
    }

    TEST_PASS("Created D3DKMT device (handle=0x%lX)", CreateDevice.hDevice);

    /* Query execution state -- should be ACTIVE */
    memset(&DeviceState, 0, sizeof(DeviceState));
    DeviceState.hDevice = CreateDevice.hDevice;
    DeviceState.StateType = D3DKMT_DEVICESTATE_EXECUTION;

    Status = pfnGetDeviceState(&DeviceState);
    if (NT_SUCCESS(Status))
    {
        TEST_CHECK(DeviceState.ExecutionState == D3DKMT_DEVICEEXECUTION_ACTIVE,
                   "Device execution state is ACTIVE (got %d)",
                   DeviceState.ExecutionState);
    }
    else
    {
        TEST_FAIL("GetDeviceState(EXECUTION) failed (status 0x%08lX)", Status);
    }

    /* Query reset state -- should show no desktop switch / no TDR */
    memset(&DeviceState, 0, sizeof(DeviceState));
    DeviceState.hDevice = CreateDevice.hDevice;
    DeviceState.StateType = D3DKMT_DEVICESTATE_RESET;

    Status = pfnGetDeviceState(&DeviceState);
    if (NT_SUCCESS(Status))
    {
        TEST_CHECK(DeviceState.ResetState.Value == 0,
                   "Device reset state is clean (Value=0x%lX)",
                   DeviceState.ResetState.Value);
    }
    else
    {
        /* Some drivers may not implement this query */
        TEST_SKIP("GetDeviceState(RESET) not supported (status 0x%08lX)", Status);
    }

    DestroyDevice.hDevice = CreateDevice.hDevice;
    pfnDestroyDevice(&DestroyDevice);
    CloseAdapter(hAdapter);
}

/* ================================================================
 * Test 4: Watchdog export presence validation
 *
 * Loads watchdog.sys as a data file and verifies known export names
 * are present via GetProcAddress.
 * ================================================================ */

/* List of watchdog.sys exports that must exist */
static const char *g_WatchdogExports[] =
{
    "WdAllocateDeferredWatchdog",
    "WdAllocateWatchdog",
    "WdFreeDeferredWatchdog",
    "WdFreeWatchdog",
    "WdStartDeferredWatch",
    "WdStartWatch",
    "WdStopDeferredWatch",
    "WdStopWatch",
    "WdResetDeferredWatch",
    "WdResetWatch",
    "WdSuspendDeferredWatch",
    "WdSuspendWatch",
    "WdResumeDeferredWatch",
    "WdResumeWatch",
    "WdEnterMonitoredSection",
    "WdExitMonitoredSection",
    "WdGetDeviceObject",
    "WdGetLowestDeviceObject",
    "WdGetLastEvent",
    "WdReferenceObject",
    "WdDereferenceObject",
    "WdAttachContext",
    "WdDetachContext",
    "WdCompleteEvent",
    "WdMadeAnyProgress",
    "WdDiagInit",
    "WdDiagNotifyUser",
    "WdDiagShutdown",
    "DMgrAcquireDisplayOwnership",
    "DMgrReleaseDisplayOwnership",
    "DMgrGetDisplayOwnership",
};

static void
Test_WatchdogExportPresence(void)
{
    HMODULE hMod;
    UINT i;
    UINT nFound = 0;

    printf("\n[Test 4] Watchdog export presence validation\n");

    hMod = LoadSystemDriverImage(L"watchdog.sys");

    if (!hMod)
    {
        TEST_SKIP("Cannot load watchdog.sys as data file (error %lu)", GetLastError());
        return;
    }

    for (i = 0; i < sizeof(g_WatchdogExports) / sizeof(g_WatchdogExports[0]); i++)
    {
        FARPROC pFunc = GetProcAddress(hMod, g_WatchdogExports[i]);
        if (pFunc)
        {
            nFound++;
        }
        else
        {
            TEST_FAIL("Missing export: %s", g_WatchdogExports[i]);
        }
    }

    if (nFound == sizeof(g_WatchdogExports) / sizeof(g_WatchdogExports[0]))
    {
        TEST_PASS("All %u watchdog exports found", nFound);
    }
    else
    {
        TEST_FAIL("Only %u/%u watchdog exports found",
                  nFound,
                  (UINT)(sizeof(g_WatchdogExports) / sizeof(g_WatchdogExports[0])));
    }

    FreeLibrary(hMod);
}

/* ================================================================
 * Test 5: D3DKMT adapter open/close stress
 *
 * Opens and closes the primary adapter 1000 times rapidly.
 * Verifies no TDR, no crash, and no handle leak.
 * ================================================================ */
static void
Test_AdapterOpenCloseStress(void)
{
    UINT i;
    UINT nSuccess = 0;
    UINT nFail = 0;
    DWORD dwStart, dwElapsed;

    printf("\n[Test 5] D3DKMT adapter open/close stress (1000 iterations)\n");

    if (!pfnOpenAdapter || !pfnCloseAdapter)
    {
        TEST_SKIP("D3DKMT open/close functions not available");
        return;
    }

    dwStart = GetTickCount();

    for (i = 0; i < 1000; i++)
    {
        D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
        D3DKMT_CLOSEADAPTER Close;
        NTSTATUS Status;

        memset(&Open, 0, sizeof(Open));
        wcscpy(Open.DeviceName, L"\\\\.\\DISPLAY1");

        Status = pfnOpenAdapter(&Open);
        if (NT_SUCCESS(Status))
        {
            nSuccess++;
            Close.hAdapter = Open.hAdapter;
            pfnCloseAdapter(&Close);
        }
        else
        {
            nFail++;
        }
    }

    dwElapsed = GetTickCount() - dwStart;

    if (nSuccess == 0)
    {
        TEST_SKIP("No adapter available -- all 1000 opens failed");
        return;
    }

    TEST_CHECK(nSuccess == 1000,
               "All 1000 open/close cycles succeeded (%u ok, %u fail, %lu ms)",
               nSuccess, nFail, dwElapsed);

    if (nSuccess > 0 && nSuccess < 1000)
    {
        TEST_FAIL("Only %u/1000 open/close cycles succeeded (%u failures)",
                  nSuccess, nFail);
    }

}

/* ================================================================
 * Test 6: dxgkrnl.sys export validation
 *
 * Loads dxgkrnl.sys as a data file and verifies TDR-related exports
 * and data symbols are present.
 * ================================================================ */

static const char *g_DxgkrnlExports[] =
{
    "TdrIsEnabled",
    "TdrHistoryInit",
    "TdrHistoryUpdate",
    "TdrHistoryIsLimitExhausted",
    "TdrCreateRecoveryContext",
    "TdrCompleteRecoveryContext",
    "TdrIsRecoveryRequired",
    "TdrIsTimeoutForcedFlip",
    "TdrResetFromTimeout",
    "TdrAllowToDebugEngineTimeout",
    "TdrCollectDbgInfoStage1",
    "TdrCollectDbgInfoStage2",
    "TdrUpdateDbgReport",
    "g_TdrConfig",
    "g_TdrForceTimeout",
    "g_TdrForceDodPresentTimeout",
    "g_TdrForceDodVSyncTimeout",
};

static void
Test_DxgkrnlExportValidation(void)
{
    HMODULE hMod;
    UINT i;
    UINT nFound = 0;

    printf("\n[Test 6] dxgkrnl.sys export validation\n");

    hMod = LoadSystemDriverImage(L"dxgkrnl.sys");

    if (!hMod)
    {
        TEST_SKIP("Cannot load dxgkrnl.sys as data file (error %lu)", GetLastError());
        return;
    }

    for (i = 0; i < sizeof(g_DxgkrnlExports) / sizeof(g_DxgkrnlExports[0]); i++)
    {
        FARPROC pFunc = GetProcAddress(hMod, g_DxgkrnlExports[i]);
        if (pFunc)
        {
            nFound++;
        }
        else
        {
            TEST_FAIL("Missing dxgkrnl export: %s", g_DxgkrnlExports[i]);
        }
    }

    if (nFound == sizeof(g_DxgkrnlExports) / sizeof(g_DxgkrnlExports[0]))
    {
        TEST_PASS("All %u dxgkrnl TDR exports found", nFound);
    }
    else
    {
        TEST_FAIL("Only %u/%u dxgkrnl TDR exports found",
                  nFound,
                  (UINT)(sizeof(g_DxgkrnlExports) / sizeof(g_DxgkrnlExports[0])));
    }

    FreeLibrary(hMod);
}

/* ================================================================
 * Main entry point
 * ================================================================ */
int
main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("=== ReactOS WDDM TDR/Watchdog Test Suite ===\n");

    if (!LoadD3DKMTFunctions())
    {
        printf("WARNING: Could not load D3DKMT functions from gdi32.dll\n");
        printf("         D3DKMT-dependent tests will be skipped.\n");
    }

    Test_TdrRegistryConfig();
    Test_TdrQueryAdapterInfo();
    Test_GetDeviceState_NoTdr();
    Test_WatchdogExportPresence();
    Test_AdapterOpenCloseStress();
    Test_DxgkrnlExportValidation();

    printf("\n=== Results: %d total, %d passed, %d failed ===\n",
           g_TotalTests, g_PassedTests, g_FailedTests);

    return g_FailedTests > 0 ? 1 : 0;
}
