/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     In-depth D3DKMT allocation-feature coverage: standard allocations,
 *              the CreateAllocation flags matrix, allocation priority get/set,
 *              residency + property update, video-memory reservation / queued
 *              limit, and the WDDM 2.0 destroy/lock/unlock-2 contracts.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Goes deeper than d3dkmt_alloc.c (NULL contract), vidmm_test.c (basic
 * lifecycle) and d3dkmt_surface.c (alloc2 NULL contract), which already exist.
 *
 * Win11-green policy (see header struct gating; build pins DXGKDDI_INTERFACE_VERSION
 * to 0x5023 / WDDM 2.0 via CMakeLists):
 *   - A generic user-mode caller cannot supply the per-KMD private driver data a
 *     real Win11 display driver validates, so any plain CreateAllocation /
 *     CreateAllocation2 is EXPECTED TO FAIL on Win11 -- treat create-failure as
 *     skip(), never assert success on a normal allocation.
 *   - STANDARD ALLOCATIONS are the exception: the OS can build them without UMD
 *     data, so we attempt them, skip on failure, and validate the handle on
 *     success.
 *   - NULL / bad-handle / contradictory-flag cases assert refusal only (any
 *     non-success NTSTATUS, or an access violation in the thunk -- both count).
 *
 * Reference: Microsoft "D3DKMT_CREATEALLOCATION structure",
 *   "D3DKMT_CREATEALLOCATIONFLAGS", "D3DKMT_CREATESTANDARDALLOCATION",
 *   "D3DKMT_STANDARDALLOCATIONTYPE enumeration", "D3DKMTSetAllocationPriority",
 *   "D3DKMTGetAllocationPriority", "D3DKMTQueryAllocationResidency",
 *   "D3DKMTUpdateAllocationProperty", "D3DKMTChangeVideoMemoryReservation",
 *   "D3DKMTSetQueuedLimit", "D3DKMTDestroyAllocation2", "D3DKMTLock2/Unlock2".
 */

#include "precomp.h"
#include <pseh/pseh2.h>

/*
 * Flags.StandardAllocation is bit 0x00010000 of D3DKMT_CREATEALLOCATIONFLAGS,
 * but the *named* bitfield only exists from WDDM 2.3 (DXGKDDI 0x8001). At the
 * pinned 0x5023 it falls inside Reserved, so we set it through a value union.
 * The whole flags word is exactly 32 bits of bitfields; assert that so the
 * value-union aliasing is well defined.
 */
C_ASSERT(sizeof(D3DKMT_CREATEALLOCATIONFLAGS) == sizeof(UINT));

#define CAF_CREATERESOURCE          0x00000001u
#define CAF_CREATESHARED            0x00000002u
#define CAF_CREATEPROTECTED         0x00000008u  /* forbidden from user mode */
#define CAF_EXISTINGSYSMEM          0x00000020u  /* forbidden from user mode */
#define CAF_CREATEWRITECOMBINED     0x00000100u  /* forbidden from user mode */
#define CAF_CREATECACHED            0x00000200u  /* forbidden from user mode */
#define CAF_OPENCROSSADAPTER        0x00001000u  /* forbidden from user mode */
#define CAF_STANDARDALLOCATION      0x00010000u

/* A pointer-taking NTSTATUS thunk -- the shared shape of every D3DKMT entry
 * point. Used both to invoke functions whose typed PFN/struct is gated out at
 * 0x5023 and to SEH-guard bad-handle / create attempts uniformly. */
typedef NTSTATUS (APIENTRY *PFN_PTRARG)(const void *);

static D3DKMT_CREATEALLOCATIONFLAGS
MakeAllocFlags(UINT Value)
{
    union { D3DKMT_CREATEALLOCATIONFLAGS f; UINT v; } u;
    u.v = Value;
    return u.f;
}

/* Invoke a one-pointer D3DKMT thunk under SEH so a faulting/validating thunk
 * cannot abort the subtest. Returns status + whether it faulted via out-params
 * (mirrors the proven pattern in d3dkmt_alloc.c). */
static void
SafeCallPtr(FARPROC Proc, const void *Arg, NTSTATUS *pStatus, BOOL *pFaulted)
{
    *pStatus = STATUS_INVALID_PARAMETER;
    *pFaulted = FALSE;
    _SEH2_TRY
    {
        *pStatus = ((PFN_PTRARG)Proc)(Arg);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *pFaulted = TRUE;
    }
    _SEH2_END;
}

/* Assert that a call is refused (non-success status or a fault). Name may be a
 * string literal or a runtime const char*, so format it with %s. */
#define EXPECT_REFUSED(Proc, Arg, Name)                                         \
do {                                                                            \
    NTSTATUS _st; BOOL _f;                                                       \
    SafeCallPtr((FARPROC)(Proc), (Arg), &_st, &_f);                             \
    ok(_f || !NT_SUCCESS(_st),                                                   \
       "%s must be refused, got 0x%08lX%s\n",                                   \
       (Name), (long)_st, _f ? " (faulted)" : "");                              \
} while (0)

/* Open \\.\DISPLAY1 + a device, or skip the caller. Returns FALSE on failure
 * (after emitting skip and cleaning up any partial handles). */
static BOOL
OpenAdapterAndDevice(D3DKMT_HANDLE *phAdapter, D3DKMT_HANDLE *phDevice)
{
    *phAdapter = OpenAdapterFromDisplay1();
    if (!*phAdapter)
    {
        skip("No WDDM adapter on \\\\.\\DISPLAY1\n");
        return FALSE;
    }
    *phDevice = CreateTestDevice(*phAdapter);
    if (!*phDevice)
    {
        skip("CreateDevice failed -- no render device on this adapter\n");
        CloseAdapter(*phAdapter);
        *phAdapter = 0;
        return FALSE;
    }
    return TRUE;
}

static void
DestroyByList(D3DKMT_HANDLE hDevice, D3DKMT_HANDLE hResource,
              D3DKMT_HANDLE *List, UINT Count)
{
    PFN_D3DKMTDestroyAllocation pfn =
        (PFN_D3DKMTDestroyAllocation)LoadD3DKMTProc("D3DKMTDestroyAllocation");
    D3DKMT_DESTROYALLOCATION da;

    if (!pfn)
        return;
    memset(&da, 0, sizeof(da));
    da.hDevice = hDevice;
    da.hResource = hResource;
    da.phAllocationList = (hResource ? NULL : List);
    da.AllocationCount = (hResource ? 0 : Count);
    pfn(&da);
}

/* =====================================================================
 * Group 1: standard allocations (D3DKMT_CREATESTANDARDALLOCATION)
 *
 * Standard allocations are described by a D3DKMT_STANDARDALLOCATION* and the
 * StandardAllocation flag, NOT by KMD-private data, so the OS can build them
 * for a generic caller. We attempt each D3DKMT_STANDARDALLOCATIONTYPE the
 * header defines through both CreateAllocation and CreateAllocation2, skip on
 * failure, validate the handle on success, and destroy.
 * ===================================================================== */

static void
Std_Attempt(D3DKMT_STANDARDALLOCATIONTYPE Type, const char *TypeName, BOOL UseV2)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATESTANDARDALLOCATION std;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    D3DDDI_ALLOCATIONINFO2 ai2;
    void *heap = NULL;
    const SIZE_T HeapSize = 0x1000;
    D3DKMT_HANDLE hAlloc;
    NTSTATUS st;
    BOOL faulted;
    const char *via = UseV2 ? "CreateAllocation2" : "CreateAllocation";
    PFND3DKMT_CREATEALLOCATION pCreate;

    /* LOADFN concatenates Name as a literal, so resolve the variant by hand. */
    pCreate = (PFND3DKMT_CREATEALLOCATION)LoadD3DKMTProc(
        UseV2 ? "D3DKMTCreateAllocation2" : "D3DKMTCreateAllocation");
    if (!pCreate)
    {
        skip("D3DKMT%s not exported by gdi32.dll\n", via);
        return;
    }

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    memset(&std, 0, sizeof(std));
    std.Type = Type;
    if (Type == D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP)
    {
        /* EXISTINGHEAP wraps caller pages: give it a real committed range. */
        heap = VirtualAlloc(NULL, HeapSize, MEM_COMMIT | MEM_RESERVE,
                            PAGE_READWRITE);
        if (!heap)
        {
            skip("VirtualAlloc failed for EXISTINGHEAP backing\n");
            goto cleanup;
        }
        std.ExistingHeapData.Size = HeapSize;
    }

    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.pStandardAllocation = &std;            /* union: read because flag set  */
    ca.NumAllocations = 1;
    ca.Flags = MakeAllocFlags(CAF_STANDARDALLOCATION);

    if (UseV2)
    {
        memset(&ai2, 0, sizeof(ai2));
        if (heap) ai2.pSystemMem = heap;
        ca.pAllocationInfo2 = &ai2;
    }
    else
    {
        memset(&ai, 0, sizeof(ai));
        if (heap) ai.pSystemMem = heap;
        ca.pAllocationInfo = &ai;
    }

    SafeCallPtr((FARPROC)pCreate, &ca, &st, &faulted);

    if (faulted || !NT_SUCCESS(st))
    {
        skip("standard %s via %s not supported here (0x%08lX%s)\n",
             TypeName, via, (long)st, faulted ? " faulted" : "");
        goto cleanup;
    }

    hAlloc = UseV2 ? ai2.hAllocation : ai.hAllocation;
    ok(hAlloc != 0, "standard %s via %s: allocation handle must be non-zero\n",
       TypeName, via);
    trace("standard %s via %s OK: hAllocation=0x%lx hResource=0x%lx\n",
          TypeName, via, (unsigned long)hAlloc, (unsigned long)ca.hResource);

    DestroyByList(hDevice, ca.hResource, &hAlloc, 1);

cleanup:
    if (heap)
        VirtualFree(heap, 0, MEM_RELEASE);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* StandardAllocation flag set but no descriptor: malformed independent of the
 * KMD -> must be refused. */
static void
Std_NullDescriptor(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;

    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreate, "D3DKMTCreateAllocation");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    memset(&ai, 0, sizeof(ai));
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.pStandardAllocation = NULL;            /* flag set, descriptor missing  */
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &ai;
    ca.Flags = MakeAllocFlags(CAF_STANDARDALLOCATION);
    EXPECT_REFUSED(pCreate, &ca,
                   "CreateAllocation(StandardAllocation, NULL descriptor)");

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* Out-of-range D3DKMT_STANDARDALLOCATIONTYPE -> must be refused. */
static void
Std_BadType(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATESTANDARDALLOCATION std;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;

    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreate, "D3DKMTCreateAllocation");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    memset(&std, 0, sizeof(std));
    std.Type = D3DKMT_STANDARDALLOCATIONTYPE_MAX; /* sentinel, not a real type */
    memset(&ai, 0, sizeof(ai));
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.pStandardAllocation = &std;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &ai;
    ca.Flags = MakeAllocFlags(CAF_STANDARDALLOCATION);
    EXPECT_REFUSED(pCreate, &ca,
                   "CreateAllocation(StandardAllocation, invalid Type)");

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(allocstandard)
{
    /* Every D3DKMT_STANDARDALLOCATIONTYPE the header defines, both paths. */
    Std_Attempt(D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP, "EXISTINGHEAP", FALSE);
    Std_Attempt(D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP, "EXISTINGHEAP", TRUE);
    Std_Attempt(D3DKMT_STANDARDALLOCATIONTYPE_INTERNALBACKINGSTORE,
                "INTERNALBACKINGSTORE", FALSE);
    Std_Attempt(D3DKMT_STANDARDALLOCATIONTYPE_INTERNALBACKINGSTORE,
                "INTERNALBACKINGSTORE", TRUE);
    Std_NullDescriptor();
    Std_BadType();
}

/* =====================================================================
 * Group 2: CreateAllocation flags matrix (D3DKMT_CREATEALLOCATIONFLAGS)
 * ===================================================================== */

/* Valid flag combos: provide a per-allocation size blob (best effort for a
 * stub KMD), attempt, validate-on-success, skip-on-failure (Win11). */
static void
Flags_ValidCombo(UINT FlagValue, UINT NumAllocs, const char *Desc)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai[3];
    D3DKMT_HANDLE handles[3];
    UINT sizeblob[3] = { 0x1000, 0x2000, 0x4000 };
    NTSTATUS st;
    BOOL faulted;
    UINT i;

    if (NumAllocs > 3) NumAllocs = 3;

    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreate, "D3DKMTCreateAllocation");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    memset(ai, 0, sizeof(ai));
    for (i = 0; i < NumAllocs; i++)
    {
        ai[i].pPrivateDriverData = &sizeblob[i];
        ai[i].PrivateDriverDataSize = sizeof(UINT);
    }

    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = NumAllocs;
    ca.pAllocationInfo = ai;
    ca.Flags = MakeAllocFlags(FlagValue);

    SafeCallPtr((FARPROC)pCreate, &ca, &st, &faulted);
    if (faulted || !NT_SUCCESS(st))
    {
        skip("CreateAllocation[%s] needs UMD-private data on this KMD (0x%08lX)\n",
             Desc, (long)st);
        goto cleanup;
    }

    for (i = 0; i < NumAllocs; i++)
    {
        handles[i] = ai[i].hAllocation;
        ok(handles[i] != 0, "[%s] allocation[%u] handle must be non-zero\n",
           Desc, i);
    }
    trace("CreateAllocation[%s] OK: hResource=0x%lx hGlobalShare=0x%lx\n",
          Desc, (unsigned long)ca.hResource, (unsigned long)ca.hGlobalShare);

    DestroyByList(hDevice, ca.hResource, handles, NumAllocs);

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* Flags documented "Cannot be used when allocation is created from the user
 * mode" -> must be refused. (On Win11 every UMD-data-less create fails anyway,
 * so this is green; on a permissive stub KMD it asserts the documented gate.) */
static void
Flags_Forbidden(UINT FlagValue, const char *Name)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    UINT sizeblob = 0x1000;

    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreate, "D3DKMTCreateAllocation");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    memset(&ai, 0, sizeof(ai));
    ai.pPrivateDriverData = &sizeblob;
    ai.PrivateDriverDataSize = sizeof(UINT);

    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &ai;
    ca.Flags = MakeAllocFlags(FlagValue);
    EXPECT_REFUSED(pCreate, &ca, Name);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* Malformed shapes that are refused before reaching the miniport. */
static void
Flags_Malformed(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    UINT sizeblob = 0x1000;

    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreate, "D3DKMTCreateAllocation");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    /* NumAllocations == 0 but an array supplied. */
    memset(&ai, 0, sizeof(ai));
    ai.pPrivateDriverData = &sizeblob;
    ai.PrivateDriverDataSize = sizeof(UINT);
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 0;
    ca.pAllocationInfo = &ai;
    EXPECT_REFUSED(pCreate, &ca, "CreateAllocation(NumAllocations==0, array!=NULL)");

    /* Implausibly large NumAllocations against a 1-element array. */
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 0x10000000;
    ca.pAllocationInfo = &ai;
    EXPECT_REFUSED(pCreate, &ca, "CreateAllocation(huge NumAllocations)");

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(allocflags)
{
    /* Valid combinations (skip on failure -- Win11 lacks UMD data). */
    Flags_ValidCombo(CAF_CREATERESOURCE, 2, "CreateResource");
    Flags_ValidCombo(CAF_CREATERESOURCE | CAF_CREATESHARED, 2,
                     "CreateResource|CreateShared");
    Flags_ValidCombo(CAF_CREATESHARED, 1, "CreateShared");

    /* User-mode-forbidden flags (assert refusal). */
    Flags_Forbidden(CAF_CREATEPROTECTED,
                    "CreateAllocation(CreateProtected from user mode)");
    Flags_Forbidden(CAF_EXISTINGSYSMEM,
                    "CreateAllocation(ExistingSysMem from user mode)");
    Flags_Forbidden(CAF_CREATEWRITECOMBINED,
                    "CreateAllocation(CreateWriteCombined from user mode)");
    Flags_Forbidden(CAF_CREATECACHED,
                    "CreateAllocation(CreateCached from user mode)");
    Flags_Forbidden(CAF_OPENCROSSADAPTER,
                    "CreateAllocation(OpenCrossAdapter from user mode)");

    /* Malformed request shapes (assert refusal). */
    Flags_Malformed();
}

/* =====================================================================
 * Group 3: allocation priority (Set / Get across all priority values)
 * ===================================================================== */

static void
Prio_GetNull(void)
{
    /* D3DKMTGetAllocationPriority's typed PFN/struct is gated to WDDM 2.2 and is
     * absent at 0x5023, but the export may exist; verify the NULL contract via a
     * generic thunk. (SetAllocationPriority NULL is covered in d3dkmt_alloc.c.) */
    CHECK_NULL_REJECTED(PFN_PTRARG, "D3DKMTGetAllocationPriority");
}

static void
Prio_SetBadHandle(void)
{
    D3DKMT_SETALLOCATIONPRIORITY sp;
    D3DKMT_HANDLE bogus = (D3DKMT_HANDLE)0xDEAD3001;
    UINT prio = D3DDDI_ALLOCATIONPRIORITY_NORMAL;

    LOADFN(PFND3DKMT_SETALLOCATIONPRIORITY, p, "D3DKMTSetAllocationPriority");

    memset(&sp, 0, sizeof(sp));
    sp.hDevice = bogus;
    sp.phAllocationList = &bogus;
    sp.AllocationCount = 1;
    sp.pPriorities = &prio;
    EXPECT_REFUSED(p, &sp, "SetAllocationPriority(bogus device/allocation)");
}

static void
Prio_GetBadHandle(void)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    D3DKMT_GETALLOCATIONPRIORITY gp;
    D3DKMT_HANDLE bogus = (D3DKMT_HANDLE)0xDEAD3002;
    UINT prio = 0;

    LOADFN(PFND3DKMT_GETALLOCATIONPRIORITY, p, "D3DKMTGetAllocationPriority");

    memset(&gp, 0, sizeof(gp));
    gp.hDevice = bogus;
    gp.phAllocationList = &bogus;
    gp.AllocationCount = 1;
    gp.pPriorities = &prio;
    EXPECT_REFUSED(p, &gp, "GetAllocationPriority(bogus device/allocation)");
#else
    skip("D3DKMT_GETALLOCATIONPRIORITY struct gated out below WDDM 2.2 (build 0x%x)\n",
         (unsigned)DXGKDDI_INTERFACE_VERSION);
#endif
}

/* Sweep every D3DDDI_ALLOCATIONPRIORITY level on a real allocation, then (where
 * the header exposes it) round-trip through Get. Skip-guarded: needs a created
 * allocation, which Win11 refuses without UMD data. */
static void
Prio_Sweep(void)
{
    static const struct { UINT value; const char *name; } Levels[] = {
        { D3DDDI_ALLOCATIONPRIORITY_MINIMUM, "MINIMUM" },
        { D3DDDI_ALLOCATIONPRIORITY_LOW,     "LOW" },
        { D3DDDI_ALLOCATIONPRIORITY_NORMAL,  "NORMAL" },
        { D3DDDI_ALLOCATIONPRIORITY_HIGH,    "HIGH" },
        { D3DDDI_ALLOCATIONPRIORITY_MAXIMUM, "MAXIMUM" },
    };
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    D3DKMT_SETALLOCATIONPRIORITY sp;
    UINT sizeblob = 0x1000;
    NTSTATUS st;
    BOOL faulted;
    UINT i;

    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreate, "D3DKMTCreateAllocation");
    LOADFN(PFND3DKMT_SETALLOCATIONPRIORITY, pSet, "D3DKMTSetAllocationPriority");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    memset(&ai, 0, sizeof(ai));
    ai.pPrivateDriverData = &sizeblob;
    ai.PrivateDriverDataSize = sizeof(UINT);
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &ai;

    SafeCallPtr((FARPROC)pCreate, &ca, &st, &faulted);
    if (faulted || !NT_SUCCESS(st))
    {
        skip("priority sweep needs an allocation this KMD won't make (0x%08lX)\n",
             (long)st);
        goto cleanup;
    }
    hAlloc = ai.hAllocation;

    for (i = 0; i < sizeof(Levels) / sizeof(Levels[0]); i++)
    {
        memset(&sp, 0, sizeof(sp));
        sp.hDevice = hDevice;
        sp.phAllocationList = &hAlloc;
        sp.AllocationCount = 1;
        sp.pPriorities = &Levels[i].value;
        st = pSet(&sp);
        ok(NT_SUCCESS(st), "SetAllocationPriority(%s=0x%08X) failed 0x%08lX\n",
           Levels[i].name, Levels[i].value, (long)st);
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    {
        PFND3DKMT_GETALLOCATIONPRIORITY pGet =
            (PFND3DKMT_GETALLOCATIONPRIORITY)
                LoadD3DKMTProc("D3DKMTGetAllocationPriority");
        if (pGet)
        {
            D3DKMT_GETALLOCATIONPRIORITY gp;
            UINT got = 0;
            memset(&gp, 0, sizeof(gp));
            gp.hDevice = hDevice;
            gp.phAllocationList = &hAlloc;
            gp.AllocationCount = 1;
            gp.pPriorities = &got;
            st = pGet(&gp);
            if (NT_SUCCESS(st))
                trace("GetAllocationPriority round-trip read 0x%08X\n", got);
            else
                trace("GetAllocationPriority returned 0x%08lX\n", (long)st);
        }
    }
#endif

    DestroyByList(hDevice, 0, &hAlloc, 1);

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(allocpriority)
{
    Prio_GetNull();
    Prio_SetBadHandle();
    Prio_GetBadHandle();
    Prio_Sweep();
}

/* =====================================================================
 * Group 4: residency query + allocation property update
 * ===================================================================== */

static void
Resid_QueryBadHandle(void)
{
    D3DKMT_QUERYALLOCATIONRESIDENCY qr;
    D3DKMT_HANDLE bogus = (D3DKMT_HANDLE)0xDEAD4001;
    D3DKMT_ALLOCATIONRESIDENCYSTATUS status = (D3DKMT_ALLOCATIONRESIDENCYSTATUS)0;

    LOADFN(PFND3DKMT_QUERYALLOCATIONRESIDENCY, p, "D3DKMTQueryAllocationResidency");

    memset(&qr, 0, sizeof(qr));
    qr.hDevice = bogus;
    qr.phAllocationList = &bogus;
    qr.AllocationCount = 1;
    qr.pResidencyStatus = &status;
    EXPECT_REFUSED(p, &qr, "QueryAllocationResidency(bogus device/allocation)");
}

/* Two-allocation residency query on real handles (skip-guarded positive). */
static void
Resid_QueryMulti(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai[2];
    D3DKMT_HANDLE handles[2];
    D3DKMT_QUERYALLOCATIONRESIDENCY qr;
    D3DKMT_ALLOCATIONRESIDENCYSTATUS status[2];
    UINT sizeblob[2] = { 0x1000, 0x2000 };
    NTSTATUS st;
    BOOL faulted;
    UINT i;

    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreate, "D3DKMTCreateAllocation");
    LOADFN(PFND3DKMT_QUERYALLOCATIONRESIDENCY, pQuery,
           "D3DKMTQueryAllocationResidency");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    memset(ai, 0, sizeof(ai));
    for (i = 0; i < 2; i++)
    {
        ai[i].pPrivateDriverData = &sizeblob[i];
        ai[i].PrivateDriverDataSize = sizeof(UINT);
    }
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 2;
    ca.pAllocationInfo = ai;
    ca.Flags = MakeAllocFlags(CAF_CREATERESOURCE);

    SafeCallPtr((FARPROC)pCreate, &ca, &st, &faulted);
    if (faulted || !NT_SUCCESS(st))
    {
        skip("residency query needs allocations this KMD won't make (0x%08lX)\n",
             (long)st);
        goto cleanup;
    }
    handles[0] = ai[0].hAllocation;
    handles[1] = ai[1].hAllocation;

    memset(status, 0, sizeof(status));
    memset(&qr, 0, sizeof(qr));
    qr.hDevice = hDevice;
    qr.phAllocationList = handles;
    qr.AllocationCount = 2;
    qr.pResidencyStatus = status;
    st = pQuery(&qr);
    ok(NT_SUCCESS(st), "QueryAllocationResidency(2 allocations) failed 0x%08lX\n",
       (long)st);
    if (NT_SUCCESS(st))
    {
        for (i = 0; i < 2; i++)
        {
            ok(status[i] == D3DKMT_ALLOCATIONRESIDENCYSTATUS_RESIDENTINGPUMEMORY ||
               status[i] == D3DKMT_ALLOCATIONRESIDENCYSTATUS_RESIDENTINSHAREDMEMORY ||
               status[i] == D3DKMT_ALLOCATIONRESIDENCYSTATUS_NOTRESIDENT ||
               status[i] == 0 /* stub */,
               "residency[%u] has unexpected value %u\n", i, (UINT)status[i]);
            trace("residency[%u]=%u\n", i, (UINT)status[i]);
        }
    }

    DestroyByList(hDevice, ca.hResource, handles, 2);

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void
Update_Null(void)
{
    /* D3DKMTUpdateAllocationProperty's typed PFN/struct (D3DDDI_UPDATEALLOCPROPERTY)
     * is gated to WDDM 2.1 and absent at 0x5023; verify NULL via a generic thunk. */
    CHECK_NULL_REJECTED(PFN_PTRARG, "D3DKMTUpdateAllocationProperty");
}

static void
Update_BadHandle(void)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    D3DDDI_UPDATEALLOCPROPERTY up;

    LOADFN(PFND3DKMT_UPDATEALLOCATIONPROPERTY, p, "D3DKMTUpdateAllocationProperty");

    memset(&up, 0, sizeof(up));
    up.hPagingQueue = (D3DKMT_HANDLE)0xDEAD4002;
    up.hAllocation = (D3DKMT_HANDLE)0xDEAD4003;
    EXPECT_REFUSED(p, &up, "UpdateAllocationProperty(bogus paging queue/allocation)");
#else
    skip("D3DDDI_UPDATEALLOCPROPERTY struct gated out below WDDM 2.1 (build 0x%x)\n",
         (unsigned)DXGKDDI_INTERFACE_VERSION);
#endif
}

START_TEST(allocresidency)
{
    Resid_QueryBadHandle();
    Resid_QueryMulti();
    Update_Null();
    Update_BadHandle();
}

/* =====================================================================
 * Group 5: video-memory reservation + queued limit
 * ===================================================================== */

static void
Reserve_Null(void)
{
    LOADFN(PFND3DKMT_CHANGEVIDEOMEMORYRESERVATION, p,
           "D3DKMTChangeVideoMemoryReservation");
    EXPECT_NULL_REJECTED(p, "D3DKMTChangeVideoMemoryReservation");
}

static void
Reserve_BadHandle(void)
{
    D3DKMT_CHANGEVIDEOMEMORYRESERVATION cr;

    LOADFN(PFND3DKMT_CHANGEVIDEOMEMORYRESERVATION, p,
           "D3DKMTChangeVideoMemoryReservation");

    memset(&cr, 0, sizeof(cr));
    cr.hProcess = NULL;                                /* current process */
    cr.hAdapter = (D3DKMT_HANDLE)0xDEAD5001;
    cr.MemorySegmentGroup = D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL;
    cr.Reservation = 0;
    EXPECT_REFUSED(p, &cr, "ChangeVideoMemoryReservation(bogus adapter)");
}

/* Reserve 0 bytes on a real adapter: a valid no-op request. Skip on failure. */
static void
Reserve_Positive(void)
{
    D3DKMT_CHANGEVIDEOMEMORYRESERVATION cr;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS st;

    LOADFN(PFND3DKMT_CHANGEVIDEOMEMORYRESERVATION, p,
           "D3DKMTChangeVideoMemoryReservation");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter for reservation test\n"); return; }

    memset(&cr, 0, sizeof(cr));
    cr.hProcess = NULL;
    cr.hAdapter = hAdapter;
    cr.MemorySegmentGroup = D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL;
    cr.Reservation = 0;
    st = p(&cr);
    if (NT_SUCCESS(st))
        trace("ChangeVideoMemoryReservation(LOCAL, 0) succeeded\n");
    else
        skip("ChangeVideoMemoryReservation(LOCAL, 0) not supported (0x%08lX)\n",
             (long)st);

    CloseAdapter(hAdapter);
}

static void
QueuedLimit_Null(void)
{
    LOADFN(PFND3DKMT_SETQUEUEDLIMIT, p, "D3DKMTSetQueuedLimit");
    EXPECT_NULL_REJECTED(p, "D3DKMTSetQueuedLimit");
}

static void
QueuedLimit_BadHandle(void)
{
    D3DKMT_SETQUEUEDLIMIT ql;

    LOADFN(PFND3DKMT_SETQUEUEDLIMIT, p, "D3DKMTSetQueuedLimit");

    memset(&ql, 0, sizeof(ql));
    ql.hDevice = (D3DKMT_HANDLE)0xDEAD5002;
    ql.Type = D3DKMT_SET_QUEUEDLIMIT_PRESENT;
    ql.QueuedPresentLimit = 2;
    EXPECT_REFUSED(p, &ql, "SetQueuedLimit(bogus device)");
}

/* Set the present queued limit on a real device. Skip on failure. */
static void
QueuedLimit_Positive(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_SETQUEUEDLIMIT ql;
    NTSTATUS st;

    LOADFN(PFND3DKMT_SETQUEUEDLIMIT, p, "D3DKMTSetQueuedLimit");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    memset(&ql, 0, sizeof(ql));
    ql.hDevice = hDevice;
    ql.Type = D3DKMT_SET_QUEUEDLIMIT_PRESENT;
    ql.QueuedPresentLimit = 2;
    st = p(&ql);
    if (NT_SUCCESS(st))
        trace("SetQueuedLimit(PRESENT, 2) succeeded\n");
    else
        skip("SetQueuedLimit(PRESENT) not supported here (0x%08lX)\n", (long)st);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(allocreserve)
{
    Reserve_Null();
    Reserve_BadHandle();
    Reserve_Positive();
    QueuedLimit_Null();
    QueuedLimit_BadHandle();
    QueuedLimit_Positive();
}

/* =====================================================================
 * Group 6: WDDM 2.0 DestroyAllocation2 / Lock2 / Unlock2 contracts
 * (NULL contract for these lives in d3dkmt_surface.c's "alloc2" group)
 * ===================================================================== */

static void
Destroy2_BadHandle(void)
{
    D3DKMT_DESTROYALLOCATION2 da;
    D3DKMT_HANDLE bogus = (D3DKMT_HANDLE)0xDEAD6001;

    LOADFN(PFND3DKMT_DESTROYALLOCATION2, p, "D3DKMTDestroyAllocation2");

    memset(&da, 0, sizeof(da));
    da.hDevice = bogus;
    da.phAllocationList = &bogus;
    da.AllocationCount = 1;
    EXPECT_REFUSED(p, &da, "DestroyAllocation2(bogus device/allocation)");

    /* Same, with AssumeNotInUse set -- still refused. */
    memset(&da, 0, sizeof(da));
    da.hDevice = bogus;
    da.phAllocationList = &bogus;
    da.AllocationCount = 1;
    da.Flags.AssumeNotInUse = 1;
    EXPECT_REFUSED(p, &da, "DestroyAllocation2(bogus, AssumeNotInUse)");
}

/* Valid device, bogus allocation handle -> refused (exercises device-valid path). */
static void
Destroy2_ValidDeviceBogusAlloc(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_DESTROYALLOCATION2 da;
    D3DKMT_HANDLE bogus = (D3DKMT_HANDLE)0xDEAD6002;

    LOADFN(PFND3DKMT_DESTROYALLOCATION2, p, "D3DKMTDestroyAllocation2");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    memset(&da, 0, sizeof(da));
    da.hDevice = hDevice;
    da.phAllocationList = &bogus;
    da.AllocationCount = 1;
    EXPECT_REFUSED(p, &da, "DestroyAllocation2(valid device, bogus allocation)");

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void
Lock2_BadHandle(void)
{
    D3DKMT_LOCK2 lk;

    LOADFN(PFND3DKMT_LOCK2, p, "D3DKMTLock2");

    memset(&lk, 0, sizeof(lk));
    lk.hDevice = (D3DKMT_HANDLE)0xDEAD6003;
    lk.hAllocation = (D3DKMT_HANDLE)0xDEAD6004;
    EXPECT_REFUSED(p, &lk, "Lock2(bogus device/allocation)");
}

static void
Unlock2_BadHandle(void)
{
    D3DKMT_UNLOCK2 ul;

    LOADFN(PFND3DKMT_UNLOCK2, p, "D3DKMTUnlock2");

    memset(&ul, 0, sizeof(ul));
    ul.hDevice = (D3DKMT_HANDLE)0xDEAD6005;
    ul.hAllocation = (D3DKMT_HANDLE)0xDEAD6006;
    EXPECT_REFUSED(p, &ul, "Unlock2(bogus device/allocation)");
}

static void
Allocation2_PositiveRoundTrip(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEALLOCATION CreateAllocation;
    D3DDDI_ALLOCATIONINFO AllocationInfo;
    D3DKMT_DESTROYALLOCATION2 DestroyAllocation;
    D3DKMT_LOCK2 Lock;
    D3DKMT_UNLOCK2 Unlock;
    UINT AllocationSize = 4096;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreate, "D3DKMTCreateAllocation");
    LOADFN(PFND3DKMT_LOCK2, pLock, "D3DKMTLock2");
    LOADFN(PFND3DKMT_UNLOCK2, pUnlock, "D3DKMTUnlock2");
    LOADFN(PFND3DKMT_DESTROYALLOCATION2, pDestroy, "D3DKMTDestroyAllocation2");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
        return;

    memset(&AllocationInfo, 0, sizeof(AllocationInfo));
    AllocationInfo.pPrivateDriverData = &AllocationSize;
    AllocationInfo.PrivateDriverDataSize = sizeof(AllocationSize);
    memset(&CreateAllocation, 0, sizeof(CreateAllocation));
    CreateAllocation.hDevice = hDevice;
    CreateAllocation.pAllocationInfo = &AllocationInfo;
    CreateAllocation.NumAllocations = 1;
    Status = pCreate(&CreateAllocation);
    if (!NT_SUCCESS(Status))
    {
        skip("CreateAllocation is not supported by this KMD (0x%08lX)\n", (long)Status);
        goto cleanup;
    }

    ok(AllocationInfo.hAllocation != 0, "CreateAllocation returned a zero allocation handle\n");
    memset(&Lock, 0, sizeof(Lock));
    Lock.hDevice = hDevice;
    Lock.hAllocation = AllocationInfo.hAllocation;
    Status = pLock(&Lock);
    ok(NT_SUCCESS(Status), "Lock2 failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        ok(Lock.pData != NULL, "Lock2 succeeded without returning a mapping\n");
        memset(&Unlock, 0, sizeof(Unlock));
        Unlock.hDevice = hDevice;
        Unlock.hAllocation = AllocationInfo.hAllocation;
        Status = pUnlock(&Unlock);
        ok(NT_SUCCESS(Status), "Unlock2 failed 0x%08lX\n", (long)Status);
    }

    memset(&DestroyAllocation, 0, sizeof(DestroyAllocation));
    DestroyAllocation.hDevice = hDevice;
    DestroyAllocation.phAllocationList = &AllocationInfo.hAllocation;
    DestroyAllocation.AllocationCount = 1;
    Status = pDestroy(&DestroyAllocation);
    ok(NT_SUCCESS(Status), "DestroyAllocation2 failed 0x%08lX\n", (long)Status);

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(allocdestroy2)
{
    Destroy2_BadHandle();
    Destroy2_ValidDeviceBogusAlloc();
    Lock2_BadHandle();
    Unlock2_BadHandle();
    Allocation2_PositiveRoundTrip();
}
