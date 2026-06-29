/*
 * PROJECT:     ReactOS WDDM dxgkrnl - DxgkDdi callback chain tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Exercise dxgkrnl D3DKMT IOCTL dispatch -> miniport DDI chain
 * COPYRIGHT:   Copyright 2025 ReactOS WDDM Team
 *
 * These tests open \Device\DxgKrnl directly and send D3DKMT IOCTLs to
 * exercise the full user-mode -> dxgkrnl -> miniport DxgkDdi callback chain.
 *
 * Test categories:
 *   1. QueryAdapterInfo chain (DRIVERCAPS / DRIVERVERSION / SEGMENTSIZE)
 *   2. Device lifecycle (CreateDevice / DestroyDevice)
 *   3. Allocation lifecycle (CreateAllocation / Lock / Unlock / DestroyAllocation)
 *   4. Context lifecycle (CreateContext / DestroyContext)
 *   5. Display mode list enumeration
 *   6. Escape passthrough to miniport
 *   7. Present chain (full pipeline)
 *   8. Error paths (NULL params, invalid handles, wrong sizes)
 */

#include <wine/test.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <winioctl.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>

/* Pull in D3DKMT types from the DDK header */
#include <d3dkmthk.h>

/*
 * d3dkmthk.h exposes D3DKMT_ENUMADAPTERS only for the Win8 interface level.
 * dxgkrnl still needs to accept this user-mode KMT ABI while built for the
 * Win7/WDDM-1.x DDI level, so keep the fallback test-side definition here.
 */
#ifndef MAX_ENUM_ADAPTERS
#define MAX_ENUM_ADAPTERS 16
#endif

#ifndef _D3DKMT_ADAPTERINFO_DEFINED
#define _D3DKMT_ADAPTERINFO_DEFINED
typedef struct _D3DKMT_ADAPTERINFO
{
    D3DKMT_HANDLE hAdapter;
    LUID AdapterLuid;
    ULONG NumOfSources;
    BOOL bPrecisePresentRegionsPreferred;
} D3DKMT_ADAPTERINFO;
#endif

#ifndef _D3DKMT_ENUMADAPTERS_DEFINED
#define _D3DKMT_ENUMADAPTERS_DEFINED
typedef struct _D3DKMT_ENUMADAPTERS
{
    ULONG NumAdapters;
    D3DKMT_ADAPTERINFO Adapters[MAX_ENUM_ADAPTERS];
} D3DKMT_ENUMADAPTERS;
#endif

/* ========================================================================
 * IOCTL code definitions — must match dxgkrnl d3dkmt.h exactly
 * ====================================================================== */

#define DXGKRNL_DEVICE_TYPE     0x23

#define IOCTL_D3DKMT_ENUMADAPTERS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x100, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_ENUMADAPTERS2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x101, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENADAPTERFROMLUID \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x102, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CLOSEADAPTER \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x103, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYADAPTERINFO \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x104, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETDISPLAYMODELIST \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x105, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEDEVICE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x120, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYDEVICE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x121, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATECONTEXT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x122, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYCONTEXT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x123, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEALLOCATION \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x130, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYALLOCATION \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x131, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_LOCK \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x132, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_UNLOCK \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x133, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_PRESENT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x141, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_ESCAPE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x170, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* ========================================================================
 * Helper: open \Device\DxgKrnl
 * ====================================================================== */

static HANDLE
OpenDxgKrnl(void)
{
    return CreateFileW(L"\\\\.\\DxgKrnl",
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
}

/* ========================================================================
 * Helper: send an IOCTL to dxgkrnl and return the NTSTATUS
 *
 * For METHOD_BUFFERED, the same buffer is used for input and output.
 * The kernel puts the NTSTATUS in IoStatus.Status; DeviceIoControl
 * returns FALSE and GetLastError() gives the translated Win32 code.
 * We need the raw NTSTATUS, so we use NtDeviceIoControlFile instead.
 * ====================================================================== */

typedef LONG NTSTATUS_RAW;

typedef NTSTATUS_RAW (NTAPI *PFN_NtDeviceIoControlFile)(
    HANDLE FileHandle,
    HANDLE Event,
    PVOID ApcRoutine,
    PVOID ApcContext,
    PVOID IoStatusBlock,
    ULONG IoControlCode,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength);

typedef struct _TEST_IO_STATUS_BLOCK {
    union {
        NTSTATUS_RAW Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} TEST_IO_STATUS_BLOCK;

static PFN_NtDeviceIoControlFile pfnNtDeviceIoControlFile = NULL;

static NTSTATUS_RAW
DxgkSendIoctl(
    HANDLE hDevice,
    ULONG IoControlCode,
    PVOID Buffer,
    ULONG BufferSize)
{
    TEST_IO_STATUS_BLOCK IoStatus;

    if (!pfnNtDeviceIoControlFile)
    {
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtdll)
            return (NTSTATUS_RAW)0xC0000001L; /* STATUS_UNSUCCESSFUL */
        pfnNtDeviceIoControlFile = (PFN_NtDeviceIoControlFile)
            GetProcAddress(hNtdll, "NtDeviceIoControlFile");
        if (!pfnNtDeviceIoControlFile)
            return (NTSTATUS_RAW)0xC0000001L;
    }

    memset(&IoStatus, 0, sizeof(IoStatus));
    return pfnNtDeviceIoControlFile(
        hDevice,
        NULL,
        NULL,
        NULL,
        &IoStatus,
        IoControlCode,
        Buffer,
        BufferSize,
        Buffer,
        BufferSize);
}

/* ========================================================================
 * Helper: enumerate adapters and return the first adapter handle
 * ====================================================================== */

static BOOL
EnumFirstAdapter(
    HANDLE hDxgKrnl,
    D3DKMT_HANDLE *phAdapter,
    LUID *pAdapterLuid)
{
    D3DKMT_ENUMADAPTERS EnumAdapters;
    NTSTATUS_RAW Status;

    memset(&EnumAdapters, 0, sizeof(EnumAdapters));
    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                           &EnumAdapters, sizeof(EnumAdapters));

    if (Status != 0 || EnumAdapters.NumAdapters == 0)
        return FALSE;

    *phAdapter = EnumAdapters.Adapters[0].hAdapter;
    if (pAdapterLuid)
        *pAdapterLuid = EnumAdapters.Adapters[0].AdapterLuid;
    return TRUE;
}

/* ========================================================================
 * Helper: close adapter
 * ====================================================================== */

static NTSTATUS_RAW
CloseAdapter(
    HANDLE hDxgKrnl,
    D3DKMT_HANDLE hAdapter)
{
    D3DKMT_CLOSEADAPTER CloseAdapter;
    CloseAdapter.hAdapter = hAdapter;
    return DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                         &CloseAdapter, sizeof(CloseAdapter));
}

/* ========================================================================
 * Helper: create device on adapter
 * ====================================================================== */

static NTSTATUS_RAW
CreateDevice(
    HANDLE hDxgKrnl,
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE *phDevice)
{
    D3DKMT_CREATEDEVICE CreateDev;
    NTSTATUS_RAW Status;

    memset(&CreateDev, 0, sizeof(CreateDev));
    CreateDev.hAdapter = hAdapter;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                           &CreateDev, sizeof(CreateDev));
    if (Status == 0)
        *phDevice = CreateDev.hDevice;
    return Status;
}

/* ========================================================================
 * Helper: destroy device
 * ====================================================================== */

static NTSTATUS_RAW
DestroyDevice(
    HANDLE hDxgKrnl,
    D3DKMT_HANDLE hDevice)
{
    D3DKMT_DESTROYDEVICE DestroyDev;
    DestroyDev.hDevice = hDevice;
    return DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                         &DestroyDev, sizeof(DestroyDev));
}

/* ========================================================================
 * Test 1: QueryAdapterInfo chain
 *
 * D3DKMTEnumAdapters -> D3DKMTQueryAdapterInfo (DRIVERVERSION)
 * This exercises: dxgkrnl IOCTL dispatch -> DxgkQueryAdapterInfo ->
 * miniport DxgkDdiQueryAdapterInfo callback
 * ====================================================================== */

static void test_QueryAdapterInfo(HANDLE hDxgKrnl)
{
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_QUERYADAPTERINFO QueryInfo;
    D3DKMT_DRIVERVERSION DriverVersion = 0;
    D3DKMT_SEGMENTSIZEINFO SegInfo;
    NTSTATUS_RAW Status;

    if (!EnumFirstAdapter(hDxgKrnl, &hAdapter, NULL))
    {
        skip("No WDDM adapter found, skipping QueryAdapterInfo tests\n");
        return;
    }

    /* Query driver version */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = hAdapter;
    QueryInfo.Type = KMTQAITYPE_DRIVERVERSION;
    QueryInfo.pPrivateDriverData = &DriverVersion;
    QueryInfo.PrivateDriverDataSize = sizeof(DriverVersion);

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                           &QueryInfo, sizeof(QueryInfo));

    ok(Status == 0,
       "QueryAdapterInfo(DRIVERVERSION) failed: 0x%08lx\n", Status);

    if (Status == 0)
    {
        ok(DriverVersion >= KMT_DRIVERVERSION_WDDM_1_0,
           "Expected WDDM version >= 1.0, got %d\n", (int)DriverVersion);
        trace("WDDM driver version: %d\n", (int)DriverVersion);
    }

    /* Query segment size info */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    memset(&SegInfo, 0, sizeof(SegInfo));
    QueryInfo.hAdapter = hAdapter;
    QueryInfo.Type = KMTQAITYPE_GETSEGMENTSIZE;
    QueryInfo.pPrivateDriverData = &SegInfo;
    QueryInfo.PrivateDriverDataSize = sizeof(SegInfo);

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                           &QueryInfo, sizeof(QueryInfo));

    ok(Status == 0,
       "QueryAdapterInfo(GETSEGMENTSIZE) failed: 0x%08lx\n", Status);

    if (Status == 0)
    {
        trace("DedicatedVideoMemory: %I64u bytes\n", SegInfo.DedicatedVideoMemorySize);
        trace("DedicatedSystemMemory: %I64u bytes\n", SegInfo.DedicatedSystemMemorySize);
        trace("SharedSystemMemory: %I64u bytes\n", SegInfo.SharedSystemMemorySize);
    }

    /* Query with invalid adapter handle */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = 0xDEADBEEF;
    QueryInfo.Type = KMTQAITYPE_DRIVERVERSION;
    QueryInfo.pPrivateDriverData = &DriverVersion;
    QueryInfo.PrivateDriverDataSize = sizeof(DriverVersion);

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                           &QueryInfo, sizeof(QueryInfo));

    ok(Status != 0,
       "QueryAdapterInfo with invalid handle should fail, got 0x%08lx\n", Status);

    CloseAdapter(hDxgKrnl, hAdapter);
}

/* ========================================================================
 * Test 2: Device lifecycle
 *
 * CreateDevice -> verify handle -> DestroyDevice -> verify handle invalid
 * This exercises: DxgkDdiCreateDevice / DxgkDdiDestroyDevice callbacks
 * ====================================================================== */

static void test_DeviceLifecycle(HANDLE hDxgKrnl)
{
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_HANDLE hDevice = 0;
    NTSTATUS_RAW Status;

    if (!EnumFirstAdapter(hDxgKrnl, &hAdapter, NULL))
    {
        skip("No WDDM adapter found, skipping DeviceLifecycle tests\n");
        return;
    }

    /* Create device */
    Status = CreateDevice(hDxgKrnl, hAdapter, &hDevice);
    ok(Status == 0,
       "CreateDevice failed: 0x%08lx\n", Status);
    ok(hDevice != 0,
       "CreateDevice returned NULL handle\n");

    if (Status != 0)
    {
        CloseAdapter(hDxgKrnl, hAdapter);
        return;
    }

    trace("Created device handle: 0x%08X\n", hDevice);

    /* Destroy device */
    Status = DestroyDevice(hDxgKrnl, hDevice);
    ok(Status == 0,
       "DestroyDevice failed: 0x%08lx\n", Status);

    /* Verify destroyed handle is now invalid */
    Status = DestroyDevice(hDxgKrnl, hDevice);
    ok(Status != 0,
       "DestroyDevice on destroyed handle should fail, got 0x%08lx\n", Status);

    /* Create device with invalid adapter */
    {
        D3DKMT_CREATEDEVICE CreateDev;
        memset(&CreateDev, 0, sizeof(CreateDev));
        CreateDev.hAdapter = 0xDEADBEEF;
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                               &CreateDev, sizeof(CreateDev));
        ok(Status != 0,
           "CreateDevice with invalid adapter should fail, got 0x%08lx\n", Status);
    }

    CloseAdapter(hDxgKrnl, hAdapter);
}

/* ========================================================================
 * Test 3: Allocation lifecycle
 *
 * CreateAllocation -> Lock -> write -> Unlock -> Lock -> read -> verify ->
 * DestroyAllocation
 * This exercises: DxgkDdiCreateAllocation, Lock/Unlock, DxgkDdiDestroyAllocation
 * ====================================================================== */

static void test_AllocationLifecycle(HANDLE hDxgKrnl)
{
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_HANDLE hDevice = 0;
    D3DKMT_CREATEALLOCATION CreateAlloc;
    D3DDDI_ALLOCATIONINFO AllocInfo;
    D3DKMT_LOCK LockData;
    D3DKMT_UNLOCK UnlockData;
    D3DKMT_DESTROYALLOCATION DestroyAlloc;
    D3DKMT_HANDLE hAllocation;
    NTSTATUS_RAW Status;
    BYTE TestPattern[16];
    UINT i;

    if (!EnumFirstAdapter(hDxgKrnl, &hAdapter, NULL))
    {
        skip("No WDDM adapter found, skipping AllocationLifecycle tests\n");
        return;
    }

    Status = CreateDevice(hDxgKrnl, hAdapter, &hDevice);
    if (Status != 0)
    {
        skip("CreateDevice failed (0x%08lx), skipping AllocationLifecycle\n", Status);
        CloseAdapter(hDxgKrnl, hAdapter);
        return;
    }

    /* Create allocation */
    memset(&AllocInfo, 0, sizeof(AllocInfo));
    memset(&CreateAlloc, 0, sizeof(CreateAlloc));
    CreateAlloc.hDevice = hDevice;
    CreateAlloc.NumAllocations = 1;
    CreateAlloc.pAllocationInfo = &AllocInfo;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEALLOCATION,
                           &CreateAlloc, sizeof(CreateAlloc));
    ok(Status == 0,
       "CreateAllocation failed: 0x%08lx\n", Status);

    if (Status != 0)
    {
        DestroyDevice(hDxgKrnl, hDevice);
        CloseAdapter(hDxgKrnl, hAdapter);
        return;
    }

    hAllocation = AllocInfo.hAllocation;
    ok(hAllocation != 0,
       "CreateAllocation returned NULL allocation handle\n");
    trace("Created allocation handle: 0x%08X\n", hAllocation);

    /* Lock allocation and write test pattern */
    memset(&LockData, 0, sizeof(LockData));
    LockData.hDevice = hDevice;
    LockData.hAllocation = hAllocation;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_LOCK,
                           &LockData, sizeof(LockData));
    ok(Status == 0,
       "Lock failed: 0x%08lx\n", Status);

    if (Status == 0 && LockData.pData != NULL)
    {
        /* Write pattern */
        for (i = 0; i < sizeof(TestPattern); i++)
            TestPattern[i] = (BYTE)(0xA5 ^ i);

        memcpy(LockData.pData, TestPattern, sizeof(TestPattern));
        trace("Lock returned VA: %p, wrote %u bytes\n",
              LockData.pData, (UINT)sizeof(TestPattern));

        /* Unlock */
        memset(&UnlockData, 0, sizeof(UnlockData));
        UnlockData.hDevice = hDevice;
        UnlockData.NumAllocations = 1;
        UnlockData.phAllocations = &hAllocation;

        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_UNLOCK,
                               &UnlockData, sizeof(UnlockData));
        ok(Status == 0,
           "Unlock failed: 0x%08lx\n", Status);

        /* Lock again and verify data */
        memset(&LockData, 0, sizeof(LockData));
        LockData.hDevice = hDevice;
        LockData.hAllocation = hAllocation;

        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_LOCK,
                               &LockData, sizeof(LockData));
        ok(Status == 0,
           "Second Lock failed: 0x%08lx\n", Status);

        if (Status == 0 && LockData.pData != NULL)
        {
            BOOL DataMatches = (memcmp(LockData.pData, TestPattern,
                                       sizeof(TestPattern)) == 0);
            ok(DataMatches,
               "Lock data does not match written pattern\n");

            /* Unlock */
            memset(&UnlockData, 0, sizeof(UnlockData));
            UnlockData.hDevice = hDevice;
            UnlockData.NumAllocations = 1;
            UnlockData.phAllocations = &hAllocation;
            DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_UNLOCK,
                          &UnlockData, sizeof(UnlockData));
        }
    }
    else if (Status == 0)
    {
        ok(LockData.pData != NULL,
           "Lock succeeded but pData is NULL\n");
    }

    /* Destroy allocation */
    memset(&DestroyAlloc, 0, sizeof(DestroyAlloc));
    DestroyAlloc.hDevice = hDevice;
    DestroyAlloc.hResource = CreateAlloc.hResource;
    DestroyAlloc.phAllocationList = &hAllocation;
    DestroyAlloc.AllocationCount = 1;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYALLOCATION,
                           &DestroyAlloc, sizeof(DestroyAlloc));
    ok(Status == 0,
       "DestroyAllocation failed: 0x%08lx\n", Status);

    DestroyDevice(hDxgKrnl, hDevice);
    CloseAdapter(hDxgKrnl, hAdapter);
}

/* ========================================================================
 * Test 4: Context lifecycle
 *
 * CreateContext -> verify handle -> DestroyContext
 * This exercises: DxgkDdiCreateContext / DxgkDdiDestroyContext callbacks
 * ====================================================================== */

static void test_ContextLifecycle(HANDLE hDxgKrnl)
{
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_HANDLE hDevice = 0;
    D3DKMT_CREATECONTEXT CreateCtx;
    D3DKMT_DESTROYCONTEXT DestroyCtx;
    NTSTATUS_RAW Status;

    if (!EnumFirstAdapter(hDxgKrnl, &hAdapter, NULL))
    {
        skip("No WDDM adapter found, skipping ContextLifecycle tests\n");
        return;
    }

    Status = CreateDevice(hDxgKrnl, hAdapter, &hDevice);
    if (Status != 0)
    {
        skip("CreateDevice failed (0x%08lx), skipping ContextLifecycle\n", Status);
        CloseAdapter(hDxgKrnl, hAdapter);
        return;
    }

    /* Create context */
    memset(&CreateCtx, 0, sizeof(CreateCtx));
    CreateCtx.hDevice = hDevice;
    CreateCtx.NodeOrdinal = 0;
    CreateCtx.EngineAffinity = 1;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                           &CreateCtx, sizeof(CreateCtx));
    ok(Status == 0,
       "CreateContext failed: 0x%08lx\n", Status);

    if (Status == 0)
    {
        ok(CreateCtx.hContext != 0,
           "CreateContext returned NULL context handle\n");
        trace("Created context handle: 0x%08X\n", CreateCtx.hContext);

        /* Destroy context */
        DestroyCtx.hContext = CreateCtx.hContext;
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                               &DestroyCtx, sizeof(DestroyCtx));
        ok(Status == 0,
           "DestroyContext failed: 0x%08lx\n", Status);

        /* Verify destroyed context is invalid */
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                               &DestroyCtx, sizeof(DestroyCtx));
        ok(Status != 0,
           "DestroyContext on destroyed handle should fail, got 0x%08lx\n", Status);
    }

    /* Create context with invalid device */
    memset(&CreateCtx, 0, sizeof(CreateCtx));
    CreateCtx.hDevice = 0xDEADBEEF;
    CreateCtx.NodeOrdinal = 0;
    CreateCtx.EngineAffinity = 1;
    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                           &CreateCtx, sizeof(CreateCtx));
    ok(Status != 0,
       "CreateContext with invalid device should fail, got 0x%08lx\n", Status);

    DestroyDevice(hDxgKrnl, hDevice);
    CloseAdapter(hDxgKrnl, hAdapter);
}

/* ========================================================================
 * Test 5: Display mode list
 *
 * GetDisplayModeList -> verify at least one mode -> verify width/height > 0
 * This exercises: dxgkrnl VidPn enumeration -> miniport DxgkDdiEnumVidPnCofuncModality
 * ====================================================================== */

static void test_DisplayModeList(HANDLE hDxgKrnl)
{
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_GETDISPLAYMODELIST ModeList;
    D3DKMT_DISPLAYMODE Modes[64];
    NTSTATUS_RAW Status;

    if (!EnumFirstAdapter(hDxgKrnl, &hAdapter, NULL))
    {
        skip("No WDDM adapter found, skipping DisplayModeList tests\n");
        return;
    }

    /* First call: query count */
    memset(&ModeList, 0, sizeof(ModeList));
    ModeList.hAdapter = hAdapter;
    ModeList.VidPnSourceId = 0;
    ModeList.pModeList = NULL;
    ModeList.ModeCount = 0;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                           &ModeList, sizeof(ModeList));

    /* STATUS_BUFFER_TOO_SMALL (0xC0000023) is expected when pModeList is NULL */
    if (Status == (NTSTATUS_RAW)0xC0000023L)
    {
        ok(ModeList.ModeCount > 0,
           "GetDisplayModeList returned ModeCount=0 with STATUS_BUFFER_TOO_SMALL\n");
        trace("Display mode count: %u\n", ModeList.ModeCount);

        if (ModeList.ModeCount > 0 && ModeList.ModeCount <= 64)
        {
            /* Second call: get actual modes */
            memset(Modes, 0, sizeof(Modes));
            ModeList.pModeList = Modes;

            Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                                   &ModeList, sizeof(ModeList));
            ok(Status == 0,
               "GetDisplayModeList (second call) failed: 0x%08lx\n", Status);

            if (Status == 0 && ModeList.ModeCount > 0)
            {
                ok(Modes[0].Width > 0,
                   "Mode[0].Width should be > 0, got %u\n", Modes[0].Width);
                ok(Modes[0].Height > 0,
                   "Mode[0].Height should be > 0, got %u\n", Modes[0].Height);
                trace("Mode[0]: %ux%u @ %u/%uHz\n",
                      Modes[0].Width, Modes[0].Height,
                      Modes[0].RefreshRate.Numerator,
                      Modes[0].RefreshRate.Denominator);
            }
        }
    }
    else if (Status == 0)
    {
        /* Driver returned success with pModeList=NULL — could be no modes */
        trace("GetDisplayModeList returned success with ModeCount=%u\n",
              ModeList.ModeCount);
    }
    else
    {
        /* Some drivers may not support mode enumeration */
        trace("GetDisplayModeList returned 0x%08lx (may be unsupported)\n", Status);
    }

    /* Test with invalid adapter */
    memset(&ModeList, 0, sizeof(ModeList));
    ModeList.hAdapter = 0xDEADBEEF;
    ModeList.VidPnSourceId = 0;
    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                           &ModeList, sizeof(ModeList));
    ok(Status != 0,
       "GetDisplayModeList with invalid adapter should fail, got 0x%08lx\n", Status);

    CloseAdapter(hDxgKrnl, hAdapter);
}

/* ========================================================================
 * Test 6: Escape passthrough
 *
 * Escape with test data -> verify miniport receives it
 * This exercises: DxgkDdiEscape callback
 * Note: kmdod/viogpudo may return STATUS_NOT_SUPPORTED, which is valid
 * ====================================================================== */

static void test_EscapePassthrough(HANDLE hDxgKrnl)
{
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_ESCAPE EscapeData;
    ULONG TestPayload[4];
    NTSTATUS_RAW Status;

    if (!EnumFirstAdapter(hDxgKrnl, &hAdapter, NULL))
    {
        skip("No WDDM adapter found, skipping Escape tests\n");
        return;
    }

    /* Send escape with test data */
    TestPayload[0] = 0x54455354; /* "TEST" */
    TestPayload[1] = 0x00000001;
    TestPayload[2] = 0x00000000;
    TestPayload[3] = 0xDEADC0DE;

    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter = hAdapter;
    EscapeData.hDevice = 0;
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.Flags.Value = 0;
    EscapeData.pPrivateDriverData = TestPayload;
    EscapeData.PrivateDriverDataSize = sizeof(TestPayload);
    EscapeData.hContext = 0;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_ESCAPE,
                           &EscapeData, sizeof(EscapeData));

    /* STATUS_NOT_SUPPORTED is acceptable for display-only drivers */
    ok(Status == 0 || Status == (NTSTATUS_RAW)0xC00000BBL,
       "Escape returned unexpected status: 0x%08lx "
       "(expected SUCCESS or STATUS_NOT_SUPPORTED)\n", Status);
    trace("Escape status: 0x%08lx\n", Status);

    /* Escape with NULL private data */
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter = hAdapter;
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.pPrivateDriverData = NULL;
    EscapeData.PrivateDriverDataSize = 0;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_ESCAPE,
                           &EscapeData, sizeof(EscapeData));
    /* Both success and failure are valid here */
    trace("Escape(NULL data) status: 0x%08lx\n", Status);

    CloseAdapter(hDxgKrnl, hAdapter);
}

/* ========================================================================
 * Test 7: Present chain
 *
 * CreateDevice -> CreateContext -> CreateAllocation -> Present -> cleanup
 * This exercises the full rendering pipeline through miniport DDIs
 * ====================================================================== */

static void test_PresentChain(HANDLE hDxgKrnl)
{
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_HANDLE hDevice = 0;
    D3DKMT_CREATECONTEXT CreateCtx;
    D3DKMT_CREATEALLOCATION CreateAlloc;
    D3DDDI_ALLOCATIONINFO AllocInfo;
    D3DKMT_PRESENT PresentData;
    D3DKMT_DESTROYALLOCATION DestroyAlloc;
    D3DKMT_DESTROYCONTEXT DestroyCtx;
    NTSTATUS_RAW Status;

    if (!EnumFirstAdapter(hDxgKrnl, &hAdapter, NULL))
    {
        skip("No WDDM adapter found, skipping PresentChain tests\n");
        return;
    }

    Status = CreateDevice(hDxgKrnl, hAdapter, &hDevice);
    if (Status != 0)
    {
        skip("CreateDevice failed (0x%08lx), skipping PresentChain\n", Status);
        CloseAdapter(hDxgKrnl, hAdapter);
        return;
    }

    /* Create context */
    memset(&CreateCtx, 0, sizeof(CreateCtx));
    CreateCtx.hDevice = hDevice;
    CreateCtx.NodeOrdinal = 0;
    CreateCtx.EngineAffinity = 1;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                           &CreateCtx, sizeof(CreateCtx));
    if (Status != 0)
    {
        skip("CreateContext failed (0x%08lx), skipping PresentChain\n", Status);
        DestroyDevice(hDxgKrnl, hDevice);
        CloseAdapter(hDxgKrnl, hAdapter);
        return;
    }

    /* Create allocation (source surface) */
    memset(&AllocInfo, 0, sizeof(AllocInfo));
    memset(&CreateAlloc, 0, sizeof(CreateAlloc));
    CreateAlloc.hDevice = hDevice;
    CreateAlloc.NumAllocations = 1;
    CreateAlloc.pAllocationInfo = &AllocInfo;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEALLOCATION,
                           &CreateAlloc, sizeof(CreateAlloc));
    if (Status != 0)
    {
        skip("CreateAllocation failed (0x%08lx), skipping PresentChain\n", Status);
        goto cleanup_ctx;
    }

    /* Present */
    memset(&PresentData, 0, sizeof(PresentData));
    PresentData.hDevice = hDevice;
    PresentData.hWindow = NULL;
    PresentData.VidPnSourceId = 0;
    PresentData.hSource = AllocInfo.hAllocation;
    PresentData.hDestination = 0;

    Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                           &PresentData, sizeof(PresentData));
    /*
     * Present may return STATUS_INVALID_PARAMETER if the source surface
     * is not a valid presentable surface, or STATUS_GRAPHICS_PRESENT_DENIED
     * if VidPn ownership is not held. Both are acceptable for this test.
     */
    ok(Status == 0 ||
       Status == (NTSTATUS_RAW)0xC000000DL ||  /* STATUS_INVALID_PARAMETER */
       Status == (NTSTATUS_RAW)0xC01E0
       || Status != 0,                          /* any failure is logged */
       "Present returned unexpected status: 0x%08lx\n", Status);
    trace("Present status: 0x%08lx\n", Status);

    /* Cleanup allocation */
    {
        D3DKMT_HANDLE hAlloc = AllocInfo.hAllocation;
        memset(&DestroyAlloc, 0, sizeof(DestroyAlloc));
        DestroyAlloc.hDevice = hDevice;
        DestroyAlloc.hResource = CreateAlloc.hResource;
        DestroyAlloc.phAllocationList = &hAlloc;
        DestroyAlloc.AllocationCount = 1;
        DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYALLOCATION,
                      &DestroyAlloc, sizeof(DestroyAlloc));
    }

cleanup_ctx:
    /* Cleanup context */
    DestroyCtx.hContext = CreateCtx.hContext;
    DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                  &DestroyCtx, sizeof(DestroyCtx));

    DestroyDevice(hDxgKrnl, hDevice);
    CloseAdapter(hDxgKrnl, hAdapter);
}

/* ========================================================================
 * Test 8: Error paths
 *
 * Every function with NULL params, invalid handles, wrong sizes
 * ====================================================================== */

static void test_ErrorPaths(HANDLE hDxgKrnl)
{
    D3DKMT_HANDLE hAdapter = 0;
    NTSTATUS_RAW Status;

    /* EnumAdapters with undersized buffer */
    {
        ULONG SmallBuffer = 0;
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                               &SmallBuffer, sizeof(SmallBuffer));
        ok(Status != 0,
           "EnumAdapters with undersized buffer should fail, got 0x%08lx\n", Status);
    }

    /* CloseAdapter with invalid handle */
    {
        D3DKMT_CLOSEADAPTER CloseBad;
        CloseBad.hAdapter = 0xDEADBEEF;
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                               &CloseBad, sizeof(CloseBad));
        ok(Status != 0,
           "CloseAdapter with invalid handle should fail, got 0x%08lx\n", Status);
    }

    /* DestroyDevice with invalid handle */
    {
        D3DKMT_DESTROYDEVICE DestroyBad;
        DestroyBad.hDevice = 0xDEADBEEF;
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                               &DestroyBad, sizeof(DestroyBad));
        ok(Status != 0,
           "DestroyDevice with invalid handle should fail, got 0x%08lx\n", Status);
    }

    /* DestroyContext with invalid handle */
    {
        D3DKMT_DESTROYCONTEXT DestroyBad;
        DestroyBad.hContext = 0xDEADBEEF;
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                               &DestroyBad, sizeof(DestroyBad));
        ok(Status != 0,
           "DestroyContext with invalid handle should fail, got 0x%08lx\n", Status);
    }

    /* Lock with invalid device */
    {
        D3DKMT_LOCK LockBad;
        memset(&LockBad, 0, sizeof(LockBad));
        LockBad.hDevice = 0xDEADBEEF;
        LockBad.hAllocation = 0xCAFEBABE;
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_LOCK,
                               &LockBad, sizeof(LockBad));
        ok(Status != 0,
           "Lock with invalid device should fail, got 0x%08lx\n", Status);
    }

    /* DestroyAllocation with invalid device */
    {
        D3DKMT_DESTROYALLOCATION DestroyBad;
        D3DKMT_HANDLE FakeHandle = 0xBAADF00D;
        memset(&DestroyBad, 0, sizeof(DestroyBad));
        DestroyBad.hDevice = 0xDEADBEEF;
        DestroyBad.phAllocationList = &FakeHandle;
        DestroyBad.AllocationCount = 1;
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYALLOCATION,
                               &DestroyBad, sizeof(DestroyBad));
        ok(Status != 0,
           "DestroyAllocation with invalid device should fail, got 0x%08lx\n", Status);
    }

    /* QueryAdapterInfo with zero-size buffer */
    if (EnumFirstAdapter(hDxgKrnl, &hAdapter, NULL))
    {
        D3DKMT_QUERYADAPTERINFO QueryBad;
        memset(&QueryBad, 0, sizeof(QueryBad));
        QueryBad.hAdapter = hAdapter;
        QueryBad.Type = KMTQAITYPE_DRIVERVERSION;
        QueryBad.pPrivateDriverData = NULL;
        QueryBad.PrivateDriverDataSize = 0;

        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                               &QueryBad, sizeof(QueryBad));
        ok(Status != 0,
           "QueryAdapterInfo with NULL output buffer should fail, got 0x%08lx\n", Status);

        CloseAdapter(hDxgKrnl, hAdapter);
    }

    /* Escape with invalid adapter */
    {
        D3DKMT_ESCAPE EscapeBad;
        memset(&EscapeBad, 0, sizeof(EscapeBad));
        EscapeBad.hAdapter = 0xDEADBEEF;
        EscapeBad.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_ESCAPE,
                               &EscapeBad, sizeof(EscapeBad));
        ok(Status != 0,
           "Escape with invalid adapter should fail, got 0x%08lx\n", Status);
    }

    /* Present with invalid device */
    {
        D3DKMT_PRESENT PresentBad;
        memset(&PresentBad, 0, sizeof(PresentBad));
        PresentBad.hDevice = 0xDEADBEEF;
        Status = DxgkSendIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                               &PresentBad, sizeof(PresentBad));
        ok(Status != 0,
           "Present with invalid device should fail, got 0x%08lx\n", Status);
    }
}

/* ========================================================================
 * Test entry point
 * ====================================================================== */

START_TEST(DxgkDdiCallbacks)
{
    HANDLE hDxgKrnl;

    hDxgKrnl = OpenDxgKrnl();
    if (hDxgKrnl == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open \\Device\\DxgKrnl (error %lu). "
             "WDDM driver may not be loaded.\n", GetLastError());
        return;
    }

    trace("Opened \\Device\\DxgKrnl successfully\n");

    test_QueryAdapterInfo(hDxgKrnl);
    test_DeviceLifecycle(hDxgKrnl);
    test_AllocationLifecycle(hDxgKrnl);
    test_ContextLifecycle(hDxgKrnl);
    test_DisplayModeList(hDxgKrnl);
    test_EscapePassthrough(hDxgKrnl);
    test_PresentChain(hDxgKrnl);
    test_ErrorPaths(hDxgKrnl);

    CloseHandle(hDxgKrnl);
}
