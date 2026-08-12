/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Executive address push-lock behavior and wake-race stress
 */

#include <kmt_test.h>

#ifdef _M_ARM64

#define ADDRESS_PUSH_LOCK_MICRO_ROUNDS 8192
#define ADDRESS_PUSH_LOCK_AFFINITY_ROUNDS 8192
#define ADDRESS_PUSH_LOCK_RACE_ROUNDS 65536

typedef struct _ADDRESS_PUSH_LOCK_CONTEXT
{
    EX_PUSH_LOCK PushLock;
    KEVENT ReadyEvent;
    KEVENT DoneEvent;
    volatile LONGLONG Address;
    volatile LONG MainProcessor;
    volatile LONG WakerProcessor;
    ULONG Rounds;
    ULONG ForwardRemoteRounds;
    ULONG WaitFailures;
    ULONG WakerMigrations;
    KAFFINITY WakerAffinity;
    KAFFINITY WakerSeedAffinity;
    KAFFINITY WakerProcessorMask;
    BOOLEAN UseAddressWait;
} ADDRESS_PUSH_LOCK_CONTEXT, *PADDRESS_PUSH_LOCK_CONTEXT;

static
VOID
NTAPI
AddressPushLockWakeThread(
    _In_ PVOID Parameter)
{
    PADDRESS_PUSH_LOCK_CONTEXT Context = Parameter;
    KAFFINITY OldAffinity = 0;
    NTSTATUS Status;
    ULONG PreviousProcessor = MAXULONG, Processor, Round;

    if (Context->WakerSeedAffinity)
    {
        OldAffinity = KeSetSystemAffinityThreadEx(Context->WakerSeedAffinity);
        KeRevertToUserAffinityThreadEx(OldAffinity);
        OldAffinity = 0;
    }
    if (Context->WakerAffinity) OldAffinity = KeSetSystemAffinityThreadEx(Context->WakerAffinity);
    for (Round = 0; Round < Context->Rounds; Round++)
    {
        Status = KeWaitForSingleObject(&Context->ReadyEvent, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(Status))
        {
            Context->WaitFailures++;
            break;
        }

        Processor = KeGetCurrentProcessorNumber();
        if ((ULONG)Context->MainProcessor != Processor) Context->ForwardRemoteRounds++;
        if ((PreviousProcessor != MAXULONG) && (PreviousProcessor != Processor)) Context->WakerMigrations++;
        PreviousProcessor = Processor;
        if (Processor < sizeof(KAFFINITY) * CHAR_BIT) Context->WakerProcessorMask |= (KAFFINITY)1 << Processor;
        if (Context->UseAddressWait)
        {
            InterlockedExchange64(&Context->Address, 1);
            KeMemoryBarrier();
            if (Context->PushLock.Value) ExfUnblockPushLock(&Context->PushLock, NULL);
        }
        InterlockedExchange(&Context->WakerProcessor, Processor);
        KeSetEvent(&Context->DoneEvent, IO_NO_INCREMENT, FALSE);
    }
    if (Context->WakerAffinity) KeRevertToUserAffinityThreadEx(OldAffinity);
}

static
VOID
TestAddressPushLockImmediateBehavior(VOID)
{
    static const SIZE_T AddressSizes[] = { sizeof(UCHAR), sizeof(USHORT), sizeof(ULONG), sizeof(ULONGLONG) };
    EX_PUSH_LOCK PushLock;
    LARGE_INTEGER ZeroTimeout;
    ULONGLONG Address, Compare;
    NTSTATUS Status;
    ULONG i;

    Address = 0x0102030405060708ULL;
    Compare = 0x1112131415161718ULL;
    for (i = 0; i < RTL_NUMBER_OF(AddressSizes); i++)
    {
        PushLock.Value = 0;
        Status = ExBlockOnAddressPushLock(&PushLock, &Address, &Compare, AddressSizes[i], NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(PushLock.Ptr, NULL);
    }

    PushLock.Value = 0;
    Status = ExBlockOnAddressPushLock(&PushLock, &Address, &Compare, 3, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(PushLock.Ptr, NULL);

    Compare = Address;
    ZeroTimeout.QuadPart = 0;
    PushLock.Value = 0;
    Status = ExBlockOnAddressPushLock(&PushLock, &Address, &Compare, sizeof(Address), &ZeroTimeout);
    ok_eq_hex(Status, STATUS_TIMEOUT);
    ok_eq_pointer(PushLock.Ptr, NULL);
}

static
VOID
TestAddressPushLockMicrobenchmarks(VOID)
{
    EX_PUSH_LOCK PushLock;
    LARGE_INTEGER EndTime, Frequency, StartTime, ZeroTimeout;
    ULONGLONG Address, Compare, MismatchMicroseconds, TimeoutMicroseconds;
    NTSTATUS Status;
    ULONG Failures = 0, Round;

    Address = 1;
    Compare = 0;
    StartTime = KeQueryPerformanceCounter(&Frequency);
    for (Round = 0; Round < ADDRESS_PUSH_LOCK_MICRO_ROUNDS; Round++)
    {
        PushLock.Value = 0;
        Status = ExBlockOnAddressPushLock(&PushLock, &Address, &Compare, sizeof(Address), NULL);
        if (!NT_SUCCESS(Status) || PushLock.Value) Failures++;
    }
    EndTime = KeQueryPerformanceCounter(NULL);
    MismatchMicroseconds = Frequency.QuadPart > 0 ? (ULONGLONG)(EndTime.QuadPart - StartTime.QuadPart) * 1000000 / Frequency.QuadPart : 0;

    Address = 1;
    Compare = 1;
    ZeroTimeout.QuadPart = 0;
    StartTime = KeQueryPerformanceCounter(NULL);
    for (Round = 0; Round < ADDRESS_PUSH_LOCK_MICRO_ROUNDS; Round++)
    {
        PushLock.Value = 0;
        Status = ExBlockOnAddressPushLock(&PushLock, &Address, &Compare, sizeof(Address), &ZeroTimeout);
        if (Status != STATUS_TIMEOUT || PushLock.Value) Failures++;
    }
    EndTime = KeQueryPerformanceCounter(NULL);
    TimeoutMicroseconds = Frequency.QuadPart > 0 ? (ULONGLONG)(EndTime.QuadPart - StartTime.QuadPart) * 1000000 / Frequency.QuadPart : 0;

    trace("address push-lock micro: rounds=%lu mismatch-us=%I64u zero-timeout-us=%I64u\n", ADDRESS_PUSH_LOCK_MICRO_ROUNDS, MismatchMicroseconds, TimeoutMicroseconds);
    ok_eq_ulong(Failures, 0);
}

static
ULONGLONG
TestAddressPushLockRoundTrips(
    _In_ BOOLEAN UseAddressWait,
    _In_ KAFFINITY MainAffinity,
    _In_ KAFFINITY WakerAffinity,
    _In_ KAFFINITY WakerSeedAffinity,
    _In_ ULONG Rounds,
    _In_z_ PCSTR Label)
{
    ADDRESS_PUSH_LOCK_CONTEXT Context;
    LARGE_INTEGER Frequency, StartTime, EndTime, Timeout;
    LONGLONG Compare;
    KAFFINITY MainProcessorMask = 0, OldAffinity = 0;
    PKTHREAD WakerThread;
    NTSTATUS Status;
    ULONG MainMigrations = 0, MainProcessor, PreviousProcessor = MAXULONG, ReturnRemoteRounds = 0, Round, StatusFailures = 0, Timeouts = 0;
    ULONGLONG ElapsedMicroseconds;

    RtlZeroMemory(&Context, sizeof(Context));
    KeInitializeEvent(&Context.ReadyEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.DoneEvent, SynchronizationEvent, FALSE);
    Context.Rounds = Rounds;
    Context.WakerAffinity = WakerAffinity;
    Context.WakerSeedAffinity = WakerSeedAffinity;
    Context.UseAddressWait = UseAddressWait;
    Compare = 0;
    Timeout.QuadPart = -100LL * 10 * 1000;
    WakerThread = KmtStartThread(AddressPushLockWakeThread, &Context);
    if (MainAffinity) OldAffinity = KeSetSystemAffinityThreadEx(MainAffinity);
    StartTime = KeQueryPerformanceCounter(&Frequency);

    for (Round = 0; Round < Context.Rounds; Round++)
    {
        InterlockedExchange64(&Context.Address, 0);
        MainProcessor = KeGetCurrentProcessorNumber();
        if ((PreviousProcessor != MAXULONG) && (PreviousProcessor != MainProcessor)) MainMigrations++;
        PreviousProcessor = MainProcessor;
        if (MainProcessor < sizeof(KAFFINITY) * CHAR_BIT) MainProcessorMask |= (KAFFINITY)1 << MainProcessor;
        InterlockedExchange(&Context.MainProcessor, MainProcessor);
        KeSetEvent(&Context.ReadyEvent, IO_NO_INCREMENT, FALSE);
        if (UseAddressWait)
        {
            Status = ExBlockOnAddressPushLock(&Context.PushLock, &Context.Address, &Compare, sizeof(Context.Address), &Timeout);
            if (Status == STATUS_TIMEOUT) Timeouts++;
            else if (!NT_SUCCESS(Status)) StatusFailures++;
        }
        Status = KeWaitForSingleObject(&Context.DoneEvent, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(Status)) StatusFailures++;
        MainProcessor = KeGetCurrentProcessorNumber();
        if ((ULONG)Context.WakerProcessor != MainProcessor) ReturnRemoteRounds++;
    }

    KmtFinishThread(WakerThread, NULL);
    EndTime = KeQueryPerformanceCounter(NULL);
    if (MainAffinity) KeRevertToUserAffinityThreadEx(OldAffinity);
    ElapsedMicroseconds = Frequency.QuadPart > 0 ? (ULONGLONG)(EndTime.QuadPart - StartTime.QuadPart) * 1000000 / Frequency.QuadPart : 0;
    trace("address push-lock %s: rounds=%lu elapsed-us=%I64u main-mask=%Ix waker-mask=%Ix forward-remote=%lu return-remote=%lu main-migrations=%lu waker-migrations=%lu\n", Label, Context.Rounds, ElapsedMicroseconds, MainProcessorMask, Context.WakerProcessorMask, Context.ForwardRemoteRounds, ReturnRemoteRounds, MainMigrations, Context.WakerMigrations);
    if (MainAffinity && WakerAffinity)
    {
        ok_eq_ulong(Context.ForwardRemoteRounds, MainAffinity == WakerAffinity ? 0 : Context.Rounds);
        ok_eq_ulong(ReturnRemoteRounds, MainAffinity == WakerAffinity ? 0 : Context.Rounds);
    }
    else
    {
        ok(Context.ForwardRemoteRounds < Context.Rounds / 2, "forward remote wake count %lu of %lu\n", Context.ForwardRemoteRounds, Context.Rounds);
        ok(ReturnRemoteRounds < Context.Rounds / 2, "return remote wake count %lu of %lu\n", ReturnRemoteRounds, Context.Rounds);
    }
    ok_eq_ulong(Context.WaitFailures, 0);
    ok_eq_ulong(StatusFailures, 0);
    ok_eq_ulong(Timeouts, 0);
    ok_eq_longlong(Context.Address, UseAddressWait ? 1 : 0);
    ok_eq_pointer(Context.PushLock.Ptr, NULL);
    return ElapsedMicroseconds;
}

static
VOID
TestAddressPushLockWakeRace(VOID)
{
    KAFFINITY ActiveProcessors, FirstProcessor, SecondProcessor;
    ULONGLONG AddressMicroseconds, BaselineMicroseconds;
    ULONG Processor;

    ActiveProcessors = KeQueryActiveProcessors();
    FirstProcessor = 0;
    SecondProcessor = 0;
    for (Processor = 0; Processor < sizeof(KAFFINITY) * CHAR_BIT; Processor++)
    {
        if (!(ActiveProcessors & ((KAFFINITY)1 << Processor))) continue;
        if (!FirstProcessor) FirstProcessor = (KAFFINITY)1 << Processor;
        else
        {
            SecondProcessor = (KAFFINITY)1 << Processor;
            break;
        }
    }

    if (SecondProcessor)
    {
        TestAddressPushLockRoundTrips(FALSE, FirstProcessor, FirstProcessor, 0, ADDRESS_PUSH_LOCK_AFFINITY_ROUNDS, "event-same-cpu");
        TestAddressPushLockRoundTrips(FALSE, FirstProcessor, SecondProcessor, 0, ADDRESS_PUSH_LOCK_AFFINITY_ROUNDS, "event-cross-cpu");
        TestAddressPushLockRoundTrips(FALSE, FirstProcessor, 0, SecondProcessor, ADDRESS_PUSH_LOCK_AFFINITY_ROUNDS, "event-seeded-remote");
    }

    BaselineMicroseconds = TestAddressPushLockRoundTrips(FALSE, 0, 0, 0, ADDRESS_PUSH_LOCK_RACE_ROUNDS, "event-baseline");
    AddressMicroseconds = TestAddressPushLockRoundTrips(TRUE, 0, 0, 0, ADDRESS_PUSH_LOCK_RACE_ROUNDS, "race");
    trace("address push-lock excess: baseline-us=%I64u address-us=%I64u excess-us=%I64u\n", BaselineMicroseconds, AddressMicroseconds, AddressMicroseconds > BaselineMicroseconds ? AddressMicroseconds - BaselineMicroseconds : 0);
}

#endif

START_TEST(ExPushLock)
{
#ifdef _M_ARM64
    TestAddressPushLockImmediateBehavior();
    TestAddressPushLockMicrobenchmarks();
    TestAddressPushLockWakeRace();
#else
    skip(TRUE, "ExBlockOnAddressPushLock is currently exposed for ARM64\n");
#endif
}
