/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     System memory counter units and untouched commit accounting
 */

#include "precomp.h"

START_TEST(MemoryAccounting)
{
    SYSTEM_BASIC_INFORMATION Basic;
    SYSTEM_FILECACHE_INFORMATION Cache;
    SYSTEM_PERFORMANCE_INFORMATION Before, Committed, Released;
    VM_COUNTERS VmBefore, VmAfter;
    PVOID Base = NULL;
    SIZE_T Size, Pages;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = NtQuerySystemInformation(SystemFileCacheInformation, &Cache, sizeof(Cache), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(Cache.CurrentSize % Basic.PageSize == 0,
           "cache CurrentSize is not page-aligned bytes: %Iu\n", Cache.CurrentSize);
        ok(Cache.PeakSize >= Cache.CurrentSize, "cache peak smaller than current\n");
        ok(Cache.CurrentSize / Basic.PageSize <= Cache.CurrentSizeIncludingTransitionInPages,
           "cache byte count exceeds including-transition page count\n");
    }

    Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessVmCounters,
                                      &VmBefore, sizeof(VmBefore), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = NtQuerySystemInformation(SystemPerformanceInformation, &Before, sizeof(Before), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    /* Commit without touching: resident-page counters cannot satisfy this.
     * Leave some tolerance for unrelated processes freeing memory between
     * the global snapshots; the process-local charge must include the full
     * allocation even if the test harness makes additional allocations. */
    Size = 32 * 1024 * 1024;
    Pages = Size / Basic.PageSize;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size,
                                     MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessVmCounters,
                                      &VmAfter, sizeof(VmAfter), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(VmAfter.PagefileUsage >= VmBefore.PagefileUsage + Size,
           "process commit did not reflect the allocation\n");
    Status = NtQuerySystemInformation(SystemPerformanceInformation, &Committed, sizeof(Committed), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok((LONGLONG)Committed.CommittedPages - Before.CommittedPages >= (LONGLONG)Pages / 2,
           "system commit did not reflect untouched allocation: %lu -> %lu (expected ~%Iu pages)\n",
           Before.CommittedPages, Committed.CommittedPages, Pages);

    Size = 0;
    Status = NtFreeVirtualMemory(NtCurrentProcess(), &Base, &Size, MEM_RELEASE);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = NtQuerySystemInformation(SystemPerformanceInformation, &Released, sizeof(Released), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok((LONGLONG)Released.CommittedPages - Before.CommittedPages < (LONGLONG)Pages / 2,
           "system commit did not return near baseline: %lu -> %lu\n",
           Before.CommittedPages, Released.CommittedPages);
        ok(Released.PeakCommitment >= Released.CommittedPages,
           "peak commit below current commit\n");
    }
}
