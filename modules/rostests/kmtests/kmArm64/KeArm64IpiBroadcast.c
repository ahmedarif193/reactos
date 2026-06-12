/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 KeIpiGenericCall broadcast and cross-CPU DPC targeting
 */

#include <kmt_test.h>

VOID Test_KeArm64IpiBroadcast(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static volatile LONG64 BroadcastMask;
static volatile LONG BroadcastCount;
static KDPC TargetDpc;
static volatile LONG TargetDpcCpu;

static ULONG_PTR NTAPI Arm64BroadcastWorker(_In_ ULONG_PTR Argument)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    if (Prcb != NULL)
        InterlockedOr64(&BroadcastMask, (LONG64)Prcb->SetMember);
    InterlockedIncrement(&BroadcastCount);
    return Argument;
}

static VOID NTAPI Arm64TargetDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    InterlockedExchange(&TargetDpcCpu, (LONG)KeGetCurrentProcessorNumber());
}

static VOID Arm64IpiBroadcastCheck(VOID)
{
    KAFFINITY Active = KeQueryActiveProcessors();
    ULONG Round;

    for (Round = 0; Round < 4; Round++)
    {
        ULONG_PTR Result;

        InterlockedExchange64(&BroadcastMask, 0);
        InterlockedExchange(&BroadcastCount, 0);

        Result = KeIpiGenericCall(Arm64BroadcastWorker, 0x4242 + Round);

        ok_eq_ulongptr(Result, 0x4242 + Round);
        ok_eq_longlong((LONG64)BroadcastMask, (LONG64)Active);
        ok_eq_long(BroadcastCount, (LONG)KeNumberProcessors);
    }

    dump_trace("[arm64][KeArm64IpiBroadcast] mask=0x%I64x cpus=%lu\n",
               (LONG64)BroadcastMask, KeNumberProcessors);
}

static VOID Arm64TargetedDpcCheck(VOID)
{
    ULONG Cpu;

    KeInitializeDpc(&TargetDpc, Arm64TargetDpcRoutine, NULL);

    for (Cpu = 0; Cpu < KeNumberProcessors; Cpu++)
    {
        ULONG Spins;

        InterlockedExchange(&TargetDpcCpu, -1);
        KeSetTargetProcessorDpc(&TargetDpc, (CCHAR)Cpu);
        KeInsertQueueDpc(&TargetDpc, NULL, NULL);

        for (Spins = 0; Spins < 100000 && TargetDpcCpu == -1; Spins++)
            KeStallExecutionProcessor(10);

        ok(TargetDpcCpu == (LONG)Cpu, "DPC for cpu %lu ran on %ld\n", Cpu, TargetDpcCpu);
    }
}

static VOID Arm64RemoteSenderCheck(VOID)
{
    ULONG LastCpu = KeNumberProcessors - 1;

    if (LastCpu == 0)
    {
        skip(FALSE, "Single CPU -- skipping remote sender check\n");
        return;
    }

    KeSetSystemAffinityThread((KAFFINITY)1 << LastCpu);
    ok_eq_ulong(KeGetCurrentProcessorNumber(), LastCpu);

    InterlockedExchange64(&BroadcastMask, 0);
    InterlockedExchange(&BroadcastCount, 0);
    KeIpiGenericCall(Arm64BroadcastWorker, 0);

    ok_eq_longlong((LONG64)BroadcastMask, (LONG64)KeQueryActiveProcessors());
    ok_eq_long(BroadcastCount, (LONG)KeNumberProcessors);

    KeRevertToUserAffinityThread();
}

#endif /* _M_ARM64 */

START_TEST(KeArm64IpiBroadcast)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64IpiBroadcast is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64IpiBroadcast] enter cpus=%lu\n", KeNumberProcessors);
    Arm64IpiBroadcastCheck();
    Arm64TargetedDpcCheck();
    Arm64RemoteSenderCheck();
    dump_trace("[arm64][KeArm64IpiBroadcast] done\n");
#endif
}
