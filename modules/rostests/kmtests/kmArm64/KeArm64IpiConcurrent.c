/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Concurrent ARM64 IPI broadcasts with masked and enabled IRQs
 */

#include <kmt_test.h>

VOID Test_KeArm64IpiConcurrent(VOID);

#define BROADCAST_ROUNDS 128

typedef struct _CONCURRENT_IPI
{
    PKEVENT Go;
    ULONG Cpu;
    ULONG Token;
    volatile LONG Calls;
    volatile LONG BadCallbackIrql;
    volatile LONG64 Mask;
    ULONG Completed;
    ULONG Errors;
} CONCURRENT_IPI;

static ULONG_PTR NTAPI
ConcurrentBroadcast(ULONG_PTR Argument)
{
    CONCURRENT_IPI *State = (CONCURRENT_IPI *)Argument;
    ULONG Cpu = KeGetCurrentProcessorNumber();

    if (KeGetCurrentIrql() != IPI_LEVEL) InterlockedIncrement(&State->BadCallbackIrql);
    InterlockedIncrement(&State->Calls);
    InterlockedOr64(&State->Mask, (LONG64)((ULONG64)1 << Cpu));
    return Argument ^ State->Token ^ Cpu;
}

static VOID NTAPI
BroadcastThread(PVOID Context)
{
    CONCURRENT_IPI *State = Context;
    KAFFINITY Active = KeQueryActiveProcessors();
    ULONG Round;
    KIRQL OldIrql, EntryIrql, ReturnedIrql;
    ULONG64 SavedDaif, ReturnedDaif, ExpectedDaif;
    ULONG_PTR Result;

    KeSetSystemAffinityThread((KAFFINITY)1 << State->Cpu);
    KeWaitForSingleObject(State->Go, Executive, KernelMode, FALSE, NULL);
    for (Round = 0; Round < BROADCAST_ROUNDS; Round++)
    {
        State->Token = (State->Cpu << 16) | Round;
        State->Calls = 0;
        State->Mask = 0;
        EntryIrql = (Round & 1) ? CLOCK_LEVEL : DISPATCH_LEVEL;
        KeRaiseIrql(EntryIrql, &OldIrql);
        __asm__ __volatile__("mrs %0, daif" : "=r"(SavedDaif));
        if (Round & 2) __asm__ __volatile__("msr daifset, #2" ::: "memory");
        ExpectedDaif = SavedDaif | ((Round & 2) ? 0x80 : 0);
        Result = KeIpiGenericCall(ConcurrentBroadcast, (ULONG_PTR)State);
        ReturnedIrql = KeGetCurrentIrql();
        __asm__ __volatile__("mrs %0, daif" : "=r"(ReturnedDaif));
        __asm__ __volatile__("msr daif, %0" :: "r"(SavedDaif) : "memory");
        KeLowerIrql(OldIrql);
        if (Result != ((ULONG_PTR)State ^ State->Token ^ State->Cpu) ||
            ReturnedIrql != EntryIrql || ReturnedDaif != ExpectedDaif ||
            State->Calls != KeNumberProcessors || (KAFFINITY)State->Mask != Active)
        {
            State->Errors++;
        }
        State->Completed++;
    }
    KeRevertToUserAffinityThread();
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(KeArm64IpiConcurrent)
{
    CONCURRENT_IPI Workers[MAXIMUM_PROCESSORS] = {0};
    HANDLE Threads[MAXIMUM_PROCESSORS];
    OBJECT_ATTRIBUTES Attributes;
    KEVENT Go;
    ULONG Cpu, Created = 0;
    NTSTATUS Status;

    if (skip(KeNumberProcessors >= 2, "SMP required\n")) return;
    KeInitializeEvent(&Go, NotificationEvent, FALSE);
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
    {
        Workers[Cpu].Go = &Go;
        Workers[Cpu].Cpu = Cpu;
        Status = PsCreateSystemThread(&Threads[Cpu], SYNCHRONIZE, &Attributes,
                                     NULL, NULL, BroadcastThread, &Workers[Cpu]);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) break;
        Created++;
    }
    KeSetEvent(&Go, IO_NO_INCREMENT, FALSE);
    for (Cpu = 0; Cpu < Created; Cpu++)
    {
        Status = ZwWaitForSingleObject(Threads[Cpu], FALSE, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Threads[Cpu]);
        ok_eq_ulong(Workers[Cpu].Completed, BROADCAST_ROUNDS);
        ok_eq_ulong(Workers[Cpu].Errors, 0);
        ok_eq_long(Workers[Cpu].BadCallbackIrql, 0);
    }
}
