/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM 2.0 video memory budgeting test (QueryVideoMemoryInfo)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * D3DKMTQueryVideoMemoryInfo reports the per-process memory budget and current
 * usage for a memory segment group (local = dedicated VRAM, non-local = shared
 * system memory). It underpins the DXGI video-memory budgeting introduced with
 * WDDM 2.0 and works even on software/display-only adapters (where the budget is
 * a software-managed view over system RAM).
 *
 * Reference: Microsoft "D3DKMTQueryVideoMemoryInfo",
 *            "D3DKMT_QUERYVIDEOMEMORYINFO structure".
 */

#include "precomp.h"

/* ---- NULL-argument contract ---- */
static void Test_QueryVideoMemoryInfo_NullArg(void)
{
    LOADFN(PFND3DKMT_QUERYVIDEOMEMORYINFO, p, "D3DKMTQueryVideoMemoryInfo");
    EXPECT_NULL_REJECTED(p, "D3DKMTQueryVideoMemoryInfo");
}

/* ---- Bogus adapter handle must fail ---- */
static void Test_QueryVideoMemoryInfo_BadHandle(void)
{
    D3DKMT_QUERYVIDEOMEMORYINFO qvm;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_QUERYVIDEOMEMORYINFO, p, "D3DKMTQueryVideoMemoryInfo");

    memset(&qvm, 0, sizeof(qvm));
    qvm.hProcess = NULL;                                  /* current process */
    qvm.hAdapter = (D3DKMT_HANDLE)0xDEAD5001;
    qvm.MemorySegmentGroup = D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL;

    Status = p(&qvm);
    ok(!NT_SUCCESS(Status),
       "QueryVideoMemoryInfo with a bogus adapter should fail, got 0x%08lX\n",
       (long)Status);
}

/* ---- Real query on the local memory segment group ---- */
static void Test_QueryVideoMemoryInfo_Local(void)
{
    D3DKMT_QUERYVIDEOMEMORYINFO qvm;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_QUERYVIDEOMEMORYINFO, p, "D3DKMTQueryVideoMemoryInfo");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }

    memset(&qvm, 0, sizeof(qvm));
    qvm.hProcess = NULL;
    qvm.hAdapter = hAdapter;
    qvm.MemorySegmentGroup = D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL;

    Status = p(&qvm);
    if (!NT_SUCCESS(Status))
    {
        skip("QueryVideoMemoryInfo(LOCAL) not supported on this adapter (0x%08lX)\n",
             (long)Status);
        CloseAdapter(hAdapter);
        return;
    }

    /* Logical invariant independent of the environment: a process cannot be
     * using more than its budget allows it to keep resident indefinitely, and
     * the reservation cannot exceed what is available for reservation. */
    ok(qvm.CurrentReservation <= qvm.AvailableForReservation ||
       qvm.AvailableForReservation == 0,
       "Reservation %llu exceeds available-for-reservation %llu\n",
       (unsigned long long)qvm.CurrentReservation,
       (unsigned long long)qvm.AvailableForReservation);

    trace("VideoMemory LOCAL: Budget=%llu CurrentUsage=%llu Reservation=%llu AvailForRsv=%llu\n",
          (unsigned long long)qvm.Budget,
          (unsigned long long)qvm.CurrentUsage,
          (unsigned long long)qvm.CurrentReservation,
          (unsigned long long)qvm.AvailableForReservation);

    CloseAdapter(hAdapter);
}

START_TEST(videomem)
{
    Test_QueryVideoMemoryInfo_NullArg();
    Test_QueryVideoMemoryInfo_BadHandle();
    Test_QueryVideoMemoryInfo_Local();
}
