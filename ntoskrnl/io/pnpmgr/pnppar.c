/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Parallel PnP device-tree enumeration
 * COPYRIGHT:   Copyright ReactOS contributors
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern BOOLEAN PnPBootDriversLoaded;

BOOLEAN PnpEnableParallelEnum = TRUE;

/* Dedicated worker pool: subtree workers block in IopSynchronousCall, so the shared queue won't do. */
#define PI_PARALLEL_ENUM_MAX 8
static volatile LONG PiParallelEnumActive = 0;
static volatile LONG PiParallelPoolState = 0; /* 0=uninit 1=init 2=ready 3=failed */
static LONG PiParallelPoolThreads = 0;
static LIST_ENTRY PiParallelWorkList;
static KSPIN_LOCK PiParallelWorkLock;
static KSEMAPHORE PiParallelWorkSem;

typedef struct _PI_PARALLEL_CONTEXT
{
    KEVENT DoneEvent;
    volatile LONG Outstanding;
    volatile LONG RefCount;
    PPI_PARALLEL_ROUTINE Routine;
} PI_PARALLEL_CONTEXT, *PPI_PARALLEL_CONTEXT;

typedef struct _PI_PARALLEL_WORK
{
    LIST_ENTRY ListEntry;
    PVOID Item;
    PPI_PARALLEL_CONTEXT Context;
} PI_PARALLEL_WORK, *PPI_PARALLEL_WORK;

static
VOID
PiParallelContextRelease(_In_ PPI_PARALLEL_CONTEXT Context)
{
    if (InterlockedDecrement(&Context->RefCount) == 0)
        ExFreePoolWithTag(Context, TAG_IO);
}

static
VOID
PiParallelItemComplete(_In_ PPI_PARALLEL_CONTEXT Context)
{
    if (InterlockedDecrement(&Context->Outstanding) == 0)
        KeSetEvent(&Context->DoneEvent, IO_NO_INCREMENT, FALSE);
}

static
BOOLEAN
PiParallelRunOneWork(VOID)
{
    PLIST_ENTRY entry;
    PPI_PARALLEL_WORK work;
    PPI_PARALLEL_CONTEXT context;
    PVOID item;
    KIRQL irql;

    KeAcquireSpinLock(&PiParallelWorkLock, &irql);
    if (IsListEmpty(&PiParallelWorkList))
    {
        KeReleaseSpinLock(&PiParallelWorkLock, irql);
        return FALSE;
    }
    entry = RemoveHeadList(&PiParallelWorkList);
    KeReleaseSpinLock(&PiParallelWorkLock, irql);

    work = CONTAINING_RECORD(entry, PI_PARALLEL_WORK, ListEntry);
    item = work->Item;
    context = work->Context;
    ExFreePoolWithTag(work, TAG_IO);

    context->Routine(item);

    InterlockedDecrement(&PiParallelEnumActive);
    PiParallelItemComplete(context);
    PiParallelContextRelease(context);
    return TRUE;
}

static
VOID
NTAPI
PiParallelPoolThread(_In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    for (;;)
    {
        KeWaitForSingleObject(&PiParallelWorkSem, Executive, KernelMode, FALSE, NULL);

        PiParallelRunOneWork();
    }
}

static
BOOLEAN
PiParallelPoolInit(VOID)
{
    LONG prev, t, n;

    prev = InterlockedCompareExchange(&PiParallelPoolState, 1, 0);
    if (prev == 2) return TRUE;
    if (prev != 0) return FALSE; /* init in progress or failed */

    n = (LONG)KeNumberProcessors;
    if (n > PI_PARALLEL_ENUM_MAX) n = PI_PARALLEL_ENUM_MAX;
    if (n < 1) n = 1;

    InitializeListHead(&PiParallelWorkList);
    KeInitializeSpinLock(&PiParallelWorkLock);
    KeInitializeSemaphore(&PiParallelWorkSem, 0, MAXLONG);

    for (t = 0; t < n; t++)
    {
        HANDLE handle;
        NTSTATUS st = PsCreateSystemThread(&handle, THREAD_ALL_ACCESS, NULL, NULL, NULL, PiParallelPoolThread, NULL);
        if (NT_SUCCESS(st))
        {
            ZwClose(handle);
            PiParallelPoolThreads++;
        }
    }

    if (PiParallelPoolThreads == 0)
    {
        InterlockedExchange(&PiParallelPoolState, 3);
        return FALSE;
    }

    InterlockedExchange(&PiParallelPoolState, 2);
    return TRUE;
}

/**
 * @brief      Run Routine over every item concurrently, then join.
 *
 * The caller's thread takes the last item itself and, while waiting for the
 * rest, keeps draining the pool queue, so a saturated pool degrades to inline
 * execution instead of deadlocking.
 *
 * @return     TRUE if the items were run here, FALSE if the caller must run
 *             them itself.
 */
BOOLEAN
PiRunParallel(
    _In_ PVOID *Items,
    _In_ LONG Count,
    _In_ PPI_PARALLEL_ROUTINE Routine)
{
    PPI_PARALLEL_CONTEXT context;
    BOOLEAN poolReady;
    LONG i;

    if (!PnPBootDriversLoaded || Count < 2)
        return FALSE;

    context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*context), TAG_IO);
    if (context == NULL)
        return FALSE;

    KeInitializeEvent(&context->DoneEvent, NotificationEvent, FALSE);
    context->Outstanding = Count;
    context->RefCount = 1;
    context->Routine = Routine;

    poolReady = PiParallelPoolInit();

    for (i = 0; i < Count - 1; i++)
    {
        PPI_PARALLEL_WORK work = NULL;
        KIRQL workIrql;

        if (poolReady)
        {
            if (InterlockedIncrement(&PiParallelEnumActive) <= PiParallelPoolThreads)
                work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*work), TAG_IO);
            if (work == NULL) InterlockedDecrement(&PiParallelEnumActive);
        }

        if (work == NULL)
        {
            /* Pool unavailable or saturated: run inline. */
            Routine(Items[i]);
            PiParallelItemComplete(context);
            continue;
        }

        InterlockedIncrement(&context->RefCount);
        work->Item = Items[i];
        work->Context = context;
        KeAcquireSpinLock(&PiParallelWorkLock, &workIrql);
        InsertTailList(&PiParallelWorkList, &work->ListEntry);
        KeReleaseSpinLock(&PiParallelWorkLock, workIrql);
        KeReleaseSemaphore(&PiParallelWorkSem, IO_NO_INCREMENT, 1, FALSE);
    }

    Routine(Items[Count - 1]);
    PiParallelItemComplete(context);

    for (;;)
    {
        LARGE_INTEGER timeout;

        if (PiParallelRunOneWork())
            continue;

        if (context->Outstanding == 0)
            break;

        timeout.QuadPart = -10 * 1000 * 10;
        KeWaitForSingleObject(&context->DoneEvent, Executive, KernelMode, FALSE, &timeout);
    }

    PiParallelContextRelease(context);
    return TRUE;
}

static
VOID
NTAPI
PiWalkSubtreeRoutine(_In_ PVOID Item)
{
    PDEVICE_NODE node = Item;
    PDEVICE_OBJECT pdo = node->PhysicalDeviceObject;

    PiDevNodeStateMachine(node);
    ObDereferenceObject(pdo);
}

/* Run Parent's child subtrees concurrently, then join. Returns TRUE if handled. */
BOOLEAN
PiProcessChildrenParallel(_In_ PDEVICE_NODE Parent)
{
    KIRQL oldIrql;
    PDEVICE_NODE child;
    PDEVICE_NODE *children;
    LONG count = 0, workCount = 0, i;
    BOOLEAN listGrew;

    /* Stay serial until boot drivers are loaded. */
    if (!PnPBootDriversLoaded)
        return FALSE;

    KeAcquireSpinLock(&IopDeviceTreeLock, &oldIrql);
    for (child = Parent->Child; child != NULL; child = child->Sibling)
    {
        count++;
        if (child->State == DeviceNodeRemoved || child->State == DeviceNodeDeleted) continue;
        if (child->State != DeviceNodeStarted || (child->Flags & (DNF_REENUMERATE | DNF_RESOURCE_REQUIREMENTS_CHANGED))) workCount++;
    }
    KeReleaseSpinLock(&IopDeviceTreeLock, oldIrql);

    /* Only fork when >= 2 children need work. */
    if (count <= 1) return FALSE;
    if (workCount < 2) return FALSE;

    children = ExAllocatePoolWithTag(NonPagedPool, count * sizeof(*children), TAG_IO);
    if (children == NULL) return FALSE;

    /* Snapshot and reference children under the tree lock; bail to serial if the list changed past our allocation. */
    KeAcquireSpinLock(&IopDeviceTreeLock, &oldIrql);
    i = 0;
    for (child = Parent->Child; child != NULL && i < count; child = child->Sibling)
    {
        ObReferenceObject(child->PhysicalDeviceObject);
        children[i++] = child;
    }
    listGrew = (child != NULL);
    count = i;
    KeReleaseSpinLock(&IopDeviceTreeLock, oldIrql);

    /* List grew past the array, or shrank below 2: let the serial descent handle them. */
    if (listGrew || count < 2)
    {
        for (i = 0; i < count; i++)
            ObDereferenceObject(children[i]->PhysicalDeviceObject);
        ExFreePoolWithTag(children, TAG_IO);
        return FALSE;
    }

    DPRINT("PnP: dispatching %d child subtrees of %wZ in parallel\n", (int)count, &Parent->InstancePath);

    if (!PiRunParallel((PVOID *)children, count, PiWalkSubtreeRoutine))
    {
        for (i = 0; i < count; i++)
            PiWalkSubtreeRoutine(children[i]);
    }

    ExFreePoolWithTag(children, TAG_IO);
    return TRUE;
}

#if DBG
/* Debug-only: assert the parallel walk never processes one devnode on two threads. */
VOID
PiDiagAcquireDevNode(_In_ PDEVICE_NODE Node)
{
    PVOID prev = InterlockedCompareExchangePointer(&Node->DiagWalkOwner, (PVOID)KeGetCurrentThread(), NULL);
    ASSERT(prev == NULL || prev == (PVOID)KeGetCurrentThread());
}

VOID
PiDiagReleaseDevNode(_In_ PDEVICE_NODE Node)
{
    InterlockedExchangePointer(&Node->DiagWalkOwner, NULL);
}
#endif
