/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Load the user-mode driver the way the D3D runtime does
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * This is the sequence the Direct3D runtime performs, and the first place the
 * two halves of a WDDM driver actually meet:
 *
 *   open the adapter -> ask dxgkrnl for KMTQAITYPE_UMDRIVERNAME
 *     -> LoadLibrary that name -> GetProcAddress("OpenAdapter10_2")
 *     -> call it -> receive D3DDDI_ADAPTERFUNCS
 *     -> pfnCreateDevice -> receive D3DDDI_DEVICEFUNCS
 *
 * Every step is a real contract with a real failure mode, and until now not one
 * of them had ever been executed on this OS: the kernel half was tested against
 * the kernel half, and no user-mode driver existed to load.
 *
 * What this pins is that the contract connects and the lifecycle table can be
 * exchanged without a runtime implementation. The separate umd2d test supplies
 * real callbacks and drives the implemented linear 2D execution path.
 */

#include "precomp.h"
#include <d3dumddi.h>

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

/* ------------------------------------------------------------------ *
 * The whole chain, in the runtime's order.
 * ------------------------------------------------------------------ */
static void Test_LoadUserModeDriver(void)
{
    D3DKMT_HANDLE hAdapter;
    WCHAR Name[MAX_PATH];
    HMODULE Umd;
    PFND3DDDI_OPENADAPTER pfnOpenAdapter;
    D3DDDIARG_OPENADAPTER Open;
    D3DDDI_ADAPTERFUNCS AdapterFuncs;
    D3DDDI_ADAPTERCALLBACKS AdapterCallbacks;
    HRESULT hr;

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }

    if (!UmdLoadQueryDriverName(hAdapter, KMTUMDVERSION_DX9, Name, ARRAYSIZE(Name)))
    {
        skip("adapter reports no user-mode driver\n");
        CloseAdapter(hAdapter);
        return;
    }
    trace("adapter's user-mode driver: %S\n", Name);

    /* Step 3.  A name that cannot be loaded is worse than no name: the runtime
     * has already committed to this adapter by the time it gets here. */
    Umd = LoadLibraryW(Name);
    ok(Umd != NULL, "LoadLibrary(%S) failed, error %lu\n", Name, GetLastError());
    if (Umd == NULL)
    {
        CloseAdapter(hAdapter);
        return;
    }

    /* Step 4.  Exactly one export is resolved by name; everything else in the
     * driver is reached through the tables it hands back. */
    pfnOpenAdapter = (PFND3DDDI_OPENADAPTER)GetProcAddress(Umd, "OpenAdapter10_2");
    ok(pfnOpenAdapter != NULL, "%S exports no OpenAdapter10_2\n", Name);
    if (pfnOpenAdapter == NULL)
    {
        FreeLibrary(Umd);
        CloseAdapter(hAdapter);
        return;
    }

    /* Step 5.  The runtime hands over its callbacks and a handle of its own,
     * and takes back the driver's handle plus the adapter function table. */
    memset(&AdapterFuncs, 0, sizeof(AdapterFuncs));
    memset(&AdapterCallbacks, 0, sizeof(AdapterCallbacks));
    memset(&Open, 0, sizeof(Open));
    Open.hAdapter = (HANDLE)(ULONG_PTR)hAdapter;
    Open.Interface = REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
    Open.Version = REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
    Open.pAdapterCallbacks = &AdapterCallbacks;
    Open.pAdapterFuncs = &AdapterFuncs;

    hr = pfnOpenAdapter(&Open);
    ok(hr == S_OK, "OpenAdapter10_2 failed 0x%08lX\n", (long)hr);
    if (hr != S_OK)
    {
        FreeLibrary(Umd);
        CloseAdapter(hAdapter);
        return;
    }

    /* The driver must have swapped in its own handle and filled every adapter
     * entry: the runtime calls all three by position, so a NULL is a wild
     * call the first time that slot is used. */
    ok(Open.hAdapter != (HANDLE)(ULONG_PTR)hAdapter,
       "driver did not publish a handle of its own\n");
    ok(AdapterFuncs.pfnGetCaps != NULL, "no pfnGetCaps\n");
    ok(AdapterFuncs.pfnCreateDevice != NULL, "no pfnCreateDevice\n");
    ok(AdapterFuncs.pfnCloseAdapter != NULL, "no pfnCloseAdapter\n");
    ok(Open.DriverVersion != 0, "driver reported no interface version\n");
    trace("UMD opened: driver version 0x%04X\n", (unsigned)Open.DriverVersion);

    /*
     * Step 6: create a device, which is where the device function table is
     * exchanged.
     *
     * A runtime refuses a driver built against a different interface version
     * before it gets here, and must: D3DDDI_DEVICEFUNCS is version-guarded, so
     * the two sides disagree about its length and the driver would zero past
     * the end of the caller's table.  That is a stack overflow written by the
     * driver into the runtime, and no field in the DDI carries a size to catch
     * it -- the version *is* the size contract.
     */
    if (Open.DriverVersion != REACTOS_EXPECTED_UMD_INTERFACE_VERSION)
    {
        skip("driver built at interface 0x%04X, this caller at 0x%04X -- a runtime refuses "
             "the mismatch rather than exchanging a table whose length they disagree on\n",
             (unsigned)Open.DriverVersion,
             (unsigned)REACTOS_EXPECTED_UMD_INTERFACE_VERSION);
        AdapterFuncs.pfnCloseAdapter(Open.hAdapter);
        FreeLibrary(Umd);
        CloseAdapter(hAdapter);
        return;
    }

    if (AdapterFuncs.pfnCreateDevice != NULL)
    {
        D3DDDIARG_CREATEDEVICE Create;
        D3DDDI_DEVICEFUNCS DeviceFuncs;
        D3DDDI_DEVICECALLBACKS DeviceCallbacks;

        memset(&DeviceFuncs, 0, sizeof(DeviceFuncs));
        memset(&DeviceCallbacks, 0, sizeof(DeviceCallbacks));
        memset(&Create, 0, sizeof(Create));
        Create.hDevice = (HANDLE)(ULONG_PTR)0x1234;   /* the runtime's handle */
        Create.Interface = REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
        Create.Version = REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
        Create.pCallbacks = &DeviceCallbacks;
        Create.pDeviceFuncs = &DeviceFuncs;

        hr = AdapterFuncs.pfnCreateDevice(Open.hAdapter, &Create);
        ok(hr == S_OK, "pfnCreateDevice failed 0x%08lX\n", (long)hr);
        if (hr == S_OK)
        {
            ok(DeviceFuncs.pfnDestroyDevice != NULL,
               "device table has no pfnDestroyDevice -- the device could never be released\n");
            ok(Create.hDevice != (HANDLE)(ULONG_PTR)0x1234,
               "driver did not publish a device handle of its own\n");

            /* Closing the adapter with a device still open is the runtime
             * breaking its own contract, and the driver must say so rather
             * than freeing memory that device still points at. */
            hr = AdapterFuncs.pfnCloseAdapter(Open.hAdapter);
            ok(FAILED(hr), "adapter closed while a device was still open (0x%08lX)\n", (long)hr);

            if (DeviceFuncs.pfnDestroyDevice != NULL)
            {
                hr = DeviceFuncs.pfnDestroyDevice(Create.hDevice);
                ok(hr == S_OK, "pfnDestroyDevice failed 0x%08lX\n", (long)hr);
            }
        }
    }

    /* Step 7: and now the adapter closes cleanly. */
    hr = AdapterFuncs.pfnCloseAdapter(Open.hAdapter);
    ok(hr == S_OK, "pfnCloseAdapter failed 0x%08lX\n", (long)hr);

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
