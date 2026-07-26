/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM 2.0 video memory budgeting test (QueryVideoMemoryInfo)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * D3DKMTQueryVideoMemoryInfo reports the per-process memory budget and current
 * usage for a memory segment group (local = dedicated VRAM, non-local = shared
 * system memory). It underpins the DXGI video-memory budgeting introduced with
 * WDDM 2.0. A backend with no segment in one group still reports that group
 * truthfully with a zero budget rather than inventing capacity.
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
    ok_failed(Status, "QueryVideoMemoryInfo with a bogus adapter should fail, got 0x%08lX\n", (long)Status);
}

static NTSTATUS QueryVideoMemoryInfo(PFND3DKMT_QUERYVIDEOMEMORYINFO pfn, HANDLE hProcess, D3DKMT_HANDLE hAdapter, D3DKMT_MEMORY_SEGMENT_GROUP Group, UINT PhysicalAdapterIndex, D3DKMT_QUERYVIDEOMEMORYINFO *Info)
{
    memset(Info, 0, sizeof(*Info));
    Info->hProcess = hProcess;
    Info->hAdapter = hAdapter;
    Info->MemorySegmentGroup = Group;
    Info->PhysicalAdapterIndex = PhysicalAdapterIndex;
    return pfn(Info);
}

static void CheckVideoMemoryInfo(const char *Name, const D3DKMT_QUERYVIDEOMEMORYINFO *Info)
{
    ok(Info->CurrentReservation <= Info->AvailableForReservation, "%s reservation %llu exceeds available-for-reservation %llu\n", Name, (unsigned long long)Info->CurrentReservation, (unsigned long long)Info->AvailableForReservation);
    trace("VideoMemory %s: Budget=%llu CurrentUsage=%llu Reservation=%llu AvailForRsv=%llu\n", Name, (unsigned long long)Info->Budget, (unsigned long long)Info->CurrentUsage, (unsigned long long)Info->CurrentReservation, (unsigned long long)Info->AvailableForReservation);
}

/* ---- Real queries, validation, and explicit-process equivalence ---- */
static void Test_QueryVideoMemoryInfo_RealAdapter(void)
{
    D3DKMT_QUERYVIDEOMEMORYINFO LocalInfo;
    D3DKMT_QUERYVIDEOMEMORYINFO NonLocalInfo;
    D3DKMT_QUERYVIDEOMEMORYINFO ExplicitInfo;
    D3DKMT_HANDLE hAdapter;
    HANDLE hProcess;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_QUERYVIDEOMEMORYINFO, p, "D3DKMTQueryVideoMemoryInfo");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }

    Status = QueryVideoMemoryInfo(p, NULL, hAdapter, D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL, 0, &LocalInfo);
    ok_succeeded(Status, "QueryVideoMemoryInfo(LOCAL) failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status)) CheckVideoMemoryInfo("LOCAL", &LocalInfo);

    Status = QueryVideoMemoryInfo(p, NULL, hAdapter, D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL, 0, &NonLocalInfo);
    ok_succeeded(Status, "QueryVideoMemoryInfo(NON_LOCAL) failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status)) CheckVideoMemoryInfo("NON_LOCAL", &NonLocalInfo);

    Status = QueryVideoMemoryInfo(p, NULL, hAdapter, (D3DKMT_MEMORY_SEGMENT_GROUP)2, 0, &ExplicitInfo);
    ok_failed(Status, "QueryVideoMemoryInfo accepted invalid segment group 2\n");

    Status = QueryVideoMemoryInfo(p, NULL, hAdapter, D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL, 1, &ExplicitInfo);
    ok_failed(Status, "QueryVideoMemoryInfo accepted unsupported physical adapter index 1\n");

    Status = QueryVideoMemoryInfo(p, (HANDLE)(ULONG_PTR)0xDEAD5002, hAdapter, D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL, 0, &ExplicitInfo);
    ok_failed(Status, "QueryVideoMemoryInfo accepted a bogus process handle\n");

    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    ok(hProcess != NULL, "OpenProcess(PROCESS_QUERY_INFORMATION) failed, error %lu\n", GetLastError());
    if (hProcess != NULL)
    {
        Status = QueryVideoMemoryInfo(p, hProcess, hAdapter, D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL, 0, &ExplicitInfo);
        ok_succeeded(Status, "QueryVideoMemoryInfo with an explicit current-process handle failed 0x%08lX\n", (long)Status);
        if (NT_SUCCESS(Status) && NT_SUCCESS(QueryVideoMemoryInfo(p, NULL, hAdapter, D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL, 0, &LocalInfo)))
        {
            ok(ExplicitInfo.Budget == LocalInfo.Budget, "Explicit-process budget %llu differs from implicit-process budget %llu\n", (unsigned long long)ExplicitInfo.Budget, (unsigned long long)LocalInfo.Budget);
            ok(ExplicitInfo.CurrentReservation == LocalInfo.CurrentReservation, "Explicit-process reservation %llu differs from implicit-process reservation %llu\n", (unsigned long long)ExplicitInfo.CurrentReservation, (unsigned long long)LocalInfo.CurrentReservation);
            ok(ExplicitInfo.AvailableForReservation == LocalInfo.AvailableForReservation, "Explicit-process available reservation %llu differs from implicit-process value %llu\n", (unsigned long long)ExplicitInfo.AvailableForReservation, (unsigned long long)LocalInfo.AvailableForReservation);
        }
        CloseHandle(hProcess);
    }

    CloseAdapter(hAdapter);
}

START_TEST(videomem)
{
    Test_QueryVideoMemoryInfo_NullArg();
    Test_QueryVideoMemoryInfo_BadHandle();
    Test_QueryVideoMemoryInfo_RealAdapter();
}
