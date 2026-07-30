/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     High-value Windows 11 parity cases
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Deterministic Win11-ARM64 parity gaps the rest of the suite missed:
 *   - render-adapter (WARP) targeting so render positives actually run
 *   - LUID / enumeration identity round-trips
 *   - cross-type handle confusion (valid handle of the wrong object type)
 *   - cross-API stale-handle use
 *   - GetDisplayModeList undersized-buffer semantics
 */

#include "precomp.h"

static BOOL LuidEqual(LUID a, LUID b)
{
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

static LONG FindLuid(const D3DKMT_ENUMADAPTERS *pEnum, LUID Luid)
{
    ULONG i;

    for (i = 0; i < pEnum->NumAdapters; i++)
        if (LuidEqual(pEnum->Adapters[i].AdapterLuid, Luid)) return (LONG)i;
    return -1;
}

/* ---- Render-adapter (WARP) targeting: render positives must run, not skip ---- */
START_TEST(renderadapter)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    LUID luid;
    BOOL software = FALSE;

    hAdapter = OpenRenderAdapterEx(&luid, &software);
    if (!hAdapter)
    {
        skip("No render-capable adapter enumerated (no WARP?)\n");
        return;
    }
    trace("render adapter LUID=%08lx:%08lx software(WARP)=%d\n", (unsigned long)luid.HighPart, (unsigned long)luid.LowPart, software);

    /* CreateDevice must work on a render-capable adapter. */
    hDevice = CreateTestDevice(hAdapter);
    ok(hDevice != 0, "CreateDevice on the render adapter should succeed\n");
    if (!hDevice) { CloseAdapter(hAdapter); return; }

    /* A graphics context needs a render node -- this is the operation that
     * distinguishes the selected render adapter from a display-only adapter. */
    {
        PFN_D3DKMTCreateContext pfnCC = (PFN_D3DKMTCreateContext)LoadD3DKMTProc("D3DKMTCreateContext");
        PFN_D3DKMTDestroyContext pfnDC = (PFN_D3DKMTDestroyContext)LoadD3DKMTProc("D3DKMTDestroyContext");
        if (pfnCC && pfnDC)
        {
            D3DKMT_CREATECONTEXT cc;
            NTSTATUS st;
            memset(&cc, 0, sizeof(cc));
            cc.hDevice = hDevice;
            cc.NodeOrdinal = 0;
            cc.EngineAffinity = 1;
            st = pfnCC(&cc);
            ok(NT_SUCCESS(st) && cc.hContext != 0, "CreateContext on the render adapter failed 0x%08lX\n", (long)st);
            if (NT_SUCCESS(st) && cc.hContext)
            {
                D3DKMT_DESTROYCONTEXT dc;
                memset(&dc, 0, sizeof(dc));
                dc.hContext = cc.hContext;
                st = pfnDC(&dc);
                ok_succeeded(st, "DestroyContext on the render adapter failed 0x%08lX\n", (long)st);
            }
        }
        else
        {
            skip("CreateContext or DestroyContext is not exported\n");
        }
    }

    /* WaitForIdle on the render device (informational). */
    {
        PFN_D3DKMTWaitForIdle pfnWI = (PFN_D3DKMTWaitForIdle)LoadD3DKMTProc("D3DKMTWaitForIdle");
        if (pfnWI)
        {
            D3DKMT_WAITFORIDLE wi;
            NTSTATUS st;
            memset(&wi, 0, sizeof(wi));
            wi.hDevice = hDevice;
            st = pfnWI(&wi);
            trace("WaitForIdle on render device -> 0x%08lX\n", (long)st);
        }
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- LUID / enumeration identity round-trips ---- */
START_TEST(luidident)
{
    D3DKMT_ENUMADAPTERS ea;
    NTSTATUS st;
    ULONG i, j;

    LOAD_D3DKMT(D3DKMTEnumAdapters);
    LOAD_D3DKMT(D3DKMTOpenAdapterFromLuid);
    LOAD_D3DKMT(D3DKMTCloseAdapter);

    memset(&ea, 0, sizeof(ea));
    st = pfnD3DKMTEnumAdapters(&ea);
    if (!NT_SUCCESS(st) || ea.NumAdapters == 0)
    {
        skip("EnumAdapters returned nothing (0x%08lX)\n", (long)st);
        return;
    }

    /* Every enumerated LUID is non-zero, pairwise unique, and re-openable. */
    for (i = 0; i < ea.NumAdapters; i++)
    {
        D3DKMT_OPENADAPTERFROMLUID ol;
        ok(ea.Adapters[i].AdapterLuid.LowPart != 0 || ea.Adapters[i].AdapterLuid.HighPart != 0, "Adapter[%lu] LUID is zero\n", i);
        for (j = i + 1; j < ea.NumAdapters; j++)
            ok(!LuidEqual(ea.Adapters[i].AdapterLuid, ea.Adapters[j].AdapterLuid), "Adapter[%lu] and [%lu] share a LUID\n", i, j);

        memset(&ol, 0, sizeof(ol));
        ol.AdapterLuid = ea.Adapters[i].AdapterLuid;
        st = pfnD3DKMTOpenAdapterFromLuid(&ol);
        ok(NT_SUCCESS(st) && ol.hAdapter != 0, "OpenAdapterFromLuid for enumerated LUID[%lu] failed 0x%08lX\n", i, (long)st);
        if (NT_SUCCESS(st) && ol.hAdapter)
        {
            D3DKMT_CLOSEADAPTER ca;
            memset(&ca, 0, sizeof(ca));
            ca.hAdapter = ol.hAdapter;
            st = pfnD3DKMTCloseAdapter(&ca);
            ok_succeeded(st, "CloseAdapter for reopened LUID[%lu] failed 0x%08lX\n", i, (long)st);
        }
    }

    /* GDI-name and per-display-HDC opens must identify the same enumerated source. */
    {
        PFN_D3DKMTOpenAdapterFromGdiDisplayName pfnG = (PFN_D3DKMTOpenAdapterFromGdiDisplayName)LoadD3DKMTProc("D3DKMTOpenAdapterFromGdiDisplayName");
        PFN_D3DKMTOpenAdapterFromHdc pfnH = (PFN_D3DKMTOpenAdapterFromHdc)LoadD3DKMTProc("D3DKMTOpenAdapterFromHdc");
        if (pfnG)
        {
            D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME g;
            LONG EnumIndex;
            memset(&g, 0, sizeof(g));
            wcscpy(g.DeviceName, L"\\\\.\\DISPLAY1");
            st = pfnG(&g);
            ok(NT_SUCCESS(st) && g.hAdapter != 0, "OpenAdapterFromGdiDisplayName(DISPLAY1) failed 0x%08lX\n", (long)st);
            if (NT_SUCCESS(st) && g.hAdapter)
            {
                EnumIndex = FindLuid(&ea, g.AdapterLuid);
                ok(EnumIndex >= 0, "DISPLAY1 LUID not found in the EnumAdapters set\n");
                if (EnumIndex >= 0)
                    ok(g.VidPnSourceId < ea.Adapters[EnumIndex].NumOfSources, "DISPLAY1 source %u is outside adapter source count %lu\n", g.VidPnSourceId, ea.Adapters[EnumIndex].NumOfSources);

                if (pfnH)
                {
                    D3DKMT_OPENADAPTERFROMHDC h;
                    HDC hdc = CreateDCW(L"DISPLAY", L"\\\\.\\DISPLAY1", NULL, NULL);
                    ok(hdc != NULL, "CreateDCW for DISPLAY1 failed, error %lu\n", GetLastError());
                    if (hdc)
                    {
                        memset(&h, 0, sizeof(h));
                        h.hDc = hdc;
                        st = pfnH(&h);
                        ok(NT_SUCCESS(st) && h.hAdapter != 0, "OpenAdapterFromHdc(DISPLAY1) failed 0x%08lX\n", (long)st);
                        if (NT_SUCCESS(st) && h.hAdapter)
                        {
                            ok(LuidEqual(h.AdapterLuid, g.AdapterLuid), "HDC and GDI display-name opens returned different LUIDs\n");
                            ok(h.VidPnSourceId == g.VidPnSourceId, "HDC source %u differs from GDI display-name source %u\n", h.VidPnSourceId, g.VidPnSourceId);
                            EnumIndex = FindLuid(&ea, h.AdapterLuid);
                            ok(EnumIndex >= 0, "DISPLAY1 HDC LUID not found in the EnumAdapters set\n");
                            if (EnumIndex >= 0)
                                ok(h.VidPnSourceId < ea.Adapters[EnumIndex].NumOfSources, "DISPLAY1 HDC source %u is outside adapter source count %lu\n", h.VidPnSourceId, ea.Adapters[EnumIndex].NumOfSources);
                            CloseAdapter(h.hAdapter);
                        }
                        DeleteDC(hdc);
                    }
                }
                else
                {
                    skip("D3DKMTOpenAdapterFromHdc not exported\n");
                }
                CloseAdapter(g.hAdapter);
            }
        }
        else
        {
            skip("D3DKMTOpenAdapterFromGdiDisplayName not exported\n");
        }
    }

    /* EnumAdapters2 and legacy enumeration sets match when EnumAdapters2 is not at the legacy cap. */
    {
        PFND3DKMT_ENUMADAPTERS2 pfn2 = (PFND3DKMT_ENUMADAPTERS2)LoadD3DKMTProc("D3DKMTEnumAdapters2");
        if (pfn2)
        {
            D3DKMT_ENUMADAPTERS2 e2;
            D3DKMT_ADAPTERINFO *arr;
            ULONG Capacity;
            memset(&e2, 0, sizeof(e2));
            st = pfn2(&e2);
            if ((NT_SUCCESS(st) || st == STATUS_BUFFER_TOO_SMALL) && e2.NumAdapters > 0 && e2.NumAdapters <= 1024)
            {
                Capacity = e2.NumAdapters;
                arr = (D3DKMT_ADAPTERINFO *)LocalAlloc(LMEM_ZEROINIT, Capacity * sizeof(D3DKMT_ADAPTERINFO));
                if (arr)
                {
                    memset(&e2, 0, sizeof(e2));
                    e2.NumAdapters = Capacity;
                    e2.pAdapters = arr;
                    st = pfn2(&e2);
                    ok_succeeded(st, "EnumAdapters2 enumeration failed 0x%08lX\n", (long)st);
                    if (NT_SUCCESS(st))
                    {
                        ok(e2.NumAdapters <= Capacity, "EnumAdapters2 returned %lu adapters into a %lu-entry array\n", e2.NumAdapters, Capacity);
                        if (e2.NumAdapters <= Capacity && e2.NumAdapters < MAX_ENUM_ADAPTERS)
                        {
                            ok(e2.NumAdapters == ea.NumAdapters, "EnumAdapters2 count %lu != EnumAdapters count %lu\n", e2.NumAdapters, ea.NumAdapters);
                            for (i = 0; i < e2.NumAdapters; i++)
                                ok(FindLuid(&ea, arr[i].AdapterLuid) >= 0, "EnumAdapters2 LUID[%lu] missing from EnumAdapters set\n", i);
                            for (i = 0; i < ea.NumAdapters; i++)
                            {
                                BOOL InEnum2 = FALSE;
                                for (j = 0; j < e2.NumAdapters; j++)
                                    if (LuidEqual(ea.Adapters[i].AdapterLuid, arr[j].AdapterLuid)) InEnum2 = TRUE;
                                ok(InEnum2, "EnumAdapters LUID[%lu] missing from EnumAdapters2 set\n", i);
                            }
                        }

                        if (e2.NumAdapters > Capacity) e2.NumAdapters = Capacity;
                        for (i = 0; i < e2.NumAdapters; i++)
                        {
                            if (arr[i].hAdapter)
                            {
                                D3DKMT_CLOSEADAPTER ca;
                                memset(&ca, 0, sizeof(ca));
                                ca.hAdapter = arr[i].hAdapter;
                                st = pfnD3DKMTCloseAdapter(&ca);
                                ok_succeeded(st, "CloseAdapter for EnumAdapters2 handle[%lu] failed 0x%08lX\n", i, (long)st);
                            }
                        }
                    }
                    LocalFree(arr);
                }
            }
            else
            {
                skip("EnumAdapters2 count query unavailable (0x%08lX, count %lu)\n", (long)st, e2.NumAdapters);
            }
        }
        else
        {
            skip("D3DKMTEnumAdapters2 not exported\n");
        }
    }

    /* Close the EnumAdapters handles. */
    for (i = 0; i < ea.NumAdapters; i++)
    {
        D3DKMT_CLOSEADAPTER ca;
        memset(&ca, 0, sizeof(ca));
        ca.hAdapter = ea.Adapters[i].hAdapter;
        if (ca.hAdapter) pfnD3DKMTCloseAdapter(&ca);
    }
}

/* ---- Cross-type handle confusion: a valid handle of the WRONG type ---- */
START_TEST(handletype)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS st;

    LOAD_D3DKMT(D3DKMTCreateDevice);
    LOAD_D3DKMT(D3DKMTQueryAdapterInfo);
    LOAD_D3DKMT(D3DKMTCloseAdapter);
    LOAD_D3DKMT(D3DKMTCreateContext);
    LOAD_D3DKMT(D3DKMTDestroyDevice);
    LOAD_D3DKMT(D3DKMTDestroyContext);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    /* CreateDevice with a device handle where an adapter handle is expected. */
    {
        D3DKMT_CREATEDEVICE cd;
        memset(&cd, 0, sizeof(cd));
        cd.hAdapter = hDevice;          /* wrong type: a device, not an adapter */
        st = pfnD3DKMTCreateDevice(&cd);
        ok(st == STATUS_INVALID_PARAMETER, "CreateDevice(hAdapter=hDevice) returned 0x%08lX, expected STATUS_INVALID_PARAMETER\n", (long)st);
    }

    /* QueryAdapterInfo with a device handle. */
    {
        D3DKMT_QUERYADAPTERINFO qai;
        D3DKMT_ADAPTERTYPE at;
        memset(&qai, 0, sizeof(qai)); memset(&at, 0, sizeof(at));
        qai.hAdapter = hDevice;         /* wrong type */
        qai.Type = KMTQAITYPE_ADAPTERTYPE;
        qai.pPrivateDriverData = &at;
        qai.PrivateDriverDataSize = sizeof(at);
        st = pfnD3DKMTQueryAdapterInfo(&qai);
        ok(st == STATUS_INVALID_PARAMETER, "QueryAdapterInfo(hAdapter=hDevice) returned 0x%08lX, expected STATUS_INVALID_PARAMETER\n", (long)st);
    }

    /* CreateContext with an adapter handle where a device handle is expected. */
    {
        D3DKMT_CREATECONTEXT cc;
        memset(&cc, 0, sizeof(cc));
        cc.hDevice = hAdapter;          /* wrong type */
        cc.EngineAffinity = 1;
        st = pfnD3DKMTCreateContext(&cc);
        ok(st == STATUS_INVALID_PARAMETER, "CreateContext(hDevice=hAdapter) returned 0x%08lX, expected STATUS_INVALID_PARAMETER\n", (long)st);
    }

    /* CloseAdapter with a device handle (must not close the device). */
    {
        D3DKMT_CLOSEADAPTER ca;
        memset(&ca, 0, sizeof(ca));
        ca.hAdapter = hDevice;          /* wrong type */
        st = pfnD3DKMTCloseAdapter(&ca);
        ok(st == STATUS_INVALID_PARAMETER, "CloseAdapter(hDevice) returned 0x%08lX, expected STATUS_INVALID_PARAMETER\n", (long)st);
    }

    /* DestroyDevice with an adapter handle. */
    {
        D3DKMT_DESTROYDEVICE dd;
        memset(&dd, 0, sizeof(dd));
        dd.hDevice = hAdapter;          /* wrong type */
        st = pfnD3DKMTDestroyDevice(&dd);
        ok(st == STATUS_INVALID_PARAMETER, "DestroyDevice(hAdapter) returned 0x%08lX, expected STATUS_INVALID_PARAMETER\n", (long)st);
    }

    /* DestroyContext with a device handle. */
    {
        D3DKMT_DESTROYCONTEXT dc;
        memset(&dc, 0, sizeof(dc));
        dc.hContext = hDevice;          /* wrong type */
        st = pfnD3DKMTDestroyContext(&dc);
        ok(st == STATUS_INVALID_PARAMETER, "DestroyContext(hDevice) returned 0x%08lX, expected STATUS_INVALID_PARAMETER\n", (long)st);
    }

    {
        D3DKMT_DESTROYDEVICE dd;
        D3DKMT_CLOSEADAPTER ca;
        memset(&dd, 0, sizeof(dd));
        dd.hDevice = hDevice;
        st = pfnD3DKMTDestroyDevice(&dd);
        ok_succeeded(st, "DestroyDevice after wrong-type probes failed 0x%08lX\n", (long)st);
        memset(&ca, 0, sizeof(ca));
        ca.hAdapter = hAdapter;
        st = pfnD3DKMTCloseAdapter(&ca);
        ok_succeeded(st, "CloseAdapter after wrong-type probes failed 0x%08lX\n", (long)st);
    }
}

/* ---- Cross-API stale-handle use (no object created between free and use) ---- */
START_TEST(stalehandle)
{
    NTSTATUS st;

    LOAD_D3DKMT(D3DKMTCloseAdapter);
    LOAD_D3DKMT(D3DKMTQueryAdapterInfo);
    LOAD_D3DKMT(D3DKMTCreateContext);
    LOAD_D3DKMT(D3DKMTDestroyDevice);
    LOAD_D3DKMT(D3DKMTDestroyContext);

    /* Close an adapter, then query the closed handle via a different API. */
    {
        D3DKMT_HANDLE hAdapter = OpenAdapterFromDisplay1();
        if (hAdapter)
        {
            D3DKMT_CLOSEADAPTER ca;
            D3DKMT_QUERYADAPTERINFO qai;
            D3DKMT_ADAPTERTYPE at;
            memset(&ca, 0, sizeof(ca));
            ca.hAdapter = hAdapter;
            st = pfnD3DKMTCloseAdapter(&ca);
            ok_succeeded(st, "CloseAdapter for stale-handle probe failed 0x%08lX\n", (long)st);
            if (NT_SUCCESS(st))
            {
                memset(&qai, 0, sizeof(qai));
                memset(&at, 0, sizeof(at));
                qai.hAdapter = hAdapter;    /* stale */
                qai.Type = KMTQAITYPE_ADAPTERTYPE;
                qai.pPrivateDriverData = &at;
                qai.PrivateDriverDataSize = sizeof(at);
                st = pfnD3DKMTQueryAdapterInfo(&qai);
                ok(st == STATUS_INVALID_PARAMETER, "QueryAdapterInfo on a closed adapter returned 0x%08lX, expected STATUS_INVALID_PARAMETER\n", (long)st);
            }
        }
        else skip("No adapter for stale-adapter test\n");
    }

    /* Destroy a device, then create a context on the destroyed handle. */
    {
        D3DKMT_HANDLE hAdapter = OpenAdapterFromDisplay1();
        D3DKMT_HANDLE hDevice = hAdapter ? CreateTestDevice(hAdapter) : 0;
        if (hDevice)
        {
            D3DKMT_CREATECONTEXT cc;
            D3DKMT_DESTROYDEVICE dd;
            memset(&dd, 0, sizeof(dd));
            dd.hDevice = hDevice;
            st = pfnD3DKMTDestroyDevice(&dd);
            ok_succeeded(st, "DestroyDevice for stale-handle probe failed 0x%08lX\n", (long)st);
            if (NT_SUCCESS(st))
            {
                memset(&cc, 0, sizeof(cc));
                cc.hDevice = hDevice;           /* stale */
                cc.EngineAffinity = 1;
                st = pfnD3DKMTCreateContext(&cc);
                ok(st == STATUS_INVALID_PARAMETER, "CreateContext on a destroyed device returned 0x%08lX, expected STATUS_INVALID_PARAMETER\n", (long)st);
                if (NT_SUCCESS(st) && cc.hContext)
                {
                    D3DKMT_DESTROYCONTEXT dc;
                    memset(&dc, 0, sizeof(dc));
                    dc.hContext = cc.hContext;
                    pfnD3DKMTDestroyContext(&dc);
                }
            }
        }
        else skip("No device for stale-device test\n");
        if (hAdapter) CloseAdapter(hAdapter);
    }
}

/* ---- GetDisplayModeList undersized-buffer semantics ---- */
START_TEST(modelistsize)
{
    D3DKMT_GETDISPLAYMODELIST ml;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS st;
    UINT n;

    LOAD_D3DKMT(D3DKMTGetDisplayModeList);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }

    memset(&ml, 0, sizeof(ml));
    ml.hAdapter = hAdapter;
    ml.VidPnSourceId = 0;
    st = pfnD3DKMTGetDisplayModeList(&ml);
    if (st != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(st))
    {
        skip("GetDisplayModeList count query failed 0x%08lX\n", (long)st);
        CloseAdapter(hAdapter);
        return;
    }
    n = ml.ModeCount;
    trace("display mode count = %u\n", n);

    if (n > 1)
    {
        D3DKMT_DISPLAYMODE *modes = (D3DKMT_DISPLAYMODE *)LocalAlloc(LMEM_ZEROINIT, (n - 1) * sizeof(D3DKMT_DISPLAYMODE));
        if (modes)
        {
            memset(&ml, 0, sizeof(ml));
            ml.hAdapter = hAdapter;
            ml.VidPnSourceId = 0;
            ml.pModeList = modes;
            ml.ModeCount = n - 1;        /* deliberately one short */
            st = pfnD3DKMTGetDisplayModeList(&ml);
            ok(st == STATUS_BUFFER_TOO_SMALL, "Undersized GetDisplayModeList returned 0x%08lX, expected STATUS_BUFFER_TOO_SMALL\n", (long)st);
            ok(ml.ModeCount == n, "Undersized GetDisplayModeList returned mode count %u, expected %u\n", ml.ModeCount, n);
            LocalFree(modes);
        }
    }
    else
    {
        skip("Only %u mode(s); undersized-buffer case needs >1\n", n);
    }

    CloseAdapter(hAdapter);
}
