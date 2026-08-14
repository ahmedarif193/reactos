/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel mode tests for process quotas
 * COPYRIGHT:   Copyright 2021 George Bișoc <george.bisoc@reactos.org>
 */

#include <kmt_test.h>

/* KeEnterGuardedRegion / KeLeaveGuardedRegion landed in NT 5.2 SP1 along
 * with KGUARDED_MUTEX. Resolve them dynamically so the test driver still
 * loads on older NT 5.x kernels, then fall back to the critical-region pair
 * where the guarded variant is missing. */
typedef VOID (NTAPI *PFNKEGUARDEDREGION)(VOID);

START_TEST(PsQuota)
{
    NTSTATUS Status;
    VM_COUNTERS VmCounters, ChargedVmCounters;
    QUOTA_LIMITS QuotaLimits;
    SIZE_T NonPagedUsage, PagedUsage;
    PEPROCESS Process = PsGetCurrentProcess();
    PFNKEGUARDEDREGION pKeEnterGuardedRegion;
    PFNKEGUARDEDREGION pKeLeaveGuardedRegion;

    pKeEnterGuardedRegion = (PFNKEGUARDEDREGION)KmtGetSystemRoutineAddress(L"KeEnterGuardedRegion");
    pKeLeaveGuardedRegion = (PFNKEGUARDEDREGION)KmtGetSystemRoutineAddress(L"KeLeaveGuardedRegion");

    /* Guard the quota operations. On NT 5.x RTM the guarded variant is
     * absent, but KeEnterCriticalRegion is functionally sufficient for
     * what this test needs (nothing wakes us with a special APC). */
    if (pKeEnterGuardedRegion != NULL)
        pKeEnterGuardedRegion();
    else
        KeEnterCriticalRegion();

    /* Report the current process' quota limits */
    Status = ZwQueryInformationProcess(NtCurrentProcess(),
                                       ProcessQuotaLimits,
                                       &QuotaLimits,
                                       sizeof(QuotaLimits),
                                       NULL);
    if (skip(NT_SUCCESS(Status), "Failed to query quota limits -- %lx\n", Status))
    {
        return;
    }

    trace("Process paged pool quota limit -- %lu\n", QuotaLimits.PagedPoolLimit);
    trace("Process non paged pool quota limit -- %lu\n", QuotaLimits.NonPagedPoolLimit);
    trace("Process page file quota limit -- %lu\n\n", QuotaLimits.PagefileLimit);

    /* Query the quota usage */
    Status = ZwQueryInformationProcess(NtCurrentProcess(),
                                       ProcessVmCounters,
                                       &VmCounters,
                                       sizeof(VmCounters),
                                       NULL);
    if (skip(NT_SUCCESS(Status), "Failed to query quota usage -- %lx\n", Status))
    {
        return;
    }

    /* Test that quotas usage are within limits */
    ok(VmCounters.QuotaNonPagedPoolUsage < QuotaLimits.NonPagedPoolLimit, "Non paged quota over limits (usage -> %lu || limit -> %lu)\n",
       VmCounters.QuotaNonPagedPoolUsage, QuotaLimits.NonPagedPoolLimit);
    ok(VmCounters.QuotaPagedPoolUsage < QuotaLimits.PagedPoolLimit, "Paged quota over limits (usage -> %lu || limit -> %lu)\n",
       VmCounters.QuotaPagedPoolUsage, QuotaLimits.PagedPoolLimit);

    /* Cache the quota usage pools for later checks  */
    NonPagedUsage = VmCounters.QuotaNonPagedPoolUsage;
    PagedUsage = VmCounters.QuotaPagedPoolUsage;

    /* Charge some paged and non paged quotas */
    Status = PsChargeProcessNonPagedPoolQuota(Process, 0x200);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = PsChargeProcessPagedPoolQuota(Process, 0x500);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* Query the quota usage again */
    Status = ZwQueryInformationProcess(NtCurrentProcess(),
                                       ProcessVmCounters,
                                       &VmCounters,
                                       sizeof(VmCounters),
                                       NULL);
    if (skip(NT_SUCCESS(Status), "Failed to query quota usage -- %lx\n", Status))
    {
        return;
    }


    /*
     * Preserve the charged snapshot and return our quota before reporting it.
     * Formatting test output can grow the current kernel stack, which is a
     * legitimate process quota charge and must stay outside this interval.
     */
    ChargedVmCounters = VmCounters;

    /* Return the quotas we've charged up */
    PsReturnProcessNonPagedPoolQuota(Process, 0x200);
    PsReturnProcessPagedPoolQuota(Process, 0x500);

    /* Query the quota usage again */
    Status = ZwQueryInformationProcess(NtCurrentProcess(),
                                       ProcessVmCounters,
                                       &VmCounters,
                                       sizeof(VmCounters),
                                       NULL);
    if (skip(NT_SUCCESS(Status), "Failed to query quota usage -- %lx\n", Status))
    {
        return;
    }

    /* Validate both snapshots only after closing the measured interval. */
    ok(ChargedVmCounters.QuotaNonPagedPoolUsage < QuotaLimits.NonPagedPoolLimit, "Non paged quota over limits (usage -> %lu || limit -> %lu)\n",
       ChargedVmCounters.QuotaNonPagedPoolUsage, QuotaLimits.NonPagedPoolLimit);
    ok(ChargedVmCounters.QuotaPagedPoolUsage < QuotaLimits.PagedPoolLimit, "Paged quota over limits (usage -> %lu || limit -> %lu)\n",
       ChargedVmCounters.QuotaPagedPoolUsage, QuotaLimits.PagedPoolLimit);
    ok_eq_size(ChargedVmCounters.QuotaNonPagedPoolUsage, NonPagedUsage + 0x200);
    ok_eq_size(ChargedVmCounters.QuotaPagedPoolUsage, PagedUsage + 0x500);
    ok_eq_size(VmCounters.QuotaNonPagedPoolUsage, NonPagedUsage);
    ok_eq_size(VmCounters.QuotaPagedPoolUsage, PagedUsage);

    /* Report both cached snapshots. */
    trace("=== QUOTA USAGE AFTER CHARGE ===\n\n");
    trace("Process paged pool quota usage -- %lu\n", ChargedVmCounters.QuotaPagedPoolUsage);
    trace("Process paged pool quota peak -- %lu\n", ChargedVmCounters.QuotaPeakPagedPoolUsage);
    trace("Process non paged pool quota usage -- %lu\n", ChargedVmCounters.QuotaNonPagedPoolUsage);
    trace("Process non paged pool quota peak -- %lu\n", ChargedVmCounters.QuotaPeakNonPagedPoolUsage);
    trace("Process page file quota usage -- %lu\n", ChargedVmCounters.PagefileUsage);
    trace("Process page file quota peak -- %lu\n\n", ChargedVmCounters.PeakPagefileUsage);

    /* Report the usage again */
    trace("=== QUOTA USAGE AFTER RETURN ===\n\n");
    trace("Process paged pool quota usage -- %lu\n", VmCounters.QuotaPagedPoolUsage);
    trace("Process paged pool quota peak -- %lu\n", VmCounters.QuotaPeakPagedPoolUsage);
    trace("Process non paged pool quota usage -- %lu\n", VmCounters.QuotaNonPagedPoolUsage);
    trace("Process non paged pool quota peak -- %lu\n", VmCounters.QuotaPeakNonPagedPoolUsage);
    trace("Process page file quota usage -- %lu\n", VmCounters.PagefileUsage);
    trace("Process page file quota peak -- %lu\n\n", VmCounters.PeakPagefileUsage);

    /* We're done, leave the region */
    if (pKeLeaveGuardedRegion != NULL)
        pKeLeaveGuardedRegion();
    else
        KeLeaveCriticalRegion();
}
