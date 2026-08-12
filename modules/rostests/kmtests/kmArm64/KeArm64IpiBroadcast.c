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
static volatile LONG BroadcastIrqlMask;
static volatile LONG64 BroadcastInterruptsEnabledMask;
static KDPC TargetDpc;
static volatile LONG TargetDpcCpu;

static ULONG_PTR NTAPI Arm64BroadcastWorker(_In_ ULONG_PTR Argument)
{
    ULONG Processor = KeGetCurrentProcessorNumberEx(NULL);

    InterlockedOr64(&BroadcastMask, (LONG64)((KAFFINITY)1 << Processor));
    InterlockedOr(&BroadcastIrqlMask, 1L << KeGetCurrentIrql());
    if (KmtAreInterruptsEnabled())
        InterlockedOr64(&BroadcastInterruptsEnabledMask, (LONG64)((KAFFINITY)1 << Processor));
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

    for (Round = 0; Round < 256; Round++)
    {
        ULONG_PTR Result;

        InterlockedExchange64(&BroadcastMask, 0);
        InterlockedExchange(&BroadcastCount, 0);
        InterlockedExchange(&BroadcastIrqlMask, 0);
        InterlockedExchange64(&BroadcastInterruptsEnabledMask, 0);

        Result = KeIpiGenericCall(Arm64BroadcastWorker, 0x4242 + Round);

        ok_eq_ulongptr(Result, 0x4242 + Round);
        ok_eq_longlong((LONG64)BroadcastMask, (LONG64)Active);
        ok_eq_long(BroadcastCount, (LONG)KeNumberProcessors);
        ok_eq_long(BroadcastIrqlMask, 1L << IPI_LEVEL);
        ok_eq_longlong(BroadcastInterruptsEnabledMask, (LONG64)Active);
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

static VOID Arm64DpcTargetEncodingCheck(VOID)
{
    KDPC Dpc;
    PROCESSOR_NUMBER ProcessorNumber;
    NTSTATUS Status;
    KIRQL OldIrql;
    ULONG Cpu;
    BOOLEAN Inserted;
    BOOLEAN Removed;
    USHORT NumberAfterSet;
    USHORT NumberAfterSetEx;

    KeInitializeDpc(&Dpc, Arm64TargetDpcRoutine, NULL);
    for (Cpu = 0; Cpu < KeNumberProcessors; Cpu++)
    {
        KeSetTargetProcessorDpc(&Dpc, (CCHAR)Cpu);
        ok_eq_uint(Dpc.Number, 0x800 + Cpu);

        KeInitializeDpc(&Dpc, Arm64TargetDpcRoutine, NULL);
        ProcessorNumber.Group = 0;
        ProcessorNumber.Number = (UCHAR)Cpu;
        ProcessorNumber.Reserved = 0;
        Status = KeSetTargetProcessorDpcEx(&Dpc, &ProcessorNumber);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_uint(Dpc.Number, 0x800 + Cpu);
        KeInitializeDpc(&Dpc, Arm64TargetDpcRoutine, NULL);
    }

    KeSetTargetProcessorDpc(&Dpc, (CCHAR)-1);
    ok_eq_uint(Dpc.Number, 0);
    KeSetTargetProcessorDpc(&Dpc, (CCHAR)KeNumberProcessors);
    ok_eq_uint(Dpc.Number, 0);

    ProcessorNumber.Group = 1;
    ProcessorNumber.Number = 0;
    ProcessorNumber.Reserved = 0;
    Status = KeSetTargetProcessorDpcEx(&Dpc, &ProcessorNumber);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_uint(Dpc.Number, 0);

    ProcessorNumber.Group = 0;
    ProcessorNumber.Number = (UCHAR)KeNumberProcessors;
    Status = KeSetTargetProcessorDpcEx(&Dpc, &ProcessorNumber);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_uint(Dpc.Number, 0);

    if (KeNumberProcessors < 2)
    {
        skip(FALSE, "Single CPU -- skipping queued target preservation checks\n");
        return;
    }

    KeSetSystemAffinityThread((KAFFINITY)1);
    InterlockedExchange(&TargetDpcCpu, -1);
    KeInitializeDpc(&Dpc, Arm64TargetDpcRoutine, NULL);
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    Inserted = KeInsertQueueDpc(&Dpc, NULL, NULL);
    KeSetTargetProcessorDpc(&Dpc, 1);
    NumberAfterSet = Dpc.Number;
    ProcessorNumber.Group = 0;
    ProcessorNumber.Number = 1;
    Status = KeSetTargetProcessorDpcEx(&Dpc, &ProcessorNumber);
    NumberAfterSetEx = Dpc.Number;
    Removed = KeRemoveQueueDpc(&Dpc);
    KeLowerIrql(OldIrql);
    KeRevertToUserAffinityThread();

    ok_bool_true(Inserted, "queue DPC before target change");
    ok_eq_uint(NumberAfterSet, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_uint(NumberAfterSetEx, 0);
    ok_bool_true(Removed, "remove queued DPC after target change");
    ok_eq_long(TargetDpcCpu, -1);
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
    InterlockedExchange(&BroadcastIrqlMask, 0);
    InterlockedExchange64(&BroadcastInterruptsEnabledMask, 0);
    KeIpiGenericCall(Arm64BroadcastWorker, 0);

    ok_eq_longlong((LONG64)BroadcastMask, (LONG64)KeQueryActiveProcessors());
    ok_eq_long(BroadcastCount, (LONG)KeNumberProcessors);
    ok_eq_long(BroadcastIrqlMask, 1L << IPI_LEVEL);
    ok_eq_longlong(BroadcastInterruptsEnabledMask, (LONG64)KeQueryActiveProcessors());

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
    Arm64DpcTargetEncodingCheck();
    Arm64RemoteSenderCheck();
    dump_trace("[arm64][KeArm64IpiBroadcast] done\n");
#endif
}
