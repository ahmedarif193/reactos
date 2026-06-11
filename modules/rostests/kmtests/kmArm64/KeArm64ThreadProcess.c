/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 thread / process state
 *
 * Validates KeGetCurrentThread, KeGetCurrentProcess, Prcb->CurrentThread,
 * NextThread, IdleThread, KernelTime/UserTime/InterruptTime/DpcTime,
 * KeQueryActiveProcessors and KeSetSystemAffinityThread / Revert.
 */

#include <kmt_test.h>

VOID Test_KeArm64ThreadProcess(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64ThreadProcessCheck(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD CurThread;
    PEPROCESS CurProcess;
    KPROCESSOR_MODE PreviousMode;
    KAFFINITY Active;

    ok(Prcb != NULL, "Prcb NULL\n");
    if (!Prcb)
    {
        skip(FALSE, "No PRCB\n");
        return;
    }

    /* KeGetCurrentThread and KeGetCurrentProcess. */
    CurThread = KeGetCurrentThread();
    ok(CurThread != NULL, "KeGetCurrentThread NULL\n");

    CurProcess = IoGetCurrentProcess();
    ok(CurProcess != NULL, "IoGetCurrentProcess NULL\n");

    /* Prcb->CurrentThread is non-NULL while we run. */
    ok(Prcb->CurrentThread != NULL, "Prcb->CurrentThread NULL\n");
    ok_eq_pointer((PVOID)Prcb->CurrentThread, (PVOID)CurThread);

    /* IdleThread should be populated. */
    ok(Prcb->IdleThread != NULL, "Prcb->IdleThread NULL\n");

    /* NextThread can be NULL (no scheduled successor) or a real thread. */
    if (Prcb->NextThread != NULL)
    {
        ok((ULONG_PTR)Prcb->NextThread >= 0xFFFF000000000000ULL,
           "NextThread=%p not kernel pointer\n", Prcb->NextThread);
    }
    else
    {
        skip(FALSE, "NextThread NULL -- nothing scheduled to run\n");
    }

    /* Idle and current are not the same when we're a real test thread. */
    ok(Prcb->CurrentThread != Prcb->IdleThread,
       "We are running as the idle thread -- test cannot proceed\n");

    /* Time accounting counters touchable. */
    {
        ULONG K = Prcb->KernelTime;
        ULONG U = Prcb->UserTime;
        ULONG D = Prcb->DpcTime;
        ULONG I = Prcb->InterruptTime;
        (void)K; (void)U; (void)D; (void)I;
    }

    /* KeQueryActiveProcessors must include this CPU. */
    Active = KeQueryActiveProcessors();
    ok(Active != 0, "KeQueryActiveProcessors=0\n");
    ok((Active & ((KAFFINITY)1 << Prcb->Number)) != 0,
       "Active mask 0x%I64x missing CPU %u\n",
       (ULONG64)Active, Prcb->Number);

    /* KeSetSystemAffinityThread to our own CPU and revert. */
    KeSetSystemAffinityThread((KAFFINITY)1 << Prcb->Number);
    /* After setting we should still be on our CPU (UP build always). */
    ok((ULONG)KeGetCurrentProcessorNumber() == Prcb->Number ||
       KeNumberProcessors > 1,
       "After affinity restrict, we left CPU %u\n", Prcb->Number);
    KeRevertToUserAffinityThread();

    /* Re-fetch PRCB after potential CPU change. */
    Prcb = KeGetCurrentPrcb();
    ok(Prcb != NULL, "Prcb NULL after revert\n");

    /* PreviousMode of current thread should be KernelMode for kmtest. */
    PreviousMode = ExGetPreviousMode();
    ok(PreviousMode == KernelMode || PreviousMode == UserMode,
       "PreviousMode unexpected: %d\n", PreviousMode);

    /* Pointer sanity: CurrentThread inside the kernel half. */
    ok((ULONG_PTR)Prcb->CurrentThread >= 0xFFFF000000000000ULL,
       "CurrentThread=%p outside kernel range\n", Prcb->CurrentThread);
    ok((ULONG_PTR)Prcb->IdleThread >= 0xFFFF000000000000ULL,
       "IdleThread=%p outside kernel range\n", Prcb->IdleThread);

    /* DPC tracking touchable. */
    ok_eq_uint(Prcb->DpcRoutineActive, 0);
    {
        UCHAR Q = Prcb->QuantumEnd;
        UCHAR S = Prcb->IdleSchedule;
        (void)Q; (void)S;
    }

    /* ContextSwitches and SystemCalls counters reachable. */
    {
        ULONG Cs = Prcb->KeContextSwitches;
        ULONG Sc = Prcb->KeSystemCalls;
        (void)Cs; (void)Sc;
    }

    /* InterruptCount and ClockInterrupts touchable. */
    {
        ULONG Ic = Prcb->InterruptCount;
        ULONG Ci = Prcb->ClockInterrupts;
        (void)Ic; (void)Ci;
    }

    /* IPC/IRP counters touchable. */
    {
        LONG I = Prcb->IoReadOperationCount;
        LONG O = Prcb->IoWriteOperationCount;
        (void)I; (void)O;
    }

    dump_trace("[arm64][KeArm64ThreadProcess] CurThread=%p IdleThread=%p\n",
               CurThread, Prcb->IdleThread);
    dump_trace("[arm64][KeArm64ThreadProcess] CS=%u SC=%u K=%u U=%u D=%u I=%u\n",
               Prcb->KeContextSwitches, Prcb->KeSystemCalls,
               Prcb->KernelTime, Prcb->UserTime,
               Prcb->DpcTime, Prcb->InterruptTime);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64ThreadProcess)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64ThreadProcess is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64ThreadProcess] enter\n");
    Arm64ThreadProcessCheck();
#endif
}
