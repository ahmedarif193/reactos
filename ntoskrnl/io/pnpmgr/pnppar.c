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

/* Parallel device-tree enumeration; default on for NT6+, serial for NT 5.02 and earlier. */
#if (NTDDI_VERSION > NTDDI_WS03)
BOOLEAN PnpEnableParallelEnum = TRUE;
#else
BOOLEAN PnpEnableParallelEnum = FALSE;
#endif

BOOLEAN PnpForceParallelEnum = FALSE;

/* Dedicated worker pool: subtree workers block in IopSynchronousCall, so the shared queue won't do. */
#define PI_PARALLEL_ENUM_MAX 8
static volatile LONG PiParallelEnumActive = 0;
static volatile LONG PiParallelPoolState = 0; /* 0=uninit 1=init 2=ready 3=failed */
static LONG PiParallelPoolThreads = 0;
static LIST_ENTRY PiParallelWorkList;
static KSPIN_LOCK PiParallelWorkLock;
static KSEMAPHORE PiParallelWorkSem;
static volatile LONG PiParallelEnumReady = 0;
static volatile LONG PiParallelEnablerStarted = 0;

typedef struct _PI_PARALLEL_CONTEXT
{
    KEVENT DoneEvent;
    volatile LONG Outstanding;
    volatile LONG RefCount;
} PI_PARALLEL_CONTEXT, *PPI_PARALLEL_CONTEXT;

typedef struct _PI_SUBTREE_WORK
{
    LIST_ENTRY ListEntry;
    PDEVICE_NODE Node;
    PPI_PARALLEL_CONTEXT Context;
} PI_SUBTREE_WORK, *PPI_SUBTREE_WORK;

static
VOID
PiParallelContextRelease(_In_ PPI_PARALLEL_CONTEXT Context)
{
    if (InterlockedDecrement(&Context->RefCount) == 0)
        ExFreePoolWithTag(Context, TAG_IO);
}

static
VOID
PiSubtreeComplete(_In_ PPI_PARALLEL_CONTEXT Context)
{
    if (InterlockedDecrement(&Context->Outstanding) == 0)
        KeSetEvent(&Context->DoneEvent, IO_NO_INCREMENT, FALSE);
}

static
BOOLEAN
PiParallelRunOneWork(VOID)
{
    PLIST_ENTRY entry;
    PPI_SUBTREE_WORK work;
    PDEVICE_NODE node;
    PPI_PARALLEL_CONTEXT context;
    KIRQL irql;

    KeAcquireSpinLock(&PiParallelWorkLock, &irql);
    if (IsListEmpty(&PiParallelWorkList))
    {
        KeReleaseSpinLock(&PiParallelWorkLock, irql);
        return FALSE;
    }
    entry = RemoveHeadList(&PiParallelWorkList);
    KeReleaseSpinLock(&PiParallelWorkLock, irql);

    work = CONTAINING_RECORD(entry, PI_SUBTREE_WORK, ListEntry);
    node = work->Node;
    context = work->Context;
    ExFreePoolWithTag(work, TAG_IO);

    PiDevNodeStateMachine(node);

    ObDereferenceObject(node->PhysicalDeviceObject);
    InterlockedDecrement(&PiParallelEnumActive);
    PiSubtreeComplete(context);
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
VOID
NTAPI
PiParallelEnablerThread(_In_ PVOID Context)
{
    LARGE_INTEGER delay;
    UNREFERENCED_PARAMETER(Context);
    delay.QuadPart = -60LL * 10 * 1000 * 1000;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    InterlockedExchange(&PiParallelEnumReady, 1);
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

/* Run Parent's child subtrees concurrently, then join. Returns TRUE if handled. */
BOOLEAN
PiProcessChildrenParallel(_In_ PDEVICE_NODE Parent)
{
    KIRQL oldIrql;
    PDEVICE_NODE child;
    PDEVICE_NODE *children;
    PPI_PARALLEL_CONTEXT context;
    LONG count = 0, workCount = 0, i;
    BOOLEAN poolReady, listGrew;

    /* Stay serial until boot drivers are loaded. */
    if (!PnPBootDriversLoaded)
        return FALSE;

    if (!PiParallelEnumReady)
    {
        if (InterlockedCompareExchange(&PiParallelEnablerStarted, 1, 0) == 0)
        {
            HANDLE handle;
            if (NT_SUCCESS(PsCreateSystemThread(&handle, THREAD_ALL_ACCESS, NULL, NULL, NULL, PiParallelEnablerThread, NULL)))
                ZwClose(handle);
        }
        return FALSE;
    }

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
    if (workCount < 2 && !PnpForceParallelEnum) return FALSE;

    children = ExAllocatePoolWithTag(NonPagedPool, count * sizeof(*children), TAG_IO);
    if (children == NULL) return FALSE;

    context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*context), TAG_IO);
    if (context == NULL)
    {
        ExFreePoolWithTag(children, TAG_IO);
        return FALSE;
    }

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
        ExFreePoolWithTag(context, TAG_IO);
        return FALSE;
    }

    DPRINT1("PnP: dispatching %d child subtrees of %wZ in parallel\n", (int)count, &Parent->InstancePath);

    KeInitializeEvent(&context->DoneEvent, NotificationEvent, FALSE);
    context->Outstanding = count;
    context->RefCount = 1;

    poolReady = PiParallelPoolInit();

    /* Dispatch all but the last child; run the last inline. */
    for (i = 0; i < count - 1; i++)
    {
        PPI_SUBTREE_WORK work = NULL;
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
            PiDevNodeStateMachine(children[i]);
            ObDereferenceObject(children[i]->PhysicalDeviceObject);
            PiSubtreeComplete(context);
            continue;
        }

        InterlockedIncrement(&context->RefCount);
        work->Node = children[i];
        work->Context = context;
        KeAcquireSpinLock(&PiParallelWorkLock, &workIrql);
        InsertTailList(&PiParallelWorkList, &work->ListEntry);
        KeReleaseSpinLock(&PiParallelWorkLock, workIrql);
        KeReleaseSemaphore(&PiParallelWorkSem, IO_NO_INCREMENT, 1, FALSE);
    }

    PiDevNodeStateMachine(children[count - 1]);
    ObDereferenceObject(children[count - 1]->PhysicalDeviceObject);
    PiSubtreeComplete(context);

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

    ExFreePoolWithTag(children, TAG_IO);
    PiParallelContextRelease(context);
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
