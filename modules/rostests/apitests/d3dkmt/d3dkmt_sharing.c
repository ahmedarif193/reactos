/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT resource/object sharing and NT-handle interop contract tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Exercises the WDDM cross-process sharing surface: legacy global-share
 * resource open/query, the WDDM 1.2+ secure NT-handle sharing model
 * (ShareObjects then Open/Query FromNtHandle, named opens), keyed-mutex/sync
 * NT-handle opens, and the shared-resource LUID / access / primary-handle
 * queries.
 *
 * Tests:
 *   D3DKMTShareObjects, D3DKMTOpenResource, D3DKMTOpenResource2,
 *   D3DKMTOpenResourceFromNtHandle, D3DKMTQueryResourceInfo,
 *   D3DKMTQueryResourceInfoFromNtHandle, D3DKMTOpenNtHandleFromName,
 *   D3DKMTOpenSyncObjectFromNtHandle, D3DKMTOpenSyncObjectFromNtHandle2,
 *   D3DKMTOpenSyncObjectNtHandleFromName, D3DKMTGetSharedResourceAdapterLuid,
 *   D3DKMTCheckSharedResourceAccess, D3DKMTGetSharedPrimaryHandle
 *
 * The portable malformed-input contract runs on every adapter tier.  When the
 * display adapter exports a shared primary, the suite also drives the complete
 * legacy QueryResourceInfo/OpenResource/DestroyAllocation lifecycle on two
 * logical devices.  Adapters with no kernel-managed shared primary skip that
 * positive flow.
 */

#include "precomp.h"

/* Clearly-invalid handle constants. D3DKMT_HANDLE is a 32-bit value; NT handles
 * are pointer-sized and always 4-aligned, so 0xDEADBEEF is guaranteed bogus. */
#define BAD_D3DKMT_HANDLE   ((D3DKMT_HANDLE)0xDEADBEEF)
#define BAD_DEVICE_HANDLE   ((D3DKMT_HANDLE)0xBAD0CAFE)
#define BAD_NT_HANDLE       ((HANDLE)(ULONG_PTR)0xDEADBEEF)
#define MAX_TEST_SHARED_ALLOCATIONS 4096

typedef struct _SHARED_OPEN_BUFFERS
{
    D3DKMT_OPENRESOURCE Request;
    D3DDDI_OPENALLOCATIONINFO *AllocationInfo;
    PVOID PrivateRuntimeData;
    PVOID ResourcePrivateDriverData;
    PVOID TotalPrivateDriverData;
} SHARED_OPEN_BUFFERS;

static void FreeSharedOpenBuffers(SHARED_OPEN_BUFFERS *Buffers)
{
    HANDLE Heap = GetProcessHeap();

    if (Buffers->TotalPrivateDriverData != NULL)
        HeapFree(Heap, 0, Buffers->TotalPrivateDriverData);
    if (Buffers->ResourcePrivateDriverData != NULL)
        HeapFree(Heap, 0, Buffers->ResourcePrivateDriverData);
    if (Buffers->PrivateRuntimeData != NULL)
        HeapFree(Heap, 0, Buffers->PrivateRuntimeData);
    if (Buffers->AllocationInfo != NULL)
        HeapFree(Heap, 0, Buffers->AllocationInfo);
    memset(Buffers, 0, sizeof(*Buffers));
}

static BOOL InitializeSharedOpenBuffers(SHARED_OPEN_BUFFERS *Buffers, D3DKMT_HANDLE hDevice, D3DKMT_HANDLE hGlobalShare, const D3DKMT_QUERYRESOURCEINFO *Query)
{
    HANDLE Heap = GetProcessHeap();

    memset(Buffers, 0, sizeof(*Buffers));
    if (Query->NumAllocations == 0 || Query->NumAllocations > MAX_TEST_SHARED_ALLOCATIONS)
        return FALSE;

    Buffers->AllocationInfo = HeapAlloc(Heap, HEAP_ZERO_MEMORY, (SIZE_T)Query->NumAllocations * sizeof(*Buffers->AllocationInfo));
    if (Buffers->AllocationInfo == NULL)
        goto Failure;
    if (Query->PrivateRuntimeDataSize != 0)
        Buffers->PrivateRuntimeData = HeapAlloc(Heap, HEAP_ZERO_MEMORY, Query->PrivateRuntimeDataSize);
    if (Query->PrivateRuntimeDataSize != 0 && Buffers->PrivateRuntimeData == NULL)
        goto Failure;
    if (Query->ResourcePrivateDriverDataSize != 0)
        Buffers->ResourcePrivateDriverData = HeapAlloc(Heap, HEAP_ZERO_MEMORY, Query->ResourcePrivateDriverDataSize);
    if (Query->ResourcePrivateDriverDataSize != 0 && Buffers->ResourcePrivateDriverData == NULL)
        goto Failure;
    if (Query->TotalPrivateDriverDataSize != 0)
        Buffers->TotalPrivateDriverData = HeapAlloc(Heap, HEAP_ZERO_MEMORY, Query->TotalPrivateDriverDataSize);
    if (Query->TotalPrivateDriverDataSize != 0 && Buffers->TotalPrivateDriverData == NULL)
        goto Failure;

    Buffers->Request.hDevice = hDevice;
    Buffers->Request.hGlobalShare = hGlobalShare;
    Buffers->Request.NumAllocations = Query->NumAllocations;
    Buffers->Request.pOpenAllocationInfo = Buffers->AllocationInfo;
    Buffers->Request.pPrivateRuntimeData = Buffers->PrivateRuntimeData;
    Buffers->Request.PrivateRuntimeDataSize = Query->PrivateRuntimeDataSize;
    Buffers->Request.pResourcePrivateDriverData = Buffers->ResourcePrivateDriverData;
    Buffers->Request.ResourcePrivateDriverDataSize = Query->ResourcePrivateDriverDataSize;
    Buffers->Request.pTotalPrivateDriverDataBuffer = Buffers->TotalPrivateDriverData;
    Buffers->Request.TotalPrivateDriverDataBufferSize = Query->TotalPrivateDriverDataSize;
    return TRUE;

Failure:
    FreeSharedOpenBuffers(Buffers);
    return FALSE;
}

static NTSTATUS DestroyOpenedResource(PFND3DKMT_DESTROYALLOCATION DestroyAllocation, D3DKMT_HANDLE hDevice, D3DKMT_HANDLE hResource)
{
    D3DKMT_DESTROYALLOCATION Destroy;

    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hDevice = hDevice;
    Destroy.hResource = hResource;
    return DestroyAllocation(&Destroy);
}

/*
 * A sharing entry point must refuse a malformed / bogus request the way Windows
 * does: either with a non-success NTSTATUS, or by raising an access violation in
 * the user-mode thunk. Both count as "correctly refused"; only NT_SUCCESS is the
 * bug. SEH keeps a faulting thunk from tearing down the subtest. We deliberately
 * never assert a *specific* status code, so the check stays green across Win11
 * driver revisions and across the display-only / WARP adapter tiers.
 */
#define EXPECT_CALL_REFUSED(Name, CallExpr)                                       \
do {                                                                              \
    NTSTATUS _st = STATUS_SUCCESS;                                                \
    BOOL _faulted = FALSE;                                                        \
    _SEH2_TRY { _st = (CallExpr); }                                               \
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { _faulted = TRUE; }                  \
    _SEH2_END;                                                                    \
    ok(_faulted || !NT_SUCCESS(_st),                                              \
       Name " must be refused (error status or fault), got 0x%08lX%s\n",          \
       (long)_st, _faulted ? " (faulted)" : "");                                  \
} while (0)

/*
 * Load one entry point and open an "else" arm for the body, so several
 * functions can be swept in a single test without an early return skipping the
 * later ones: a missing export skips only its own block. Pair it with a braced
 * body, e.g.
 *      WITH_FN(PFND3DKMT_OPENRESOURCE, pOpen, "D3DKMTOpenResource") { ... }
 */
#define WITH_FN(Type, Var, Name)                                                  \
    Type Var = (Type)LoadD3DKMTProc(Name);                                        \
    if (!(Var)) { skip(Name " not exported by gdi32.dll\n"); } else

/* ------------------------------------------------------------------ */
/* NULL-argument contract across the whole sharing surface.            */
/* Each entry is loaded independently and skipped if gdi32 does not     */
/* export it, so the same binary is portable down-level.               */
/* ------------------------------------------------------------------ */
static void Test_Sharing_NullContract(void)
{
    CHECK_NULL_REJECTED(PFND3DKMT_OPENRESOURCE,                   "D3DKMTOpenResource");
    CHECK_NULL_REJECTED(PFND3DKMT_OPENRESOURCE2,                  "D3DKMTOpenResource2");
    CHECK_NULL_REJECTED(PFND3DKMT_OPENRESOURCEFROMNTHANDLE,       "D3DKMTOpenResourceFromNtHandle");
    CHECK_NULL_REJECTED(PFND3DKMT_QUERYRESOURCEINFO,              "D3DKMTQueryResourceInfo");
    CHECK_NULL_REJECTED(PFND3DKMT_QUERYRESOURCEINFOFROMNTHANDLE,  "D3DKMTQueryResourceInfoFromNtHandle");
    CHECK_NULL_REJECTED(PFND3DKMT_OPENNTHANDLEFROMNAME,           "D3DKMTOpenNtHandleFromName");
    CHECK_NULL_REJECTED(PFND3DKMT_OPENSYNCOBJECTFROMNTHANDLE,     "D3DKMTOpenSyncObjectFromNtHandle");
    CHECK_NULL_REJECTED(PFND3DKMT_OPENSYNCOBJECTFROMNTHANDLE2,    "D3DKMTOpenSyncObjectFromNtHandle2");
    CHECK_NULL_REJECTED(PFND3DKMT_OPENSYNCOBJECTNTHANDLEFROMNAME, "D3DKMTOpenSyncObjectNtHandleFromName");
    CHECK_NULL_REJECTED(PFND3DKMT_GETSHAREDRESOURCEADAPTERLUID,   "D3DKMTGetSharedResourceAdapterLuid");
    CHECK_NULL_REJECTED(PFND3DKMT_CHECKSHAREDRESOURCEACCESS,      "D3DKMTCheckSharedResourceAccess");
    CHECK_NULL_REJECTED(PFND3DKMT_GETSHAREDPRIMARYHANDLE,         "D3DKMTGetSharedPrimaryHandle");
}

/* ------------------------------------------------------------------ */
/* D3DKMTShareObjects -- multi-argument signature (no single struct).   */
/* Cannot use EXPECT_NULL_REJECTED; drive each malformed argument combo */
/* directly and assert refusal.                                        */
/* ------------------------------------------------------------------ */
static void Test_ShareObjects_Contract(void)
{
    D3DKMT_HANDLE  hObjs[D3DKMT_MAX_OBJECTS_PER_HANDLE + 1];
    HANDLE         hShared;
    UINT           i;

    LOADFN(PFND3DKMT_SHAREOBJECTS, pShareObjects, "D3DKMTShareObjects");

    for (i = 0; i < (D3DKMT_MAX_OBJECTS_PER_HANDLE + 1); i++)
        hObjs[i] = BAD_D3DKMT_HANDLE;

    /* cObjects == 0 is below the documented [1, MAX] range. */
    hShared = NULL;
    EXPECT_CALL_REFUSED("D3DKMTShareObjects(cObjects=0)",
        pShareObjects(0, NULL, NULL, GENERIC_ALL, &hShared));

    /* Non-zero count but NULL object list. */
    hShared = NULL;
    EXPECT_CALL_REFUSED("D3DKMTShareObjects(NULL list)",
        pShareObjects(1, NULL, NULL, GENERIC_ALL, &hShared));

    /* Valid-looking count/list of bogus object handles. */
    hShared = NULL;
    EXPECT_CALL_REFUSED("D3DKMTShareObjects(bogus objects)",
        pShareObjects(1, hObjs, NULL, GENERIC_ALL, &hShared));

    /* NULL output handle pointer. */
    EXPECT_CALL_REFUSED("D3DKMTShareObjects(NULL out)",
        pShareObjects(1, hObjs, NULL, GENERIC_ALL, NULL));

    /* cObjects above D3DKMT_MAX_OBJECTS_PER_HANDLE (array is sized to match). */
    hShared = NULL;
    EXPECT_CALL_REFUSED("D3DKMTShareObjects(count>max)",
        pShareObjects(D3DKMT_MAX_OBJECTS_PER_HANDLE + 1, hObjs, NULL, GENERIC_ALL, &hShared));
}

/* ------------------------------------------------------------------ */
/* Bogus-handle contract for the open/query family WITHOUT a device.    */
/* These run on every adapter tier (no render device required).        */
/* ------------------------------------------------------------------ */
static void Test_OpenQuery_NoDevice_BadHandles(void)
{
    /* D3DKMTOpenResource: bogus device + bogus legacy global-share handle. */
    WITH_FN(PFND3DKMT_OPENRESOURCE, pOpen, "D3DKMTOpenResource")
    {
        D3DKMT_OPENRESOURCE Data;
        memset(&Data, 0, sizeof(Data));
        Data.hDevice = BAD_DEVICE_HANDLE;
        Data.hGlobalShare = BAD_D3DKMT_HANDLE;
        Data.NumAllocations = 0;
        EXPECT_CALL_REFUSED("D3DKMTOpenResource(bogus)", pOpen(&Data));
    }

    /* D3DKMTOpenResource2: reserved/internal; skip if not exported. */
    WITH_FN(PFND3DKMT_OPENRESOURCE2, pOpen2, "D3DKMTOpenResource2")
    {
        D3DKMT_OPENRESOURCE Data;
        memset(&Data, 0, sizeof(Data));
        Data.hDevice = BAD_DEVICE_HANDLE;
        Data.hGlobalShare = BAD_D3DKMT_HANDLE;
        Data.NumAllocations = 0;
        EXPECT_CALL_REFUSED("D3DKMTOpenResource2(bogus)", pOpen2(&Data));
    }

    /* D3DKMTQueryResourceInfo: bogus device + bogus global-share handle. */
    WITH_FN(PFND3DKMT_QUERYRESOURCEINFO, pQuery, "D3DKMTQueryResourceInfo")
    {
        D3DKMT_QUERYRESOURCEINFO Data;
        memset(&Data, 0, sizeof(Data));
        Data.hDevice = BAD_DEVICE_HANDLE;
        Data.hGlobalShare = BAD_D3DKMT_HANDLE;
        EXPECT_CALL_REFUSED("D3DKMTQueryResourceInfo(bogus)", pQuery(&Data));
    }

    /* D3DKMTOpenResourceFromNtHandle: bogus device + bogus NT handle. */
    WITH_FN(PFND3DKMT_OPENRESOURCEFROMNTHANDLE, pOpenNt, "D3DKMTOpenResourceFromNtHandle")
    {
        D3DKMT_OPENRESOURCEFROMNTHANDLE Data;
        memset(&Data, 0, sizeof(Data));
        Data.hDevice = BAD_DEVICE_HANDLE;
        Data.hNtHandle = BAD_NT_HANDLE;
        Data.NumAllocations = 0;
        EXPECT_CALL_REFUSED("D3DKMTOpenResourceFromNtHandle(bogus)", pOpenNt(&Data));
    }

    /* D3DKMTQueryResourceInfoFromNtHandle: bogus device + bogus NT handle. */
    WITH_FN(PFND3DKMT_QUERYRESOURCEINFOFROMNTHANDLE, pQueryNt, "D3DKMTQueryResourceInfoFromNtHandle")
    {
        D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE Data;
        memset(&Data, 0, sizeof(Data));
        Data.hDevice = BAD_DEVICE_HANDLE;
        Data.hNtHandle = BAD_NT_HANDLE;
        EXPECT_CALL_REFUSED("D3DKMTQueryResourceInfoFromNtHandle(bogus)", pQueryNt(&Data));
    }

    /* D3DKMTOpenSyncObjectFromNtHandle: bogus NT handle. */
    WITH_FN(PFND3DKMT_OPENSYNCOBJECTFROMNTHANDLE, pOpenSync, "D3DKMTOpenSyncObjectFromNtHandle")
    {
        D3DKMT_OPENSYNCOBJECTFROMNTHANDLE Data;
        memset(&Data, 0, sizeof(Data));
        Data.hNtHandle = BAD_NT_HANDLE;
        EXPECT_CALL_REFUSED("D3DKMTOpenSyncObjectFromNtHandle(bogus)", pOpenSync(&Data));
    }

    /* D3DKMTOpenSyncObjectFromNtHandle2: bogus NT handle, no device. */
    WITH_FN(PFND3DKMT_OPENSYNCOBJECTFROMNTHANDLE2, pOpenSync2, "D3DKMTOpenSyncObjectFromNtHandle2")
    {
        D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Data;
        memset(&Data, 0, sizeof(Data));
        Data.hNtHandle = BAD_NT_HANDLE;
        Data.hDevice = BAD_DEVICE_HANDLE;
        EXPECT_CALL_REFUSED("D3DKMTOpenSyncObjectFromNtHandle2(bogus)", pOpenSync2(&Data));
    }
}

/* ------------------------------------------------------------------ */
/* Named-object opens: NT-handle-from-name with no (NULL) object        */
/* attributes must be refused. OBJECT_ATTRIBUTES is only forward-       */
/* declared in this TU, so we cannot build a named one; NULL is the     */
/* portable malformed input.                                           */
/* ------------------------------------------------------------------ */
static void Test_NameOpens_BadAttributes(void)
{
    WITH_FN(PFND3DKMT_OPENNTHANDLEFROMNAME, pOpen, "D3DKMTOpenNtHandleFromName")
    {
        D3DKMT_OPENNTHANDLEFROMNAME Data;
        memset(&Data, 0, sizeof(Data));
        Data.dwDesiredAccess = GENERIC_READ;
        Data.pObjAttrib = NULL;
        EXPECT_CALL_REFUSED("D3DKMTOpenNtHandleFromName(NULL attrib)", pOpen(&Data));
    }

    WITH_FN(PFND3DKMT_OPENSYNCOBJECTNTHANDLEFROMNAME, pOpenSync, "D3DKMTOpenSyncObjectNtHandleFromName")
    {
        D3DKMT_OPENSYNCOBJECTNTHANDLEFROMNAME Data;
        memset(&Data, 0, sizeof(Data));
        Data.dwDesiredAccess = GENERIC_READ;
        Data.pObjAttrib = NULL;
        EXPECT_CALL_REFUSED("D3DKMTOpenSyncObjectNtHandleFromName(NULL attrib)", pOpenSync(&Data));
    }
}

/* ------------------------------------------------------------------ */
/* D3DKMTGetSharedResourceAdapterLuid -- bogus legacy share handle and  */
/* bogus NT handle must both fail (no valid shared resource exists).    */
/* ------------------------------------------------------------------ */
static void Test_GetSharedResourceAdapterLuid_BadHandles(void)
{
    D3DKMT_GETSHAREDRESOURCEADAPTERLUID Data;

    LOADFN(PFND3DKMT_GETSHAREDRESOURCEADAPTERLUID, pGet, "D3DKMTGetSharedResourceAdapterLuid");

    /* Bogus legacy global-share handle, no NT handle. */
    memset(&Data, 0, sizeof(Data));
    Data.hGlobalShare = BAD_D3DKMT_HANDLE;
    Data.hNtHandle = NULL;
    EXPECT_CALL_REFUSED("D3DKMTGetSharedResourceAdapterLuid(bogus global-share)", pGet(&Data));

    /* Bogus NT handle, no legacy share handle. */
    memset(&Data, 0, sizeof(Data));
    Data.hGlobalShare = 0;
    Data.hNtHandle = BAD_NT_HANDLE;
    EXPECT_CALL_REFUSED("D3DKMTGetSharedResourceAdapterLuid(bogus NT handle)", pGet(&Data));
}

/* ------------------------------------------------------------------ */
/* D3DKMTCheckSharedResourceAccess -- bogus resource handle must fail.  */
/* ------------------------------------------------------------------ */
static void Test_CheckSharedResourceAccess_BadHandle(void)
{
    D3DKMT_CHECKSHAREDRESOURCEACCESS Data;

    LOADFN(PFND3DKMT_CHECKSHAREDRESOURCEACCESS, pCheck, "D3DKMTCheckSharedResourceAccess");

    /* Bogus resource handle. */
    memset(&Data, 0, sizeof(Data));
    Data.hResource = BAD_D3DKMT_HANDLE;
    Data.ClientPid = GetCurrentProcessId();
    EXPECT_CALL_REFUSED("D3DKMTCheckSharedResourceAccess(bogus resource)", pCheck(&Data));

    /* Zero resource handle is equally invalid. */
    memset(&Data, 0, sizeof(Data));
    Data.hResource = 0;
    Data.ClientPid = GetCurrentProcessId();
    EXPECT_CALL_REFUSED("D3DKMTCheckSharedResourceAccess(zero resource)", pCheck(&Data));
}

/* ------------------------------------------------------------------ */
/* With a real device (skip if none): the open/query-from-handle family */
/* must still reject a bogus share / NT handle. This drives the path    */
/* where the device handle is valid but the shared object is not.       */
/* ------------------------------------------------------------------ */
static void Test_OpenQuery_RealDevice_BadHandles(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1 for real-device sharing tests\n");
        return;
    }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("CreateDevice failed; cannot run real-device sharing tests\n");
        CloseAdapter(hAdapter);
        return;
    }

    WITH_FN(PFND3DKMT_QUERYRESOURCEINFO, pQuery, "D3DKMTQueryResourceInfo")
    {
        D3DKMT_QUERYRESOURCEINFO Data;
        memset(&Data, 0, sizeof(Data));
        Data.hDevice = hDevice;
        Data.hGlobalShare = BAD_D3DKMT_HANDLE;
        EXPECT_CALL_REFUSED("D3DKMTQueryResourceInfo(real dev, bogus share)", pQuery(&Data));
    }

    WITH_FN(PFND3DKMT_OPENRESOURCE, pOpen, "D3DKMTOpenResource")
    {
        D3DKMT_OPENRESOURCE Data;
        memset(&Data, 0, sizeof(Data));
        Data.hDevice = hDevice;
        Data.hGlobalShare = BAD_D3DKMT_HANDLE;
        Data.NumAllocations = 0;
        EXPECT_CALL_REFUSED("D3DKMTOpenResource(real dev, bogus share)", pOpen(&Data));
    }

    WITH_FN(PFND3DKMT_QUERYRESOURCEINFOFROMNTHANDLE, pQueryNt, "D3DKMTQueryResourceInfoFromNtHandle")
    {
        D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE Data;
        memset(&Data, 0, sizeof(Data));
        Data.hDevice = hDevice;
        Data.hNtHandle = BAD_NT_HANDLE;
        EXPECT_CALL_REFUSED("D3DKMTQueryResourceInfoFromNtHandle(real dev, bogus NT)", pQueryNt(&Data));
    }

    WITH_FN(PFND3DKMT_OPENRESOURCEFROMNTHANDLE, pOpenNt, "D3DKMTOpenResourceFromNtHandle")
    {
        D3DKMT_OPENRESOURCEFROMNTHANDLE Data;
        memset(&Data, 0, sizeof(Data));
        Data.hDevice = hDevice;
        Data.hNtHandle = BAD_NT_HANDLE;
        Data.NumAllocations = 0;
        EXPECT_CALL_REFUSED("D3DKMTOpenResourceFromNtHandle(real dev, bogus NT)", pOpenNt(&Data));
    }

    WITH_FN(PFND3DKMT_OPENSYNCOBJECTFROMNTHANDLE2, pOpenSync2, "D3DKMTOpenSyncObjectFromNtHandle2")
    {
        D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Data;
        memset(&Data, 0, sizeof(Data));
        Data.hDevice = hDevice;
        Data.hNtHandle = BAD_NT_HANDLE;
        EXPECT_CALL_REFUSED("D3DKMTOpenSyncObjectFromNtHandle2(real dev, bogus NT)", pOpenSync2(&Data));
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ------------------------------------------------------------------ */
/* Positive legacy shared-primary lifecycle.                           */
/* ------------------------------------------------------------------ */
static void Test_SharedPrimary_OpenRoundTrip(void)
{
    PFND3DKMT_GETSHAREDPRIMARYHANDLE pGet = (PFND3DKMT_GETSHAREDPRIMARYHANDLE)LoadD3DKMTProc("D3DKMTGetSharedPrimaryHandle");
    PFND3DKMT_QUERYRESOURCEINFO pQuery = (PFND3DKMT_QUERYRESOURCEINFO)LoadD3DKMTProc("D3DKMTQueryResourceInfo");
    PFND3DKMT_OPENRESOURCE pOpen = (PFND3DKMT_OPENRESOURCE)LoadD3DKMTProc("D3DKMTOpenResource");
    PFND3DKMT_DESTROYALLOCATION pDestroyAllocation = (PFND3DKMT_DESTROYALLOCATION)LoadD3DKMTProc("D3DKMTDestroyAllocation");
    PFN_D3DKMTDestroyDevice pDestroyDevice = (PFN_D3DKMTDestroyDevice)LoadD3DKMTProc("D3DKMTDestroyDevice");
    D3DKMT_GETSHAREDPRIMARYHANDLE GetPrimary;
    D3DKMT_QUERYRESOURCEINFO Query;
    D3DKMT_QUERYRESOURCEINFO QueryAfterDestroy;
    D3DKMT_OPENRESOURCE Discovery;
    D3DKMT_DESTROYDEVICE DestroyDevice;
    SHARED_OPEN_BUFFERS FirstOpen;
    SHARED_OPEN_BUFFERS SecondOpen;
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_HANDLE hDevice1 = 0;
    D3DKMT_HANDLE hDevice2 = 0;
    D3DKMT_HANDLE hGlobalShare = 0;
    NTSTATUS Status;
    UINT SourceId;
    UINT Index;

    memset(&FirstOpen, 0, sizeof(FirstOpen));
    memset(&SecondOpen, 0, sizeof(SecondOpen));
    if (pGet == NULL || pQuery == NULL || pOpen == NULL || pDestroyAllocation == NULL || pDestroyDevice == NULL)
    {
        skip("Positive shared-resource entry points are not all exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0)
    {
        skip("No display adapter for shared-primary round trip\n");
        return;
    }
    hDevice1 = CreateTestDevice(hAdapter);
    hDevice2 = CreateTestDevice(hAdapter);
    if (hDevice1 == 0 || hDevice2 == 0)
    {
        skip("Two logical devices are required for shared-primary round trip\n");
        goto Cleanup;
    }

    for (SourceId = 0; SourceId < 4 && hGlobalShare == 0; ++SourceId)
    {
        memset(&GetPrimary, 0, sizeof(GetPrimary));
        GetPrimary.hAdapter = hAdapter;
        GetPrimary.VidPnSourceId = SourceId;
        Status = pGet(&GetPrimary);
        if (NT_SUCCESS(Status))
            hGlobalShare = GetPrimary.hSharedPrimary;
    }
    if (hGlobalShare == 0)
    {
        skip("Display adapter exposes no kernel-managed shared primary\n");
        goto Cleanup;
    }

    memset(&Query, 0, sizeof(Query));
    Query.hDevice = hDevice1;
    Query.hGlobalShare = hGlobalShare;
    Status = pQuery(&Query);
    ok(NT_SUCCESS(Status), "QueryResourceInfo(shared primary) failed 0x%08lX\n", (long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    ok(Query.NumAllocations != 0 && Query.NumAllocations <= MAX_TEST_SHARED_ALLOCATIONS, "QueryResourceInfo returned invalid allocation count %u\n", Query.NumAllocations);
    if (Query.NumAllocations == 0 || Query.NumAllocations > MAX_TEST_SHARED_ALLOCATIONS)
        goto Cleanup;

    memset(&Discovery, 0, sizeof(Discovery));
    Discovery.hDevice = hDevice1;
    Discovery.hGlobalShare = hGlobalShare;
    Status = pOpen(&Discovery);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "OpenResource discovery returned 0x%08lX instead of STATUS_BUFFER_TOO_SMALL\n", (long)Status);
    ok(Discovery.NumAllocations == Query.NumAllocations, "OpenResource discovery count %u, expected %u\n", Discovery.NumAllocations, Query.NumAllocations);
    ok(Discovery.PrivateRuntimeDataSize == Query.PrivateRuntimeDataSize, "OpenResource runtime-private size %u, expected %u\n", Discovery.PrivateRuntimeDataSize, Query.PrivateRuntimeDataSize);
    ok(Discovery.ResourcePrivateDriverDataSize == Query.ResourcePrivateDriverDataSize, "OpenResource resource-private size %u, expected %u\n", Discovery.ResourcePrivateDriverDataSize, Query.ResourcePrivateDriverDataSize);
    ok(Discovery.TotalPrivateDriverDataBufferSize == Query.TotalPrivateDriverDataSize, "OpenResource allocation-private size %u, expected %u\n", Discovery.TotalPrivateDriverDataBufferSize, Query.TotalPrivateDriverDataSize);
    if (Status != STATUS_BUFFER_TOO_SMALL || Discovery.NumAllocations != Query.NumAllocations)
        goto Cleanup;

    if (!InitializeSharedOpenBuffers(&FirstOpen, hDevice1, hGlobalShare, &Query) || !InitializeSharedOpenBuffers(&SecondOpen, hDevice2, hGlobalShare, &Query))
    {
        skip("Insufficient memory for shared-primary open buffers\n");
        goto Cleanup;
    }

    Status = pOpen(&FirstOpen.Request);
    ok(NT_SUCCESS(Status), "First OpenResource failed 0x%08lX\n", (long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    ok(FirstOpen.Request.hResource != 0, "First OpenResource returned a zero resource handle\n");
    for (Index = 0; Index < Query.NumAllocations; ++Index)
        ok(FirstOpen.AllocationInfo[Index].hAllocation != 0, "First OpenResource allocation %u has a zero handle\n", Index);

    Status = pOpen(&SecondOpen.Request);
    ok(NT_SUCCESS(Status), "Second OpenResource failed 0x%08lX\n", (long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    ok(SecondOpen.Request.hResource != 0 && SecondOpen.Request.hResource != FirstOpen.Request.hResource, "Per-device resource handles are not distinct: 0x%08lX / 0x%08lX\n", (unsigned long)FirstOpen.Request.hResource, (unsigned long)SecondOpen.Request.hResource);
    for (Index = 0; Index < Query.NumAllocations; ++Index)
        ok(SecondOpen.AllocationInfo[Index].hAllocation != 0 && SecondOpen.AllocationInfo[Index].hAllocation != FirstOpen.AllocationInfo[Index].hAllocation, "Per-device allocation %u handles are not distinct: 0x%08lX / 0x%08lX\n", Index, (unsigned long)FirstOpen.AllocationInfo[Index].hAllocation, (unsigned long)SecondOpen.AllocationInfo[Index].hAllocation);

    memset(&DestroyDevice, 0, sizeof(DestroyDevice));
    DestroyDevice.hDevice = hDevice1;
    Status = pDestroyDevice(&DestroyDevice);
    ok(NT_SUCCESS(Status), "DestroyDevice with first shared alias failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        hDevice1 = 0;
        FirstOpen.Request.hResource = 0;
    }

    memset(&QueryAfterDestroy, 0, sizeof(QueryAfterDestroy));
    QueryAfterDestroy.hDevice = hDevice2;
    QueryAfterDestroy.hGlobalShare = hGlobalShare;
    Status = pQuery(&QueryAfterDestroy);
    ok(NT_SUCCESS(Status), "Shared resource did not survive first alias-device teardown: 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        ok(QueryAfterDestroy.NumAllocations == Query.NumAllocations, "Post-teardown allocation count %u, expected %u\n", QueryAfterDestroy.NumAllocations, Query.NumAllocations);
        ok(QueryAfterDestroy.TotalPrivateDriverDataSize == Query.TotalPrivateDriverDataSize, "Post-teardown private-data size %u, expected %u\n", QueryAfterDestroy.TotalPrivateDriverDataSize, Query.TotalPrivateDriverDataSize);
    }

    Status = DestroyOpenedResource(pDestroyAllocation, hDevice2, SecondOpen.Request.hResource);
    ok(NT_SUCCESS(Status), "DestroyAllocation(second opened resource) failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
        SecondOpen.Request.hResource = 0;

Cleanup:
    if (FirstOpen.Request.hResource != 0 && hDevice1 != 0)
        DestroyOpenedResource(pDestroyAllocation, hDevice1, FirstOpen.Request.hResource);
    if (SecondOpen.Request.hResource != 0 && hDevice2 != 0)
        DestroyOpenedResource(pDestroyAllocation, hDevice2, SecondOpen.Request.hResource);
    FreeSharedOpenBuffers(&SecondOpen);
    FreeSharedOpenBuffers(&FirstOpen);
    if (hDevice2 != 0)
        DestroyTestDevice(hDevice2);
    if (hDevice1 != 0)
        DestroyTestDevice(hDevice1);
    if (hAdapter != 0)
        CloseAdapter(hAdapter);
}

/* Exercise the buffered bridge with nonzero runtime/resource data and three
 * distinct allocation-private slices. Native drivers may reject the synthetic
 * allocation metadata; ReactOS test miniports accept it and must preserve every
 * byte, pointer offset, and opened-alias lifetime. */
static void Test_SharedResource_MultiAllocationRoundTrip(void)
{
    PFN_D3DKMTCreateAllocation pCreate = (PFN_D3DKMTCreateAllocation)LoadD3DKMTProc("D3DKMTCreateAllocation");
    PFND3DKMT_QUERYRESOURCEINFO pQuery = (PFND3DKMT_QUERYRESOURCEINFO)LoadD3DKMTProc("D3DKMTQueryResourceInfo");
    PFND3DKMT_OPENRESOURCE pOpen = (PFND3DKMT_OPENRESOURCE)LoadD3DKMTProc("D3DKMTOpenResource");
    PFN_D3DKMTDestroyAllocation pDestroy = (PFN_D3DKMTDestroyAllocation)LoadD3DKMTProc("D3DKMTDestroyAllocation");
    PFN_D3DKMTLock pLock = (PFN_D3DKMTLock)LoadD3DKMTProc("D3DKMTLock");
    PFN_D3DKMTUnlock pUnlock = (PFN_D3DKMTUnlock)LoadD3DKMTProc("D3DKMTUnlock");
    const UINT RuntimePrivate[2] = { 0x52554E31, 0x52554E32 };
    const UINT ResourcePrivate = 0x52535243;
    UINT AllocationPrivate[3] = { 4096, 8192, 16384 };
    D3DDDI_ALLOCATIONINFO AllocationInfo[3];
    D3DKMT_CREATEALLOCATION Create;
    D3DKMT_QUERYRESOURCEINFO Query;
    SHARED_OPEN_BUFFERS Opened;
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_HANDLE hCreatorDevice = 0;
    D3DKMT_HANDLE hOpenerDevice = 0;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    UINT Index;

    memset(&Create, 0, sizeof(Create));
    memset(&Opened, 0, sizeof(Opened));
    if (pCreate == NULL || pQuery == NULL || pOpen == NULL || pDestroy == NULL || pLock == NULL || pUnlock == NULL)
    {
        skip("Positive multi-allocation sharing entry points are not all exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0)
    {
        skip("No display adapter for multi-allocation sharing\n");
        return;
    }
    hCreatorDevice = CreateTestDevice(hAdapter);
    hOpenerDevice = CreateTestDevice(hAdapter);
    if (hCreatorDevice == 0 || hOpenerDevice == 0)
    {
        skip("Two logical devices are required for multi-allocation sharing\n");
        goto Cleanup;
    }

    memset(AllocationInfo, 0, sizeof(AllocationInfo));
    for (Index = 0; Index < ARRAYSIZE(AllocationInfo); ++Index)
    {
        AllocationInfo[Index].pPrivateDriverData = &AllocationPrivate[Index];
        AllocationInfo[Index].PrivateDriverDataSize = sizeof(AllocationPrivate[Index]);
    }
    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hCreatorDevice;
    Create.pPrivateRuntimeData = RuntimePrivate;
    Create.PrivateRuntimeDataSize = sizeof(RuntimePrivate);
    Create.pPrivateDriverData = &ResourcePrivate;
    Create.PrivateDriverDataSize = sizeof(ResourcePrivate);
    Create.NumAllocations = ARRAYSIZE(AllocationInfo);
    Create.pAllocationInfo = AllocationInfo;
    Create.Flags.CreateResource = 1;
    Create.Flags.CreateShared = 1;
    Status = pCreate(&Create);
    if (!NT_SUCCESS(Status))
    {
        skip("Synthetic shared multi-allocation resource was refused 0x%08lX\n", (long)Status);
        goto Cleanup;
    }
    ok(Create.hResource != 0 && Create.hGlobalShare != 0, "CreateShared returned resource/share handles 0x%08lX/0x%08lX\n", (unsigned long)Create.hResource, (unsigned long)Create.hGlobalShare);
    if (Create.hResource == 0 || Create.hGlobalShare == 0)
        goto Cleanup;

    memset(&Query, 0, sizeof(Query));
    Query.hDevice = hOpenerDevice;
    Query.hGlobalShare = Create.hGlobalShare;
    Status = pQuery(&Query);
    ok(NT_SUCCESS(Status), "QueryResourceInfo null-buffer discovery failed 0x%08lX\n", (long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    ok(Query.NumAllocations == ARRAYSIZE(AllocationInfo), "QueryResourceInfo returned %u allocations, expected %u\n", Query.NumAllocations, (unsigned)ARRAYSIZE(AllocationInfo));
    ok(Query.PrivateRuntimeDataSize == sizeof(RuntimePrivate), "Runtime-private size %u, expected %u\n", Query.PrivateRuntimeDataSize, (unsigned)sizeof(RuntimePrivate));
    ok(Query.ResourcePrivateDriverDataSize == sizeof(ResourcePrivate), "Resource-private size %u, expected %u\n", Query.ResourcePrivateDriverDataSize, (unsigned)sizeof(ResourcePrivate));
    ok(Query.TotalPrivateDriverDataSize == sizeof(AllocationPrivate), "Total allocation-private size %u, expected %u\n", Query.TotalPrivateDriverDataSize, (unsigned)sizeof(AllocationPrivate));
    if (Query.NumAllocations != ARRAYSIZE(AllocationInfo) || Query.PrivateRuntimeDataSize != sizeof(RuntimePrivate) || Query.ResourcePrivateDriverDataSize != sizeof(ResourcePrivate) || Query.TotalPrivateDriverDataSize != sizeof(AllocationPrivate))
        goto Cleanup;
    if (!InitializeSharedOpenBuffers(&Opened, hOpenerDevice, Create.hGlobalShare, &Query))
    {
        skip("Unable to allocate exact OpenResource discovery buffers\n");
        goto Cleanup;
    }

    Status = pOpen(&Opened.Request);
    ok(NT_SUCCESS(Status), "OpenResource multi-allocation round trip failed 0x%08lX\n", (long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    ok(Opened.Request.hResource != 0, "OpenResource returned a zero resource handle\n");
    ok(memcmp(Opened.PrivateRuntimeData, RuntimePrivate, sizeof(RuntimePrivate)) == 0, "OpenResource changed runtime-private data\n");
    ok(memcmp(Opened.ResourcePrivateDriverData, &ResourcePrivate, sizeof(ResourcePrivate)) == 0, "OpenResource changed resource-private data\n");
    ok(memcmp(Opened.TotalPrivateDriverData, AllocationPrivate, sizeof(AllocationPrivate)) == 0, "OpenResource changed ordered allocation-private data\n");
    for (Index = 0; Index < ARRAYSIZE(AllocationInfo); ++Index)
    {
        PVOID ExpectedPrivateData = (PUCHAR)Opened.TotalPrivateDriverData + Index * sizeof(AllocationPrivate[Index]);

        ok(Opened.AllocationInfo[Index].hAllocation != 0, "Opened allocation %u has a zero handle\n", Index);
        ok(Opened.AllocationInfo[Index].PrivateDriverDataSize == sizeof(AllocationPrivate[Index]), "Opened allocation %u private size %u, expected %u\n", Index, Opened.AllocationInfo[Index].PrivateDriverDataSize, (unsigned)sizeof(AllocationPrivate[Index]));
        ok(Opened.AllocationInfo[Index].pPrivateDriverData == ExpectedPrivateData, "Opened allocation %u private pointer %p, expected rebased slice %p\n", Index, Opened.AllocationInfo[Index].pPrivateDriverData, ExpectedPrivateData);
    }

    Status = DestroyOpenedResource(pDestroy, hCreatorDevice, Create.hResource);
    ok(NT_SUCCESS(Status), "Destroying creator resource with a live alias failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
        Create.hResource = 0;
    if (NT_SUCCESS(Status))
    {
        D3DKMT_LOCK Lock;

        memset(&Lock, 0, sizeof(Lock));
        Lock.hDevice = hOpenerDevice;
        Lock.hAllocation = Opened.AllocationInfo[1].hAllocation;
        Lock.Flags.LockEntire = 1;
        Status = pLock(&Lock);
        ok(NT_SUCCESS(Status) && Lock.pData != NULL, "Opened alias did not survive creator destruction: 0x%08lX, pData=%p\n", (long)Status, Lock.pData);
        if (NT_SUCCESS(Status))
        {
            D3DKMT_UNLOCK Unlock;
            D3DKMT_HANDLE Allocation = Opened.AllocationInfo[1].hAllocation;

            memset(&Unlock, 0, sizeof(Unlock));
            Unlock.hDevice = hOpenerDevice;
            Unlock.NumAllocations = 1;
            Unlock.phAllocations = &Allocation;
            Status = pUnlock(&Unlock);
            ok(NT_SUCCESS(Status), "Unlock of surviving opened alias failed 0x%08lX\n", (long)Status);
        }
    }

Cleanup:
    if (Opened.Request.hResource != 0 && hOpenerDevice != 0)
        DestroyOpenedResource(pDestroy, hOpenerDevice, Opened.Request.hResource);
    if (Create.hResource != 0 && hCreatorDevice != 0)
        DestroyOpenedResource(pDestroy, hCreatorDevice, Create.hResource);
    FreeSharedOpenBuffers(&Opened);
    if (hOpenerDevice != 0)
        DestroyTestDevice(hOpenerDevice);
    if (hCreatorDevice != 0)
        DestroyTestDevice(hCreatorDevice);
    if (hAdapter != 0)
        CloseAdapter(hAdapter);
}

/* ------------------------------------------------------------------ */
/* D3DKMTGetSharedPrimaryHandle -- the one safely attemptable positive. */
/* On a real adapter it may return a handle, return a zero handle (no   */
/* kernel-managed primary), or fail; we never assert success. A bogus   */
/* adapter must fail.                                                   */
/* ------------------------------------------------------------------ */
static void Test_GetSharedPrimaryHandle_Positive(void)
{
    D3DKMT_GETSHAREDPRIMARYHANDLE Data;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;
    UINT src;

    LOADFN(PFND3DKMT_GETSHAREDPRIMARYHANDLE, pGet, "D3DKMTGetSharedPrimaryHandle");

    /* Bogus adapter handle must be rejected. */
    memset(&Data, 0, sizeof(Data));
    Data.hAdapter = BAD_DEVICE_HANDLE;
    Data.VidPnSourceId = 0;
    EXPECT_CALL_REFUSED("D3DKMTGetSharedPrimaryHandle(bogus adapter)", pGet(&Data));

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1 for GetSharedPrimaryHandle positive\n");
        return;
    }

    /* Attempt a few VidPN source IDs on the real adapter. Success is optional;
     * a zero shared-primary handle on success is normal (no kernel primary). */
    for (src = 0; src < 4; src++)
    {
        memset(&Data, 0, sizeof(Data));
        Data.hAdapter = hAdapter;
        Data.VidPnSourceId = src;
        Status = STATUS_SUCCESS;

        _SEH2_TRY { Status = pGet(&Data); }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { Status = STATUS_UNSUCCESSFUL; }
        _SEH2_END;

        if (NT_SUCCESS(Status))
            trace("GetSharedPrimaryHandle(src=%u) -> 0x%08lX (handle 0x%08lX)\n",
                  src, (long)Status, (unsigned long)Data.hSharedPrimary);
        else
            trace("GetSharedPrimaryHandle(src=%u) refused 0x%08lX\n",
                  src, (long)Status);
    }

    CloseAdapter(hAdapter);
}

START_TEST(sharing)
{
    Test_Sharing_NullContract();
    Test_ShareObjects_Contract();
    Test_OpenQuery_NoDevice_BadHandles();
    Test_NameOpens_BadAttributes();
    Test_GetSharedResourceAdapterLuid_BadHandles();
    Test_CheckSharedResourceAccess_BadHandle();
    Test_OpenQuery_RealDevice_BadHandles();
    Test_SharedPrimary_OpenRoundTrip();
    Test_SharedResource_MultiAllocationRoundTrip();
    Test_GetSharedPrimaryHandle_Positive();
}
