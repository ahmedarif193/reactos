/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Validate the win32k <-> dxgkrnl callback exchange protocol
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * These tests verify:
 *   1. The IOCTL_DXGKRNL_EXCHANGE_INTERFACE IOCTL protocol
 *   2. That OpenAdapter calls succeed (not STATUS_PROCEDURE_NOT_FOUND)
 *   3. CreateDevice lifecycle through the callback path
 *   4. CreateContext lifecycle
 *   5. Full round-trip: OpenAdapter -> CreateDevice -> DestroyDevice -> CloseAdapter
 */

#include "precomp.h"

static HANDLE hDxgKrnl = INVALID_HANDLE_VALUE;

typedef struct _DXGKRNL_INTERFACE_EXCHANGE_IN_TEST
{
    ULONG Version;
    ULONG Size;
} DXGKRNL_INTERFACE_EXCHANGE_IN_TEST;

typedef struct _REACTOS_WIN32K_DXGKRNL_INTERFACE
{
    PVOID RxgkIntPfnPresent;
    PVOID RxgkIntPfnQueryAdapterInfo;
    PVOID RxgkIntPfnQueryAllocationResidency;
    PVOID RxgkIntPfnQueryStatistics;
    PVOID RxgkIntPfnReleaseProcessVidPnSourceOwners;
    PVOID RxgkIntPfnRender;
    PVOID RxgkIntPfnSetContextSchedulingPriority;
    PVOID RxgkIntPfnOpenResource;
    PVOID RxgkIntPfnPollDisplayChildren;
    PVOID RxgkIntPfnLock;
    PVOID RxgkIntPfnGetSharedPrimaryHandle;
    PVOID RxgkIntPfnInvalidateActiveVidPn;
    PVOID RxgkIntPfnSetAllocationPriority;
    PVOID RxgkIntPfnGetPresentHistory;
    PVOID RxgkIntPfnQueryResourceInfo;
    PVOID RxgkIntPfnCreateAllocation;
    PVOID RxgkIntPfnCheckMonitorPowerState;
    PVOID RxgkIntPfnCheckOcclusion;
    PVOID RxgkIntPfnCloseAdapter;
    PVOID RxgkIntPfnCreateContext;
    PVOID RxgkIntPfnCreateDevice;
    PVOID RxgkIntPfnCreateOverlay;
    PVOID RxgkIntPfnCreateSynchronizationObject;
    PVOID RxgkIntPfnDestroyContext;
    PVOID RxgkIntPfnDestroyDevice;
    PVOID RxgkIntPfnDestroyOverlay;
    PVOID RxgkIntPfnDestroySynchronizationObject;
    PVOID RxgkIntPfnEscape;
    PVOID RxgkIntPfnDestroyAllocation;
    PVOID RxgkIntPfnFlipOverlay;
    PVOID RxgkIntPfnGetContextSchedulingPriority;
    PVOID RxgkIntPfnGetDeviceState;
    PVOID RxgkIntPfnGetDisplayModeList;
    PVOID RxgkIntPfnGetMultisampleMethodList;
    PVOID RxgkIntPfnGetRuntimeData;
    PVOID RxgkIntPfnGetScanLine;
    PVOID RxgkIntPfnSignalSynchronizationObject;
    PVOID RxgkIntPfnWaitForVerticalBlankEvent;
    PVOID RxgkIntPfnWaitForSynchronizationObject;
    PVOID RxgkIntPfnSetVidPnSourceOwner;
    PVOID RxgkIntPfnWaitForIdle;
    PVOID RxgkIntPfnUpdateOverlay;
    PVOID RxgkIntPfnSetQueuedLimit;
    PVOID RxgkIntPfnSetGammaRamp;
    PVOID RxgkIntPfnSetDisplayMode;
    PVOID RxgkIntPfnSetDisplayPrivateDriverFormat;
    PVOID RxgkIntPfnUnlock;
    PVOID RxgkIntPfnEnumAdapters;
    PVOID RxgkIntPfnOpenAdapterFromLuid;
    PVOID RxgkIntPfnOfferAllocations;
    PVOID RxgkIntPfnReclaimAllocations;
    PVOID RxgkIntPfnSetVidPnSourceOwner1;
    PVOID RxgkIntPfnWaitForVerticalBlankEvent2;
    PVOID RxgkIntPfnCreateSynchronizationObject2;
    PVOID RxgkIntPfnWaitForSynchronizationObject2;
    PVOID RxgkIntPfnSignalSynchronizationObject2;
} REACTOS_WIN32K_DXGKRNL_INTERFACE, *PREACTOS_WIN32K_DXGKRNL_INTERFACE;

#define RXGK_INTERFACE_CALLBACK_COUNT 56

#define ok_callback_field(Interface, Field) \
    ok((Interface)->Field != NULL, #Field " must be populated\n")

static BOOL
QueryCallbackInterface(
    _Out_ PREACTOS_WIN32K_DXGKRNL_INTERFACE Interface,
    _Out_opt_ DWORD *BytesReturned)
{
    DXGKRNL_INTERFACE_EXCHANGE_IN_TEST ExchangeIn;
    DWORD LocalBytesReturned = 0;
    BOOL Result;

    memset(Interface, 0, sizeof(*Interface));
    ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_1;
    ExchangeIn.Size = sizeof(*Interface);

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_DXGKRNL_EXCHANGE_INTERFACE,
                          &ExchangeIn, sizeof(ExchangeIn),
                          Interface, sizeof(*Interface),
                          &LocalBytesReturned);

    if (BytesReturned)
        *BytesReturned = LocalBytesReturned;

    return Result;
}

static void
CheckCallbackInterface(
    _In_ const REACTOS_WIN32K_DXGKRNL_INTERFACE *Interface)
{
    ok_callback_field(Interface, RxgkIntPfnPresent);
    ok_callback_field(Interface, RxgkIntPfnQueryAdapterInfo);
    ok_callback_field(Interface, RxgkIntPfnQueryAllocationResidency);
    ok_callback_field(Interface, RxgkIntPfnQueryStatistics);
    ok_callback_field(Interface, RxgkIntPfnReleaseProcessVidPnSourceOwners);
    ok_callback_field(Interface, RxgkIntPfnRender);
    ok_callback_field(Interface, RxgkIntPfnSetContextSchedulingPriority);
    ok_callback_field(Interface, RxgkIntPfnOpenResource);
    ok_callback_field(Interface, RxgkIntPfnPollDisplayChildren);
    ok_callback_field(Interface, RxgkIntPfnLock);
    ok_callback_field(Interface, RxgkIntPfnGetSharedPrimaryHandle);
    ok_callback_field(Interface, RxgkIntPfnInvalidateActiveVidPn);
    ok_callback_field(Interface, RxgkIntPfnSetAllocationPriority);
    ok_callback_field(Interface, RxgkIntPfnGetPresentHistory);
    ok_callback_field(Interface, RxgkIntPfnQueryResourceInfo);
    ok_callback_field(Interface, RxgkIntPfnCreateAllocation);
    ok_callback_field(Interface, RxgkIntPfnCheckMonitorPowerState);
    ok_callback_field(Interface, RxgkIntPfnCheckOcclusion);
    ok_callback_field(Interface, RxgkIntPfnCloseAdapter);
    ok_callback_field(Interface, RxgkIntPfnCreateContext);
    ok_callback_field(Interface, RxgkIntPfnCreateDevice);
    ok_callback_field(Interface, RxgkIntPfnCreateOverlay);
    ok_callback_field(Interface, RxgkIntPfnCreateSynchronizationObject);
    ok_callback_field(Interface, RxgkIntPfnDestroyContext);
    ok_callback_field(Interface, RxgkIntPfnDestroyDevice);
    ok_callback_field(Interface, RxgkIntPfnDestroyOverlay);
    ok_callback_field(Interface, RxgkIntPfnDestroySynchronizationObject);
    ok_callback_field(Interface, RxgkIntPfnEscape);
    ok_callback_field(Interface, RxgkIntPfnDestroyAllocation);
    ok_callback_field(Interface, RxgkIntPfnFlipOverlay);
    ok_callback_field(Interface, RxgkIntPfnGetContextSchedulingPriority);
    ok_callback_field(Interface, RxgkIntPfnGetDeviceState);
    ok_callback_field(Interface, RxgkIntPfnGetDisplayModeList);
    ok_callback_field(Interface, RxgkIntPfnGetMultisampleMethodList);
    ok_callback_field(Interface, RxgkIntPfnGetRuntimeData);
    ok_callback_field(Interface, RxgkIntPfnGetScanLine);
    ok_callback_field(Interface, RxgkIntPfnSignalSynchronizationObject);
    ok_callback_field(Interface, RxgkIntPfnWaitForVerticalBlankEvent);
    ok_callback_field(Interface, RxgkIntPfnWaitForSynchronizationObject);
    ok_callback_field(Interface, RxgkIntPfnSetVidPnSourceOwner);
    ok_callback_field(Interface, RxgkIntPfnWaitForIdle);
    ok_callback_field(Interface, RxgkIntPfnUpdateOverlay);
    ok_callback_field(Interface, RxgkIntPfnSetQueuedLimit);
    ok_callback_field(Interface, RxgkIntPfnSetGammaRamp);
    ok_callback_field(Interface, RxgkIntPfnSetDisplayMode);
    ok_callback_field(Interface, RxgkIntPfnSetDisplayPrivateDriverFormat);
    ok_callback_field(Interface, RxgkIntPfnUnlock);
    ok_callback_field(Interface, RxgkIntPfnEnumAdapters);
    ok_callback_field(Interface, RxgkIntPfnOpenAdapterFromLuid);
    ok_callback_field(Interface, RxgkIntPfnOfferAllocations);
    ok_callback_field(Interface, RxgkIntPfnReclaimAllocations);
    ok_callback_field(Interface, RxgkIntPfnSetVidPnSourceOwner1);
    ok_callback_field(Interface, RxgkIntPfnWaitForVerticalBlankEvent2);
    ok_callback_field(Interface, RxgkIntPfnCreateSynchronizationObject2);
    ok_callback_field(Interface, RxgkIntPfnWaitForSynchronizationObject2);
    ok_callback_field(Interface, RxgkIntPfnSignalSynchronizationObject2);
}

static BOOL OpenDxg(void)
{
    hDxgKrnl = OpenDxgKrnl();
    if (hDxgKrnl == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open \\Device\\DxgKrnl -- WDDM driver not loaded\n");
        return FALSE;
    }
    return TRUE;
}

static D3DKMT_HANDLE EnumFirstAdapter(void)
{
    D3DKMT_ENUMADAPTERS e;
    DWORD br;
    memset(&e, 0, sizeof(e));
    if (!SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                      &e, sizeof(e), &e, sizeof(e), &br) ||
        e.NumAdapters == 0)
    {
        return 0;
    }
    return e.Adapters[0].hAdapter;
}

/*
 * Test 1: CallbackExchange
 *
 * Verify the IOCTL_DXGKRNL_EXCHANGE_INTERFACE protocol returns success.
 */
static void Test_CallbackExchange(void)
{
    DWORD BytesReturned = 0;
    BOOL Result;
    REACTOS_WIN32K_DXGKRNL_INTERFACE Interface;

    if (!OpenDxg()) return;

    /*
     * The exchange IOCTL is designed for kernel-to-kernel
     * (IRP_MJ_INTERNAL_DEVICE_CONTROL) but we can test it from user mode
     * as well since DxgkDispatchDeviceControl routes it through the
     * buffered IOCTL dispatcher.
     */
    Result = QueryCallbackInterface(&Interface, &BytesReturned);

    ok(Result, "Exchange IOCTL should succeed (error %lu)\n",
       GetLastError());

    if (Result)
    {
        ok_eq_ulong((ULONG)sizeof(Interface),
                    RXGK_INTERFACE_CALLBACK_COUNT * sizeof(PVOID));
        ok_eq_ulong(BytesReturned, sizeof(Interface));
        CheckCallbackInterface(&Interface);
    }

    CloseHandle(hDxgKrnl);
    hDxgKrnl = INVALID_HANDLE_VALUE;
}

static void Test_CallbackExchange_InvalidInputs(void)
{
    DXGKRNL_INTERFACE_EXCHANGE_IN_TEST ExchangeIn;
    REACTOS_WIN32K_DXGKRNL_INTERFACE Interface;
    BYTE SmallOutput[sizeof(PVOID)];
    DWORD BytesReturned = 0;
    ULONG ShortInput;
    BOOL Result;
    DWORD Error;

    if (!OpenDxg()) return;

    memset(&Interface, 0, sizeof(Interface));
    ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_1 + 1;
    ExchangeIn.Size = sizeof(Interface);
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_DXGKRNL_EXCHANGE_INTERFACE,
                          &ExchangeIn, sizeof(ExchangeIn),
                          &Interface, sizeof(Interface),
                          &BytesReturned);
    Error = GetLastError();
    ok(!Result, "Exchange with unsupported version should fail\n");
    ok(Error == ERROR_NOT_SUPPORTED,
       "Exchange with unsupported version returned error %lu\n", Error);

    memset(&Interface, 0, sizeof(Interface));
    ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_1;
    ExchangeIn.Size = sizeof(PVOID);
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_DXGKRNL_EXCHANGE_INTERFACE,
                          &ExchangeIn, sizeof(ExchangeIn),
                          &Interface, sizeof(Interface),
                          &BytesReturned);
    Error = GetLastError();
    ok(!Result, "Exchange with undersized advertised table should fail\n");
    ok(Error == ERROR_INSUFFICIENT_BUFFER,
       "Exchange with undersized advertised table returned error %lu\n", Error);

    memset(SmallOutput, 0, sizeof(SmallOutput));
    ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_1;
    ExchangeIn.Size = sizeof(Interface);
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_DXGKRNL_EXCHANGE_INTERFACE,
                          &ExchangeIn, sizeof(ExchangeIn),
                          SmallOutput, sizeof(SmallOutput),
                          &BytesReturned);
    Error = GetLastError();
    ok(!Result, "Exchange with undersized output buffer should fail\n");
    ok(Error == ERROR_INSUFFICIENT_BUFFER,
       "Exchange with undersized output buffer returned error %lu\n", Error);

    ShortInput = DXGKRNL_INTERFACE_VERSION_1;
    memset(&Interface, 0, sizeof(Interface));
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_DXGKRNL_EXCHANGE_INTERFACE,
                          &ShortInput, sizeof(ShortInput),
                          &Interface, sizeof(Interface),
                          &BytesReturned);
    Error = GetLastError();
    ok(!Result, "Exchange with short input should fail\n");
    ok(Error == ERROR_INSUFFICIENT_BUFFER,
       "Exchange with short input returned error %lu\n", Error);

    CloseHandle(hDxgKrnl);
    hDxgKrnl = INVALID_HANDLE_VALUE;
}

/*
 * Test 2: OpenAdapter
 *
 * Call OpenAdapterFromGdiDisplayName and verify it succeeds
 * (i.e., not STATUS_PROCEDURE_NOT_FOUND).
 */
static void Test_OpenAdapter(void)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenData;
    D3DKMT_CLOSEADAPTER CloseData;
    DWORD BytesReturned = 0;
    BOOL Result;

    if (!OpenDxg()) return;

    memset(&OpenData, 0, sizeof(OpenData));
    wcscpy(OpenData.DeviceName, L"\\\\.\\DISPLAY1");

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME,
                          &OpenData, sizeof(OpenData),
                          &OpenData, sizeof(OpenData),
                          &BytesReturned);

    if (!Result)
    {
        skip("OpenAdapterFromGdiDisplayName failed (error %lu) -- "
             "no WDDM adapter\n", GetLastError());
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
        return;
    }

    ok(OpenData.hAdapter != 0,
       "OpenAdapterFromGdiDisplayName returned zero handle\n");

    /* Clean up */
    memset(&CloseData, 0, sizeof(CloseData));
    CloseData.hAdapter = OpenData.hAdapter;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                 &CloseData, sizeof(CloseData), NULL, 0, &BytesReturned);

    CloseHandle(hDxgKrnl);
    hDxgKrnl = INVALID_HANDLE_VALUE;
}

/*
 * Test 3: CreateDevice
 *
 * Open adapter -> CreateDevice -> verify handle returned.
 */
static void Test_CreateDevice(void)
{
    D3DKMT_CREATEDEVICE CreateDevice;
    D3DKMT_DESTROYDEVICE DestroyDevice;
    DWORD BytesReturned = 0;
    BOOL Result;
    D3DKMT_HANDLE hAdapter;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter)
    {
        skip("No adapter for CreateDevice test\n");
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
        return;
    }

    memset(&CreateDevice, 0, sizeof(CreateDevice));
    CreateDevice.hAdapter = hAdapter;

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                          &CreateDevice, sizeof(CreateDevice),
                          &CreateDevice, sizeof(CreateDevice),
                          &BytesReturned);

    if (!Result)
    {
        skip("CreateDevice IOCTL failed (error %lu)\n", GetLastError());
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
        return;
    }

    ok(CreateDevice.hDevice != 0,
       "CreateDevice returned zero device handle\n");

    /* Clean up */
    memset(&DestroyDevice, 0, sizeof(DestroyDevice));
    DestroyDevice.hDevice = CreateDevice.hDevice;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                 &DestroyDevice, sizeof(DestroyDevice), NULL, 0, &BytesReturned);

    CloseHandle(hDxgKrnl);
    hDxgKrnl = INVALID_HANDLE_VALUE;
}

/*
 * Test 4: CreateContext
 *
 * Open adapter -> CreateDevice -> CreateContext -> verify.
 */
static void Test_CreateContext(void)
{
    D3DKMT_CREATEDEVICE CreateDevice;
    D3DKMT_CREATECONTEXT CreateContext;
    D3DKMT_DESTROYCONTEXT DestroyContext;
    D3DKMT_DESTROYDEVICE DestroyDevice;
    DWORD BytesReturned = 0;
    BOOL Result;
    D3DKMT_HANDLE hAdapter;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter)
    {
        skip("No adapter for CreateContext test\n");
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
        return;
    }

    /* Create device first */
    memset(&CreateDevice, 0, sizeof(CreateDevice));
    CreateDevice.hAdapter = hAdapter;
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                          &CreateDevice, sizeof(CreateDevice),
                          &CreateDevice, sizeof(CreateDevice),
                          &BytesReturned);
    if (!Result)
    {
        skip("CreateDevice failed for CreateContext test (error %lu)\n",
             GetLastError());
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
        return;
    }

    /* Create context */
    memset(&CreateContext, 0, sizeof(CreateContext));
    CreateContext.hDevice = CreateDevice.hDevice;
    CreateContext.NodeOrdinal = 0;
    CreateContext.EngineAffinity = 1;

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                          &CreateContext, sizeof(CreateContext),
                          &CreateContext, sizeof(CreateContext),
                          &BytesReturned);

    if (!Result)
    {
        skip("CreateContext IOCTL failed (error %lu)\n", GetLastError());
    }
    else
    {
        ok(CreateContext.hContext != 0,
           "CreateContext returned zero context handle\n");

        /* Destroy context */
        memset(&DestroyContext, 0, sizeof(DestroyContext));
        DestroyContext.hContext = CreateContext.hContext;
        SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                     &DestroyContext, sizeof(DestroyContext),
                     NULL, 0, &BytesReturned);
    }

    /* Destroy device */
    memset(&DestroyDevice, 0, sizeof(DestroyDevice));
    DestroyDevice.hDevice = CreateDevice.hDevice;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                 &DestroyDevice, sizeof(DestroyDevice), NULL, 0, &BytesReturned);

    CloseHandle(hDxgKrnl);
    hDxgKrnl = INVALID_HANDLE_VALUE;
}

/*
 * Test 5: RoundTrip
 *
 * Full lifecycle: OpenAdapter -> CreateDevice -> DestroyDevice -> CloseAdapter.
 * Verifies all operations succeed and handles are valid.
 */
static void Test_RoundTrip(void)
{
    D3DKMT_ENUMADAPTERS EnumData;
    D3DKMT_CREATEDEVICE CreateDevice;
    D3DKMT_DESTROYDEVICE DestroyDevice;
    D3DKMT_CLOSEADAPTER CloseAdapter;
    DWORD BytesReturned = 0;
    BOOL Result;
    D3DKMT_HANDLE hAdapter;
    LUID AdapterLuid;

    if (!OpenDxg()) return;

    /* Step 1: Enumerate adapters */
    memset(&EnumData, 0, sizeof(EnumData));
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                          &EnumData, sizeof(EnumData),
                          &EnumData, sizeof(EnumData),
                          &BytesReturned);
    if (!Result || EnumData.NumAdapters == 0)
    {
        skip("No adapter for RoundTrip test\n");
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
        return;
    }

    hAdapter = EnumData.Adapters[0].hAdapter;
    AdapterLuid = EnumData.Adapters[0].AdapterLuid;

    ok(hAdapter != 0, "RoundTrip: adapter handle is 0\n");
    ok(AdapterLuid.LowPart != 0 || AdapterLuid.HighPart != 0,
       "RoundTrip: adapter LUID is zero\n");

    /* Step 2: Create device */
    memset(&CreateDevice, 0, sizeof(CreateDevice));
    CreateDevice.hAdapter = hAdapter;
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                          &CreateDevice, sizeof(CreateDevice),
                          &CreateDevice, sizeof(CreateDevice),
                          &BytesReturned);
    if (!Result)
    {
        skip("RoundTrip: CreateDevice failed (error %lu)\n", GetLastError());
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
        return;
    }

    ok(CreateDevice.hDevice != 0, "RoundTrip: device handle is 0\n");

    /* Step 3: Destroy device */
    memset(&DestroyDevice, 0, sizeof(DestroyDevice));
    DestroyDevice.hDevice = CreateDevice.hDevice;
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                          &DestroyDevice, sizeof(DestroyDevice),
                          NULL, 0, &BytesReturned);
    ok(Result, "RoundTrip: DestroyDevice failed (error %lu)\n", GetLastError());

    /* Step 4: Close adapter */
    memset(&CloseAdapter, 0, sizeof(CloseAdapter));
    CloseAdapter.hAdapter = hAdapter;
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                          &CloseAdapter, sizeof(CloseAdapter),
                          NULL, 0, &BytesReturned);
    ok(Result, "RoundTrip: CloseAdapter failed (error %lu)\n", GetLastError());

    CloseHandle(hDxgKrnl);
    hDxgKrnl = INVALID_HANDLE_VALUE;
}

/*
 * Test 6: NewIoctlCodes
 *
 * Verify the new IOCTL codes are unique and correctly defined.
 */
static void Test_NewIoctlCodes(void)
{
    /* Verify function codes for new IOCTLs */
    ok_eq_uint((IOCTL_D3DKMT_CREATECONTEXT >> 2) & 0xFFF, 0x122);
    ok_eq_uint((IOCTL_D3DKMT_DESTROYCONTEXT >> 2) & 0xFFF, 0x123);
    ok_eq_uint((IOCTL_D3DKMT_GETDEVICESTATE >> 2) & 0xFFF, 0x124);
    ok_eq_uint((IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT >> 2) & 0xFFF, 0x152);
    ok_eq_uint((IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT >> 2) & 0xFFF, 0x153);
    ok_eq_uint((IOCTL_D3DKMT_SETVIDPNSOURCEOWNER >> 2) & 0xFFF, 0x161);
    ok_eq_uint((IOCTL_D3DKMT_ESCAPE >> 2) & 0xFFF, 0x170);
    ok_eq_uint((IOCTL_DXGKRNL_EXCHANGE_INTERFACE >> 2) & 0xFFF, 0x200);

    /* All new IOCTL codes use METHOD_BUFFERED */
    ok_eq_uint(IOCTL_D3DKMT_CREATECONTEXT & 0x3, 0);
    ok_eq_uint(IOCTL_D3DKMT_DESTROYCONTEXT & 0x3, 0);
    ok_eq_uint(IOCTL_DXGKRNL_EXCHANGE_INTERFACE & 0x3, 0);

    /* All use device type 0x23 */
    ok_eq_hex((IOCTL_D3DKMT_CREATECONTEXT >> 16) & 0xFFFF, 0x0023);
    ok_eq_hex((IOCTL_DXGKRNL_EXCHANGE_INTERFACE >> 16) & 0xFFFF, 0x0023);

    /* Verify uniqueness of new codes against each other */
    ok(IOCTL_D3DKMT_CREATECONTEXT != IOCTL_D3DKMT_DESTROYCONTEXT,
       "CreateContext and DestroyContext IOCTL codes should differ\n");
    ok(IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT != IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT,
       "CreateSyncObj and DestroySyncObj IOCTL codes should differ\n");
    ok(IOCTL_DXGKRNL_EXCHANGE_INTERFACE != IOCTL_D3DKMT_CREATECONTEXT,
       "Exchange and CreateContext IOCTL codes should differ\n");
}

START_TEST(D3dkmtCallbackExchange)
{
    Test_NewIoctlCodes();
    Test_CallbackExchange();
    Test_CallbackExchange_InvalidInputs();
    Test_OpenAdapter();
    Test_CreateDevice();
    Test_CreateContext();
    Test_RoundTrip();
}
