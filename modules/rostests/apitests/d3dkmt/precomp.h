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

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
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

#endif /* _D3DKMT_APITEST_PRECOMP_H_ */
