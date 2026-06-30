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
 * A real shared resource cannot be constructed without a render device plus a
 * cross-process producer, so this suite focuses on the portable contract
 * surface (NULL refused, bogus handle/name refused) that must hold on every
 * WDDM adapter -- including the display-only / WARP adapters of a Win11 ARM64
 * QEMU guest -- plus the one safely attemptable positive (GetSharedPrimaryHandle
 * on the real adapter, which may succeed or fail and is therefore never
 * asserted to succeed).
 */

#include "precomp.h"

/* Clearly-invalid handle constants. D3DKMT_HANDLE is a 32-bit value; NT handles
 * are pointer-sized and always 4-aligned, so 0xDEADBEEF is guaranteed bogus. */
#define BAD_D3DKMT_HANDLE   ((D3DKMT_HANDLE)0xDEADBEEF)
#define BAD_DEVICE_HANDLE   ((D3DKMT_HANDLE)0xBAD0CAFE)
#define BAD_NT_HANDLE       ((HANDLE)(ULONG_PTR)0xDEADBEEF)

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
    Test_GetSharedPrimaryHandle_Positive();
}
