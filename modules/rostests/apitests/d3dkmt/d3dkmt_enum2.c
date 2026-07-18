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
    ULONG Count, i, j;

    LOADFN(PFND3DKMT_ENUMADAPTERS2, p, "D3DKMTEnumAdapters2");
    LOAD_D3DKMT(D3DKMTCloseAdapter);
    LOAD_D3DKMT(D3DKMTOpenAdapterFromLuid);

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
    if (Count == 0 || Count > 1024)
        return;

    pAdapters = (D3DKMT_ADAPTERINFO *)LocalAlloc(LMEM_ZEROINIT, Count * sizeof(D3DKMT_ADAPTERINFO));
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
        ok(ea.NumAdapters <= Count, "EnumAdapters2 returned %lu adapters into a %lu-entry array\n", ea.NumAdapters, Count);
        if (ea.NumAdapters > Count) ea.NumAdapters = Count;

        /* Every returned LUID is nonzero, unique, and independently reopenable. */
        for (i = 0; i < ea.NumAdapters; i++)
        {
            D3DKMT_OPENADAPTERFROMLUID OpenData;
            ok(pAdapters[i].hAdapter != 0, "EnumAdapters2 adapter[%lu] handle is zero\n", i);
            ok(pAdapters[i].AdapterLuid.LowPart != 0 || pAdapters[i].AdapterLuid.HighPart != 0, "EnumAdapters2 adapter[%lu] LUID is zero\n", i);
            for (j = i + 1; j < ea.NumAdapters; j++)
                ok(pAdapters[i].AdapterLuid.LowPart != pAdapters[j].AdapterLuid.LowPart || pAdapters[i].AdapterLuid.HighPart != pAdapters[j].AdapterLuid.HighPart, "EnumAdapters2 adapters[%lu] and [%lu] share a LUID\n", i, j);

            memset(&OpenData, 0, sizeof(OpenData));
            OpenData.AdapterLuid = pAdapters[i].AdapterLuid;
            Status = pfnD3DKMTOpenAdapterFromLuid(&OpenData);
            ok(NT_SUCCESS(Status) && OpenData.hAdapter != 0, "OpenAdapterFromLuid for EnumAdapters2 LUID[%lu] failed 0x%08lX\n", i, (long)Status);
            if (NT_SUCCESS(Status) && OpenData.hAdapter)
            {
                D3DKMT_CLOSEADAPTER CloseData;
                memset(&CloseData, 0, sizeof(CloseData));
                CloseData.hAdapter = OpenData.hAdapter;
                Status = pfnD3DKMTCloseAdapter(&CloseData);
                ok(NT_SUCCESS(Status), "CloseAdapter for reopened EnumAdapters2 LUID[%lu] failed 0x%08lX\n", i, (long)Status);
            }

            /* EnumAdapters2 itself opens each adapter; close that handle too. */
            if (pAdapters[i].hAdapter)
            {
                D3DKMT_CLOSEADAPTER ca;
                memset(&ca, 0, sizeof(ca));
                ca.hAdapter = pAdapters[i].hAdapter;
                Status = pfnD3DKMTCloseAdapter(&ca);
                ok(NT_SUCCESS(Status), "CloseAdapter for EnumAdapters2 handle[%lu] failed 0x%08lX\n", i, (long)Status);
            }
        }
    }

    LocalFree(pAdapters);
}

static NTSTATUS EnumAdapters3Filtered(PFND3DKMT_ENUMADAPTERS3 pfn, D3DKMT_ENUMADAPTERS_FILTER Filter, D3DKMT_ADAPTERINFO **ppAdapters, ULONG *pCount)
{
    D3DKMT_ENUMADAPTERS3 ea;
    D3DKMT_ADAPTERINFO *pAdapters;
    NTSTATUS Status;
    ULONG Capacity;

    *ppAdapters = NULL;
    *pCount = 0;
    memset(&ea, 0, sizeof(ea));
    ea.Filter = Filter;
    Status = pfn(&ea);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL)
        return Status;

    Capacity = ea.NumAdapters;
    if (Capacity == 0)
        return STATUS_SUCCESS;
    if (Capacity > 1024)
        return STATUS_INVALID_PARAMETER;

    pAdapters = (D3DKMT_ADAPTERINFO *)LocalAlloc(LMEM_ZEROINIT, Capacity * sizeof(*pAdapters));
    if (!pAdapters)
        return STATUS_NO_MEMORY;

    memset(&ea, 0, sizeof(ea));
    ea.Filter = Filter;
    ea.NumAdapters = Capacity;
    ea.pAdapters = pAdapters;
    Status = pfn(&ea);
    if (!NT_SUCCESS(Status))
    {
        LocalFree(pAdapters);
        return Status;
    }
    if (ea.NumAdapters > Capacity)
    {
        LocalFree(pAdapters);
        return STATUS_BUFFER_TOO_SMALL;
    }

    *ppAdapters = pAdapters;
    *pCount = ea.NumAdapters;
    return STATUS_SUCCESS;
}

static void CloseAdapterArray(PFN_D3DKMTCloseAdapter pfnClose, D3DKMT_ADAPTERINFO *pAdapters, ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        D3DKMT_CLOSEADAPTER CloseData;
        NTSTATUS Status;
        if (!pAdapters[i].hAdapter)
            continue;
        memset(&CloseData, 0, sizeof(CloseData));
        CloseData.hAdapter = pAdapters[i].hAdapter;
        Status = pfnClose(&CloseData);
        ok(NT_SUCCESS(Status), "CloseAdapter for EnumAdapters3 handle[%lu] failed 0x%08lX\n", i, (long)Status);
    }
}

/* ---- EnumAdapters3 filter comparison (WDDM 2.7; optional on WDDM 2.0) ---- */
static void Test_EnumAdapters3_Filters(void)
{
    PFND3DKMT_ENUMADAPTERS3 pfn3 = (PFND3DKMT_ENUMADAPTERS3)LoadD3DKMTProc("D3DKMTEnumAdapters3");
    PFND3DKMT_ENUMADAPTERS2 pfn2 = (PFND3DKMT_ENUMADAPTERS2)LoadD3DKMTProc("D3DKMTEnumAdapters2");
    PFN_D3DKMTCloseAdapter pfnClose = (PFN_D3DKMTCloseAdapter)LoadD3DKMTProc("D3DKMTCloseAdapter");
    D3DKMT_ADAPTERINFO *pDefault = NULL, *pInclusive = NULL;
    D3DKMT_ENUMADAPTERS_FILTER Filter;
    ULONG DefaultCount = 0, InclusiveCount = 0;
    NTSTATUS Status;

    if (!pfn3)
    {
        skip("D3DKMTEnumAdapters3 not exported; WDDM 2.7 filtering is outside the WDDM 2.0 runtime contract\n");
        return;
    }
    if (!pfnClose)
    {
        skip("D3DKMTCloseAdapter not exported\n");
        return;
    }

    Filter.Value = 0;
    Status = EnumAdapters3Filtered(pfn3, Filter, &pDefault, &DefaultCount);
    if (Status == STATUS_NOT_IMPLEMENTED || Status == STATUS_NOT_SUPPORTED || Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTEnumAdapters3 is unavailable on this WDDM 2.0 runtime (0x%08lX)\n", (long)Status);
        return;
    }
    ok(NT_SUCCESS(Status), "EnumAdapters3 default-filter enumeration failed 0x%08lX\n", (long)Status);
    if (!NT_SUCCESS(Status))
        return;
    ok(DefaultCount > 0, "EnumAdapters3 default filter returned no adapters\n");

    Filter.Value = 0;
    Filter.IncludeComputeOnly = 1;
    Filter.IncludeDisplayOnly = 1;
    Status = EnumAdapters3Filtered(pfn3, Filter, &pInclusive, &InclusiveCount);
    ok(NT_SUCCESS(Status), "EnumAdapters3 inclusive-filter enumeration failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        ok(InclusiveCount >= DefaultCount, "EnumAdapters3 inclusive count %lu is smaller than default count %lu\n", InclusiveCount, DefaultCount);

        if (pfn2)
        {
            D3DKMT_ENUMADAPTERS2 Enum2;
            memset(&Enum2, 0, sizeof(Enum2));
            Status = pfn2(&Enum2);
            ok(NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL, "EnumAdapters2 count query failed 0x%08lX\n", (long)Status);
            if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
                ok(DefaultCount <= Enum2.NumAdapters, "EnumAdapters3 default count %lu exceeds EnumAdapters2 capacity %lu\n", DefaultCount, Enum2.NumAdapters);
        }
    }

    if (pDefault)
    {
        CloseAdapterArray(pfnClose, pDefault, DefaultCount);
        LocalFree(pDefault);
    }
    if (pInclusive)
    {
        CloseAdapterArray(pfnClose, pInclusive, InclusiveCount);
        LocalFree(pInclusive);
    }
}

START_TEST(enum2)
{
    Test_EnumAdapters2_NullArg();
    Test_EnumAdapters3_NullArg();
    Test_EnumAdapters2_TwoPass();
    Test_EnumAdapters3_Filters();
}
