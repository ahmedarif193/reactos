/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Executive handle-table concurrent reuse stress
 */

#include <kmt_test.h>

#define HANDLE_STRESS_MAX_THREADS 64
#define HANDLE_STRESS_MAX_BATCH 257
#define HANDLE_VALUE_MASK 0x7fffffffUL
#define HANDLE_STRESS_MIN_DRIFT 0x1000UL

typedef enum _HANDLE_CLOSE_ORDER
{
    HandleCloseForward,
    HandleCloseReverse,
    HandleCloseStride
} HANDLE_CLOSE_ORDER;

typedef struct _HANDLE_STRESS_CONFIG
{
    PCSTR Name;
    ULONG Threads;
    ULONG Rounds;
    ULONG Batch;
    HANDLE_CLOSE_ORDER CloseOrder;
    ULONG CloseStride;
} HANDLE_STRESS_CONFIG, *PHANDLE_STRESS_CONFIG;
typedef const HANDLE_STRESS_CONFIG *PCHANDLE_STRESS_CONFIG;

typedef struct _HANDLE_STRESS_CONTEXT
{
    PKEVENT StartEvent;
    PCHANDLE_STRESS_CONFIG Config;
    PVOID Object;
    ULONG Index;
    ULONG Created;
    ULONG CreateFailures;
    ULONG CloseFailures;
    ULONG FirstHalfMaximum;
    ULONG SecondHalfMaximum;
    KAFFINITY ProcessorMask;
    ULONGLONG CreateTicks;
    ULONGLONG CloseTicks;
    ULONG StartProcessor;
    NTSTATUS WaitStatus;
} HANDLE_STRESS_CONTEXT, *PHANDLE_STRESS_CONTEXT;

static const HANDLE_STRESS_CONFIG HandleStressConfigs[] =
{
    { "single-forward", 4, 4096, 1, HandleCloseForward, 0 },
    { "small-reverse", 8, 1024, 15, HandleCloseReverse, 0 },
    { "below-boundary", 16, 384, 63, HandleCloseStride, 17 },
    { "boundary", 16, 384, 64, HandleCloseReverse, 0 },
    { "above-boundary", 16, 384, 65, HandleCloseStride, 17 },
    { "wide-prime", 24, 192, 127, HandleCloseForward, 0 },
    { "deep-prime", 64, 96, 257, HandleCloseStride, 17 },
};

static
ULONG
HandleStressCloseSlot(
    _In_ PHANDLE_STRESS_CONTEXT Context,
    _In_ ULONG Index,
    _In_ ULONG Created)
{
    switch (Context->Config->CloseOrder)
    {
        case HandleCloseReverse:
            return Created - Index - 1;

        case HandleCloseStride:
            return (Index * Context->Config->CloseStride + Context->Index) % Created;

        default:
            return Index;
    }
}

static
VOID
NTAPI
HandleStressThread(
    _In_ PVOID Parameter)
{
    PHANDLE_STRESS_CONTEXT Context = Parameter;
    HANDLE Handles[HANDLE_STRESS_MAX_BATCH];
    LARGE_INTEGER PhaseEnd;
    LARGE_INTEGER PhaseStart;
    NTSTATUS Status;
    ULONG Created, HandleValue, i, Round, Slot;

    Context->WaitStatus = KeWaitForSingleObject(Context->StartEvent, Executive, KernelMode, FALSE, NULL);
    if (!NT_SUCCESS(Context->WaitStatus)) return;
    Context->StartProcessor = KeGetCurrentProcessorNumber();

    for (Round = 0; Round < Context->Config->Rounds; Round++)
    {
        Context->ProcessorMask |= (KAFFINITY)1 << KeGetCurrentProcessorNumber();
        Created = 0;
        PhaseStart = KeQueryPerformanceCounter(NULL);
        for (i = 0; i < Context->Config->Batch; i++)
        {
            Handles[i] = NULL;
            Status = ObOpenObjectByPointer(Context->Object, OBJ_KERNEL_HANDLE, NULL, EVENT_ALL_ACCESS, *ExEventObjectType, KernelMode, &Handles[i]);
            if (!NT_SUCCESS(Status))
            {
                Context->CreateFailures++;
                break;
            }

            HandleValue = (ULONG)(ULONG_PTR)Handles[i] & HANDLE_VALUE_MASK;
            if (Round < Context->Config->Rounds / 2)
            {
                if (HandleValue > Context->FirstHalfMaximum) Context->FirstHalfMaximum = HandleValue;
            }
            else
            {
                if (HandleValue > Context->SecondHalfMaximum) Context->SecondHalfMaximum = HandleValue;
            }
            Created++;
        }
        PhaseEnd = KeQueryPerformanceCounter(NULL);
        Context->CreateTicks += PhaseEnd.QuadPart - PhaseStart.QuadPart;

        PhaseStart = KeQueryPerformanceCounter(NULL);
        for (i = 0; i < Created; i++)
        {
            Slot = HandleStressCloseSlot(Context, i, Created);
            Status = ZwClose(Handles[Slot]);
            if (!NT_SUCCESS(Status)) Context->CloseFailures++;
        }
        PhaseEnd = KeQueryPerformanceCounter(NULL);
        Context->CloseTicks += PhaseEnd.QuadPart - PhaseStart.QuadPart;

        Context->Created += Created;
        ZwYieldExecution();
        Context->ProcessorMask |= (KAFFINITY)1 << KeGetCurrentProcessorNumber();
    }
}

static
VOID
HandleStressRunConfig(
    _In_ PCHANDLE_STRESS_CONFIG Config,
    _In_ PVOID Object,
    _Inout_ PULONG TotalCreated)
{
    HANDLE_STRESS_CONTEXT Contexts[HANDLE_STRESS_MAX_THREADS];
    PKTHREAD Threads[HANDLE_STRESS_MAX_THREADS];
    KEVENT StartEvent;
    LARGE_INTEGER EndTime;
    LARGE_INTEGER Frequency;
    LARGE_INTEGER StartTime;
    ULONG CloseFailures = 0;
    ULONGLONG CloseTicks = 0;
    ULONG CreateFailures = 0;
    ULONG Created = 0;
    ULONGLONG CreateTicks = 0;
    ULONG DriftLimit;
    ULONG FirstHalfMaximum = 0;
    KAFFINITY ProcessorMask = 0;
    ULONG ProcessorStarts[MAXIMUM_PROCESSORS];
    ULONG SecondHalfMaximum = 0;
    ULONG WaitFailures = 0;
    ULONGLONG ElapsedMicroseconds;
    ULONG i;

    KeInitializeEvent(&StartEvent, NotificationEvent, FALSE);
    RtlZeroMemory(Contexts, sizeof(Contexts));
    RtlZeroMemory(ProcessorStarts, sizeof(ProcessorStarts));
    RtlZeroMemory(Threads, sizeof(Threads));

    for (i = 0; i < Config->Threads; i++)
    {
        Contexts[i].StartEvent = &StartEvent;
        Contexts[i].Config = Config;
        Contexts[i].Object = Object;
        Contexts[i].Index = i;
        Threads[i] = KmtStartThread(HandleStressThread, &Contexts[i]);
    }

    StartTime = KeQueryPerformanceCounter(&Frequency);
    KeSetEvent(&StartEvent, IO_NO_INCREMENT, FALSE);
    for (i = 0; i < Config->Threads; i++) KmtFinishThread(Threads[i], NULL);
    EndTime = KeQueryPerformanceCounter(NULL);

    for (i = 0; i < Config->Threads; i++)
    {
        if (!NT_SUCCESS(Contexts[i].WaitStatus)) WaitFailures++;
        Created += Contexts[i].Created;
        CreateFailures += Contexts[i].CreateFailures;
        CloseFailures += Contexts[i].CloseFailures;
        CloseTicks += Contexts[i].CloseTicks;
        CreateTicks += Contexts[i].CreateTicks;
        ProcessorMask |= Contexts[i].ProcessorMask;
        if (Contexts[i].StartProcessor < MAXIMUM_PROCESSORS) ProcessorStarts[Contexts[i].StartProcessor]++;
        if (Contexts[i].FirstHalfMaximum > FirstHalfMaximum) FirstHalfMaximum = Contexts[i].FirstHalfMaximum;
        if (Contexts[i].SecondHalfMaximum > SecondHalfMaximum) SecondHalfMaximum = Contexts[i].SecondHalfMaximum;
    }

    DriftLimit = Config->Threads * Config->Batch * 4;
    if (DriftLimit < HANDLE_STRESS_MIN_DRIFT) DriftLimit = HANDLE_STRESS_MIN_DRIFT;
    ElapsedMicroseconds = Frequency.QuadPart > 0 ? (ULONGLONG)(EndTime.QuadPart - StartTime.QuadPart) * 1000000 / Frequency.QuadPart : 0;
    trace("handle churn %s: processors=%lu threads=%lu rounds=%lu batch=%lu created=%lu elapsed-us=%I64u first-max=%#lx second-max=%#lx\n", Config->Name, KeQueryActiveProcessorCount(NULL), Config->Threads, Config->Rounds, Config->Batch, Created, ElapsedMicroseconds, FirstHalfMaximum, SecondHalfMaximum);
    trace("handle churn %s: processor-mask=%Ix starts=%lu/%lu/%lu/%lu\n", Config->Name, ProcessorMask, ProcessorStarts[0], ProcessorStarts[1], ProcessorStarts[2], ProcessorStarts[3]);
    trace("handle churn %s: summed-create-us=%I64u summed-close-us=%I64u\n", Config->Name, Frequency.QuadPart > 0 ? CreateTicks * 1000000 / Frequency.QuadPart : 0, Frequency.QuadPart > 0 ? CloseTicks * 1000000 / Frequency.QuadPart : 0);
    ok(Frequency.QuadPart > 0, "%s: performance-counter frequency was %I64d\n", Config->Name, Frequency.QuadPart);
    ok(EndTime.QuadPart >= StartTime.QuadPart, "%s: performance counter moved backwards: start=%I64d end=%I64d\n", Config->Name, StartTime.QuadPart, EndTime.QuadPart);
    ok_eq_ulong(WaitFailures, 0);
    ok_eq_ulong(CreateFailures, 0);
    ok_eq_ulong(CloseFailures, 0);
    ok_eq_ulong(Created, Config->Threads * Config->Rounds * Config->Batch);
    ok(FirstHalfMaximum != 0, "%s: first-half maximum handle was not recorded\n", Config->Name);
    ok(SecondHalfMaximum != 0, "%s: second-half maximum handle was not recorded\n", Config->Name);
    ok(SecondHalfMaximum <= FirstHalfMaximum + DriftLimit, "%s: handle reuse did not plateau: first-max=%#lx second-max=%#lx drift-limit=%#lx\n", Config->Name, FirstHalfMaximum, SecondHalfMaximum, DriftLimit);
    *TotalCreated += Created;
}

START_TEST(ExHandleTable)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE EventHandle = NULL;
    PVOID EventObject = NULL;
    NTSTATUS Status;
    ULONG TotalCreated = 0;
    ULONG i;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateEvent(&EventHandle, EVENT_ALL_ACCESS, &ObjectAttributes, NotificationEvent, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(EventHandle, EVENT_ALL_ACCESS, *ExEventObjectType, KernelMode, &EventObject, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    if (NT_SUCCESS(Status))
    {
        for (i = 0; i < RTL_NUMBER_OF(HandleStressConfigs); i++) HandleStressRunConfig(&HandleStressConfigs[i], EventObject, &TotalCreated);
        ObDereferenceObject(EventObject);
    }

    if (EventHandle) ZwClose(EventHandle);
    trace("handle churn total: phases=%u created=%lu\n", RTL_NUMBER_OF(HandleStressConfigs), TotalCreated);
    ok_eq_ulong(TotalCreated, 3483136);
}
