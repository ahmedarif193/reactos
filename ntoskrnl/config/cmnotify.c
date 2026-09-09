/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/config/cmnotify.c
 * PURPOSE:         Configuration Manager - Registry Change Notifications
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include "ntoskrnl.h"
#define NDEBUG
#include "debug.h"

/* Pending synchronous requests. Registry/KCB locks precede this lock. */
static FAST_MUTEX CmpNotifyLock;
static LIST_ENTRY CmpNotifyList;

VOID
NTAPI
CmpInitNotify(VOID)
{
    ExInitializeFastMutex(&CmpNotifyLock);
    InitializeListHead(&CmpNotifyList);
}

/* The waiter owns the allocation and key reference until it has unlinked. */
static VOID
CmpCompleteNotify(PCM_NOTIFY_BLOCK Notify, NTSTATUS Status)
{
    PCM_NOTIFY_BLOCK *Link;

    RemoveEntryList(&Notify->ListEntry);
    for (Link = &Notify->KeyBody->NotifyBlock; *Link != Notify; Link = &(*Link)->Next)
        NOTHING;
    *Link = Notify->Next;
    Notify->Status = Status;
    KeSetEvent(&Notify->Event, IO_NO_INCREMENT, FALSE);
}

NTSTATUS
NTAPI
CmpWaitForNotify(IN PCM_KEY_BODY KeyBody,
                 IN ULONG Filter,
                 IN BOOLEAN WatchTree,
                 IN KPROCESSOR_MODE PreviousMode)
{
    PCM_NOTIFY_BLOCK Notify;
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    NTSTATUS Status;

    PAGED_CODE();
    Notify = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Notify), 'fyNC');
    if (!Notify)
        return STATUS_INSUFFICIENT_RESOURCES;

    Notify->KeyBody = KeyBody;
    Notify->Filter = Filter;
    Notify->WatchTree = WatchTree;
    Notify->Status = STATUS_PENDING;
    KeInitializeEvent(&Notify->Event, NotificationEvent, FALSE);

    CmpLockRegistry();
    CmpAcquireKcbLockShared(Kcb);
    ExAcquireFastMutex(&CmpNotifyLock);
    if (Kcb->Delete)
        Status = STATUS_KEY_DELETED;
    else if (!OBJECT_TO_OBJECT_HEADER(KeyBody)->HandleCount)
        Status = STATUS_NOTIFY_CLEANUP;
    else
    {
        /* The last-handle close callback takes the same lock when cancelling. */
        Notify->Next = KeyBody->NotifyBlock;
        KeyBody->NotifyBlock = Notify;
        InsertTailList(&CmpNotifyList, &Notify->ListEntry);
        Status = STATUS_PENDING;
    }
    ExReleaseFastMutex(&CmpNotifyLock);
    CmpReleaseKcbLock(Kcb);
    CmpUnlockRegistry();

    if (Status == STATUS_PENDING)
    {
        Status = KeWaitForSingleObject(&Notify->Event, Executive, PreviousMode, TRUE, NULL);
        ExAcquireFastMutex(&CmpNotifyLock);
        if (Notify->Status == STATUS_PENDING)
        {
            /* A user APC or alert interrupted the wait. Never leave a request
             * referring to a caller which is no longer waiting. */
            CmpCompleteNotify(Notify, Status);
        }
        Status = Notify->Status;
        ExReleaseFastMutex(&CmpNotifyLock);
    }

    ExFreePoolWithTag(Notify, 'fyNC');
    return Status;
}

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
CmpReportNotify(IN PCM_KEY_CONTROL_BLOCK Kcb,
                IN PHHIVE Hive,
                IN HCELL_INDEX Cell,
                IN ULONG Filter)
{
    PLIST_ENTRY Entry, Next;
    PCM_NOTIFY_BLOCK Notify;
    PCM_KEY_CONTROL_BLOCK Changed, Ancestor;

    UNREFERENCED_PARAMETER(Hive);
    UNREFERENCED_PARAMETER(Cell);

    /* Name changes modify the parent's child list. Value changes belong to
     * the key itself. Parent KCBs remain referenced while the child exists. */
    Changed = (Filter & REG_NOTIFY_CHANGE_NAME) ? Kcb->ParentKcb : Kcb;
    ExAcquireFastMutex(&CmpNotifyLock);
    for (Entry = CmpNotifyList.Flink; Entry != &CmpNotifyList; Entry = Next)
    {
        Next = Entry->Flink;
        Notify = CONTAINING_RECORD(Entry, CM_NOTIFY_BLOCK, ListEntry);
        if (!(Filter & Notify->Filter))
            continue;
        for (Ancestor = Changed; Ancestor; Ancestor = Ancestor->ParentKcb)
        {
            if (Ancestor == Notify->KeyBody->KeyControlBlock)
            {
                CmpCompleteNotify(Notify, STATUS_NOTIFY_ENUM_DIR);
                break;
            }
            if (!Notify->WatchTree)
                break;
        }
    }
    ExReleaseFastMutex(&CmpNotifyLock);
}

VOID
NTAPI
CmpFlushNotify(IN PCM_KEY_BODY KeyBody,
               IN BOOLEAN LockHeld)
{
    UNREFERENCED_PARAMETER(LockHeld);

    ExAcquireFastMutex(&CmpNotifyLock);
    while (KeyBody->NotifyBlock)
        CmpCompleteNotify(KeyBody->NotifyBlock, STATUS_NOTIFY_CLEANUP);
    ExReleaseFastMutex(&CmpNotifyLock);
}

VOID
NTAPI
CmpFlushNotifyOnKcb(IN PCM_KEY_CONTROL_BLOCK Kcb)
{
    PLIST_ENTRY Entry, Next;
    PCM_NOTIFY_BLOCK Notify;

    ExAcquireFastMutex(&CmpNotifyLock);
    for (Entry = CmpNotifyList.Flink; Entry != &CmpNotifyList; Entry = Next)
    {
        Next = Entry->Flink;
        Notify = CONTAINING_RECORD(Entry, CM_NOTIFY_BLOCK, ListEntry);
        if (Notify->KeyBody->KeyControlBlock == Kcb)
            CmpCompleteNotify(Notify, STATUS_NOTIFY_CLEANUP);
    }
    ExReleaseFastMutex(&CmpNotifyLock);
}
