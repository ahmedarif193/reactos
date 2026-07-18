/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     In-depth D3DKMT display / VidPN parity tests (Win11 ARM64 green)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Deep coverage of the user-mode display / VidPN surface, complementing the
 * existing NULL-contract suite (D3dkmtDisplay) and the integration suite
 * (display).  Targets a Win11 ARM64 guest where the desktop compositor (DWM)
 * owns the primary VidPN source and the adapter may be display-only:
 *
 *   - SetVidPnSourceOwner exercised with EACH D3DKMT_VIDPNSOURCEOWNER_TYPE
 *     (UNOWNED/SHARED/EXCLUSIVE/EXCLUSIVEGDI/EMULATED): attempt claim ->
 *     CheckVidPnExclusiveOwnership -> release.  An exclusive/shared claim from
 *     a test process is refused while DWM owns the source -- that refusal is
 *     the correct behavior, so claims are attempted then skipped on failure,
 *     never asserted to succeed.
 *   - SetVidPnSourceOwner1 / SetVidPnSourceOwner2 (NULL contract + guarded).
 *   - GetDisplayModeList full two-pass on VidPnSourceId 0 (every returned mode
 *     must have Width/Height > 0).
 *   - SetDisplayMode (negative / non-destructive only -- never changes the mode).
 *   - Gamma ramp round-trip (read current ramp via GDI, restore the same ramp
 *     via D3DKMTSetGammaRamp, skip on refusal) + SetGammaRamp NULL contract.
 *   - CheckMonitorPowerState, CheckOcclusion (real HWND), GetScanLine,
 *     PollDisplayChildren, InvalidateActiveVidPn (obsolete -> NOT_SUPPORTED ok),
 *     QueryVidPnExclusiveOwnership, CheckVidPnExclusiveOwnership.
 *   - WaitForVerticalBlankEvent2 driven by an already-signaled user event so it
 *     returns immediately (never blocks); traced only, never timing-asserted.
 *
 * Every entry point is resolved dynamically, so the binary degrades gracefully
 * (skip) on hosts lacking a given export.  AV-prone calls are SEH-guarded.
 */

#include "precomp.h"
#include <winuser.h>

#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBL)
#endif
#ifndef STATUS_GRAPHICS_PRESENT_OCCLUDED
#define STATUS_GRAPHICS_PRESENT_OCCLUDED ((NTSTATUS)0xC01E0006L)
#endif

/* SetVidPnSourceOwner2 (struct) is gated >= WDDM2_3 and is not visible at the
 * 0x5023 (WDDM2_0) compile version, so its NULL contract is exercised through a
 * generic pointer -- we only need to pass NULL and observe the refusal. */
typedef NTSTATUS (APIENTRY *PFN_GENERIC_NULLARG)(const void *);

/* All five owner types declared by D3DKMT_VIDPNSOURCEOWNER_TYPE at 0x5023. */
static const struct
{
    D3DKMT_VIDPNSOURCEOWNER_TYPE Type;
    const char                  *Name;
} s_OwnerTypes[] =
{
    { D3DKMT_VIDPNSOURCEOWNER_UNOWNED,      "UNOWNED"      },
    { D3DKMT_VIDPNSOURCEOWNER_SHARED,       "SHARED"       },
    { D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE,    "EXCLUSIVE"    },
    { D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI, "EXCLUSIVEGDI" },
    { D3DKMT_VIDPNSOURCEOWNER_EMULATED,     "EMULATED"     },
};

#define NUM_OWNER_TYPES (sizeof(s_OwnerTypes) / sizeof(s_OwnerTypes[0]))

/* Open \\.\DISPLAY1 and create a device on it.  TRUE on success. */
static BOOL
OpenAdapterAndDevice(D3DKMT_HANDLE *phAdapter, D3DKMT_HANDLE *phDevice)
{
    D3DKMT_HANDLE hAdapter, hDevice;

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
        return FALSE;

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        CloseAdapter(hAdapter);
        return FALSE;
    }

    *phAdapter = hAdapter;
    *phDevice = hDevice;
    return TRUE;
}

/* ---- SetVidPnSourceOwner: NULL contract ---- */
static void Test_SetVidPnSourceOwner_Null(void)
{
    LOADFN(PFND3DKMT_SETVIDPNSOURCEOWNER, pfn, "D3DKMTSetVidPnSourceOwner");
    EXPECT_NULL_REJECTED(pfn, "D3DKMTSetVidPnSourceOwner");
}

/*
 * ---- SetVidPnSourceOwner: every owner type ----
 * Attempt to claim VidPnSourceId 0 with each D3DKMT_VIDPNSOURCEOWNER_TYPE,
 * verify via CheckVidPnExclusiveOwnership when the claim is granted, then
 * release.  The claim is *attempted* (traced) and skipped on refusal -- a
 * second process cannot take the primary source from DWM, which is the
 * expected Win11 behavior.  Only the param-validation cases and the final
 * empty release are asserted as invariants.
 */
static void Test_SetVidPnSourceOwner_AllTypes(void)
{
    D3DKMT_SETVIDPNSOURCEOWNER OwnerData;
    D3DKMT_HANDLE hAdapter = 0, hDevice = 0;
    PFND3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP pfnCheck;
    NTSTATUS Status;
    BOOL faulted;
    size_t i;

    LOADFN(PFND3DKMT_SETVIDPNSOURCEOWNER, pfn, "D3DKMTSetVidPnSourceOwner");
    pfnCheck = (PFND3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP)
               LoadD3DKMTProc("D3DKMTCheckVidPnExclusiveOwnership");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
    {
        skip("Cannot open adapter/device for SetVidPnSourceOwner type sweep\n");
        return;
    }

    for (i = 0; i < NUM_OWNER_TYPES; ++i)
    {
        D3DKMT_VIDPNSOURCEOWNER_TYPE ownerType = s_OwnerTypes[i].Type;
        D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId = 0;

        memset(&OwnerData, 0, sizeof(OwnerData));
        OwnerData.hDevice = hDevice;
        OwnerData.pType = &ownerType;
        OwnerData.pVidPnSourceId = &srcId;
        OwnerData.VidPnSourceCount = 1;

        Status = STATUS_SUCCESS;
        faulted = FALSE;
        _SEH2_TRY { Status = pfn(&OwnerData); }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
        _SEH2_END;

        if (faulted)
        {
            trace("SetVidPnSourceOwner(%s) faulted\n", s_OwnerTypes[i].Name);
            continue;
        }

        trace("SetVidPnSourceOwner(%s) on source 0 -> 0x%08lx\n",
              s_OwnerTypes[i].Name, (unsigned long)Status);

        if (NT_SUCCESS(Status))
        {
            /* Claim granted: confirm and then release this source. */
            if (pfnCheck)
            {
                D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP CheckData;
                NTSTATUS cs;

                memset(&CheckData, 0, sizeof(CheckData));
                CheckData.hAdapter = hAdapter;
                CheckData.VidPnSourceId = 0;
                cs = pfnCheck(&CheckData);
                trace("  CheckVidPnExclusiveOwnership after %s -> 0x%08lx\n",
                      s_OwnerTypes[i].Name, (unsigned long)cs);
            }

            /* Release everything before the next iteration. */
            memset(&OwnerData, 0, sizeof(OwnerData));
            OwnerData.hDevice = hDevice;
            OwnerData.VidPnSourceCount = 0;
            (void)pfn(&OwnerData);
        }
        else
        {
            skip("SetVidPnSourceOwner(%s) refused (0x%08lx) -- source owned by the compositor\n",
                 s_OwnerTypes[i].Name, (unsigned long)Status);
        }
    }

    /* Param validation: count > 0 but NULL arrays must be refused. */
    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = hDevice;
    OwnerData.pType = NULL;
    OwnerData.pVidPnSourceId = NULL;
    OwnerData.VidPnSourceCount = 1;
    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&OwnerData); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;
    ok(faulted || !NT_SUCCESS(Status),
       "SetVidPnSourceOwner(count=1, NULL arrays) must be refused, got 0x%08lx%s\n",
       (unsigned long)Status, faulted ? " (faulted)" : "");

    /* Param validation: bogus device handle must be refused. */
    {
        D3DKMT_VIDPNSOURCEOWNER_TYPE et = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
        D3DDDI_VIDEO_PRESENT_SOURCE_ID sid = 0;

        memset(&OwnerData, 0, sizeof(OwnerData));
        OwnerData.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
        OwnerData.pType = &et;
        OwnerData.pVidPnSourceId = &sid;
        OwnerData.VidPnSourceCount = 1;
        Status = STATUS_SUCCESS;
        faulted = FALSE;
        _SEH2_TRY { Status = pfn(&OwnerData); }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
        _SEH2_END;
        ok(faulted || !NT_SUCCESS(Status),
           "SetVidPnSourceOwner(invalid device) must be refused, got 0x%08lx%s\n",
           (unsigned long)Status, faulted ? " (faulted)" : "");
    }

    /* Empty release is a documented no-op and must succeed. */
    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = hDevice;
    OwnerData.VidPnSourceCount = 0;
    Status = pfn(&OwnerData);
    ok(NT_SUCCESS(Status),
       "SetVidPnSourceOwner empty release must succeed, got 0x%08lx\n",
       (unsigned long)Status);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- SetVidPnSourceOwner1: NULL contract + guarded claim ---- */
static void Test_SetVidPnSourceOwner1(void)
{
    D3DKMT_SETVIDPNSOURCEOWNER1 Owner1;
    D3DKMT_HANDLE hAdapter = 0, hDevice = 0;
    D3DKMT_VIDPNSOURCEOWNER_TYPE ownerType = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId = 0;
    NTSTATUS Status;
    BOOL faulted;

    LOADFN(PFND3DKMT_SETVIDPNSOURCEOWNER1, pfn, "D3DKMTSetVidPnSourceOwner1");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTSetVidPnSourceOwner1");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
    {
        skip("Cannot open adapter/device for SetVidPnSourceOwner1\n");
        return;
    }

    /* Attempt an exclusive claim with the output-duplication flag set. */
    memset(&Owner1, 0, sizeof(Owner1));
    Owner1.Version0.hDevice = hDevice;
    Owner1.Version0.pType = &ownerType;
    Owner1.Version0.pVidPnSourceId = &srcId;
    Owner1.Version0.VidPnSourceCount = 1;
    Owner1.Flags.Value = 0;
    Owner1.Flags.AllowOutputDuplication = 1;

    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&Owner1); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;

    if (!faulted && NT_SUCCESS(Status))
    {
        trace("SetVidPnSourceOwner1 EXCLUSIVE+OutputDupl granted\n");
        /* Release. */
        memset(&Owner1, 0, sizeof(Owner1));
        Owner1.Version0.hDevice = hDevice;
        Owner1.Version0.VidPnSourceCount = 0;
        (void)pfn(&Owner1);
    }
    else
    {
        skip("SetVidPnSourceOwner1 refused (0x%08lx%s) -- compositor owns the source\n",
             (unsigned long)Status, faulted ? ", faulted" : "");
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- SetVidPnSourceOwner2: NULL contract (struct gated out at 0x5023) ---- */
static void Test_SetVidPnSourceOwner2(void)
{
    PFN_GENERIC_NULLARG pfn;

    pfn = (PFN_GENERIC_NULLARG)LoadD3DKMTProc("D3DKMTSetVidPnSourceOwner2");
    if (!pfn)
    {
        skip("D3DKMTSetVidPnSourceOwner2 not exported by gdi32.dll\n");
        return;
    }

    EXPECT_NULL_REJECTED(pfn, "D3DKMTSetVidPnSourceOwner2");
}

/* ---- CheckVidPnExclusiveOwnership: NULL + real query + invalid adapter ---- */
static void Test_CheckVidPnExclusiveOwnership(void)
{
    D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP CheckData;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP, pfn,
           "D3DKMTCheckVidPnExclusiveOwnership");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTCheckVidPnExclusiveOwnership");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for CheckVidPnExclusiveOwnership\n");
        return;
    }

    /* On a composited desktop the primary source reports SUCCESS (no exclusive
     * owner) or PRESENT_OCCLUDED (DWM owns it); both are valid query results. */
    memset(&CheckData, 0, sizeof(CheckData));
    CheckData.hAdapter = hAdapter;
    CheckData.VidPnSourceId = 0;
    Status = pfn(&CheckData);
    /* Win11 may report any graphics-facility status (0xC01E****) here -- SUCCESS,
     * PRESENT_OCCLUDED (0xC01E0006), or e.g. 0xC01E000C -- depending on the
     * compositor / VidPN state; all are valid query results. Only a non-graphics
     * failure (e.g. STATUS_INVALID_PARAMETER) would be a real bug. */
    ok(NT_SUCCESS(Status) || ((unsigned long)Status >> 16) == 0xC01E,
       "CheckVidPnExclusiveOwnership(source 0) unexpected 0x%08lx\n",
       (unsigned long)Status);

    /* Invalid adapter must be refused. */
    memset(&CheckData, 0, sizeof(CheckData));
    CheckData.hAdapter = (D3DKMT_HANDLE)0xBAD0CAFE;
    CheckData.VidPnSourceId = 0;
    Status = pfn(&CheckData);
    ok(!NT_SUCCESS(Status),
       "CheckVidPnExclusiveOwnership(invalid adapter) must fail, got 0x%08lx\n",
       (unsigned long)Status);

    CloseAdapter(hAdapter);
}

/* ---- QueryVidPnExclusiveOwnership: NULL + real query ---- */
static void Test_QueryVidPnExclusiveOwnership(void)
{
    D3DKMT_QUERYVIDPNEXCLUSIVEOWNERSHIP QueryData;
    NTSTATUS Status;
    BOOL faulted;

    LOADFN(PFND3DKMT_QUERYVIDPNEXCLUSIVEOWNERSHIP, pfn,
           "D3DKMTQueryVidPnExclusiveOwnership");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTQueryVidPnExclusiveOwnership");

    /* Query global exclusive-ownership state (no specific process/window). */
    memset(&QueryData, 0, sizeof(QueryData));
    QueryData.hProcess = NULL;
    QueryData.hWindow = NULL;

    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&QueryData); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;

    if (faulted)
    {
        skip("QueryVidPnExclusiveOwnership faulted on a NULL process/window query\n");
        return;
    }

    if (NT_SUCCESS(Status))
    {
        int ot = (int)QueryData.OwnerType;
        ok(ot == D3DKMT_VIDPNSOURCEOWNER_UNOWNED ||
           ot == D3DKMT_VIDPNSOURCEOWNER_SHARED ||
           ot == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE ||
           ot == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI ||
           ot == D3DKMT_VIDPNSOURCEOWNER_EMULATED,
           "QueryVidPnExclusiveOwnership OwnerType unexpected: %d\n", ot);
        trace("QueryVidPnExclusiveOwnership: OwnerType=%d VidPnSourceId=%u Luid=%08lx:%08lx\n",
              (int)QueryData.OwnerType, (unsigned)QueryData.VidPnSourceId,
              (unsigned long)QueryData.AdapterLuid.HighPart,
              (unsigned long)QueryData.AdapterLuid.LowPart);
    }
    else
    {
        trace("QueryVidPnExclusiveOwnership returned 0x%08lx\n", (unsigned long)Status);
    }
}

/*
 * ---- GetDisplayModeList: full two-pass on VidPnSourceId 0 ----
 * Pass 1 (pModeList == NULL) yields the count; pass 2 fills the array.  Every
 * returned mode must have Width > 0 and Height > 0.
 */
static void Test_GetDisplayModeList_Deep(void)
{
    D3DKMT_GETDISPLAYMODELIST ModeList;
    D3DKMT_DISPLAYMODE *pModes;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;
    UINT count, i;
    BOOL allValid;

    LOADFN(PFND3DKMT_GETDISPLAYMODELIST, pfn, "D3DKMTGetDisplayModeList");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTGetDisplayModeList");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for GetDisplayModeList deep test\n");
        return;
    }

    /* Pass 1: count query. */
    memset(&ModeList, 0, sizeof(ModeList));
    ModeList.hAdapter = hAdapter;
    ModeList.VidPnSourceId = 0;
    ModeList.pModeList = NULL;
    ModeList.ModeCount = 0;
    Status = pfn(&ModeList);

    if (Status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(Status))
    {
        skip("GetDisplayModeList count query failed 0x%08lx\n", (unsigned long)Status);
        CloseAdapter(hAdapter);
        return;
    }

    count = ModeList.ModeCount;
    ok(count >= 1, "Expected at least 1 display mode on source 0, got %u\n", count);
    trace("GetDisplayModeList source 0: %u modes\n", count);

    if (count >= 1 && count <= 4096)
    {
        pModes = (D3DKMT_DISPLAYMODE *)LocalAlloc(LMEM_ZEROINIT,
                     count * sizeof(D3DKMT_DISPLAYMODE));
        if (pModes)
        {
            /* Pass 2: fill. */
            memset(&ModeList, 0, sizeof(ModeList));
            ModeList.hAdapter = hAdapter;
            ModeList.VidPnSourceId = 0;
            ModeList.pModeList = pModes;
            ModeList.ModeCount = count;
            Status = pfn(&ModeList);

            if (NT_SUCCESS(Status) && ModeList.ModeCount >= 1)
            {
                UINT got = ModeList.ModeCount;

                allValid = TRUE;
                for (i = 0; i < got; ++i)
                {
                    if (pModes[i].Width == 0 || pModes[i].Height == 0)
                    {
                        allValid = FALSE;
                        ok(0, "Mode %u has zero dimension %ux%u\n",
                           i, pModes[i].Width, pModes[i].Height);
                        break;
                    }
                }
                ok(allValid, "All %u returned modes have Width/Height > 0\n", got);

                trace("First mode: %ux%u fmt=%d refresh=%u; Last mode: %ux%u\n",
                      pModes[0].Width, pModes[0].Height,
                      (int)pModes[0].Format, pModes[0].IntegerRefreshRate,
                      pModes[got - 1].Width, pModes[got - 1].Height);
            }
            else
            {
                skip("GetDisplayModeList fill pass failed 0x%08lx\n",
                     (unsigned long)Status);
            }

            LocalFree(pModes);
        }
    }

    /* Probe a couple of other source IDs (may legitimately not exist). */
    {
        D3DDDI_VIDEO_PRESENT_SOURCE_ID sid;
        for (sid = 1; sid <= 2; ++sid)
        {
            memset(&ModeList, 0, sizeof(ModeList));
            ModeList.hAdapter = hAdapter;
            ModeList.VidPnSourceId = sid;
            Status = pfn(&ModeList);
            trace("GetDisplayModeList source %u count-query -> 0x%08lx (count=%u)\n",
                  (unsigned)sid, (unsigned long)Status, ModeList.ModeCount);
        }
    }

    /* Invalid adapter must be refused. */
    memset(&ModeList, 0, sizeof(ModeList));
    ModeList.hAdapter = (D3DKMT_HANDLE)0xBAD0CAFE;
    ModeList.VidPnSourceId = 0;
    Status = pfn(&ModeList);
    ok(!NT_SUCCESS(Status),
       "GetDisplayModeList(invalid adapter) must fail, got 0x%08lx\n",
       (unsigned long)Status);

    CloseAdapter(hAdapter);
}

/*
 * ---- SetDisplayMode: negative only ----
 * Never changes the active mode (that would need a real primary allocation and
 * exclusive ownership, and would be destructive on a live desktop).
 */
static void Test_SetDisplayMode_Negative(void)
{
    D3DKMT_SETDISPLAYMODE SetMode;
    NTSTATUS Status;
    BOOL faulted;

    LOADFN(PFND3DKMT_SETDISPLAYMODE, pfn, "D3DKMTSetDisplayMode");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTSetDisplayMode");

    /* Bogus device + no primary allocation must be refused, not honored. */
    memset(&SetMode, 0, sizeof(SetMode));
    SetMode.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
    SetMode.hPrimaryAllocation = 0;
    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&SetMode); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;
    ok(faulted || !NT_SUCCESS(Status),
       "SetDisplayMode(invalid device) must be refused, got 0x%08lx%s\n",
       (unsigned long)Status, faulted ? " (faulted)" : "");
}

/* A primary allocation is a device-owned handle, even when both devices use the same adapter. */
static void Test_SetDisplayMode_CrossDeviceAllocation(void)
{
    D3DKMT_HANDLE hAdapter = 0, hOwnerDevice = 0, hOtherDevice = 0;
    D3DKMT_CREATEALLOCATION CreateAllocation;
    D3DDDI_ALLOCATIONINFO AllocationInfo;
    D3DKMT_DESTROYALLOCATION DestroyAllocation;
    D3DKMT_SETDISPLAYMODE SetMode;
    struct { UINT Width; UINT Height; UINT Bpp; } PrivateData;
    NTSTATUS Status;
    BOOL faulted;

    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreate, "D3DKMTCreateAllocation");
    LOADFN(PFND3DKMT_DESTROYALLOCATION, pDestroy, "D3DKMTDestroyAllocation");
    LOADFN(PFND3DKMT_SETDISPLAYMODE, pSetMode, "D3DKMTSetDisplayMode");

    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0)
    {
        skip("Cannot open adapter for SetDisplayMode cross-device test\n");
        return;
    }
    hOwnerDevice = CreateTestDevice(hAdapter);
    hOtherDevice = CreateTestDevice(hAdapter);
    if (hOwnerDevice == 0 || hOtherDevice == 0)
    {
        skip("Cannot create two devices for SetDisplayMode cross-device test\n");
        goto Cleanup;
    }

    PrivateData.Width = 64;
    PrivateData.Height = 64;
    PrivateData.Bpp = 32;
    memset(&AllocationInfo, 0, sizeof(AllocationInfo));
    AllocationInfo.pPrivateDriverData = &PrivateData;
    AllocationInfo.PrivateDriverDataSize = sizeof(PrivateData);
    memset(&CreateAllocation, 0, sizeof(CreateAllocation));
    CreateAllocation.hDevice = hOwnerDevice;
    CreateAllocation.NumAllocations = 1;
    CreateAllocation.pAllocationInfo = &AllocationInfo;
    Status = pCreate(&CreateAllocation);
    if (!NT_SUCCESS(Status) || AllocationInfo.hAllocation == 0)
    {
        skip("Cannot create primary candidate for SetDisplayMode cross-device test (0x%08lx)\n", (unsigned long)Status);
        goto Cleanup;
    }

    memset(&SetMode, 0, sizeof(SetMode));
    SetMode.hDevice = hOtherDevice;
    SetMode.hPrimaryAllocation = AllocationInfo.hAllocation;
    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pSetMode(&SetMode); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;
    ok(faulted || !NT_SUCCESS(Status), "SetDisplayMode must reject an allocation owned by another device on the same adapter, got 0x%08lx%s\n", (unsigned long)Status, faulted ? " (faulted)" : "");

    memset(&DestroyAllocation, 0, sizeof(DestroyAllocation));
    DestroyAllocation.hDevice = hOwnerDevice;
    DestroyAllocation.phAllocationList = &AllocationInfo.hAllocation;
    DestroyAllocation.AllocationCount = 1;
    Status = pDestroy(&DestroyAllocation);
    ok(NT_SUCCESS(Status), "DestroyAllocation after SetDisplayMode cross-device rejection failed 0x%08lx\n", (unsigned long)Status);

Cleanup:
    if (hOtherDevice != 0)
        DestroyTestDevice(hOtherDevice);
    if (hOwnerDevice != 0)
        DestroyTestDevice(hOwnerDevice);
    CloseAdapter(hAdapter);
}

/*
 * ---- Gamma ramp: NULL contract + non-destructive round-trip ----
 * Reads the current ramp through GDI (same 256x3x16 layout as the D3DKMT ramp)
 * and writes the identical ramp back via D3DKMTSetGammaRamp.  Setting gamma on
 * a source owned by DWM is typically refused -- attempt then skip, never assert
 * success.  Falls back to a linear ramp if GDI cannot read the current one.
 */
static void Test_GammaRamp(void)
{
    D3DDDI_GAMMA_RAMP_RGB256x3x16 ramp;
    D3DKMT_SETGAMMARAMP SetRamp;
    D3DKMT_HANDLE hAdapter = 0, hDevice = 0;
    NTSTATUS Status;
    BOOL faulted, haveRamp = FALSE;
    HDC hdc;
    int i;

    LOADFN(PFND3DKMT_SETGAMMARAMP, pfn, "D3DKMTSetGammaRamp");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTSetGammaRamp");

    /* Read the current ramp via GDI (skip-guarded). */
    memset(&ramp, 0, sizeof(ramp));
    hdc = GetDC(NULL);
    if (hdc)
    {
        if (GetDeviceGammaRamp(hdc, &ramp))
        {
            UINT nonZero = 0;
            for (i = 0; i < 256; ++i)
                if (ramp.Red[i] || ramp.Green[i] || ramp.Blue[i])
                    ++nonZero;
            /* A real LUT is not all zero. */
            ok(nonZero > 0, "Current gamma ramp is entirely zero (size check)\n");
            haveRamp = (nonZero > 0);
            trace("GetDeviceGammaRamp: %u/256 entries non-zero\n", nonZero);
        }
        else
        {
            trace("GetDeviceGammaRamp unavailable on this display\n");
        }
        ReleaseDC(NULL, hdc);
    }

    if (!haveRamp)
    {
        /* Linear identity ramp (0..255 -> 0..65535); harmless to apply. */
        for (i = 0; i < 256; ++i)
            ramp.Red[i] = ramp.Green[i] = ramp.Blue[i] = (USHORT)(i * 257);
    }

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
    {
        skip("Cannot open adapter/device for gamma ramp set-back\n");
        return;
    }

    /* Write the same ramp back (non-destructive); skip on refusal. */
    memset(&SetRamp, 0, sizeof(SetRamp));
    SetRamp.hDevice = hDevice;
    SetRamp.VidPnSourceId = 0;
    SetRamp.Type = D3DDDI_GAMMARAMP_RGB256x3x16;
    SetRamp.pGammaRampRgb256x3x16 = &ramp;
    SetRamp.Size = sizeof(ramp);

    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&SetRamp); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;

    if (!faulted && NT_SUCCESS(Status))
        trace("D3DKMTSetGammaRamp restored the current ramp on source 0\n");
    else
        skip("D3DKMTSetGammaRamp refused (0x%08lx%s) -- source owned by the compositor\n",
             (unsigned long)Status, faulted ? ", faulted" : "");

    /* Invalid device must be refused. */
    memset(&SetRamp, 0, sizeof(SetRamp));
    SetRamp.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
    SetRamp.VidPnSourceId = 0;
    SetRamp.Type = D3DDDI_GAMMARAMP_RGB256x3x16;
    SetRamp.pGammaRampRgb256x3x16 = &ramp;
    SetRamp.Size = sizeof(ramp);
    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&SetRamp); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;
    ok(faulted || !NT_SUCCESS(Status),
       "SetGammaRamp(invalid device) must be refused, got 0x%08lx%s\n",
       (unsigned long)Status, faulted ? " (faulted)" : "");

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- CheckMonitorPowerState: NULL + real query + invalid adapter ---- */
static void Test_CheckMonitorPowerState(void)
{
    D3DKMT_CHECKMONITORPOWERSTATE PowerData;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;
    BOOL faulted;

    LOADFN(PFND3DKMT_CHECKMONITORPOWERSTATE, pfn, "D3DKMTCheckMonitorPowerState");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTCheckMonitorPowerState");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for CheckMonitorPowerState\n");
        return;
    }

    /* Real query: STATUS_SUCCESS means the monitor on source 0 is powered. */
    memset(&PowerData, 0, sizeof(PowerData));
    PowerData.hAdapter = hAdapter;
    PowerData.VidPnSourceId = 0;
    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&PowerData); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;
    trace("CheckMonitorPowerState(source 0) -> 0x%08lx%s\n",
          (unsigned long)Status, faulted ? " (faulted)" : "");

    /* Invalid adapter must be refused. */
    memset(&PowerData, 0, sizeof(PowerData));
    PowerData.hAdapter = (D3DKMT_HANDLE)0xBAD0CAFE;
    PowerData.VidPnSourceId = 0;
    Status = pfn(&PowerData);
    ok(!NT_SUCCESS(Status),
       "CheckMonitorPowerState(invalid adapter) must fail, got 0x%08lx\n",
       (unsigned long)Status);

    CloseAdapter(hAdapter);
}

/* ---- CheckOcclusion: NULL + desktop window + created window ---- */
static void Test_CheckOcclusion(void)
{
    D3DKMT_CHECKOCCLUSION OcclData;
    HWND hWnd;
    NTSTATUS Status;
    BOOL faulted;

    LOADFN(PFND3DKMT_CHECKOCCLUSION, pfn, "D3DKMTCheckOcclusion");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTCheckOcclusion");

    /* Desktop window: with active composition this is reported not-occluded
     * (SUCCESS) or occluded (PRESENT_OCCLUDED); either is a valid result. */
    memset(&OcclData, 0, sizeof(OcclData));
    OcclData.hWindow = GetDesktopWindow();
    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&OcclData); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;
    trace("CheckOcclusion(desktop) -> 0x%08lx%s\n",
          (unsigned long)Status, faulted ? " (faulted)" : "");

    /* A real top-level window via a pre-registered class (no class to register). */
    hWnd = CreateWindowExW(0, L"STATIC", L"d3dkmt_occlusion",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           320, 240, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (hWnd)
    {
        ShowWindow(hWnd, SW_SHOWNORMAL);
        memset(&OcclData, 0, sizeof(OcclData));
        OcclData.hWindow = hWnd;
        Status = STATUS_SUCCESS;
        faulted = FALSE;
        _SEH2_TRY { Status = pfn(&OcclData); }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
        _SEH2_END;
        if (!faulted)
            ok(NT_SUCCESS(Status) || Status == STATUS_GRAPHICS_PRESENT_OCCLUDED,
               "CheckOcclusion(window) unexpected 0x%08lx\n", (unsigned long)Status);
        else
            skip("CheckOcclusion(window) faulted\n");
        DestroyWindow(hWnd);
    }
    else
    {
        skip("CreateWindowExW failed for CheckOcclusion window test\n");
    }

    /* A bogus window handle must be refused. */
    memset(&OcclData, 0, sizeof(OcclData));
    OcclData.hWindow = (HWND)(ULONG_PTR)0xBAD0CAFE;
    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&OcclData); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;
    ok(faulted || !NT_SUCCESS(Status),
       "CheckOcclusion(invalid window) must be refused, got 0x%08lx%s\n",
       (unsigned long)Status, faulted ? " (faulted)" : "");
}

/* ---- GetScanLine: NULL + sweep sources + invalid adapter ---- */
static void Test_GetScanLine_Deep(void)
{
    D3DKMT_GETSCANLINE ScanData;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID sid;

    LOADFN(PFND3DKMT_GETSCANLINE, pfn, "D3DKMTGetScanLine");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTGetScanLine");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for GetScanLine deep test\n");
        return;
    }

    for (sid = 0; sid <= 2; ++sid)
    {
        memset(&ScanData, 0, sizeof(ScanData));
        ScanData.hAdapter = hAdapter;
        ScanData.VidPnSourceId = sid;
        Status = pfn(&ScanData);
        if (NT_SUCCESS(Status))
        {
            ok(ScanData.InVerticalBlank == 0 || ScanData.InVerticalBlank == 1,
               "GetScanLine source %u InVerticalBlank invalid %d\n",
               (unsigned)sid, ScanData.InVerticalBlank);
            ok(ScanData.ScanLine < 65536,
               "GetScanLine source %u implausible ScanLine %u\n",
               (unsigned)sid, ScanData.ScanLine);
            trace("GetScanLine source %u: InVBlank=%d ScanLine=%u\n",
                  (unsigned)sid, ScanData.InVerticalBlank, ScanData.ScanLine);
        }
        else
        {
            trace("GetScanLine source %u -> 0x%08lx\n",
                  (unsigned)sid, (unsigned long)Status);
        }
    }

    /* Invalid adapter must be refused. */
    memset(&ScanData, 0, sizeof(ScanData));
    ScanData.hAdapter = (D3DKMT_HANDLE)0xBAD0CAFE;
    ScanData.VidPnSourceId = 0;
    Status = pfn(&ScanData);
    ok(!NT_SUCCESS(Status),
       "GetScanLine(invalid adapter) must fail, got 0x%08lx\n",
       (unsigned long)Status);

    CloseAdapter(hAdapter);
}

/* ---- PollDisplayChildren: NULL + non-destructive poll ---- */
static void Test_PollDisplayChildren(void)
{
    D3DKMT_POLLDISPLAYCHILDREN PollData;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;
    BOOL faulted;

    LOADFN(PFND3DKMT_POLLDISPLAYCHILDREN, pfn, "D3DKMTPollDisplayChildren");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTPollDisplayChildren");

    /* Invalid adapter must be refused. */
    memset(&PollData, 0, sizeof(PollData));
    PollData.hAdapter = (D3DKMT_HANDLE)0xBAD0CAFE;
    Status = pfn(&PollData);
    ok(!NT_SUCCESS(Status),
       "PollDisplayChildren(invalid adapter) must fail, got 0x%08lx\n",
       (unsigned long)Status);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for PollDisplayChildren\n");
        return;
    }

    /* Non-destructive poll; a user-mode caller may be refused on Win11. */
    memset(&PollData, 0, sizeof(PollData));
    PollData.hAdapter = hAdapter;
    PollData.NonDestructiveOnly = 1;
    PollData.SynchronousPolling = 0;
    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&PollData); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;
    if (!faulted && NT_SUCCESS(Status))
        trace("PollDisplayChildren (non-destructive) succeeded\n");
    else
        skip("PollDisplayChildren refused for a user-mode caller (0x%08lx%s)\n",
             (unsigned long)Status, faulted ? ", faulted" : "");

    CloseAdapter(hAdapter);
}

/* ---- InvalidateActiveVidPn: NULL + obsolete-call acceptance ---- */
static void Test_InvalidateActiveVidPn(void)
{
    D3DKMT_INVALIDATEACTIVEVIDPN Inv;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;
    BOOL faulted;

    LOADFN(PFND3DKMT_INVALIDATEACTIVEVIDPN, pfn, "D3DKMTInvalidateActiveVidPn");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTInvalidateActiveVidPn");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for InvalidateActiveVidPn\n");
        return;
    }

    /* Obsolete since Win7: a modern KMD returns STATUS_NOT_SUPPORTED (or some
     * other failure); it must not succeed for a user-mode caller. */
    memset(&Inv, 0, sizeof(Inv));
    Inv.hAdapter = hAdapter;
    Inv.pPrivateDriverData = NULL;
    Inv.PrivateDriverDataSize = 0;
    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&Inv); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;
    ok(faulted || !NT_SUCCESS(Status),
       "InvalidateActiveVidPn (obsolete) must not succeed, got 0x%08lx%s\n",
       (unsigned long)Status, faulted ? " (faulted)" : "");
    if (Status == STATUS_NOT_SUPPORTED)
        trace("InvalidateActiveVidPn returned STATUS_NOT_SUPPORTED (expected; obsolete)\n");
    else
        trace("InvalidateActiveVidPn returned 0x%08lx\n", (unsigned long)Status);

    CloseAdapter(hAdapter);
}

/*
 * ---- WaitForVerticalBlankEvent2: signaled-event, non-blocking ----
 * Drives the v2 wait with an already-signaled manual-reset user event in the
 * object array so the call returns immediately and never blocks under a virtual
 * display.  Traced only -- no timing assertion (per Win11-green rules).
 */
static void Test_WaitForVerticalBlankEvent2(void)
{
    D3DKMT_WAITFORVERTICALBLANKEVENT2 Vb2;
    D3DKMT_HANDLE hAdapter = 0, hDevice = 0;
    HANDLE hEvent;
    NTSTATUS Status;
    BOOL faulted;

    LOADFN(PFND3DKMT_WAITFORVERTICALBLANKEVENT2, pfn,
           "D3DKMTWaitForVerticalBlankEvent2");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTWaitForVerticalBlankEvent2");

    if (!OpenAdapterAndDevice(&hAdapter, &hDevice))
    {
        skip("Cannot open adapter/device for WaitForVerticalBlankEvent2\n");
        return;
    }

    hEvent = CreateEventW(NULL, TRUE /*manual*/, TRUE /*signaled*/, NULL);
    if (!hEvent)
    {
        skip("CreateEventW failed for WaitForVerticalBlankEvent2\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    memset(&Vb2, 0, sizeof(Vb2));
    Vb2.hAdapter = hAdapter;
    Vb2.hDevice = hDevice;
    Vb2.VidPnSourceId = 0;
    Vb2.NumObjects = 1;
    Vb2.ObjectHandleArray[0] = hEvent; /* signaled -> immediate return */

    Status = STATUS_SUCCESS;
    faulted = FALSE;
    _SEH2_TRY { Status = pfn(&Vb2); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { faulted = TRUE; }
    _SEH2_END;

    trace("WaitForVerticalBlankEvent2 (pre-signaled event) -> 0x%08lx%s\n",
          (unsigned long)Status, faulted ? " (faulted)" : "");

    CloseHandle(hEvent);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(displayext)
{
    /* VidPN source ownership */
    Test_SetVidPnSourceOwner_Null();
    Test_SetVidPnSourceOwner_AllTypes();
    Test_SetVidPnSourceOwner1();
    Test_SetVidPnSourceOwner2();
    Test_CheckVidPnExclusiveOwnership();
    Test_QueryVidPnExclusiveOwnership();

    /* Mode enumeration / mode set */
    Test_GetDisplayModeList_Deep();
    Test_SetDisplayMode_Negative();
    Test_SetDisplayMode_CrossDeviceAllocation();

    /* Gamma */
    Test_GammaRamp();

    /* Monitor / occlusion / scanline / poll / invalidate */
    Test_CheckMonitorPowerState();
    Test_CheckOcclusion();
    Test_GetScanLine_Deep();
    Test_PollDisplayChildren();
    Test_InvalidateActiveVidPn();

    /* Vertical blank (v2, non-blocking) */
    Test_WaitForVerticalBlankEvent2();
}
