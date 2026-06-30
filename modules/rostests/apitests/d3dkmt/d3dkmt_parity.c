/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     High-value Win11 parity cases (codex-audit driven)
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
    trace("render adapter LUID=%08lx:%08lx software(WARP)=%d\n",
          (unsigned long)luid.HighPart, (unsigned long)luid.LowPart, software);

    /* CreateDevice must work on a render-capable adapter. */
    hDevice = CreateTestDevice(hAdapter);
    ok(hDevice != 0, "CreateDevice on the render adapter should succeed\n");
    if (!hDevice) { CloseAdapter(hAdapter); return; }

    /* A graphics context needs a render node -- this is the op that fails on a
     * display-only adapter and is the whole point of targeting the render LUID.
     * Accept skip if this particular (QEMU) WARP build lacks a render node. */
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
            if (NT_SUCCESS(st) && cc.hContext)
            {
                D3DKMT_DESTROYCONTEXT dc;
                ok(TRUE, "CreateContext succeeded on the render adapter\n");
                memset(&dc, 0, sizeof(dc));
                dc.hContext = cc.hContext;
                pfnDC(&dc);
            }
            else
            {
                skip("CreateContext on render adapter returned 0x%08lX (no render node here)\n",
                     (long)st);
            }
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
        ok(ea.Adapters[i].AdapterLuid.LowPart != 0 || ea.Adapters[i].AdapterLuid.HighPart != 0,
           "Adapter[%lu] LUID is zero\n", i);
        for (j = i + 1; j < ea.NumAdapters; j++)
            ok(!LuidEqual(ea.Adapters[i].AdapterLuid, ea.Adapters[j].AdapterLuid),
               "Adapter[%lu] and [%lu] share a LUID\n", i, j);

        memset(&ol, 0, sizeof(ol));
        ol.AdapterLuid = ea.Adapters[i].AdapterLuid;
        st = pfnD3DKMTOpenAdapterFromLuid(&ol);
        ok(NT_SUCCESS(st) && ol.hAdapter != 0,
           "OpenAdapterFromLuid for enumerated LUID[%lu] failed 0x%08lX\n", i, (long)st);
        if (NT_SUCCESS(st) && ol.hAdapter)
        {
            D3DKMT_CLOSEADAPTER ca;
            memset(&ca, 0, sizeof(ca));
            ca.hAdapter = ol.hAdapter;
            pfnD3DKMTCloseAdapter(&ca);
        }
    }

    /* The primary display's LUID must be present in the enumerated set. */
    {
        PFN_D3DKMTOpenAdapterFromGdiDisplayName pfnG =
            (PFN_D3DKMTOpenAdapterFromGdiDisplayName)LoadD3DKMTProc("D3DKMTOpenAdapterFromGdiDisplayName");
        if (pfnG)
        {
            D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME g;
            memset(&g, 0, sizeof(g));
            wcscpy(g.DeviceName, L"\\\\.\\DISPLAY1");
            if (NT_SUCCESS(pfnG(&g)) && g.hAdapter)
            {
                BOOL inset = FALSE;
                for (i = 0; i < ea.NumAdapters; i++)
                    if (LuidEqual(ea.Adapters[i].AdapterLuid, g.AdapterLuid)) inset = TRUE;
                ok(inset, "DISPLAY1 LUID not found in the EnumAdapters set\n");
                CloseAdapter(g.hAdapter);
            }
        }
    }

    /* EnumAdapters2 LUID set must match EnumAdapters when below the legacy cap. */
    if (ea.NumAdapters < MAX_ENUM_ADAPTERS)
    {
        PFND3DKMT_ENUMADAPTERS2 pfn2 =
            (PFND3DKMT_ENUMADAPTERS2)LoadD3DKMTProc("D3DKMTEnumAdapters2");
        if (pfn2)
        {
            D3DKMT_ENUMADAPTERS2 e2;
            D3DKMT_ADAPTERINFO *arr;
            memset(&e2, 0, sizeof(e2));
            if ((NT_SUCCESS(pfn2(&e2)) || e2.NumAdapters > 0) && e2.NumAdapters > 0 &&
                e2.NumAdapters < 64)
            {
                arr = (D3DKMT_ADAPTERINFO *)LocalAlloc(LMEM_ZEROINIT,
                          e2.NumAdapters * sizeof(D3DKMT_ADAPTERINFO));
                if (arr)
                {
                    e2.pAdapters = arr;
                    if (NT_SUCCESS(pfn2(&e2)))
                    {
                        ok(e2.NumAdapters == ea.NumAdapters,
                           "EnumAdapters2 count %lu != EnumAdapters count %lu\n",
                           e2.NumAdapters, ea.NumAdapters);
                        /* Each EnumAdapters2 LUID must appear in the EnumAdapters set. */
                        for (i = 0; i < e2.NumAdapters; i++)
                        {
                            BOOL inset = FALSE;
                            for (j = 0; j < ea.NumAdapters; j++)
                                if (LuidEqual(arr[i].AdapterLuid, ea.Adapters[j].AdapterLuid)) inset = TRUE;
                            ok(inset, "EnumAdapters2 LUID[%lu] missing from EnumAdapters set\n", i);
                            if (arr[i].hAdapter)
                            {
                                D3DKMT_CLOSEADAPTER ca;
                                memset(&ca, 0, sizeof(ca));
                                ca.hAdapter = arr[i].hAdapter;
                                pfnD3DKMTCloseAdapter(&ca);
                            }
                        }
                    }
                    LocalFree(arr);
                }
            }
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
        ok(!NT_SUCCESS(st), "CreateDevice(hAdapter=hDevice) must be refused, got 0x%08lX\n", (long)st);
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
        ok(!NT_SUCCESS(st), "QueryAdapterInfo(hAdapter=hDevice) must be refused, got 0x%08lX\n", (long)st);
    }

    /* CreateContext with an adapter handle where a device handle is expected. */
    {
        D3DKMT_CREATECONTEXT cc;
        memset(&cc, 0, sizeof(cc));
        cc.hDevice = hAdapter;          /* wrong type */
        st = pfnD3DKMTCreateContext(&cc);
        ok(!NT_SUCCESS(st), "CreateContext(hDevice=hAdapter) must be refused, got 0x%08lX\n", (long)st);
    }

    /* CloseAdapter with a device handle (must not close the device). */
    {
        D3DKMT_CLOSEADAPTER ca;
        memset(&ca, 0, sizeof(ca));
        ca.hAdapter = hDevice;          /* wrong type */
        st = pfnD3DKMTCloseAdapter(&ca);
        ok(!NT_SUCCESS(st), "CloseAdapter(hDevice) must be refused, got 0x%08lX\n", (long)st);
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Cross-API stale-handle use (no object created between free and use) ---- */
START_TEST(stalehandle)
{
    NTSTATUS st;

    LOAD_D3DKMT(D3DKMTCloseAdapter);
    LOAD_D3DKMT(D3DKMTQueryAdapterInfo);
    LOAD_D3DKMT(D3DKMTCreateContext);

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
            pfnD3DKMTCloseAdapter(&ca);

            memset(&qai, 0, sizeof(qai)); memset(&at, 0, sizeof(at));
            qai.hAdapter = hAdapter;    /* stale */
            qai.Type = KMTQAITYPE_ADAPTERTYPE;
            qai.pPrivateDriverData = &at;
            qai.PrivateDriverDataSize = sizeof(at);
            st = pfnD3DKMTQueryAdapterInfo(&qai);
            ok(!NT_SUCCESS(st), "QueryAdapterInfo on a closed adapter must fail, got 0x%08lX\n", (long)st);
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
            DestroyTestDevice(hDevice);     /* free */
            memset(&cc, 0, sizeof(cc));
            cc.hDevice = hDevice;           /* stale */
            st = pfnD3DKMTCreateContext(&cc);
            ok(!NT_SUCCESS(st), "CreateContext on a destroyed device must fail, got 0x%08lX\n", (long)st);
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
        D3DKMT_DISPLAYMODE *modes = (D3DKMT_DISPLAYMODE *)LocalAlloc(LMEM_ZEROINIT,
                                        (n - 1) * sizeof(D3DKMT_DISPLAYMODE));
        if (modes)
        {
            memset(&ml, 0, sizeof(ml));
            ml.hAdapter = hAdapter;
            ml.VidPnSourceId = 0;
            ml.pModeList = modes;
            ml.ModeCount = n - 1;        /* deliberately one short */
            st = pfnD3DKMTGetDisplayModeList(&ml);
            ok(st == STATUS_BUFFER_TOO_SMALL || !NT_SUCCESS(st),
               "Undersized GetDisplayModeList must be refused, got 0x%08lX\n", (long)st);
            LocalFree(modes);
        }
    }
    else
    {
        skip("Only %u mode(s); undersized-buffer case needs >1\n", n);
    }

    CloseAdapter(hAdapter);
}
