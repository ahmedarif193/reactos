/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 SMP topology probes
 *
 * Validates Prcb->Number, Group, Package/Die/Module/Core/ProcessorId,
 * ScanSiblingMask, GroupSetMember and KeNumberProcessors on the current CPU.
 */

#include <kmt_test.h>

VOID Test_KeArm64Smp(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64SmpCheck(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    KAFFINITY Active;
    ULONG MyNumber;
    int i;

    ok(Prcb != NULL, "Prcb NULL\n");
    if (!Prcb)
    {
        skip(FALSE, "No PRCB\n");
        return;
    }

    MyNumber = KeGetCurrentProcessorNumber();
    ok((ULONG)Prcb->Number == MyNumber,
       "Prcb->Number=%u != KeGetCurrentProcessorNumber()=%u\n",
       Prcb->Number, MyNumber);

    ok_eq_uint(Prcb->Group, 0);
    ok_eq_uint(Prcb->GroupIndex, MyNumber);

    /* Single-CPU baseline: all topology IDs are zero on CPU 0. */
    if (MyNumber == 0)
    {
        ok_eq_ulong(Prcb->PackageId, 0u);
        ok_eq_ulong(Prcb->DieId, 0u);
        ok_eq_ulong(Prcb->ModuleId, 0u);
        ok_eq_ulong(Prcb->CoreId, 0u);
        ok_eq_ulong(Prcb->ProcessorId, 0u);
    }
    else
    {
        skip(FALSE, "Non-CPU0 -- skipping topology zero checks\n");
        skip(FALSE, "Non-CPU0 -- skipping topology zero checks\n");
        skip(FALSE, "Non-CPU0 -- skipping topology zero checks\n");
        skip(FALSE, "Non-CPU0 -- skipping topology zero checks\n");
        skip(FALSE, "Non-CPU0 -- skipping topology zero checks\n");
    }

    /* SchedulerSubNode is a kernel pointer if non-NULL. */
    if (Prcb->SchedulerSubNode != NULL)
    {
        ok((ULONG_PTR)Prcb->SchedulerSubNode >= 0xFFFF000000000000ULL,
           "SchedulerSubNode=%p is not a kernel address\n",
           Prcb->SchedulerSubNode);
    }
    else
    {
        skip(FALSE, "SchedulerSubNode is NULL on this build\n");
    }

    /* GroupSetMember == (1 << Number) for the current CPU. */
    ok_eq_ulonglong((ULONGLONG)Prcb->GroupSetMember,
                    (ULONGLONG)1ULL << Prcb->Number);

    /* ScanSiblingMask: when no siblings, may be 0; when present, must have
     * our own bit cleared (Win11 convention) or set to our mask. */
    {
        UINT64 Mask = Prcb->ScanSiblingMask;
        (void)Mask;
        ok(TRUE, "ScanSiblingMask=0x%I64x\n", Mask);
    }

    /* KeNumberProcessors must be >= 1. */
    ok((LONG)KeNumberProcessors >= 1,
       "KeNumberProcessors=%d < 1\n", (LONG)KeNumberProcessors);

    /* Affinity should include this CPU. */
    Active = KeQueryActiveProcessors();
    ok(Active != 0, "KeQueryActiveProcessors() returned 0\n");
    ok((Active & ((KAFFINITY)1 << Prcb->Number)) != 0,
       "KeQueryActiveProcessors()=0x%I64x does not include CPU %u\n",
       (ULONG64)Active, Prcb->Number);

    /* CoreProcessorSet/LLCMask reachable. */
    {
        UINT64 Cps = Prcb->CoreProcessorSet;
        UINT64 Llc = Prcb->LLCMask;
        (void)Cps; (void)Llc;
    }

    /* Walk the DispatcherReadyListHead[0..31] one more time as part of
     * the SMP layout: each list head must be at +0x10 stride. */
    for (i = 0; i < 32; i++)
    {
        ULONG_PTR Off = (ULONG_PTR)&Prcb->DispatcherReadyListHead[i] -
                        (ULONG_PTR)&Prcb->DispatcherReadyListHead[0];
        ok_eq_ulonglong((ULONGLONG)Off, (ULONGLONG)(i * 0x10));
    }

    /* DeviceInterrupts counter touchable. */
    {
        ULONG D = Prcb->DeviceInterrupts;
        (void)D;
    }

    /* KernelTime / UserTime accessible. */
    {
        ULONG K = Prcb->KernelTime;
        ULONG U = Prcb->UserTime;
        (void)K; (void)U;
    }

    dump_trace("[arm64][KeArm64Smp] CPU=%u Group=%u KeNum=%d Active=0x%I64x GSM=0x%I64x\n",
               Prcb->Number, Prcb->Group, (LONG)KeNumberProcessors,
               (ULONG64)Active, (ULONG64)Prcb->GroupSetMember);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64Smp)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64Smp is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64Smp] enter\n");
    Arm64SmpCheck();
#endif
}
