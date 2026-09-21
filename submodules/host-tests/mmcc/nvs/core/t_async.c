/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_async.c
 * PURPOSE:     Asynchronous memory operation host-native tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include "ntpagingshim.h"
#include <cc/api/ccnt.h>

typedef struct _ASYNC_FILE
{
    ULONG Calls;
    ULONG Information;
    NTSTATUS Status;
    BOOLEAN Pending;
    BOOLEAN FailAllocate;
    PIRP Request[MI_MAX_FILE_IO_PAGES];
    ULONG RequestCount;
    ULONG Frees;
    ULONG Releases;
} ASYNC_FILE;

static ASYNC_FILE *AsyncFile;
static DEVICE_OBJECT AsyncDevice = { 1 };
static FILE_OBJECT AsyncThread;
static PWORK_QUEUE_ITEM AsyncWork;

PDEVICE_OBJECT
IoGetRelatedDeviceObject(PFILE_OBJECT File)
{
    AsyncFile = File->FsContext;
    return &AsyncDevice;
}

PIRP
IoAllocateIrp(UCHAR StackSize, BOOLEAN ChargeQuota)
{
    CHECK(StackSize == 1 && !ChargeQuota);
    return AsyncFile->FailAllocate ? NULL : calloc(1, sizeof(IRP));
}

VOID
IoFreeIrp(PIRP Irp)
{
    ASYNC_FILE *File = Irp->Tail.Overlay.OriginalFileObject->FsContext;

    CHECK(Irp->MdlAddress == NULL);
    File->Frees++;
    free(Irp);
}

PETHREAD
PsGetCurrentThread(VOID)
{
    return (PETHREAD)&AsyncThread;
}

VOID
ObReferenceObject(PVOID Object)
{
    ((PFILE_OBJECT)Object)->References++;
}

VOID
ExQueueWorkItem(PWORK_QUEUE_ITEM Work, ULONG Queue)
{
    PWORK_QUEUE_ITEM *Tail = &AsyncWork;

    CHECK(Queue == DelayedWorkQueue);
    while (*Tail != NULL)
        Tail = &(*Tail)->Next;
    Work->Next = NULL;
    *Tail = Work;
}

static void
AsyncCompleteIrp(PIRP Irp, ASYNC_FILE *File)
{
    KIRQL OldIrql = MiHostRaiseIrql(2);
    ULONG Bytes = File->Information > PAGE_SIZE ? PAGE_SIZE : File->Information;

    if (NT_SUCCESS(File->Status))
        memset(Irp->UserBuffer, 0xBA, Bytes);
    Irp->IoStatus.Status = File->Status;
    Irp->IoStatus.Information = File->Information;
    CHECK(Irp->Completion(&AsyncDevice, Irp, Irp->Context) == STATUS_MORE_PROCESSING_REQUIRED);
    MiHostLowerIrql(OldIrql);
}

NTSTATUS
IoCallDriver(PDEVICE_OBJECT Device, PIRP Irp)
{
    ASYNC_FILE *File = Irp->Tail.Overlay.OriginalFileObject->FsContext;

    CHECK(Device == &AsyncDevice && MiHostIrql == APC_LEVEL);
    CHECK(Irp->RequestorMode == KernelMode);
    CHECK(Irp->Flags == (IRP_PAGING_IO | IRP_NOCACHE | IRP_INPUT_OPERATION));
    CHECK(Irp->Stack.MajorFunction == IRP_MJ_READ);
    CHECK(Irp->Stack.FileObject == Irp->Tail.Overlay.OriginalFileObject);
    CHECK(Irp->Tail.Overlay.Thread == (PETHREAD)&AsyncThread && AsyncThread.References > 0);
    CHECK(Irp->MdlAddress->ByteCount == PAGE_SIZE && Irp->MdlAddress->ByteOffset == 0);
    CHECK(Irp->MdlAddress->MappedSystemVa == Irp->UserBuffer);
    CHECK(Irp->MdlAddress->MdlFlags == (MDL_PAGES_LOCKED | MDL_IO_PAGE_READ | MDL_MAPPED_TO_SYSTEM_VA));
    CHECK(Irp->Stack.Parameters.Read.Length == PAGE_SIZE);
    CHECK((Irp->Stack.Parameters.Read.ByteOffset.QuadPart & (PAGE_SIZE - 1)) == 0);
    File->Calls++;
    if (File->Pending)
    {
        CHECK(File->RequestCount < RTL_NUMBER_OF(File->Request));
        File->Request[File->RequestCount++] = Irp;
        return STATUS_PENDING;
    }
    AsyncCompleteIrp(Irp, File);
    return File->Status;
}

static void
AsyncDrain(ASYNC_FILE *File)
{
    ULONG i;

    for (i = 0; i < File->RequestCount; i++)
        AsyncCompleteIrp(File->Request[i], File);
    File->RequestCount = 0;
    while (AsyncWork != NULL)
    {
        PWORK_QUEUE_ITEM Work = AsyncWork;
        VOID (*Routine)(PVOID) = Work->Routine;
        PVOID Context = Work->Context;

        AsyncWork = Work->Next;
        CHECK(MiHostIrql == 0);
        Routine(Context);
    }
    CHECK(AsyncThread.References == 0);
}

static NTSTATUS
AsyncSyncRead(PVOID Context, ULONG64 Offset, ULONG Length, PVOID Buffer)
{
    memset(Buffer, 0xC7, Length);
    return STATUS_SUCCESS;
}

static void
AsyncFileRelease(PVOID Context)
{
    PMI_CONTROL_AREA Control = Context;
    ASYNC_FILE *File = Control->FileObject->FsContext;

    CHECK(Control->FileObject->References == 0);
    CHECK(File->Frees == File->Calls);
    File->Releases++;
}

static NTSTATUS
AsyncImmediateRead(PVOID Context, ULONG64 Offset, ULONG Frame, PVOID Buffer,
                   MI_READ_COMPLETION Completion, PVOID CompletionContext)
{
    memset(Buffer, 0xBA, PAGE_SIZE);
    Completion(CompletionContext, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

typedef struct _ASYNC_THREAD
{
    PMI_SEGMENT Segment;
    ULONG Cpu;
} ASYNC_THREAD;

typedef struct _ASYNC_DRAIN
{
    MI_ASYNC_DRAIN Drain;
    PMI_SEGMENT Segment;
    ULONG *Completions;
} ASYNC_DRAIN;

static void
AsyncReadDrained(PVOID Context)
{
    ASYNC_DRAIN *Drain = Context;

    CHECK(Drain->Segment->PendingReadCount == 0);
    CHECK(AsyncThread.References == 0);
    (*Drain->Completions)++;
    MiSegmentDereferenceAndClose(Drain->Segment);
}

static void *
AsyncStress(void *Context)
{
    ASYNC_THREAD *Thread = Context;
    ULONG i;

    MiHostCpu = Thread->Cpu;
    for (i = 0; i < 256; i++)
    {
        NTSTATUS Status = MiSegmentPrefetch(Thread->Segment, ((i + Thread->Cpu) % 32) * PAGE_SIZE, PAGE_SIZE);

        CHECK(Status == STATUS_SUCCESS || Status == STATUS_CANT_WAIT);
        if (i % 16 == 0)
            MiSegmentPurge(Thread->Segment, 4 * PAGE_SIZE, 28 * PAGE_SIZE);
    }
    return NULL;
}

static void
AsyncExtendRace(ULONG Case)
{
    TEST_WORLD World;
    ASYNC_FILE File = { .Information = PAGE_SIZE, .Pending = TRUE };
    FILE_OBJECT Object = { .FsContext = &File };
    MI_CONTROL_AREA Control = { .FileObject = &Object };
    MI_FILE_OPS Ops = { .Read = AsyncSyncRead, .Release = AsyncFileRelease, .ReadAsync = MiControlReadAsync };
    PMI_SEGMENT Segment;
    ULONG64 Base = 0, Size = PAGE_SIZE + 17;
    ULONG64 NewSize = ((Case & 1) ? 3 : 1) * PAGE_SIZE + 137;
    UCHAR Byte;

    WorldCreate(&World, 256, 4, 1000);
    WorldEnableFaults(&World);
    WorldAttach(&World, 0, &World.System.SystemSpace);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, Size, MI_PROT_READWRITE,
                                     &Ops, &Control, NULL, 0, &Segment)));
    Control.Segment = Segment;
    CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, PAGE_SIZE, PAGE_SIZE)));
    CHECK(File.RequestCount == 1 && Segment->PendingReadCount == 1);
    CHECK(NT_SUCCESS(MiSegmentExtend(Segment, Size)));
    CHECK(NT_SUCCESS(MiSegmentExtend(Segment, Size - 1)));
    CHECK(MiSegmentExtend(Segment, 1ULL << 53) == STATUS_SECTION_TOO_BIG);
    CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, PAGE_SIZE, PAGE_SIZE)));
    CHECK(File.Calls == 1 && File.RequestCount == 1);
    CHECK(NT_SUCCESS(MiSegmentExtend(Segment, NewSize)));
    CHECK((ULONG64)MI_ATOMIC_READ64(&Segment->SizeInBytes) == NewSize);
    if (Case < 2)
    {
        AsyncDrain(&File);
        CHECK(!MiSegmentIsResident(Segment, PAGE_SIZE, PAGE_SIZE));
        CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, PAGE_SIZE, PAGE_SIZE)));
    }
    else
    {
        CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, PAGE_SIZE, PAGE_SIZE)));
        CHECK(File.RequestCount == 2 && Segment->PendingReadCount == 2);
        if (File.RequestCount == 2)
        {
            PIRP Old = File.Request[0];

            File.Request[0] = File.Request[1];
            File.Request[1] = Old;
        }
    }
    AsyncDrain(&File);
    CHECK(File.Calls == 2 && File.Frees == 2);
    CHECK(Segment->PendingReadCount == 0 && Segment->ReferenceCount == 1);
    CHECK(Object.References == 0);
    Size = NewSize;
    CHECK(NT_SUCCESS(MiMapView(&World.System.SystemSpace, Segment, &Base, 0, &Size, MI_PROT_READWRITE, 0)));
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + PAGE_SIZE + 16, &Byte, 1, MachineRead, FALSE)));
    CHECK(Byte == 0xBA);
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + PAGE_SIZE + 65, &Byte, 1, MachineRead, FALSE)));
    CHECK(Byte == 0xBA);
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + PAGE_SIZE + 136, &Byte, 1, MachineRead, FALSE)));
    CHECK(Byte == 0xBA);
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + PAGE_SIZE + 137, &Byte, 1, MachineRead, FALSE)));
    CHECK(Byte == ((Case & 1) ? 0xBA : 0));
    CHECK(NT_SUCCESS(MiUnmapView(&World.System.SystemSpace, Base)));
    MiSegmentDereferenceAndClose(Segment);
    CHECK(File.Releases == 1 && WorldCheck(&World) == 0);
    WorldAttach(&World, 0, NULL);
    WorldDestroy(&World);
}

void
TestAsync(void)
{
    TEST_WORLD World;
    ASYNC_FILE File = { .Information = PAGE_SIZE, .Pending = TRUE };
    FILE_OBJECT Object = { .FsContext = &File };
    MI_CONTROL_AREA Control = { .FileObject = &Object };
    MI_FILE_OPS Ops = { .Read = AsyncSyncRead, .Release = AsyncFileRelease, .ReadAsync = MiControlReadAsync };
    CC_NT_MAP Map = { .Control = &Control };
    PMI_SEGMENT Segment;
    ULONG64 Base = 0, Size = 32 * PAGE_SIZE, Physical;
    ULONG Calls;
    UCHAR Byte;

    WorldCreate(&World, 256, 4, 1000);
    World.System.UnusedSegmentLimit = 4;
    World.Machine.StrictTlb = TRUE;
    WorldEnableFaults(&World);
    WorldAttach(&World, 0, &World.System.SystemSpace);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, Size, MI_PROT_READWRITE,
                                     &Ops, &Control, NULL, 0, &Segment)));
    Control.Segment = Segment;
    CHECK(NT_SUCCESS(MiMapView(&World.System.SystemSpace, Segment, &Base, 0, &Size, MI_PROT_READWRITE, 0)));
    CHECK(NT_SUCCESS(CcNtBackingOps.Prefetch(&Map, 0, 10)));
    CHECK(File.Calls == 1 && File.RequestCount == 1 && File.Frees == 0);
    CHECK(Object.References == 1 && Segment->PendingReadCount == 1 && MiHostIrql == 0);
    CHECK(!CcNtBackingOps.IsResident(&Map, 0, 10));
    CHECK(NT_SUCCESS(CcNtBackingOps.Prefetch(&Map, 0, PAGE_SIZE)) && File.Calls == 1);
    AsyncDrain(&File);
    CHECK(Object.References == 0 && Segment->PendingReadCount == 0 && File.Frees == 1);
    CHECK(CcNtBackingOps.IsResident(&Map, 0, PAGE_SIZE));
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base, &Byte, 1, MachineRead, FALSE)));
    CHECK(Byte == 0xBA);
    CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, 0, PAGE_SIZE)) && File.Calls == 1);

    File.Information = 17;
    File.Pending = FALSE;
    CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, PAGE_SIZE, PAGE_SIZE)));
    CHECK(!MiSegmentIsResident(Segment, PAGE_SIZE, PAGE_SIZE));
    AsyncDrain(&File);
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + PAGE_SIZE + 16, &Byte, 1, MachineRead, FALSE)));
    CHECK(Byte == 0xBA);
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + PAGE_SIZE + 17, &Byte, 1, MachineRead, FALSE)));
    CHECK(Byte == 0);

    File.Pending = TRUE;
    File.Status = STATUS_END_OF_FILE;
    CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, 2 * PAGE_SIZE, PAGE_SIZE)));
    AsyncDrain(&File);
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + 2 * PAGE_SIZE, &Byte, 1, MachineRead, FALSE)));
    CHECK(Byte == 0);

    File.Status = STATUS_UNEXPECTED_IO_ERROR;
    CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, 3 * PAGE_SIZE, PAGE_SIZE)));
    AsyncDrain(&File);
    CHECK(!MiSegmentIsResident(Segment, 3 * PAGE_SIZE, PAGE_SIZE) && Segment->PendingReadCount == 0);
    File.Status = STATUS_SUCCESS;
    File.Information = PAGE_SIZE;
    File.FailAllocate = TRUE;
    CHECK(MiSegmentPrefetch(Segment, 3 * PAGE_SIZE, PAGE_SIZE) == STATUS_INSUFFICIENT_RESOURCES);
    CHECK(Segment->PendingReadCount == 0 && Object.References == 0);
    File.FailAllocate = FALSE;

    CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, 3 * PAGE_SIZE, PAGE_SIZE)));
    CHECK(NT_SUCCESS(MiSegmentMakeResident(Segment, 3 * PAGE_SIZE, PAGE_SIZE)));
    AsyncDrain(&File);
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + 3 * PAGE_SIZE, &Byte, 1, MachineRead, FALSE)));
    CHECK(Byte == 0xC7);

    CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, 4 * PAGE_SIZE, PAGE_SIZE)));
    CHECK(MiSegmentPurge(Segment, 4 * PAGE_SIZE, PAGE_SIZE));
    AsyncDrain(&File);
    CHECK(!MiSegmentIsResident(Segment, 4 * PAGE_SIZE, PAGE_SIZE));
    Calls = File.Calls;
    MI_MUTEX_ACQUIRE(&Segment->Lock);
    CHECK(MiSegmentPrefetch(Segment, 4 * PAGE_SIZE, PAGE_SIZE) == STATUS_CANT_WAIT);
    CHECK(!MiSegmentIsResident(Segment, 0, PAGE_SIZE));
    MI_MUTEX_RELEASE(&Segment->Lock);
    CHECK(File.Calls == Calls);
    CHECK(MiSegmentPrefetch(Segment, ~0ULL - 8, 16) == STATUS_INVALID_PARAMETER);
    CHECK(MiSegmentPrefetch(Segment, Size, 1) == STATUS_INVALID_PARAMETER);

    CHECK(MiSegmentPrefetch(Segment, 4 * PAGE_SIZE, 17 * PAGE_SIZE) == STATUS_CANT_WAIT);
    CHECK(Segment->PendingReadCount == MI_MAX_FILE_IO_PAGES);
    AsyncDrain(&File);
    CHECK(Segment->PendingReadCount == 0 && Object.References == 0);
    CHECK(MiPtTranslate(&World.System.SystemSpace, Base, &Physical, NULL));
    CHECK(*(PUCHAR)MiArchMapFrame(Physical >> PAGE_SHIFT) == 0xBA);
    {
        ASYNC_DRAIN Drains[2];
        ULONG Completions = 0;
        ULONG i;

        CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, 20 * PAGE_SIZE, PAGE_SIZE)));
        for (i = 0; i < RTL_NUMBER_OF(Drains); i++)
        {
            Drains[i] = (ASYNC_DRAIN){ .Segment = Segment, .Completions = &Completions };
            Drains[i].Drain.Complete = AsyncReadDrained;
            Drains[i].Drain.Context = &Drains[i];
            MiSegmentReference(Segment);
            MiSegmentDrainReads(Segment, &Drains[i].Drain);
        }
        CHECK(Completions == 0 && Segment->ReferenceCount == 5);
        AsyncDrain(&File);
        CHECK(Completions == 2 && Segment->ReferenceCount == 2);
        MiSegmentReference(Segment);
        MiSegmentDrainReads(Segment, &Drains[0].Drain);
        CHECK(Completions == 3 && Segment->ReferenceCount == 2);
    }
    {
        pthread_t Threads[4];
        ASYNC_THREAD Args[4];
        ULONG i;

        Segment->FileOps.ReadAsync = AsyncImmediateRead;
        for (i = 0; i < RTL_NUMBER_OF(Threads); i++)
        {
            Args[i] = (ASYNC_THREAD){ Segment, i };
            CHECK(pthread_create(&Threads[i], NULL, AsyncStress, &Args[i]) == 0);
        }
        for (i = 0; i < RTL_NUMBER_OF(Threads); i++)
            CHECK(pthread_join(Threads[i], NULL) == 0);
        CHECK(Segment->PendingReadCount == 0 && Segment->ReferenceCount == 2);
        Segment->FileOps.ReadAsync = MiControlReadAsync;
        CHECK(MiSegmentPurge(Segment, 24 * PAGE_SIZE, PAGE_SIZE));
    }
    CHECK(NT_SUCCESS(MiSegmentPrefetch(Segment, 24 * PAGE_SIZE, PAGE_SIZE)));
    CHECK(NT_SUCCESS(MiUnmapView(&World.System.SystemSpace, Base)));
    MiSegmentDereference(Segment);
    CHECK(Object.References == 1);
    AsyncDrain(&File);
    CHECK(Object.References == 0 && File.Calls == File.Frees);
    CHECK(File.Releases == 1 && World.System.UnusedSegmentCount == 0);
    CHECK(WorldCheck(&World) == 0);
    WorldAttach(&World, 0, NULL);
    WorldDestroy(&World);
    for (Calls = 0; Calls < 4; Calls++)
        AsyncExtendRace(Calls);
}
