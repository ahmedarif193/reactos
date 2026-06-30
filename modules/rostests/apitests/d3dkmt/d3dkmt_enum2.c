/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMTEnumAdapters2 / EnumAdapters3 enumeration tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * EnumAdapters2 (WDDM 1.2) and EnumAdapters3 (WDDM 2.7) are the modern
 * two-pass adapter enumeration entry points: call once with a NULL array to
 * learn the count, then again with a sized array. EnumAdapters3 adds a filter
 * (e.g. include compute-only or render-only adapters). Each enumerated entry
 * carries an opened adapter handle that the caller must close.
 *
 * Reference: Microsoft "D3DKMTEnumAdapters2", "D3DKMTEnumAdapters3".
 */

#include "precomp.h"

/* ---- NULL-argument contract ---- */
static void Test_EnumAdapters2_NullArg(void)
{
    LOADFN(PFND3DKMT_ENUMADAPTERS2, p, "D3DKMTEnumAdapters2");
    EXPECT_NULL_REJECTED(p, "D3DKMTEnumAdapters2");
}

static void Test_EnumAdapters3_NullArg(void)
{
    LOADFN(PFND3DKMT_ENUMADAPTERS3, p, "D3DKMTEnumAdapters3");
    EXPECT_NULL_REJECTED(p, "D3DKMTEnumAdapters3");
}

/* ---- Two-pass EnumAdapters2: count, enumerate, close ---- */
static void Test_EnumAdapters2_TwoPass(void)
{
    D3DKMT_ENUMADAPTERS2 ea;
    D3DKMT_ADAPTERINFO *pAdapters;
    NTSTATUS Status;
    ULONG Count, i;

    LOADFN(PFND3DKMT_ENUMADAPTERS2, p, "D3DKMTEnumAdapters2");
    LOAD_D3DKMT(D3DKMTCloseAdapter);

    /* Pass 1: NULL array -> learn the count. */
    memset(&ea, 0, sizeof(ea));
    ea.pAdapters = NULL;
    ea.NumAdapters = 0;
    Status = p(&ea);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL)
    {
        skip("EnumAdapters2 count query failed 0x%08lX\n", (long)Status);
        return;
    }

    Count = ea.NumAdapters;
    ok(Count >= 1, "Expected at least one adapter, got %lu\n", Count);
    if (Count == 0 || Count > MAX_ENUM_ADAPTERS)
        return;

    pAdapters = (D3DKMT_ADAPTERINFO *)LocalAlloc(LMEM_ZEROINIT,
                    Count * sizeof(D3DKMT_ADAPTERINFO));
    if (!pAdapters) { skip("Out of memory for adapter array\n"); return; }

    /* Pass 2: enumerate into the sized array. */
    memset(&ea, 0, sizeof(ea));
    ea.pAdapters = pAdapters;
    ea.NumAdapters = Count;
    Status = p(&ea);
    ok(NT_SUCCESS(Status), "EnumAdapters2 enumerate failed 0x%08lX\n", (long)Status);

    if (NT_SUCCESS(Status))
    {
        ok(ea.NumAdapters >= 1, "Enumerated %lu adapters\n", ea.NumAdapters);
        ok(pAdapters[0].hAdapter != 0, "First enumerated adapter handle is zero\n");

        /* EnumAdapters2 opens each adapter; close them all. */
        for (i = 0; i < ea.NumAdapters; i++)
        {
            if (pAdapters[i].hAdapter)
            {
                D3DKMT_CLOSEADAPTER ca;
                memset(&ca, 0, sizeof(ca));
                ca.hAdapter = pAdapters[i].hAdapter;
                pfnD3DKMTCloseAdapter(&ca);
            }
        }
    }

    LocalFree(pAdapters);
}

/* ---- EnumAdapters3 count query (WDDM 2.7; default filter) ---- */
static void Test_EnumAdapters3_Count(void)
{
    D3DKMT_ENUMADAPTERS3 ea;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_ENUMADAPTERS3, p, "D3DKMTEnumAdapters3");

    memset(&ea, 0, sizeof(ea));   /* zero filter = include all adapters */
    ea.pAdapters = NULL;
    ea.NumAdapters = 0;
    Status = p(&ea);

    /* Older down-level systems may not implement it; accept success or the
     * buffer-size signal, and trace the count. */
    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
        trace("EnumAdapters3 reports %lu adapters\n", ea.NumAdapters);
    else
        skip("EnumAdapters3 not available (0x%08lX)\n", (long)Status);
}

START_TEST(enum2)
{
    Test_EnumAdapters2_NullArg();
    Test_EnumAdapters3_NullArg();
    Test_EnumAdapters2_TwoPass();
    Test_EnumAdapters3_Count();
}
