/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for D3DKMT allocation management
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests: D3DKMTCreateAllocation, D3DKMTDestroyAllocation, D3DKMTLock,
 *        D3DKMTUnlock, D3DKMTQueryAllocationResidency,
 *        D3DKMTSetAllocationPriority, D3DKMTQueryResourceInfo,
 *        D3DKMTOpenResource, D3DKMTGetSharedPrimaryHandle
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef NTSTATUS (APIENTRY *PFN_D3DKMT_NULLPROC)(void *);

/*
 * On Win11 ARM64 several gdi32 D3DKMT thunks dereference the argument before
 * validating it, so passing a literal NULL access-violates -- and PSEH2 does
 * NOT reliably catch a hardware AV on this target (verified: a literal-NULL
 * D3DKMTCreateAllocation crashes the process despite _SEH2_TRY). Pass a pointer
 * to a zeroed buffer instead: valid memory whose all-zero contents (null
 * handles, zero counts) the kernel still rejects with a status, so we exercise
 * parameter validation crash-free.
 */
static void
TestNullD3DKMTCall(const char *Name, FARPROC Proc)
{
    static unsigned char ZeroArg[4096];
    NTSTATUS Status;

    memset(ZeroArg, 0, sizeof(ZeroArg));
    Status = ((PFN_D3DKMT_NULLPROC)Proc)((void *)ZeroArg);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("%s returned STATUS_PROCEDURE_NOT_FOUND\n", Name);
        return;
    }

    ok_failed(Status,
       "%s with a zeroed argument must be refused, got 0x%lx\n", Name, Status);
}

static void Test_CreateAllocation_NullParam(void)
{
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);

    /* The Win11 gdi32 CreateAllocation wrapper dereferences pAllocationInfo
     * before the syscall, so a NULL/zeroed argument access-violates uncatchably
     * (PSEH2 does not catch the AV on ARM64). Pass a valid pAllocationInfo with
     * a bogus device instead: the kernel rejects it with a status, no fault. */
    memset(&ai, 0, sizeof(ai));
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = (D3DKMT_HANDLE)0xDEAD0001;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &ai;
    Status = pfnD3DKMTCreateAllocation(&ca);
    ok_failed(Status,
       "D3DKMTCreateAllocation(bogus device) must be refused, got 0x%lx\n", Status);
}

static void Test_DestroyAllocation_NullParam(void)
{
    LOAD_D3DKMT(D3DKMTDestroyAllocation);
    TestNullD3DKMTCall("D3DKMTDestroyAllocation", (FARPROC)pfnD3DKMTDestroyAllocation);
}

static void Test_DestroyAllocation_InvalidHandle(void)
{
    D3DKMT_DESTROYALLOCATION DestroyData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTDestroyAllocation);

    memset(&DestroyData, 0, sizeof(DestroyData));
    DestroyData.hDevice = 0xBAD0CAFE;
    DestroyData.hResource = 0;
    DestroyData.AllocationCount = 0;
    DestroyData.phAllocationList = NULL;
    Status = pfnD3DKMTDestroyAllocation(&DestroyData);
    ok_failed(Status,
       "D3DKMTDestroyAllocation with invalid device should fail, got 0x%lx\n",
       Status);
}

static void Test_CreateDestroyAllocation(void)
{
    D3DKMT_CREATEALLOCATION CreateData;
    D3DDDI_ALLOCATIONINFO AllocationInfo;
    UINT PrivateDataSizeOnly = 0x1000;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for allocation tests\n");
        return;
    }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("Cannot create device for allocation tests\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* A zero-allocation request is malformed before it reaches the miniport.
     * Always pass a valid pAllocationInfo -- the Win11 gdi32 wrapper derefs it
     * before the syscall, so NULL would access-violate. */
    memset(&AllocationInfo, 0, sizeof(AllocationInfo));
    memset(&CreateData, 0, sizeof(CreateData));
    CreateData.hDevice = hDevice;
    CreateData.NumAllocations = 0;
    CreateData.pAllocationInfo = &AllocationInfo;
    Status = pfnD3DKMTCreateAllocation(&CreateData);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTCreateAllocation not implemented\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    ok_failed(Status,
       "D3DKMTCreateAllocation with zero allocations returned 0x%lx\n",
       Status);

    memset(&CreateData, 0, sizeof(CreateData));
    memset(&AllocationInfo, 0, sizeof(AllocationInfo));
    CreateData.hDevice = hDevice;
    CreateData.NumAllocations = 1;
    CreateData.pAllocationInfo = &AllocationInfo;
    CreateData.PrivateDriverDataSize = sizeof(PrivateDataSizeOnly);
    CreateData.pPrivateDriverData = NULL;
    Status = pfnD3DKMTCreateAllocation(&CreateData);
    ok_failed(Status,
       "D3DKMTCreateAllocation with NULL private driver data returned 0x%lx\n",
       Status);

    memset(&CreateData, 0, sizeof(CreateData));
    memset(&AllocationInfo, 0, sizeof(AllocationInfo));
    CreateData.hDevice = hDevice;
    CreateData.NumAllocations = 1;
    CreateData.pAllocationInfo = &AllocationInfo;
    CreateData.PrivateRuntimeDataSize = sizeof(PrivateDataSizeOnly);
    CreateData.pPrivateRuntimeData = NULL;
    Status = pfnD3DKMTCreateAllocation(&CreateData);
    ok_failed(Status,
       "D3DKMTCreateAllocation with NULL private runtime data returned 0x%lx\n",
       Status);

    memset(&CreateData, 0, sizeof(CreateData));
    memset(&AllocationInfo, 0, sizeof(AllocationInfo));
    AllocationInfo.PrivateDriverDataSize = sizeof(PrivateDataSizeOnly);
    AllocationInfo.pPrivateDriverData = NULL;
    CreateData.hDevice = hDevice;
    CreateData.NumAllocations = 1;
    CreateData.pAllocationInfo = &AllocationInfo;
    Status = pfnD3DKMTCreateAllocation(&CreateData);
    ok_failed(Status,
       "D3DKMTCreateAllocation with NULL allocation private data returned 0x%lx\n",
       Status);

    /* Invalid device for create (valid pAllocationInfo avoids the wrapper AV). */
    memset(&CreateData, 0, sizeof(CreateData));
    memset(&AllocationInfo, 0, sizeof(AllocationInfo));
    CreateData.hDevice = 0xDEADBEEF;
    CreateData.NumAllocations = 1;
    CreateData.pAllocationInfo = &AllocationInfo;
    Status = pfnD3DKMTCreateAllocation(&CreateData);
    ok_failed(Status,
       "D3DKMTCreateAllocation with invalid device should fail, got 0x%lx\n",
       Status);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void Test_Lock_NullParam(void)
{
    LOAD_D3DKMT(D3DKMTLock);
    TestNullD3DKMTCall("D3DKMTLock", (FARPROC)pfnD3DKMTLock);
}

static void Test_Unlock_NullParam(void)
{
    LOAD_D3DKMT(D3DKMTUnlock);
    TestNullD3DKMTCall("D3DKMTUnlock", (FARPROC)pfnD3DKMTUnlock);
}

static void Test_Lock_InvalidHandle(void)
{
    D3DKMT_LOCK LockData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTLock);

    memset(&LockData, 0, sizeof(LockData));
    LockData.hDevice = 0xBAD0CAFE;
    LockData.hAllocation = 0xDEADBEEF;
    Status = pfnD3DKMTLock(&LockData);
    ok_failed(Status,
       "D3DKMTLock with invalid handles should fail, got 0x%lx\n", Status);
}

static void Test_Unlock_InvalidHandle(void)
{
    D3DKMT_UNLOCK UnlockData;
    D3DKMT_HANDLE BadAlloc = 0xDEADBEEF;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTUnlock);

    memset(&UnlockData, 0, sizeof(UnlockData));
    UnlockData.hDevice = 0xBAD0CAFE;
    UnlockData.NumAllocations = 1;
    UnlockData.phAllocations = &BadAlloc;
    Status = pfnD3DKMTUnlock(&UnlockData);
    ok_failed(Status,
       "D3DKMTUnlock with invalid handles should fail, got 0x%lx\n", Status);
}

static void Test_QueryAllocationResidency_NullParam(void)
{
    LOAD_D3DKMT(D3DKMTQueryAllocationResidency);
    TestNullD3DKMTCall("D3DKMTQueryAllocationResidency", (FARPROC)pfnD3DKMTQueryAllocationResidency);
}

static void Test_SetAllocationPriority_NullParam(void)
{
    LOAD_D3DKMT(D3DKMTSetAllocationPriority);
    TestNullD3DKMTCall("D3DKMTSetAllocationPriority", (FARPROC)pfnD3DKMTSetAllocationPriority);
}

static void Test_QueryResourceInfo_NullParam(void)
{
    LOAD_D3DKMT(D3DKMTQueryResourceInfo);
    TestNullD3DKMTCall("D3DKMTQueryResourceInfo", (FARPROC)pfnD3DKMTQueryResourceInfo);
}

static void Test_OpenResource_NullParam(void)
{
    LOAD_D3DKMT(D3DKMTOpenResource);
    TestNullD3DKMTCall("D3DKMTOpenResource", (FARPROC)pfnD3DKMTOpenResource);
}

static void Test_GetSharedPrimaryHandle(void)
{
    D3DKMT_GETSHAREDPRIMARYHANDLE Data;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTGetSharedPrimaryHandle);

    /* NULL parameter */
    TestNullD3DKMTCall("D3DKMTGetSharedPrimaryHandle", (FARPROC)pfnD3DKMTGetSharedPrimaryHandle);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for GetSharedPrimaryHandle test\n");
        return;
    }

    /* Valid adapter, VidPnSourceId 0 */
    memset(&Data, 0, sizeof(Data));
    Data.hAdapter = hAdapter;
    Data.VidPnSourceId = 0;
    Status = pfnD3DKMTGetSharedPrimaryHandle(&Data);
    /* May succeed (returns 0 handle if no shared primary) or fail */
    if (NT_SUCCESS(Status))
    {
        trace("GetSharedPrimaryHandle returned handle 0x%lx\n",
              (unsigned long)Data.hSharedPrimary);
    }

    /* Invalid adapter */
    memset(&Data, 0, sizeof(Data));
    Data.hAdapter = 0xBAD0CAFE;
    Data.VidPnSourceId = 0;
    Status = pfnD3DKMTGetSharedPrimaryHandle(&Data);
    ok_failed(Status,
       "GetSharedPrimaryHandle with invalid adapter should fail, got 0x%lx\n",
       Status);

    CloseAdapter(hAdapter);
}

START_TEST(D3dkmtAlloc)
{
    Test_CreateAllocation_NullParam();
    Test_DestroyAllocation_NullParam();
    Test_DestroyAllocation_InvalidHandle();
    Test_CreateDestroyAllocation();
    Test_Lock_NullParam();
    Test_Unlock_NullParam();
    Test_Lock_InvalidHandle();
    Test_Unlock_InvalidHandle();
    Test_QueryAllocationResidency_NullParam();
    Test_SetAllocationPriority_NullParam();
    Test_QueryResourceInfo_NullParam();
    Test_OpenResource_NullParam();
    Test_GetSharedPrimaryHandle();
}
