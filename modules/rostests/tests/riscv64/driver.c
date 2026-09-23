/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */
#include <ntifs.h>
#include <ndk/halfuncs.h>
#include <ndk/kefuncs.h>

static ULONG Checks, Failures;
#define CHECK(e) do { ++Checks; if (!(e)) { ++Failures; \
    DbgPrint("RISCVKTEST: FAIL line=%lu\n", (ULONG)__LINE__); } } while (0)

static VOID NTAPI Unload(PDRIVER_OBJECT Driver)
{
    IoDeleteDevice(Driver->DeviceObject);
}

static VOID FloatingPointTest(VOID)
{
    KFLOATING_SAVE Original, Saved;
    ULONG64 Pattern = 0x400921fb54442d18ULL, Result;
    ULONG Fcsr;
    NTSTATUS Status = KeSaveFloatingPointState(&Original);
    CHECK(NT_SUCCESS(Status));
    if (!NT_SUCCESS(Status)) return;
    __asm__ __volatile__("fmv.d.x f0, %0\n\tli t0, 32\n\tfscsr t0"
                         :: "r"(Pattern) : "f0", "t0", "memory");
    Status = KeSaveFloatingPointState(&Saved);
    CHECK(NT_SUCCESS(Status));
    CHECK(Saved.F[0] == Pattern && Saved.Fcsr == 32);
    __asm__ __volatile__("fmv.d.x f0, zero\n\tfscsr zero" ::: "f0", "memory");
    CHECK(NT_SUCCESS(KeRestoreFloatingPointState(&Saved)));
    __asm__ __volatile__("fmv.x.d %0, f0\n\tfrcsr %1" : "=r"(Result), "=r"(Fcsr));
    CHECK(Result == Pattern && Fcsr == 32);
    CHECK(KeRestoreFloatingPointState(&Saved) == STATUS_INVALID_DEVICE_STATE);
    CHECK(NT_SUCCESS(KeRestoreFloatingPointState(&Original)));
}

static VOID MdlTest(VOID)
{
    PHYSICAL_ADDRESS Low, High, Skip;
    PMDL Mdl;
    volatile ULONG *Kernel = NULL, *User = NULL;
    Low.QuadPart = Skip.QuadPart = 0;
    High.QuadPart = MAXLONGLONG;
    Mdl = MmAllocatePagesForMdl(Low, High, Skip, PAGE_SIZE);
    CHECK(Mdl != NULL);
    if (!Mdl) return;
    Kernel = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    CHECK(Kernel != NULL);
    if (Kernel)
    {
        *Kernel = 0x12345678;
        CHECK(NT_SUCCESS(MmProtectMdlSystemAddress(Mdl, PAGE_READONLY)));
        /* A kernel write through this read-only mapping is a deliberate
         * bugcheck, not a recoverable SEH probe. Verify retained contents. */
        CHECK(*Kernel == 0x12345678);
        CHECK(NT_SUCCESS(MmProtectMdlSystemAddress(Mdl, PAGE_READWRITE)));
        CHECK(*Kernel == 0x12345678);
    }
    __try
    {
        User = MmMapLockedPagesSpecifyCache(Mdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority);
        CHECK(User != NULL);
        if (User && Kernel)
        {
            CHECK(*User == *Kernel);
            *User = 0x87654321;
            CHECK(*Kernel == 0x87654321);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { CHECK(FALSE); }
    if (User) MmUnmapLockedPages((PVOID)User, Mdl);
    if (Kernel) MmUnmapLockedPages((PVOID)Kernel, Mdl);
    MmFreePagesFromMdl(Mdl);
    ExFreePool(Mdl);
}

typedef struct _SMP_PACKET
{
    KAFFINITY Expected;
    volatile LONG64 Seen;
    volatile LONG Arrived;
    volatile LONG Errors;
    ULONG Count;
} SMP_PACKET;

typedef struct DECLSPEC_ALIGN(16) _SMP_LIST_ENTRY
{
    SLIST_ENTRY Entry;
    volatile LONG Owner;
    ULONG Visits;
} SMP_LIST_ENTRY;

typedef struct _SMP_LIST
{
    SLIST_HEADER Head;
    SMP_LIST_ENTRY Entries[64];
} SMP_LIST;

typedef struct _SMP_WORKER
{
    ULONG Number;
    KAFFINITY Mask;
    PKEVENT Start;
    SMP_LIST *List;
    HANDLE Handle;
    KDPC Dpc;
    KEVENT DpcDone;
    volatile LONG Errors;
    volatile LONG ErrorPhases;
    ULONG DpcProcessor;
} SMP_WORKER;

/* Phases of SmpThread, reported when a worker records an error. */
#define SMP_PHASE_IPI       0x01
#define SMP_PHASE_SLIST     0x02
#define SMP_PHASE_PREEMPT   0x04
#define SMP_PHASE_MIGRATE   0x08
#define SMP_PHASE_DPC       0x10

static VOID SmpError(SMP_WORKER *Worker, LONG Phase)
{
    InterlockedIncrement(&Worker->Errors);
    InterlockedOr(&Worker->ErrorPhases, Phase);
}

static ULONG_PTR NTAPI SmpBroadcast(ULONG_PTR Argument)
{
    SMP_PACKET *Packet = (SMP_PACKET *)Argument;
    ULONG Number = KeGetCurrentProcessorNumber();
    KAFFINITY Bit = (KAFFINITY)1 << Number;
    if (KeGetCurrentIrql() != IPI_LEVEL || !(Packet->Expected & Bit) ||
        (InterlockedOr64(&Packet->Seen, Bit) & Bit))
        InterlockedIncrement(&Packet->Errors);
    InterlockedIncrement(&Packet->Arrived);
    /* The callback itself rendezvous: a sequential fake broadcast deadlocks. */
    while ((ULONG)InterlockedCompareExchange(&Packet->Arrived, 0, 0) < Packet->Count)
        YieldProcessor();
    return 0x5256534D;
}

static VOID NTAPI SmpThread(PVOID Context)
{
    SMP_WORKER *Worker = Context;
    PKTHREAD Self = KeGetCurrentThread();
    ERESOURCE Resource;
    SMP_PACKET Packet;
    ULONG Iteration, Count = 0;
    KAFFINITY Bits;
    KeSetSystemAffinityThread((KAFFINITY)1 << Worker->Number);
    KeWaitForSingleObject(Worker->Start, Executive, KernelMode, FALSE, NULL);
    for (Bits = Worker->Mask; Bits; Bits &= Bits - 1) ++Count;
    for (Iteration = 0; Iteration < 32; ++Iteration)
    {
        RtlZeroMemory(&Packet, sizeof(Packet));
        Packet.Expected = Worker->Mask;
        Packet.Count = Count;
        if (KeIpiGenericCall(SmpBroadcast, (ULONG_PTR)&Packet) != 0x5256534D ||
            Packet.Seen != (LONG64)Worker->Mask || Packet.Errors ||
            KeGetCurrentProcessorNumber() != Worker->Number)
            SmpError(Worker, SMP_PHASE_IPI);
    }
    for (Iteration = 0; Iteration < 4096; ++Iteration)
    {
        SMP_LIST_ENTRY *Entry;
        do
        {
            Entry = (SMP_LIST_ENTRY *)InterlockedPopEntrySList(&Worker->List->Head);
        } while (!Entry);
        if (InterlockedCompareExchange(&Entry->Owner, Worker->Number + 1, 0))
            SmpError(Worker, SMP_PHASE_SLIST);
        ++Entry->Visits;
        if (InterlockedExchange(&Entry->Owner, 0) != (LONG)Worker->Number + 1)
            SmpError(Worker, SMP_PHASE_SLIST);
        InterlockedPushEntrySList(&Worker->List->Head, &Entry->Entry);
    }
    KeRevertToUserAffinityThread();
    /* Exercise preemption with unrestricted affinity, then force migration
     * through every CPU while checking the per-thread/per-CPU accessors. */
    if (NT_SUCCESS(ExInitializeResourceLite(&Resource)))
    {
        for (Iteration = 0; Iteration < 4096; ++Iteration)
        {
            if (KeGetCurrentThread() != Self || KeGetCurrentIrql() != PASSIVE_LEVEL)
                SmpError(Worker, SMP_PHASE_PREEMPT);
            /* The checked resource path also validates KeIsExecutingDpc,
             * which is kernel-private on the native 64-bit ABI. */
            KeEnterCriticalRegion();
            ExAcquireResourceSharedLite(&Resource, TRUE);
            ExReleaseResourceLite(&Resource);
            KeLeaveCriticalRegion();
            if (!(Iteration & 15)) ZwYieldExecution();
        }
        ExDeleteResourceLite(&Resource);
    }
    else SmpError(Worker, SMP_PHASE_PREEMPT);
    for (Iteration = 0; Iteration < 32 * Count; ++Iteration)
    {
        ULONG Number = (Worker->Number + Iteration) % Count;
        KeSetSystemAffinityThread((KAFFINITY)1 << Number);
        if (KeGetCurrentProcessorNumber() != Number || KeGetCurrentThread() != Self ||
            KeGetCurrentIrql() != PASSIVE_LEVEL)
            SmpError(Worker, SMP_PHASE_MIGRATE);
    }
    KeRevertToUserAffinityThread();
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID NTAPI SmpDpc(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2)
{
    SMP_WORKER *Worker = Context;
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    Worker->DpcProcessor = KeGetCurrentProcessorNumber();
    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        SmpError(Worker, SMP_PHASE_DPC);
    KeSetEvent(&Worker->DpcDone, IO_NO_INCREMENT, FALSE);
}

static VOID SmpTest(VOID)
{
    KAFFINITY Mask = KeQueryActiveProcessors();
    ULONG Number, Count = 0;
    KEVENT Start;
    SMP_WORKER *Workers;
    SMP_LIST *List;
    ULONG Visits = 0;
    KAFFINITY Seen = 0;
    PSLIST_ENTRY Entry;
    NTSTATUS Status;

    Workers = ExAllocatePoolZero(NonPagedPool, MAXIMUM_PROCESSORS * sizeof(*Workers), 'pmSR');
    CHECK(Workers != NULL);
    if (!Workers) return;
    List = ExAllocatePoolZero(NonPagedPool, sizeof(*List), 'pmSR');
    CHECK(List != NULL);
    if (!List)
    {
        ExFreePoolWithTag(Workers, 'pmSR');
        return;
    }
    InitializeSListHead(&List->Head);
    for (Number = 0; Number < RTL_NUMBER_OF(List->Entries); ++Number)
        InterlockedPushEntrySList(&List->Head, &List->Entries[Number].Entry);
    KeInitializeEvent(&Start, NotificationEvent, FALSE);
    for (Number = 0; Number < MAXIMUM_PROCESSORS; ++Number)
    {
        SMP_WORKER *Worker = &Workers[Number];
        if (!(Mask & ((KAFFINITY)1 << Number))) continue;
        ++Count;
        Worker->Number = Number;
        Worker->Mask = Mask;
        Worker->Start = &Start;
        Worker->List = List;
        Worker->DpcProcessor = MAXULONG;
        KeInitializeEvent(&Worker->DpcDone, NotificationEvent, FALSE);
        KeInitializeDpc(&Worker->Dpc, SmpDpc, Worker);
        KeSetTargetProcessorDpc(&Worker->Dpc, (CCHAR)Number);
        CHECK(KeInsertQueueDpc(&Worker->Dpc, NULL, NULL));
        Status = PsCreateSystemThread(&Worker->Handle, THREAD_ALL_ACCESS, NULL,
                                      NULL, NULL, SmpThread, Worker);
        CHECK(NT_SUCCESS(Status));
    }
    DbgPrint("RISCVSMP: BEGIN processors=%lu mask=%Ix\n", Count, Mask);
    KeSetEvent(&Start, IO_NO_INCREMENT, FALSE);
    for (Number = 0; Number < MAXIMUM_PROCESSORS; ++Number)
    {
        SMP_WORKER *Worker = &Workers[Number];
        if (!(Mask & ((KAFFINITY)1 << Number))) continue;
        KeWaitForSingleObject(&Worker->DpcDone, Executive, KernelMode, FALSE, NULL);
        if (Worker->Handle)
        {
            CHECK(NT_SUCCESS(ZwWaitForSingleObject(Worker->Handle, FALSE, NULL)));
            ZwClose(Worker->Handle);
        }
        CHECK(Worker->Errors == 0 && Worker->DpcProcessor == Number);
        if (Worker->Errors || Worker->DpcProcessor != Number)
        {
            DbgPrint("RISCVSMP: worker %lu errors %ld phases 0x%lx dpc on %lu\n",
                     Number, Worker->Errors, Worker->ErrorPhases, Worker->DpcProcessor);
        }
    }
    KeFlushQueuedDpcs();
    CHECK(ExQueryDepthSList(&List->Head) == RTL_NUMBER_OF(List->Entries));
    Entry = InterlockedFlushSList(&List->Head);
    while (Entry)
    {
        SMP_LIST_ENTRY *Item = (SMP_LIST_ENTRY *)Entry;
        SIZE_T Index = Item - List->Entries;
        CHECK(Index < RTL_NUMBER_OF(List->Entries));
        if (Index >= RTL_NUMBER_OF(List->Entries)) break;
        CHECK(!(Seen & ((KAFFINITY)1 << Index)) && Item->Owner == 0);
        if (Seen & ((KAFFINITY)1 << Index)) break;
        Seen |= (KAFFINITY)1 << Index;
        Visits += Item->Visits;
        Entry = Entry->Next;
    }
    CHECK(Seen == ~(KAFFINITY)0 && Visits == Count * 4096);
    CHECK(ExQueryDepthSList(&List->Head) == 0);
    DbgPrint("RISCVSMP: END processors=%lu broadcasts=%lu failures=%lu\n",
             Count, Count * 32, Failures);
    ExFreePoolWithTag(Workers, 'pmSR');
    ExFreePoolWithTag(List, 'pmSR');
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT Driver, PUNICODE_STRING RegistryPath)
{
    volatile BOOLEAN Caught = FALSE;
    PDEVICE_OBJECT Device;
    UNREFERENCED_PARAMETER(RegistryPath);
    Driver->DriverUnload = Unload;
    DbgPrint("RISCVKTEST: BEGIN\n");
    __try { *(volatile ULONG *)(ULONG_PTR)0x42 = 1; }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Caught = TRUE;
        CHECK(GetExceptionCode() == STATUS_ACCESS_VIOLATION);
    }
    CHECK(Caught);
    FloatingPointTest();
    MdlTest();
    SmpTest();
    {
        TIME_FIELDS Time, After, Invalid = {0};
        BOOLEAN Available = HalQueryRealTimeClock(&Time);
        CHECK(Available);
        if (Available)
        {
            CHECK(Time.Year >= 2026 && Time.Month >= 1 && Time.Month <= 12);
            CHECK(!HalSetRealTimeClock(&Invalid));
            CHECK(HalSetRealTimeClock(&Time));
            CHECK(HalQueryRealTimeClock(&After));
            CHECK(Time.Year == After.Year && Time.Month == After.Month && Time.Day == After.Day);
        }
    }
    /* A legacy driver's device keeps it eligible for normal SCM unload. */
    CHECK(NT_SUCCESS(IoCreateDevice(Driver, 0, NULL, FILE_DEVICE_UNKNOWN,
                                    0, FALSE, &Device)));
    if (Driver->DeviceObject)
    {
        if (Failures) IoDeleteDevice(Driver->DeviceObject);
        else Driver->DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    }
    DbgPrint("RISCVKTEST: END checks=%lu failures=%lu\n", Checks, Failures);
    return Failures ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}
