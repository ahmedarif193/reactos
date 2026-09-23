/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Load the user-mode driver the way the D3D runtime does
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * OpenAdapter10_2 uses D3D10DDIARG_OPENADAPTER and the five-entry
 * D3D10_2DDI_ADAPTERFUNCS table. The Direct3D 9 D3DDDIARG_OPENADAPTER
 * layout belongs to OpenAdapter and cannot be passed to this entry point.
 * This test loads the selected renderer's DX11 UMD, exchanges the adapter
 * table, queries supported DDI versions, and closes the adapter.
 */

#include "precomp.h"
#include <d3dumddi.h>
#include <d3d10umddi.h>
#include <drivers/directx/umd_adapter.h>

static PFND3DKMT_QUERYADAPTERINFO pfnQueryAdapterInfo;

/* Step 2: the name dxgkrnl reports for this adapter's user-mode half. */
static BOOL UmdLoadQueryDriverName(D3DKMT_HANDLE hAdapter, KMTUMDVERSION Version,
                                   WCHAR *Name, SIZE_T NameChars)
{
    D3DKMT_QUERYADAPTERINFO qai;
    D3DKMT_UMDFILENAMEINFO Info;
    NTSTATUS Status;

    memset(&Info, 0, sizeof(Info));
    Info.Version = Version;
    memset(&qai, 0, sizeof(qai));
    qai.hAdapter = hAdapter;
    qai.Type = KMTQAITYPE_UMDRIVERNAME;
    qai.pPrivateDriverData = &Info;
    qai.PrivateDriverDataSize = sizeof(Info);

    Status = pfnQueryAdapterInfo(&qai);
    if (!NT_SUCCESS(Status))
    {
        trace("UMDRIVERNAME(version %u) refused 0x%08lX\n", (unsigned)Version, (long)Status);
        return FALSE;
    }
    if (Info.UmdFileName[0] == UNICODE_NULL)
        return FALSE;
    wcsncpy(Name, Info.UmdFileName, NameChars - 1);
    Name[NameChars - 1] = UNICODE_NULL;
    return TRUE;
}

static HRESULT APIENTRY
UmdLoadQueryAdapterInfo(HANDLE RuntimeAdapter, const D3DDDICB_QUERYADAPTERINFO *Info)
{
    D3DKMT_QUERYADAPTERINFO Query;
    NTSTATUS Status;

    if (Info == NULL)
        return E_INVALIDARG;
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = (D3DKMT_HANDLE)(ULONG_PTR)RuntimeAdapter;
    Query.Type = KMTQAITYPE_UMDRIVERPRIVATE;
    Query.pPrivateDriverData = Info->pPrivateDriverData;
    Query.PrivateDriverDataSize = Info->PrivateDriverDataSize;
    Status = pfnQueryAdapterInfo(&Query);
    return NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
}

static HRESULT APIENTRY
UmdLoadQueryAdapterInfo2(HANDLE RuntimeAdapter, const D3DDDICB_QUERYADAPTERINFO2 *Info)
{
    return RosUmdQueryAdapterInfo2(RuntimeAdapter, Info, pfnQueryAdapterInfo);
}

static void Test_LoadUserModeDriver(void)
{
    D3DKMT_HANDLE hAdapter;
    WCHAR Name[MAX_PATH];
    HMODULE Umd;
    PFND3D10DDI_OPENADAPTER pfnOpenAdapter;
    D3D10DDIARG_OPENADAPTER Open;
    D3D10_2DDI_ADAPTERFUNCS AdapterFuncs;
    ROS_UMD_ADAPTER_CALLBACKS AdapterCallbacks;
    UINT32 Count, Capacity, Index;
    UINT64 *Versions = NULL;
    HRESULT hr;
    BOOL Faulted = FALSE;
    DWORD LoadFlags = 0;

    hAdapter = OpenRenderAdapter();
    if (!hAdapter)
    {
        skip("No render-capable adapter\n");
        return;
    }
    if (!UmdLoadQueryDriverName(hAdapter, KMTUMDVERSION_DX11, Name, ARRAYSIZE(Name)))
    {
        skip("adapter reports no Direct3D 11 user-mode driver\n");
        CloseAdapter(hAdapter);
        return;
    }
    trace("adapter's user-mode driver: %S\n", Name);
    if ((Name[0] == L'\\' && Name[1] == L'\\') ||
        (Name[0] != 0 && Name[1] == L':' && Name[2] == L'\\'))
        LoadFlags = LOAD_WITH_ALTERED_SEARCH_PATH;
    Umd = LoadLibraryExW(Name, NULL, LoadFlags);
    ok(Umd != NULL, "LoadLibrary(%S) failed, error %lu\n", Name, GetLastError());
    if (Umd == NULL)
    {
        CloseAdapter(hAdapter);
        return;
    }

    pfnOpenAdapter = (PFND3D10DDI_OPENADAPTER)GetProcAddress(Umd, "OpenAdapter10_2");
    ok(pfnOpenAdapter != NULL, "GetProcAddress(%S, OpenAdapter10_2) failed, error %lu\n", Name, GetLastError());
    if (pfnOpenAdapter == NULL)
        goto unload;

    memset(&AdapterFuncs, 0, sizeof(AdapterFuncs));
    memset(&AdapterCallbacks, 0, sizeof(AdapterCallbacks));
    AdapterCallbacks.pfnQueryAdapterInfoCb = UmdLoadQueryAdapterInfo;
    AdapterCallbacks.pfnQueryAdapterInfoCb2 = UmdLoadQueryAdapterInfo2;
    memset(&Open, 0, sizeof(Open));
    Open.hRTAdapter.handle = (HANDLE)(ULONG_PTR)hAdapter;
    Open.pAdapterCallbacks = (const D3DDDI_ADAPTERCALLBACKS *)&AdapterCallbacks;
    Open.pAdapterFuncs_2 = &AdapterFuncs;
    /* OpenAdapter10_2 negotiates DDI versions through GetSupportedVersions;
     * no device or device-function table is exchanged by this probe. */
    hr = E_FAIL;
    _SEH2_TRY { hr = pfnOpenAdapter(&Open); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { Faulted = TRUE; }
    _SEH2_END;
    ok(!Faulted, "%S faulted inside OpenAdapter10_2\n", Name);
    if (Faulted)
        goto unload;
    ok(hr == S_OK, "OpenAdapter10_2 failed 0x%08lX\n", (long)hr);
    if (hr != S_OK)
        goto unload;

    ok(AdapterFuncs.pfnCalcPrivateDeviceSize != NULL, "no pfnCalcPrivateDeviceSize\n");
    ok(AdapterFuncs.pfnCreateDevice != NULL, "no pfnCreateDevice\n");
    ok(AdapterFuncs.pfnCloseAdapter != NULL, "no pfnCloseAdapter\n");
    ok(AdapterFuncs.pfnGetSupportedVersions != NULL, "no pfnGetSupportedVersions\n");
    ok(AdapterFuncs.pfnGetCaps != NULL, "no pfnGetCaps\n");
    if (AdapterFuncs.pfnGetSupportedVersions == NULL)
        goto close_adapter;

    Count = 0;
    hr = AdapterFuncs.pfnGetSupportedVersions(Open.hAdapter, &Count, NULL);
    ok(hr == S_OK, "GetSupportedVersions(count) failed 0x%08lX\n", (long)hr);
    if (hr != S_OK)
        goto close_adapter;
    ok(Count != 0 && Count <= MAXDWORD / sizeof(*Versions), "invalid DDI version count %u\n", Count);
    if (Count == 0 || Count > MAXDWORD / sizeof(*Versions))
        goto close_adapter;
    Capacity = Count;
    Versions = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)Capacity * sizeof(*Versions));
    ok(Versions != NULL, "allocating DDI version list failed\n");
    if (Versions == NULL)
        goto close_adapter;
    hr = AdapterFuncs.pfnGetSupportedVersions(Open.hAdapter, &Count, Versions);
    ok(hr == S_OK, "GetSupportedVersions(list) failed 0x%08lX\n", (long)hr);
    ok(Count != 0 && Count <= Capacity, "DDI version count %u exceeds capacity %u\n", Count, Capacity);
    if (hr == S_OK && Count <= Capacity)
    {
        for (Index = 0; Index < Count; ++Index)
            trace("supported DDI version[%u] = 0x%I64x\n", Index, Versions[Index]);
    }
    HeapFree(GetProcessHeap(), 0, Versions);

    if (AdapterFuncs.pfnGetCaps != NULL)
    {
        D3D10_2DDIARG_GETCAPS Query;
        D3D11DDI_3DPIPELINESUPPORT_CAPS Pipeline;

        memset(&Query, 0, sizeof(Query));
        memset(&Pipeline, 0, sizeof(Pipeline));
        Query.Type = D3D11DDICAPS_3DPIPELINESUPPORT;
        Query.pData = &Pipeline;
        Query.DataSize = sizeof(Pipeline);
        hr = E_FAIL;
        _SEH2_TRY { hr = AdapterFuncs.pfnGetCaps(Open.hAdapter, &Query); }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { Faulted = TRUE; }
        _SEH2_END;
        ok(!Faulted, "%S faulted inside GetCaps(3DPIPELINESUPPORT)\n", Name);
        if (!Faulted)
        {
            ok(hr == S_OK, "GetCaps(3DPIPELINESUPPORT) failed 0x%08lX\n", (long)hr);
            if (hr == S_OK)
            {
                trace("UMD 3D pipeline support mask = 0x%08X\n", Pipeline.Caps);
                ok(Pipeline.Caps != 0, "UMD reports no supported 3D pipeline\n");
            }
        }
    }

close_adapter:
    if (AdapterFuncs.pfnCloseAdapter != NULL)
    {
        hr = AdapterFuncs.pfnCloseAdapter(Open.hAdapter);
        ok(hr == S_OK, "CloseAdapter failed 0x%08lX\n", (long)hr);
    }
unload:
    FreeLibrary(Umd);
    CloseAdapter(hAdapter);
}

/* ------------------------------------------------------------------ *
 * The driver is reported for every runtime generation, because the
 * value is a REG_MULTI_SZ indexed by KMTUMDVERSION and a runtime asks
 * for its own slot.  A driver present only at DX9 is invisible to D3D11.
 * ------------------------------------------------------------------ */
static void Test_DriverNamedForEveryRuntime(void)
{
    D3DKMT_HANDLE hAdapter;
    WCHAR Name[MAX_PATH];
    UINT Version;
    UINT Found = 0;

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    for (Version = 0; Version < NUM_KMTUMDVERSIONS; ++Version)
    {
        if (UmdLoadQueryDriverName(hAdapter, (KMTUMDVERSION)Version, Name, ARRAYSIZE(Name)))
        {
            trace("  version %u -> %S\n", Version, Name);
            Found++;
        }
    }
    ok(Found != 0, "no runtime generation has a user-mode driver\n");
    trace("user-mode driver named for %u of %u runtime generations\n",
          Found, (unsigned)NUM_KMTUMDVERSIONS);
    CloseAdapter(hAdapter);
}

START_TEST(umdload)
{
    pfnQueryAdapterInfo = (PFND3DKMT_QUERYADAPTERINFO)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    if (pfnQueryAdapterInfo == NULL)
    {
        skip("D3DKMTQueryAdapterInfo not exported\n");
        return;
    }
    Test_LoadUserModeDriver();
    Test_DriverNamedForEveryRuntime();
}

/* EOF */
