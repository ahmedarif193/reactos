/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            lib/drivers/chew/workqueue.c
 * PURPOSE:         Common Highlevel Executive Worker
 *
 * PROGRAMMERS:     arty (ayerkes@speakeasy.net)
 */

#include <ntddk.h>

#define FOURCC(w,x,y,z) (((w) << 24) | ((x) << 16) | ((y) << 8) | (z))
#define CHEW_TAG FOURCC('C','H','E','W')

PDEVICE_OBJECT WorkQueueDevice;
LIST_ENTRY     WorkQueue;
LIST_ENTRY     HighPriorityWorkQueue;
KSPIN_LOCK     WorkQueueLock;
KEVENT         WorkQueueEvent;
KEVENT         WorkQueueClear;
PKTHREAD       WorkQueueThread;
BOOLEAN        WorkQueueStop;
NPAGED_LOOKASIDE_LIST ChewLookaside;

typedef struct _WORK_ITEM
{
    LIST_ENTRY Entry;
    PIO_WORKITEM WorkItem;
    VOID (*Worker)(PVOID WorkerContext);
    PVOID WorkerContext;
} WORK_ITEM, *PWORK_ITEM;

static BOOLEAN
ChewQueuesEmpty(VOID)
{
    return IsListEmpty(&HighPriorityWorkQueue) && IsListEmpty(&WorkQueue);
}

VOID NTAPI ChewWorkItem(PDEVICE_OBJECT DeviceObject, PVOID ChewItem)
{
    PWORK_ITEM WorkItem = ChewItem;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    WorkItem->Worker(WorkItem->WorkerContext);

    IoFreeWorkItem(WorkItem->WorkItem);

    KeAcquireSpinLock(&WorkQueueLock, &OldIrql);
    RemoveEntryList(&WorkItem->Entry);

    if (ChewQueuesEmpty())
        KeSetEvent(&WorkQueueClear, 0, FALSE);

    KeReleaseSpinLock(&WorkQueueLock, OldIrql);

    ExFreeToNPagedLookasideList(&ChewLookaside, WorkItem);
}

VOID NTAPI ChewWorkerThread(PVOID Context)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PWORK_ITEM Item;

    UNREFERENCED_PARAMETER(Context);

    for (;;)
    {
        KeWaitForSingleObject(&WorkQueueEvent, Executive, KernelMode, FALSE, NULL);

        for (;;)
        {
            KeAcquireSpinLock(&WorkQueueLock, &OldIrql);
            if (ChewQueuesEmpty())
            {
                KeReleaseSpinLock(&WorkQueueLock, OldIrql);
                break;
            }

            if (!IsListEmpty(&HighPriorityWorkQueue))
                Entry = RemoveHeadList(&HighPriorityWorkQueue);
            else
                Entry = RemoveHeadList(&WorkQueue);
            KeReleaseSpinLock(&WorkQueueLock, OldIrql);

            Item = CONTAINING_RECORD(Entry, WORK_ITEM, Entry);
            Item->Worker(Item->WorkerContext);
            ExFreeToNPagedLookasideList(&ChewLookaside, Item);
        }

        if (WorkQueueStop)
            break;
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

VOID ChewInit(PDEVICE_OBJECT DeviceObject)
{
    HANDLE ThreadHandle;

    WorkQueueDevice = DeviceObject;
    InitializeListHead(&WorkQueue);
    InitializeListHead(&HighPriorityWorkQueue);
    KeInitializeSpinLock(&WorkQueueLock);
    KeInitializeEvent(&WorkQueueEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&WorkQueueClear, NotificationEvent, TRUE);
    WorkQueueStop = FALSE;
    WorkQueueThread = NULL;

    ExInitializeNPagedLookasideList(&ChewLookaside, NULL, NULL, 0,
                                    sizeof(WORK_ITEM), CHEW_TAG, 0);

    if (PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                             ChewWorkerThread, NULL) == STATUS_SUCCESS)
    {
        ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode,
                                  (PVOID *)&WorkQueueThread, NULL);
        ZwClose(ThreadHandle);
    }
}

VOID ChewShutdown(VOID)
{
    WorkQueueStop = TRUE;

    if (WorkQueueThread)
    {
        KeSetEvent(&WorkQueueEvent, 0, FALSE);
        KeWaitForSingleObject(WorkQueueThread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(WorkQueueThread);
        WorkQueueThread = NULL;
    }
    else
    {
        KeWaitForSingleObject(&WorkQueueClear, Executive, KernelMode, FALSE, NULL);
    }

    ExDeleteNPagedLookasideList(&ChewLookaside);
}

static BOOLEAN
ChewCreateInternal(VOID (*Worker)(PVOID),
                   PVOID WorkerContext,
                   BOOLEAN HighPriority)
{
    PWORK_ITEM Item;
    PLIST_ENTRY Queue;

    if (WorkQueueStop)
        return FALSE;

    Item = ExAllocateFromNPagedLookasideList(&ChewLookaside);
    if (!Item)
        return FALSE;

    Item->Worker = Worker;
    Item->WorkerContext = WorkerContext;
    Queue = HighPriority ? &HighPriorityWorkQueue : &WorkQueue;

    if (WorkQueueThread)
    {
        Item->WorkItem = NULL;
        ExInterlockedInsertTailList(Queue, &Item->Entry, &WorkQueueLock);
        KeSetEvent(&WorkQueueEvent, 0, FALSE);
        return TRUE;
    }

    Item->WorkItem = IoAllocateWorkItem(WorkQueueDevice);
    if (!Item->WorkItem)
    {
        ExFreeToNPagedLookasideList(&ChewLookaside, Item);
        return FALSE;
    }

    ExInterlockedInsertTailList(Queue, &Item->Entry, &WorkQueueLock);
    KeClearEvent(&WorkQueueClear);
    IoQueueWorkItem(Item->WorkItem, ChewWorkItem, DelayedWorkQueue, Item);

    return TRUE;
}

BOOLEAN
ChewCreate(VOID (*Worker)(PVOID), PVOID WorkerContext)
{
    return ChewCreateInternal(Worker, WorkerContext, FALSE);
}

BOOLEAN
ChewCreateHighPriority(VOID (*Worker)(PVOID), PVOID WorkerContext)
{
    return ChewCreateInternal(Worker, WorkerContext, TRUE);
}
