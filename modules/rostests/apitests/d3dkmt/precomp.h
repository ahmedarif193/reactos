/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Precompiled header for D3DKMT user-mode API tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#ifndef _D3DKMT_APITEST_PRECOMP_H_
#define _D3DKMT_APITEST_PRECOMP_H_

#include <apitest.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <d3dkmthk.h>
#include <pseh/pseh2.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif
#ifndef STATUS_NO_MEMORY
#define STATUS_NO_MEMORY ((NTSTATUS)0xC0000017L)
#endif
#ifndef STATUS_GRAPHICS_PRESENT_OCCLUDED
#define STATUS_GRAPHICS_PRESENT_OCCLUDED ((NTSTATUS)0xC01E0006L)
#endif

/*
 * D3DKMT function typedefs for GetProcAddress loading from gdi32.dll.
 * We load dynamically so the test binary can run on systems where
 * some D3DKMT exports may not exist (pre-Vista, or missing exports).
 */

/* Adapter */
typedef NTSTATUS (APIENTRY *PFN_D3DKMTEnumAdapters)(D3DKMT_ENUMADAPTERS *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTOpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTOpenAdapterFromGdiDisplayName)(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTOpenAdapterFromDeviceName)(D3DKMT_OPENADAPTERFROMDEVICENAME *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTOpenAdapterFromLuid)(D3DKMT_OPENADAPTERFROMLUID *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCloseAdapter)(const D3DKMT_CLOSEADAPTER *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTQueryAdapterInfo)(const D3DKMT_QUERYADAPTERINFO *);

/* Device */
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCreateDevice)(D3DKMT_CREATEDEVICE *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTDestroyDevice)(const D3DKMT_DESTROYDEVICE *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTGetDeviceState)(D3DKMT_GETDEVICESTATE *);

/* Context */
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCreateContext)(D3DKMT_CREATECONTEXT *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTDestroyContext)(const D3DKMT_DESTROYCONTEXT *);

/* Allocation */
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCreateAllocation)(D3DKMT_CREATEALLOCATION *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTDestroyAllocation)(const D3DKMT_DESTROYALLOCATION *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTLock)(D3DKMT_LOCK *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTUnlock)(const D3DKMT_UNLOCK *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTQueryAllocationResidency)(const D3DKMT_QUERYALLOCATIONRESIDENCY *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTSetAllocationPriority)(const D3DKMT_SETALLOCATIONPRIORITY *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTGetAllocationPriority)(const D3DKMT_GETALLOCATIONPRIORITY *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTQueryResourceInfo)(D3DKMT_QUERYRESOURCEINFO *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTOpenResource)(D3DKMT_OPENRESOURCE *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTGetSharedPrimaryHandle)(D3DKMT_GETSHAREDPRIMARYHANDLE *);

/* Sync */
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCreateSynchronizationObject)(D3DKMT_CREATESYNCHRONIZATIONOBJECT *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTDestroySynchronizationObject)(const D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTWaitForSynchronizationObject)(const D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTSignalSynchronizationObject)(const D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTWaitForIdle)(const D3DKMT_WAITFORIDLE *);

/* Display */
typedef NTSTATUS (APIENTRY *PFN_D3DKMTSetVidPnSourceOwner)(const D3DKMT_SETVIDPNSOURCEOWNER *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTGetDisplayModeList)(D3DKMT_GETDISPLAYMODELIST *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTSetDisplayMode)(const D3DKMT_SETDISPLAYMODE *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTWaitForVerticalBlankEvent)(const D3DKMT_WAITFORVERTICALBLANKEVENT *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTGetScanLine)(D3DKMT_GETSCANLINE *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTPollDisplayChildren)(const D3DKMT_POLLDISPLAYCHILDREN *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTInvalidateActiveVidPn)(const D3DKMT_INVALIDATEACTIVEVIDPN *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTSetGammaRamp)(const D3DKMT_SETGAMMARAMP *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTEscape)(const D3DKMT_ESCAPE *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCheckVidPnExclusiveOwnership)(const D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCheckMonitorPowerState)(const D3DKMT_CHECKMONITORPOWERSTATE *);

/* Present / Render */
typedef NTSTATUS (APIENTRY *PFN_D3DKMTPresent)(D3DKMT_PRESENT *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTRender)(D3DKMT_RENDER *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTGetPresentHistory)(D3DKMT_GETPRESENTHISTORY *);

/* Helper: load a D3DKMT function from gdi32 */
static inline FARPROC
LoadD3DKMTProc(const char *Name)
{
    HMODULE hGdi32 = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi32)
        hGdi32 = LoadLibraryW(L"gdi32.dll");
    if (!hGdi32)
        return NULL;
    return GetProcAddress(hGdi32, Name);
}

#define LOAD_D3DKMT(Name) \
    PFN_##Name pfn##Name = (PFN_##Name)LoadD3DKMTProc(#Name); \
    if (!pfn##Name) { skip(#Name " not exported by gdi32.dll\n"); return; }

/*
 * Generic loader for the WDDM2 entry points, typed via the canonical
 * PFND3DKMT_* signatures from d3dkmthk.h. Skips the whole test when gdi32
 * does not export the function (e.g. on down-level ReactOS), so the same
 * binary is portable between Windows 11 and ReactOS.
 *
 *   LOADFN(PFND3DKMT_CREATEPAGINGQUEUE, pCreate, "D3DKMTCreatePagingQueue");
 */
#define LOADFN(Type, Var, Name) \
    Type Var = (Type)LoadD3DKMTProc(Name); \
    if (!(Var)) { skip(Name " not exported by gdi32.dll\n"); return; }

/*
 * Contract check: a D3DKMT entry point must refuse a NULL argument the way
 * Windows does -- either return STATUS_INVALID_PARAMETER, or raise an access
 * violation in the user-mode thunk. Both count as "correctly refused"; only a
 * success status is the bug. SEH keeps a faulting thunk from aborting the
 * whole subtest process, which matters on Win11 where some thunks dereference
 * before validating.
 */
#define EXPECT_NULL_REJECTED(Pfn, Name) \
do { \
    NTSTATUS _st = STATUS_SUCCESS; \
    BOOL _faulted = FALSE; \
    _SEH2_TRY { _st = (Pfn)(NULL); } \
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { _faulted = TRUE; } \
    _SEH2_END; \
    ok(_faulted || !NT_SUCCESS(_st), \
       Name "(NULL) must be refused (STATUS_INVALID_PARAMETER or fault), got 0x%08lX%s\n", \
       (long)_st, _faulted ? " (faulted)" : ""); \
    if (!_faulted && !NT_SUCCESS(_st) && _st != STATUS_INVALID_PARAMETER) \
        trace(Name "(NULL) refused with 0x%08lX (not STATUS_INVALID_PARAMETER)\n", (long)_st); \
} while (0)

/*
 * Block-scoped variant for sweeping many entry points in one START_TEST: loads
 * the function, skips (without leaving the test) if the export is absent, then
 * runs the NULL contract. Lets each function be checked independently.
 */
#define CHECK_NULL_REJECTED(Type, Name) \
do { \
    Type _p = (Type)LoadD3DKMTProc(Name); \
    if (!_p) { skip(Name " not exported by gdi32.dll\n"); break; } \
    EXPECT_NULL_REJECTED(_p, Name); \
} while (0)

/*
 * Helper: open adapter via D3DKMTOpenAdapterFromGdiDisplayName
 * using \\.\DISPLAY1. Returns adapter handle, 0 on failure.
 */
static inline D3DKMT_HANDLE
OpenAdapterFromDisplay1(void)
{
    PFN_D3DKMTOpenAdapterFromGdiDisplayName pfn;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Data;

    pfn = (PFN_D3DKMTOpenAdapterFromGdiDisplayName)
          LoadD3DKMTProc("D3DKMTOpenAdapterFromGdiDisplayName");
    if (!pfn)
        return 0;

    memset(&Data, 0, sizeof(Data));
    wcscpy(Data.DeviceName, L"\\\\.\\DISPLAY1");

    if (!NT_SUCCESS(pfn(&Data)))
        return 0;

    return Data.hAdapter;
}

static inline void
CloseAdapter(D3DKMT_HANDLE hAdapter)
{
    PFN_D3DKMTCloseAdapter pfn;
    D3DKMT_CLOSEADAPTER Data;

    pfn = (PFN_D3DKMTCloseAdapter)LoadD3DKMTProc("D3DKMTCloseAdapter");
    if (!pfn || !hAdapter)
        return;

    memset(&Data, 0, sizeof(Data));
    Data.hAdapter = hAdapter;
    pfn(&Data);
}

static inline D3DKMT_HANDLE
CreateTestDevice(D3DKMT_HANDLE hAdapter)
{
    PFN_D3DKMTCreateDevice pfn;
    D3DKMT_CREATEDEVICE Data;

    pfn = (PFN_D3DKMTCreateDevice)LoadD3DKMTProc("D3DKMTCreateDevice");
    if (!pfn)
        return 0;

    memset(&Data, 0, sizeof(Data));
    Data.hAdapter = hAdapter;

    if (!NT_SUCCESS(pfn(&Data)))
        return 0;

    return Data.hDevice;
}

static inline void
DestroyTestDevice(D3DKMT_HANDLE hDevice)
{
    PFN_D3DKMTDestroyDevice pfn;
    D3DKMT_DESTROYDEVICE Data;

    pfn = (PFN_D3DKMTDestroyDevice)LoadD3DKMTProc("D3DKMTDestroyDevice");
    if (!pfn || !hDevice)
        return;

    memset(&Data, 0, sizeof(Data));
    Data.hDevice = hDevice;
    pfn(&Data);
}

/*
 * Open a *render-capable* adapter and return its handle (0 if none).
 *
 * The primary display (\\.\DISPLAY1) is often a display-only adapter (e.g. the
 * Microsoft Basic Display Adapter) on which render objects (contexts, GPU-VA,
 * etc.) cannot be created. Windows always also exposes the WARP software render
 * adapter ("Microsoft Basic Render Driver"), which IS render-capable. This
 * enumerates adapters, finds one whose ADAPTERTYPE reports RenderSupported, and
 * re-opens it by LUID so render-path positive tests actually execute instead of
 * degrading to skip. The returned handle must be closed with CloseAdapter().
 * Out params (optional): *pLuid receives the adapter LUID; *pSoftware whether it
 * is the WARP software adapter.
 */
static inline D3DKMT_HANDLE
OpenRenderAdapterEx(LUID *pLuid, BOOL *pSoftware)
{
    PFN_D3DKMTEnumAdapters pfnEnum;
    PFN_D3DKMTQueryAdapterInfo pfnQAI;
    PFN_D3DKMTCloseAdapter pfnClose;
    PFN_D3DKMTOpenAdapterFromLuid pfnOpenLuid;
    D3DKMT_ENUMADAPTERS ea;
    D3DKMT_OPENADAPTERFROMLUID ol;
    LUID renderLuid;
    BOOL found = FALSE, software = FALSE;
    ULONG i;

    pfnEnum = (PFN_D3DKMTEnumAdapters)LoadD3DKMTProc("D3DKMTEnumAdapters");
    pfnQAI = (PFN_D3DKMTQueryAdapterInfo)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    pfnClose = (PFN_D3DKMTCloseAdapter)LoadD3DKMTProc("D3DKMTCloseAdapter");
    pfnOpenLuid = (PFN_D3DKMTOpenAdapterFromLuid)LoadD3DKMTProc("D3DKMTOpenAdapterFromLuid");
    if (!pfnEnum || !pfnQAI || !pfnClose || !pfnOpenLuid)
        return 0;

    memset(&ea, 0, sizeof(ea));
    if (!NT_SUCCESS(pfnEnum(&ea)))
        return 0;

    for (i = 0; i < ea.NumAdapters; i++)
    {
        D3DKMT_QUERYADAPTERINFO qai;
        D3DKMT_ADAPTERTYPE at;
        memset(&at, 0, sizeof(at));
        memset(&qai, 0, sizeof(qai));
        qai.hAdapter = ea.Adapters[i].hAdapter;
        qai.Type = KMTQAITYPE_ADAPTERTYPE;
        qai.pPrivateDriverData = &at;
        qai.PrivateDriverDataSize = sizeof(at);
        if (!found && NT_SUCCESS(pfnQAI(&qai)) && at.RenderSupported)
        {
            renderLuid = ea.Adapters[i].AdapterLuid;
            software = at.SoftwareDevice ? TRUE : FALSE;
            found = TRUE;
        }
    }

    /* EnumAdapters opened every adapter; close them all. */
    for (i = 0; i < ea.NumAdapters; i++)
    {
        D3DKMT_CLOSEADAPTER ca;
        memset(&ca, 0, sizeof(ca));
        ca.hAdapter = ea.Adapters[i].hAdapter;
        if (ca.hAdapter)
            pfnClose(&ca);
    }

    if (!found)
        return 0;

    memset(&ol, 0, sizeof(ol));
    ol.AdapterLuid = renderLuid;
    if (!NT_SUCCESS(pfnOpenLuid(&ol)) || !ol.hAdapter)
        return 0;

    if (pLuid) *pLuid = renderLuid;
    if (pSoftware) *pSoftware = software;
    return ol.hAdapter;
}

static inline D3DKMT_HANDLE
OpenRenderAdapter(void)
{
    return OpenRenderAdapterEx(NULL, NULL);
}

#endif /* _D3DKMT_APITEST_PRECOMP_H_ */
