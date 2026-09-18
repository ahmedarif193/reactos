/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Concurrent generic DPC calls and repeated cross-processor barriers
 */

#include <kmt_test.h>

VOID Test_KeArm64GenericDpc(VOID);

#define GENERIC_DPC_ROUNDS 128
#define GENERIC_DPC_PHASES 3

typedef struct _GENERIC_DPC_CONTEXT
{
    PKEVENT Go;
    ULONG Cpu;
    ULONG Round;
    ULONG Completed;
    ULONG Errors;
    volatile LONG Calls;
    volatile LONG BadIrql;
    volatile LONG BadData;
    volatile LONG Leaders[GENERIC_DPC_PHASES];
    volatile LONG64 Mask;
    ULONG Values[MAXIMUM_PROCESSORS];
} GENERIC_DPC_CONTEXT;

static VOID NTAPI
BroadcastDpc(PKDPC Dpc, PVOID Parameter, PVOID Done, PVOID Sync)
{
    GENERIC_DPC_CONTEXT *Context = Parameter;
    ULONG Cpu = KeGetCurrentProcessorNumber();
    ULONG Index, Phase, Token = 0x8FA00000 ^ (Context->Cpu << 16) ^ Context->Round;

    UNREFERENCED_PARAMETER(Dpc);
    if (KeGetCurrentIrql() != DISPATCH_LEVEL) InterlockedIncrement(&Context->BadIrql);
    InterlockedIncrement(&Context->Calls);
    InterlockedOr64(&Context->Mask, (LONG64)((KAFFINITY)1 << Cpu));
    for (Phase = 0; Phase < GENERIC_DPC_PHASES; Phase++)
    {
        Context->Values[Cpu] = Token ^ (Phase << 8) ^ Cpu;
        if (KeSignalCallDpcSynchronize(Sync)) InterlockedIncrement(&Context->Leaders[Phase]);
        for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
        {
            if (Context->Values[Index] != (Token ^ (Phase << 8) ^ Index))
                InterlockedIncrement(&Context->BadData);
        }
        KeSignalCallDpcSynchronize(Sync);
    }
    KeSignalCallDpcDone(Done);
}

static VOID NTAPI
BroadcastThread(PVOID Parameter)
{
    GENERIC_DPC_CONTEXT *Context = Parameter;
    KIRQL OldIrql, ReturnedIrql, EntryIrql;
    ULONG Phase;

    KeSetSystemAffinityThread((KAFFINITY)1 << Context->Cpu);
    KeWaitForSingleObject(Context->Go, Executive, KernelMode, FALSE, NULL);
    for (Context->Round = 0; Context->Round < GENERIC_DPC_ROUNDS; Context->Round++)
    {
        Context->Calls = 0;
        Context->Mask = 0;
        for (Phase = 0; Phase < GENERIC_DPC_PHASES; Phase++) Context->Leaders[Phase] = 0;
        EntryIrql = (Context->Round & 1) ? APC_LEVEL : PASSIVE_LEVEL;
        KeRaiseIrql(EntryIrql, &OldIrql);
        KeGenericCallDpc(BroadcastDpc, Context);
        ReturnedIrql = KeGetCurrentIrql();
        KeLowerIrql(OldIrql);
        if (ReturnedIrql != EntryIrql || Context->Calls != KeNumberProcessors ||
            (KAFFINITY)Context->Mask != KeQueryActiveProcessors())
        {
            Context->Errors++;
        }
        for (Phase = 0; Phase < GENERIC_DPC_PHASES; Phase++)
            if (Context->Leaders[Phase] != 1) Context->Errors++;
        Context->Completed++;
    }
    KeRevertToUserAffinityThread();
}

START_TEST(KeArm64GenericDpc)
{
    GENERIC_DPC_CONTEXT *Workers;
    PKTHREAD Threads[MAXIMUM_PROCESSORS];
    KEVENT Go;
    ULONG Cpu, Created = 0, Count = KeNumberProcessors;

    Workers = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(*Workers), 'gdDK');
    ok(Workers != NULL, "Generic DPC test allocation failed\n");
    if (!Workers) return;
    RtlZeroMemory(Workers, Count * sizeof(*Workers));
    KeInitializeEvent(&Go, NotificationEvent, FALSE);
    for (Cpu = 0; Cpu < Count; Cpu++)
    {
        Workers[Cpu].Go = &Go;
        Workers[Cpu].Cpu = Cpu;
        Threads[Cpu] = KmtStartThread(BroadcastThread, &Workers[Cpu]);
        if (!Threads[Cpu]) break;
        Created++;
    }
    KeSetEvent(&Go, IO_NO_INCREMENT, FALSE);
    for (Cpu = 0; Cpu < Created; Cpu++)
    {
        KmtFinishThread(Threads[Cpu], NULL);
        ok_eq_ulong(Workers[Cpu].Completed, GENERIC_DPC_ROUNDS);
        ok_eq_ulong(Workers[Cpu].Errors, 0);
        ok_eq_long(Workers[Cpu].BadIrql, 0);
        ok_eq_long(Workers[Cpu].BadData, 0);
    }
    KeFlushQueuedDpcs();
    ExFreePoolWithTag(Workers, 'gdDK');
}
