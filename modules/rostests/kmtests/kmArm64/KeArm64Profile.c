/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 profile affinity, SMP sample accounting and source lifetime
 */

#include <kmt_test.h>

VOID Test_KeArm64Profile(VOID);

#define SAMPLE_PC 0x100000
#define SAMPLE_COUNT 100000

static HANDLE
CreateTestProfile(KMT_PROFILE_PARAMETERS *Parameters)
{
    PKMT_RESPONSE Response;
    HANDLE Handle = NULL;
    NTSTATUS Status;

    Response = KmtUserModeCallback(CreateProfile, Parameters);
    ok(Response != NULL, "Profile creation callback failed\n");
    if (!Response) return NULL;
    Status = Response->Profile.Status;
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) Handle = Response->Profile.Handle;
    KmtFreeCallbackResponse(Response);
    return Handle;
}

static BOOLEAN
StartProfile(HANDLE Handle)
{
    NTSTATUS Status = ZwStartProfile(Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status);
}

static VOID
StopProfile(HANDLE Handle)
{
    NTSTATUS Status = ZwStopProfile(Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static VOID
InjectSample(ULONG_PTR Pc)
{
    KTRAP_FRAME Frame = {0};
    KIRQL OldIrql;

    Frame.Pc = Pc;
    KeRaiseIrql(PROFILE_LEVEL, &OldIrql);
    KeProfileInterruptWithSource(&Frame, ProfileAlignmentFixup);
    KeLowerIrql(OldIrql);
}

static VOID
TestAffinityAndRange(KMT_PROFILE_PARAMETERS *Parameters)
{
    HANDLE Profile;
    ULONG Cpu, Target;

    for (Target = 0; Target < (ULONG)KeNumberProcessors; Target++)
    {
        Parameters->Affinity = (KAFFINITY)1 << Target;
        Parameters->Buffer[0] = 0;
        Parameters->Buffer[1] = 0xabcdef01;
        Profile = CreateTestProfile(Parameters);
        if (!Profile) continue;
        if (StartProfile(Profile))
        {
            for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
            {
                KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
                InjectSample(SAMPLE_PC);
            }
            KeSetSystemAffinityThread((KAFFINITY)1 << Target);
            InjectSample(SAMPLE_PC - 4);
            InjectSample(SAMPLE_PC + 64);
            KeRevertToUserAffinityThread();
            StopProfile(Profile);
            ok_eq_ulong(Parameters->Buffer[0], 1);
            ok_eq_hex(Parameters->Buffer[1], 0xabcdef01);
        }
        ZwClose(Profile);
    }
}

typedef struct _PROFILE_WORKERS
{
    KEVENT Go;
    volatile LONG Arrived;
    volatile LONG Timeouts;
    ULONG Count;
} PROFILE_WORKERS;

typedef struct _PROFILE_WORKER
{
    PROFILE_WORKERS *Shared;
    ULONG Cpu;
} PROFILE_WORKER;

static VOID NTAPI
SampleWorker(PVOID Context)
{
    PROFILE_WORKER *Worker = Context;
    KTRAP_FRAME Frame = {0};
    KIRQL OldIrql;
    LARGE_INTEGER Delay;
    ULONG Index, Attempts = 0;

    Frame.Pc = SAMPLE_PC;
    KeSetSystemAffinityThread((KAFFINITY)1 << Worker->Cpu);
    KeWaitForSingleObject(&Worker->Shared->Go, Executive, KernelMode, FALSE, NULL);
    InterlockedIncrement(&Worker->Shared->Arrived);
    Delay.QuadPart = -10000;
    while ((ULONG)InterlockedCompareExchange(&Worker->Shared->Arrived, 0, 0) < Worker->Shared->Count)
    {
        if (++Attempts == 2000)
        {
            InterlockedIncrement(&Worker->Shared->Timeouts);
            PsTerminateSystemThread(STATUS_TIMEOUT);
        }
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    KeRaiseIrql(PROFILE_LEVEL, &OldIrql);
    for (Index = 0; Index < SAMPLE_COUNT; Index++)
        KeProfileInterruptWithSource(&Frame, ProfileAlignmentFixup);
    KeLowerIrql(OldIrql);
    KeRevertToUserAffinityThread();
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID
TestConcurrentSamples(KMT_PROFILE_PARAMETERS *Parameters)
{
    PROFILE_WORKERS Shared;
    PROFILE_WORKER Workers[MAXIMUM_PROCESSORS];
    HANDLE Threads[MAXIMUM_PROCESSORS], Profile;
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;
    ULONG Cpu;

    Parameters->Affinity = KeQueryActiveProcessors();
    Parameters->Buffer[0] = 0;
    Profile = CreateTestProfile(Parameters);
    if (!Profile) return;
    if (!StartProfile(Profile))
    {
        ZwClose(Profile);
        return;
    }
    KeInitializeEvent(&Shared.Go, NotificationEvent, FALSE);
    Shared.Arrived = 0;
    Shared.Timeouts = 0;
    Shared.Count = 0;
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
    {
        Workers[Cpu].Shared = &Shared;
        Workers[Cpu].Cpu = Cpu;
        Status = PsCreateSystemThread(&Threads[Cpu], SYNCHRONIZE, &Attributes,
                                     NtCurrentProcess(), NULL, SampleWorker, &Workers[Cpu]);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) break;
        Shared.Count++;
    }
    KeSetEvent(&Shared.Go, IO_NO_INCREMENT, FALSE);
    for (Cpu = 0; Cpu < Shared.Count; Cpu++)
    {
        Status = ZwWaitForSingleObject(Threads[Cpu], FALSE, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Threads[Cpu]);
    }
    StopProfile(Profile);
    ok_eq_long(Shared.Timeouts, 0);
    trace("PROFILE_SMP expected=%lu observed=%lu\n", Shared.Count * SAMPLE_COUNT, Parameters->Buffer[0]);
    ok_eq_ulong(Parameters->Buffer[0], Shared.Count * SAMPLE_COUNT);
    ZwClose(Profile);
}

static DECLSPEC_NOINLINE VOID
BusySampleWindow(VOID)
{
    ULONG64 Begin, Now, Frequency;

    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(Frequency));
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(Begin));
    do
    {
        __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(Now));
    } while (Now - Begin < Frequency / 5);
}

typedef struct _PROFILE_RUNDOWN
{
    volatile LONG Run;
    volatile LONG Calls;
    LONGLONG Deadline;
} PROFILE_RUNDOWN;

static VOID NTAPI
RundownWorker(PVOID Context)
{
    PROFILE_RUNDOWN *State = Context;
    KIRQL OldIrql;

    KeSetSystemAffinityThread(2);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    while (InterlockedCompareExchange(&State->Run, 0, 0) &&
           KeQueryPerformanceCounter(NULL).QuadPart < State->Deadline)
    {
        InjectSample(SAMPLE_PC);
        InterlockedIncrement(&State->Calls);
    }
    KeLowerIrql(OldIrql);
    KeRevertToUserAffinityThread();
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static BOOLEAN
WaitForSamples(PROFILE_RUNDOWN *State)
{
    LONG Begin = InterlockedCompareExchange(&State->Calls, 0, 0);

    while (InterlockedCompareExchange(&State->Calls, 0, 0) - Begin < 64)
    {
        if (KeQueryPerformanceCounter(NULL).QuadPart >= State->Deadline)
            return FALSE;
        YieldProcessor();
    }
    return TRUE;
}

static VOID
TestSampleRundown(KMT_PROFILE_PARAMETERS *Parameters)
{
    PROFILE_RUNDOWN State;
    OBJECT_ATTRIBUTES Attributes;
    LARGE_INTEGER Frequency;
    HANDLE Thread, Profile;
    NTSTATUS Status;
    ULONG Round, Count;
    BOOLEAN Advanced;

    if (KeNumberProcessors < 2) return;
    Parameters->Affinity = 2;
    State.Run = 1;
    State.Calls = 0;
    State.Deadline = KeQueryPerformanceCounter(&Frequency).QuadPart;
    State.Deadline += 5 * Frequency.QuadPart;
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    KeSetSystemAffinityThread(1);
    Status = PsCreateSystemThread(&Thread, SYNCHRONIZE, &Attributes,
                                 NtCurrentProcess(), NULL, RundownWorker, &State);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        for (Round = 0; Round < 32; Round++)
        {
            Parameters->Buffer[0] = 0;
            Profile = CreateTestProfile(Parameters);
            if (!Profile) break;
            if (StartProfile(Profile))
            {
                Advanced = WaitForSamples(&State);
                ok(Advanced, "Sampling worker timed out before stop in round %lu\n", Round);
                if (!(Round & 1)) StopProfile(Profile);
                ZwClose(Profile);
                Profile = NULL;
                Count = Parameters->Buffer[0];
                Advanced = WaitForSamples(&State);
                ok(Advanced, "Sampling worker timed out after stop in round %lu\n", Round);
                ok(Count > 0, "Profile recorded no samples in round %lu\n", Round);
                ok_eq_ulong(Parameters->Buffer[0], Count);
            }
            if (Profile) ZwClose(Profile);
        }
        InterlockedExchange(&State.Run, 0);
        Status = ZwWaitForSingleObject(Thread, FALSE, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Thread);
    }
    KeRevertToUserAffinityThread();
}

static VOID
TestSharedSource(KMT_PROFILE_PARAMETERS *Parameters)
{
    HANDLE First, Second;
    PULONG Buffers = Parameters->Buffer;
    ULONG Before;

    Parameters->RangeBase = (PVOID)((ULONG_PTR)BusySampleWindow & ~((ULONG_PTR)PAGE_SIZE - 1));
    Parameters->RangeSize = PAGE_SIZE;
    Parameters->BucketSize = PAGE_SHIFT;
    Parameters->Source = ProfileTime;
    Parameters->Affinity = KeQueryActiveProcessors();
    Buffers[0] = Buffers[2] = 0;
    First = CreateTestProfile(Parameters);
    Parameters->Buffer = Buffers + 2;
    Second = CreateTestProfile(Parameters);
    if (First && Second && StartProfile(First) && StartProfile(Second))
    {
        KeSetSystemAffinityThread(1);
        BusySampleWindow();
        ok(Buffers[0] > 0 && Buffers[2] > 0, "No timer samples: %lu/%lu\n", Buffers[0], Buffers[2]);
        StopProfile(First);
        Before = Buffers[2];
        BusySampleWindow();
        trace("PROFILE_SHARED stop before=%lu after=%lu\n", Before, Buffers[2]);
        ok(Buffers[2] > Before, "Stopping one profile disabled the remaining profile\n");
        if (StartProfile(First))
        {
            ZwClose(First);
            First = NULL;
            Before = Buffers[2];
            BusySampleWindow();
            trace("PROFILE_SHARED close before=%lu after=%lu\n", Before, Buffers[2]);
            ok(Buffers[2] > Before, "Closing one profile disabled the remaining profile\n");
        }
        StopProfile(Second);
        KeRevertToUserAffinityThread();
    }
    if (First) ZwClose(First);
    if (Second) ZwClose(Second);
}

START_TEST(KeArm64Profile)
{
    PVOID Allocation = NULL;
    SIZE_T Size = PAGE_SIZE;
    KMT_PROFILE_PARAMETERS *Parameters;
    NTSTATUS Status;

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &Allocation, 0, &Size,
                                     MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Parameters = Allocation;
    Parameters->RangeBase = (PVOID)SAMPLE_PC;
    Parameters->RangeSize = 64;
    Parameters->BucketSize = 6;
    Parameters->Buffer = (PULONG)(Parameters + 1);
    Parameters->BufferSize = 2 * sizeof(ULONG);
    Parameters->Source = ProfileAlignmentFixup;
    DbgPrint("PROFILE_TEST affinity and range\n");
    TestAffinityAndRange(Parameters);
    DbgPrint("PROFILE_TEST concurrent samples\n");
    TestConcurrentSamples(Parameters);
    DbgPrint("PROFILE_TEST sample rundown\n");
    TestSampleRundown(Parameters);
    DbgPrint("PROFILE_TEST shared source\n");
    TestSharedSource(Parameters);
    Size = 0;
    Status = ZwFreeVirtualMemory(NtCurrentProcess(), &Allocation, &Size, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}
