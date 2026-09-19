/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Mutant/Mutex test
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include <kmt_test.h>

static
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
(NTAPI
*pKeAreAllApcsDisabled)(VOID);

#define ULONGS_PER_POINTER (sizeof(PVOID) / sizeof(ULONG))
#define MUTANT_SIZE (2 + 6 * ULONGS_PER_POINTER)

C_ASSERT(sizeof(DISPATCHER_HEADER) == 8 + 2 * sizeof(PVOID));
C_ASSERT(sizeof(KMUTANT) == sizeof(DISPATCHER_HEADER) + 3 * sizeof(PVOID) + sizeof(PVOID));
C_ASSERT(sizeof(KMUTANT) == MUTANT_SIZE * sizeof(ULONG));

/* ETHREAD mutant-list offsets are not stable across NT versions. Check the
 * public mutant state and owner identity instead. */
static BOOLEAN KmtApcBaseline = FALSE;

#define CheckMutex(Mutex, State, New, ExpectedApcDisable) do {                  \
    PKTHREAD Thread = KeGetCurrentThread();                                     \
    ok_eq_uint((Mutex)->Header.Type, MutantObject);                             \
    ok_eq_uint((Mutex)->Header.Abandoned,                                       \
               GetNTVersion() >= _WIN32_WINNT_WIN10 ? 0 : 0x55);                \
    ok_eq_uint((Mutex)->Header.Size,                                            \
               GetNTVersion() >= _WIN32_WINNT_WIN10 ? 0 : MUTANT_SIZE);         \
    /* NT 6.1+ KeInitializeMutant zeros Header.DpcActive; NT 5.x leaves the   \
     * 0x55 pre-init fill pattern. */                                          \
    ok_eq_uint((Mutex)->Header.DpcActive,                                       \
               GetNTVersion() >= _WIN32_WINNT_WIN7 ? 0 : 0x55);                 \
    ok_eq_pointer((Mutex)->Header.WaitListHead.Flink,                           \
                  &(Mutex)->Header.WaitListHead);                               \
    ok_eq_pointer((Mutex)->Header.WaitListHead.Blink,                           \
                  &(Mutex)->Header.WaitListHead);                               \
    if ((State) <= 0)                                                           \
    {                                                                           \
        ok_eq_long((Mutex)->Header.SignalState, State);                         \
        ok_eq_pointer((Mutex)->OwnerThread, Thread);                            \
    }                                                                           \
    else                                                                        \
    {                                                                           \
        ok_eq_long((Mutex)->Header.SignalState, State);                         \
        if (New)                                                                \
        {                                                                       \
            ok_eq_pointer((Mutex)->MutantListEntry.Flink,                       \
                          GetNTVersion() >= _WIN32_WINNT_WIN10 ?                \
                          NULL : (PVOID)0x5555555555555555ULL);                 \
            ok_eq_pointer((Mutex)->MutantListEntry.Blink,                       \
                          GetNTVersion() >= _WIN32_WINNT_WIN10 ?                \
                          NULL : (PVOID)0x5555555555555555ULL);                 \
        }                                                                       \
        ok_eq_pointer((Mutex)->OwnerThread, NULL);                              \
    }                                                                           \
    ok_eq_uint((Mutex)->Abandoned, 0);                                          \
    ok_eq_uint((Mutex)->ApcDisable, ExpectedApcDisable);                        \
    UNREFERENCED_PARAMETER(Thread);                                             \
} while (0)

/* Same ETHREAD-layout caveat as KeApc.c: drive only public APIs.
 * Treat the {Kernel,Special}ApcsDisabled parameters as the signed counter
 * value; KeAreApcsDisabled() is TRUE when either counter is non-zero. */
#define CheckApcs(KernelApcsDisabled, SpecialApcsDisabled, AllApcsDisabled, Irql) do    \
{                                                                                       \
    ok_eq_bool(KeAreApcsDisabled(), KmtApcBaseline ||                                  \
                                    (LONG)(KernelApcsDisabled) != 0 ||                  \
                                    (LONG)(SpecialApcsDisabled) != 0 ||                 \
                                    ((Irql) >= APC_LEVEL));                             \
    if (pKeAreAllApcsDisabled)                                                          \
        ok_eq_bool(pKeAreAllApcsDisabled(),                                             \
                   (AllApcsDisabled) || ((Irql) >= APC_LEVEL));                         \
    ok_irql(Irql);                                                                      \
    UNREFERENCED_PARAMETER(Thread);                                                     \
} while (0)

static
VOID
TestMutant(VOID)
{
    NTSTATUS Status;
    KMUTANT Mutant;
    LONG State;
    LONG i;
    PKTHREAD Thread = KeGetCurrentThread();

    KmtApcBaseline = KeAreApcsDisabled();
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
    RtlFillMemory(&Mutant, sizeof(Mutant), 0x55);
    KeInitializeMutant(&Mutant, FALSE);
    CheckMutex(&Mutant, 1L, TRUE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    RtlFillMemory(&Mutant, sizeof(Mutant), 0x55);
    KeInitializeMutant(&Mutant, TRUE);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
    CheckMutex(&Mutant, 0L, TRUE, 0);
    State = KeReleaseMutant(&Mutant, 1, FALSE, FALSE);
    ok_eq_long(State, 0L);
    CheckMutex(&Mutant, 1L, FALSE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* Acquire and release */
    Status = KeWaitForSingleObject(&Mutant,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckMutex(&Mutant, 0L, TRUE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    State = KeReleaseMutant(&Mutant, 1, FALSE, FALSE);
    ok_eq_long(State, 0L);
    CheckMutex(&Mutant, 1L, FALSE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* Acquire recursively */
    for (i = 0; i < 8; i++)
    {
        KmtStartSeh()
            Status = KeWaitForSingleObject(&Mutant,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           NULL);
        KmtEndSeh(STATUS_SUCCESS);
        ok_eq_hex(Status, STATUS_SUCCESS);
        CheckMutex(&Mutant, -i, FALSE, 0);
        CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
    }

    for (i = 0; i < 7; i++)
    {
        KmtStartSeh()
            State = KeReleaseMutant(&Mutant, 1, FALSE, FALSE);
        KmtEndSeh(STATUS_SUCCESS);
        ok_eq_long(State, -7L + i);
        CheckMutex(&Mutant, -6L + i, FALSE, 0);
        CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
    }

    State = KeReleaseMutant(&Mutant, 1, FALSE, FALSE);
    ok_eq_long(State, 0L);
    CheckMutex(&Mutant, 1L, FALSE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* Pretend to acquire it recursively -MINLONG times */
    KmtStartSeh()
        Status = KeWaitForSingleObject(&Mutant,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
    KmtEndSeh(STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckMutex(&Mutant, 0L, FALSE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    Mutant.Header.SignalState = MINLONG + 1;
    KmtStartSeh()
        Status = KeWaitForSingleObject(&Mutant,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
    KmtEndSeh(STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckMutex(&Mutant, (LONG)MINLONG, FALSE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    KmtStartSeh()
        KeWaitForSingleObject(&Mutant,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    KmtEndSeh(STATUS_MUTANT_LIMIT_EXCEEDED);
    CheckMutex(&Mutant, (LONG)MINLONG, FALSE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    State = KeReleaseMutant(&Mutant, 1, FALSE, FALSE);
    ok_eq_long(State, (LONG)MINLONG);
    CheckMutex(&Mutant, (LONG)MINLONG + 1L, FALSE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    Mutant.Header.SignalState = -1;
    State = KeReleaseMutant(&Mutant, 1, FALSE, FALSE);
    ok_eq_long(State, -1L);
    CheckMutex(&Mutant, 0L, FALSE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    State = KeReleaseMutant(&Mutant, 1, FALSE, FALSE);
    ok_eq_long(State, 0L);
    CheckMutex(&Mutant, 1L, FALSE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* Now release it once too often */
    KmtStartSeh()
        KeReleaseMutant(&Mutant, 1, FALSE, FALSE);
    KmtEndSeh(STATUS_MUTANT_NOT_OWNED);
    CheckMutex(&Mutant, 1L, FALSE, 0);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
}

static
VOID
TestMutex(VOID)
{
    NTSTATUS Status;
    KMUTEX Mutex;
    LONG State;
    LONG i;
    PKTHREAD Thread = KeGetCurrentThread();

    KmtApcBaseline = KeAreApcsDisabled();
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
    RtlFillMemory(&Mutex, sizeof(Mutex), 0x55);
    KeInitializeMutex(&Mutex, 0);
    CheckMutex(&Mutex, 1L, TRUE, 1);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    RtlFillMemory(&Mutex, sizeof(Mutex), 0x55);
    KeInitializeMutex(&Mutex, 123);
    CheckMutex(&Mutex, 1L, TRUE, 1);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* Acquire and release */
    Status = KeWaitForSingleObject(&Mutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckMutex(&Mutex, 0L, FALSE, 1);
    CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);

    State = KeReleaseMutex(&Mutex, FALSE);
    ok_eq_long(State, 0L);
    CheckMutex(&Mutex, 1L, FALSE, 1);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* Acquire recursively */
    for (i = 0; i < 8; i++)
    {
        KmtStartSeh()
            Status = KeWaitForSingleObject(&Mutex,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           NULL);
        KmtEndSeh(STATUS_SUCCESS);
        ok_eq_hex(Status, STATUS_SUCCESS);
        CheckMutex(&Mutex, -i, FALSE, 1);
        CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);
    }

    for (i = 0; i < 7; i++)
    {
        KmtStartSeh()
            State = KeReleaseMutex(&Mutex, FALSE);
        KmtEndSeh(STATUS_SUCCESS);
        ok_eq_long(State, -7L + i);
        CheckMutex(&Mutex, -6L + i, FALSE, 1);
        CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);
    }

    State = KeReleaseMutex(&Mutex, FALSE);
    ok_eq_long(State, 0L);
    CheckMutex(&Mutex, 1L, FALSE, 1);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* Pretend to acquire it recursively -MINLONG times */
    KmtStartSeh()
        Status = KeWaitForSingleObject(&Mutex,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
    KmtEndSeh(STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckMutex(&Mutex, 0L, FALSE, 1);
    CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);

    Mutex.Header.SignalState = MINLONG + 1;
    KmtStartSeh()
        Status = KeWaitForSingleObject(&Mutex,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
    KmtEndSeh(STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckMutex(&Mutex, (LONG)MINLONG, FALSE, 1);
    CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);

    KmtStartSeh()
        KeWaitForSingleObject(&Mutex,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    KmtEndSeh(STATUS_MUTANT_LIMIT_EXCEEDED);
    CheckMutex(&Mutex, (LONG)MINLONG, FALSE, 1);
    CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);

    State = KeReleaseMutex(&Mutex, FALSE);
    ok_eq_long(State, (LONG)MINLONG);
    CheckMutex(&Mutex, (LONG)MINLONG + 1L, FALSE, 1);
    CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);

    Mutex.Header.SignalState = -1;
    State = KeReleaseMutex(&Mutex, FALSE);
    ok_eq_long(State, -1L);
    CheckMutex(&Mutex, 0L, FALSE, 1);
    CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);

    State = KeReleaseMutex(&Mutex, FALSE);
    ok_eq_long(State, 0L);
    CheckMutex(&Mutex, 1L, FALSE, 1);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* Now release it once too often */
    KmtStartSeh()
        KeReleaseMutex(&Mutex, FALSE);
    KmtEndSeh(STATUS_MUTANT_NOT_OWNED);
    CheckMutex(&Mutex, 1L, FALSE, 1);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
}

#define MUTEX_CONTENTION_WORKERS 4
#define MUTEX_CONTENTION_ROUNDS 4000

typedef struct _MUTEX_CONTENTION
{
    KMUTEX Mutex;
    KEVENT Start;
    volatile LONG Inside, Stop, WorkerIndex, Completed;
    volatile LONG WaitErrors, OwnerErrors, OverlapErrors, RecursiveErrors;
} MUTEX_CONTENTION;

static VOID NTAPI
MutexContentionWorker(PVOID Parameter)
{
    MUTEX_CONTENTION *Test = Parameter;
    LONG Worker = InterlockedIncrement(&Test->WorkerIndex) - 1;
    ULONG Round;
    NTSTATUS Status;
    LARGE_INTEGER Delay;
    PKTHREAD Current = KeGetCurrentThread();

    Delay.QuadPart = -10000;
    KeWaitForSingleObject(&Test->Start, Executive, KernelMode, FALSE, NULL);
    for (Round = 0; Round < MUTEX_CONTENTION_ROUNDS && !Test->Stop; ++Round)
    {
        /* Follow timed waits with contended, untimed mutex waits. */
        if (((Round + Worker) & 7) == 0)
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        Status = KeWaitForSingleObject(&Test->Mutex, Executive, KernelMode, FALSE, NULL);
        if (Status != STATUS_SUCCESS)
        {
            trace("Mutex contention worker %ld round %lu wait %lx owner %p current %p state %ld\n",
                  Worker, Round, Status, Test->Mutex.OwnerThread, Current, Test->Mutex.Header.SignalState);
            InterlockedIncrement(&Test->WaitErrors);
            InterlockedExchange(&Test->Stop, 1);
            if (Test->Mutex.OwnerThread == Current)
                KeReleaseMutex(&Test->Mutex, FALSE);
            break;
        }
        if (Test->Mutex.OwnerThread != Current || Test->Mutex.Header.SignalState != 0)
        {
            InterlockedIncrement(&Test->OwnerErrors);
            InterlockedExchange(&Test->Stop, 1);
            if (Test->Mutex.OwnerThread == Current)
                KeReleaseMutex(&Test->Mutex, FALSE);
            break;
        }
        if (InterlockedIncrement(&Test->Inside) != 1)
        {
            InterlockedIncrement(&Test->OverlapErrors);
            InterlockedExchange(&Test->Stop, 1);
        }
        else
        {
            Status = KeWaitForSingleObject(&Test->Mutex, Executive, KernelMode, FALSE, NULL);
            if (Status != STATUS_SUCCESS || Test->Mutex.OwnerThread != Current ||
                Test->Mutex.Header.SignalState != -1)
            {
                InterlockedIncrement(&Test->RecursiveErrors);
                InterlockedExchange(&Test->Stop, 1);
            }
            if (Test->Mutex.OwnerThread == Current && Test->Mutex.Header.SignalState < 0)
                KeReleaseMutex(&Test->Mutex, FALSE);
            if (((Round + Worker) & 63) == 0)
                KeDelayExecutionThread(KernelMode, FALSE, &Delay);
            else
                YieldProcessor();
            InterlockedIncrement(&Test->Completed);
        }
        InterlockedDecrement(&Test->Inside);
        if (Test->Mutex.OwnerThread == Current)
            KeReleaseMutex(&Test->Mutex, FALSE);
        else
        {
            InterlockedIncrement(&Test->OwnerErrors);
            InterlockedExchange(&Test->Stop, 1);
        }
    }
}

static VOID
TestMutexContention(VOID)
{
    MUTEX_CONTENTION *Test;
    PKTHREAD Threads[MUTEX_CONTENTION_WORKERS] = {0};
    ULONG Index, Created = 0;

    Test = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Test), 'cMmK');
    if (skip(Test != NULL, "Could not allocate mutex contention state\n"))
        return;
    RtlZeroMemory(Test, sizeof(*Test));
    KeInitializeMutex(&Test->Mutex, 0);
    KeInitializeEvent(&Test->Start, NotificationEvent, FALSE);
    for (Index = 0; Index < ARRAYSIZE(Threads); ++Index)
    {
        Threads[Index] = KmtStartThread(MutexContentionWorker, Test);
        if (Threads[Index] == NULL)
            break;
        ++Created;
    }
    KeSetEvent(&Test->Start, IO_NO_INCREMENT, FALSE);
    for (Index = 0; Index < Created; ++Index)
        KmtFinishThread(Threads[Index], NULL);
    ok_eq_ulong(Created, MUTEX_CONTENTION_WORKERS);
    ok_eq_long(Test->WaitErrors, 0);
    ok_eq_long(Test->OwnerErrors, 0);
    ok_eq_long(Test->OverlapErrors, 0);
    ok_eq_long(Test->RecursiveErrors, 0);
    ok_eq_long(Test->Completed, Created * MUTEX_CONTENTION_ROUNDS);
    ok_eq_long(Test->Inside, 0);
    ok_eq_long(Test->Mutex.Header.SignalState, 1);
    ok_eq_pointer(Test->Mutex.OwnerThread, NULL);
    trace("Mutex contention: %lu workers, %ld completed acquisitions\n", Created, Test->Completed);
    ExFreePoolWithTag(Test, 'cMmK');
}

START_TEST(KeMutex)
{
    pKeAreAllApcsDisabled = KmtGetSystemRoutineAddress(L"KeAreAllApcsDisabled");
    if (skip(pKeAreAllApcsDisabled != NULL, "KeAreAllApcsDisabled unavailable\n"))
    {
        /* We can live without this function here */
    }

    TestMutant();
    TestMutex();
    TestMutexContention();
}
