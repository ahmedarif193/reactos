/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM 2.0 GPU virtual addressing tests
 *              (Reserve / Map / Free / Update GpuVirtualAddress)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * GPU virtual addressing is the defining WDDM 2.0 feature: each device owns a
 * per-process GPU virtual address space, into which allocations are mapped at
 * explicit virtual addresses (rather than being patched at submit time as in
 * WDDM 1.x). Address ranges are reserved, allocations mapped/unmapped, and
 * ranges freed, all synchronized through a paging queue.
 *
 * Reference: Microsoft "D3DKMTReserveGpuVirtualAddress",
 *            "D3DKMTMapGpuVirtualAddress", "D3DKMTFreeGpuVirtualAddress",
 *            "D3DKMTUpdateGpuVirtualAddress", "GPU virtual addressing (WDDMv2)".
 *
 * Positively exercising the mapping path needs a render-capable device, a
 * paging queue and real allocations; here we validate the portable contract
 * (NULL refused, bogus handles fail).
 */

#include "precomp.h"

/* ---- NULL-argument contract ---- */
static void Test_ReserveGpuVa_NullArg(void)
{
    LOADFN(PFND3DKMT_RESERVEGPUVIRTUALADDRESS, p, "D3DKMTReserveGpuVirtualAddress");
    EXPECT_NULL_REJECTED(p, "D3DKMTReserveGpuVirtualAddress");
}

static void Test_MapGpuVa_NullArg(void)
{
    LOADFN(PFND3DKMT_MAPGPUVIRTUALADDRESS, p, "D3DKMTMapGpuVirtualAddress");
    EXPECT_NULL_REJECTED(p, "D3DKMTMapGpuVirtualAddress");
}

static void Test_FreeGpuVa_NullArg(void)
{
    LOADFN(PFND3DKMT_FREEGPUVIRTUALADDRESS, p, "D3DKMTFreeGpuVirtualAddress");
    EXPECT_NULL_REJECTED(p, "D3DKMTFreeGpuVirtualAddress");
}

static void Test_UpdateGpuVa_NullArg(void)
{
    LOADFN(PFND3DKMT_UPDATEGPUVIRTUALADDRESS, p, "D3DKMTUpdateGpuVirtualAddress");
    EXPECT_NULL_REJECTED(p, "D3DKMTUpdateGpuVirtualAddress");
}

/* ---- Reserve with a bogus adapter handle must fail ---- */
static void Test_ReserveGpuVa_BadHandle(void)
{
    D3DDDI_RESERVEGPUVIRTUALADDRESS rsv;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_RESERVEGPUVIRTUALADDRESS, p, "D3DKMTReserveGpuVirtualAddress");

    memset(&rsv, 0, sizeof(rsv));
    rsv.hAdapter = (D3DKMT_HANDLE)0xDEAD3001;   /* M2 form: reserve against an adapter */
    rsv.Size = 0x10000;                         /* 64 KiB */

    Status = p(&rsv);
    ok(!NT_SUCCESS(Status),
       "ReserveGpuVirtualAddress with a bogus adapter should fail, got 0x%08lX\n",
       (long)Status);
}

START_TEST(gpuva)
{
    Test_ReserveGpuVa_NullArg();
    Test_MapGpuVa_NullArg();
    Test_FreeGpuVa_NullArg();
    Test_UpdateGpuVa_NullArg();
    Test_ReserveGpuVa_BadHandle();
}
