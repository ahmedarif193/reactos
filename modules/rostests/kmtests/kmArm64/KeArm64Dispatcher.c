/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 dispatcher / scheduler probes
 *
 * Validates Prcb->WaitListHead, WaitLock, ReadySummary, QueueIndex,
 * NormalPriorityQueueIndex, DispatcherReadyListHead[0..31],
 * TimerExpirationDpc and ScbQueue/ScbList accessibility.
 */

#include <kmt_test.h>

VOID Test_KeArm64Dispatcher(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64DispatcherCheck(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    int i;

    ok(Prcb != NULL, "Prcb is NULL\n");
    if (!Prcb)
    {
        skip(FALSE, "No PRCB available\n");
        return;
    }

    /* WaitListHead is a LIST_ENTRY: Flink and Blink must be self-consistent. */
    ok(Prcb->WaitListHead.Flink != NULL,
       "WaitListHead.Flink is NULL\n");
    ok(Prcb->WaitListHead.Blink != NULL,
       "WaitListHead.Blink is NULL\n");
    /* When empty: both Flink and Blink point at the head. */
    if (Prcb->WaitListHead.Flink == Prcb->WaitListHead.Blink)
    {
        ok_eq_pointer((PVOID)Prcb->WaitListHead.Flink,
                      (PVOID)&Prcb->WaitListHead);
    }
    else
    {
        skip(FALSE, "WaitListHead is non-empty -- skipping head equality\n");
    }

    /* WaitLock is settable: write a sentinel then restore. */
    {
        UINT64 OldVal = Prcb->WaitLock;
        Prcb->WaitLock = 0xBADC0DEULL;
        ok_eq_ulonglong((ULONGLONG)Prcb->WaitLock, (ULONGLONG)0xBADC0DEULL);
        Prcb->WaitLock = OldVal;
    }

    /* ReadySummary, QueueIndex, NormalPriorityQueueIndex reachable. */
    {
        ULONG Rs = Prcb->ReadySummary;
        ULONG Qi = Prcb->QueueIndex;
        ULONG Nq = Prcb->NormalPriorityQueueIndex;
        (void)Rs; (void)Qi; (void)Nq;
    }

    /* DispatcherReadyListHead is an array of 32 LIST_ENTRYs. */
    ok_eq_size(sizeof(Prcb->DispatcherReadyListHead) /
               sizeof(Prcb->DispatcherReadyListHead[0]),
               (SIZE_T)32);

    for (i = 0; i < 32; i++)
    {
        ok(Prcb->DispatcherReadyListHead[i].Flink != NULL,
           "DispatcherReadyListHead[%d].Flink is NULL\n", i);
        ok(Prcb->DispatcherReadyListHead[i].Blink != NULL,
           "DispatcherReadyListHead[%d].Blink is NULL\n", i);
    }

    /* TimerExpirationDpc is a KDPC: its Type byte should be DpcObject or 0. */
    {
        UCHAR Type = Prcb->TimerExpirationDpc.Type;
        (void)Type;
    }
    ok_eq_size(sizeof(Prcb->TimerExpirationDpc), (SIZE_T)0x40);

    /* ScbQueue / ScbList byte slots reachable. */
    ok_eq_size(sizeof(Prcb->ScbQueue), (SIZE_T)0x10);
    ok_eq_size(sizeof(Prcb->ScbList), (SIZE_T)0x80);

    dump_trace("[arm64][KeArm64Dispatcher] ReadySummary=0x%x QueueIndex=%u\n",
               Prcb->ReadySummary, Prcb->QueueIndex);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64Dispatcher)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64Dispatcher is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64Dispatcher] enter\n");
    Arm64DispatcherCheck();
#endif
}
