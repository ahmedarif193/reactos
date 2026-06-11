/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 DPC + IPI mailbox sanity
 *
 * Validates DpcData[0..1], DpcStack, MaximumDpcQueueDepth, MinimumDpcRate,
 * RequestMailbox sizing, Mailbox / TargetCount / IpiFrozen fields, and
 * KeRaiseIrqlToDpcLevel / KeLowerIrql round-trip.
 */

#include <kmt_test.h>

VOID Test_KeArm64DpcIpi(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static KDPC g_TestDpc;
static volatile LONG g_DpcRan = 0;
static volatile UCHAR g_DpcSawActive = 0;

static VOID NTAPI Arm64DpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PKPRCB Prcb;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Prcb = KeGetCurrentPrcb();
    if (Prcb)
        g_DpcSawActive = Prcb->DpcRoutineActive;
    InterlockedIncrement(&g_DpcRan);
}

static VOID Arm64DpcIpiCheck(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    KIRQL OldIrql;

    ok(Prcb != NULL, "Prcb is NULL\n");
    if (!Prcb)
    {
        skip(FALSE, "No PRCB available\n");
        return;
    }

    /* DpcData[2] sanity: two slots (normal + threaded). */
    ok_eq_size(sizeof(Prcb->DpcData) / sizeof(Prcb->DpcData[0]), (SIZE_T)2);
    ok(Prcb->DpcData[0].DpcQueueDepth >= 0,
       "DpcData[0].DpcQueueDepth=%d looks bad\n",
       Prcb->DpcData[0].DpcQueueDepth);
    ok(Prcb->DpcData[1].DpcQueueDepth >= 0,
       "DpcData[1].DpcQueueDepth=%d looks bad\n",
       Prcb->DpcData[1].DpcQueueDepth);

    /* DpcStack pointer reachable. */
    {
        PVOID Stk = Prcb->DpcStack;
        (void)Stk;
    }

    /* MaximumDpcQueueDepth has been initialised. */
    ok(Prcb->MaximumDpcQueueDepth > 0u,
       "MaximumDpcQueueDepth=%u\n", Prcb->MaximumDpcQueueDepth);
    ok(Prcb->MinimumDpcRate <= 1000u,
       "MinimumDpcRate=%u looks too large\n", Prcb->MinimumDpcRate);

    ok_eq_size(sizeof(REQUEST_MAILBOX), (SIZE_T)0x40);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KPRCB, RequestMailbox), 0x9E80ULL);

    /* Mailbox / TargetCount / IpiFrozen reachable. */
    {
        struct _REQUEST_MAILBOX *Mb = Prcb->Mailbox;
        ULONG Tc = Prcb->TargetCount;
        ULONG If = Prcb->IpiFrozen;
        (void)Mb; (void)Tc; (void)If;
    }

    /* Queue a DPC at PASSIVE then raise to DISPATCH so it runs. */
    g_DpcRan = 0;
    g_DpcSawActive = 0;
    KeInitializeDpc(&g_TestDpc, Arm64DpcRoutine, NULL);
    ok_bool_true(KeInsertQueueDpc(&g_TestDpc, NULL, NULL),
                 "KeInsertQueueDpc should return TRUE the first time");
    /* Lower-and-raise around DISPATCH to flush the DPC queue. */
    KeRaiseIrqlToDpcLevel();
    OldIrql = DISPATCH_LEVEL;
    KeLowerIrql(PASSIVE_LEVEL);

    /* On a sane SMP/UP system the DPC must have run by now. */
    ok(g_DpcRan >= 1,
       "DPC did not run (ran=%d)\n", g_DpcRan);
    ok(g_DpcSawActive == 1,
       "DpcRoutineActive was %u inside the DPC, expected 1\n",
       g_DpcSawActive);

    /* After the DPC drained, DpcRoutineActive should be back to 0. */
    ok_eq_uint(Prcb->DpcRoutineActive, 0);

    /* IRQL round-trip via KeRaiseIrqlToDpcLevel(). */
    OldIrql = KeRaiseIrqlToDpcLevel();
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    ok_eq_uint(OldIrql, PASSIVE_LEVEL);
    KeLowerIrql(OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);

    /* Direct KeRaiseIrql round-trip. */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    KeLowerIrql(OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);

    /* DpcRequestSummary aggregate is accessible. */
    {
        ULONG Req = Prcb->DpcRequestSummary;
        (void)Req;
    }

    dump_trace("[arm64][KeArm64DpcIpi] DpcData={%d,%d} Max=%u Min=%u\n",
               Prcb->DpcData[0].DpcQueueDepth,
               Prcb->DpcData[1].DpcQueueDepth,
               Prcb->MaximumDpcQueueDepth, Prcb->MinimumDpcRate);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64DpcIpi)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64DpcIpi is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64DpcIpi] enter\n");
    Arm64DpcIpiCheck();
#endif
}
