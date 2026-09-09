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

/* FUNCTIONS *****************************************************************/

#define TAG_CM_NOTIFY 'nNmC'
#define TAG_CM_POST   'pNmC'

/* All notification and thread post lists are protected by this mutex. Registry
 * and KCB locks, when needed, must be acquired before the notification mutex. */
static FAST_MUTEX CmpNotifyMutex;
static LIST_ENTRY CmpNotifyList;
/* ETHREAD::PostBlockList overlaps StartAddress in the NT10 layout. Keep
 * notification ownership here so registering a watch cannot overwrite it. */
static LIST_ENTRY CmpNotifyThreadList = { &CmpNotifyThreadList, &CmpNotifyThreadList };

typedef struct _CMP_NOTIFY_POST
{
    LIST_ENTRY KeyList;
    LIST_ENTRY ThreadList;
    KEVENT WakeEvent;
    PKEVENT Event;
    PETHREAD Thread;
    NTSTATUS Status;
    BOOLEAN Asynchronous;
} CMP_NOTIFY_POST, *PCMP_NOTIFY_POST;

CODE_SEG("INIT")
VOID
NTAPI
CmpInitNotify(VOID)
{
    ExInitializeFastMutex(&CmpNotifyMutex);
    InitializeListHead(&CmpNotifyList);
}

/* The caller holds CmpNotifyMutex. Synchronous posts belong to the waiting
 * system call; asynchronous posts are freed when their event is signaled. */
static VOID
CmpCompleteNotify(PCMP_NOTIFY_POST Post, NTSTATUS Status)
{
    RemoveEntryList(&Post->KeyList);
    InitializeListHead(&Post->KeyList);
    RemoveEntryList(&Post->ThreadList);
    InitializeListHead(&Post->ThreadList);
    Post->Status = Status;
    if (Post->Asynchronous)
    {
        KeSetEvent(Post->Event, IO_NO_INCREMENT, FALSE);
        ObDereferenceObjectDeferDelete(Post->Event);
        ExFreePoolWithTag(Post, TAG_CM_POST);
    }
    else
    {
        KeSetEvent(&Post->WakeEvent, IO_NO_INCREMENT, FALSE);
    }
}

static VOID
CmpFlushNotifyLocked(PCM_KEY_BODY KeyBody)
{
    PCM_NOTIFY_BLOCK Notify = KeyBody->NotifyBlock;
    PCMP_NOTIFY_POST Post;

    if (!Notify) return;
    while (!IsListEmpty(&Notify->PostList))
    {
        Post = CONTAINING_RECORD(Notify->PostList.Flink, CMP_NOTIFY_POST, KeyList);
        CmpCompleteNotify(Post, STATUS_NOTIFY_CLEANUP);
    }
    RemoveEntryList(&Notify->HiveList);
    KeyBody->NotifyBlock = NULL;
    ExFreePoolWithTag(Notify, TAG_CM_NOTIFY);
}

VOID
NTAPI
CmpCloseNotify(PCM_KEY_BODY KeyBody)
{
    ExAcquireFastMutex(&CmpNotifyMutex);
    /* Remember the last handle close even if registration has not yet linked
     * its post. Referencing a key body does not keep its handles open. */
    KeyBody->NotifyClosed = TRUE;
    CmpFlushNotifyLocked(KeyBody);
    ExReleaseFastMutex(&CmpNotifyMutex);
}

VOID
NTAPI
CmpFlushNotifyThread(PETHREAD Thread)
{
    PCMP_NOTIFY_POST Post;
    PLIST_ENTRY Entry, Next;

    /* The statically initialized list is also safe before CM initialization. */
    if (IsListEmpty(&CmpNotifyThreadList)) return;
    ExAcquireFastMutex(&CmpNotifyMutex);
    for (Entry = CmpNotifyThreadList.Flink; Entry != &CmpNotifyThreadList; Entry = Next)
    {
        Next = Entry->Flink;
        Post = CONTAINING_RECORD(Entry, CMP_NOTIFY_POST, ThreadList);
        if (Post->Thread == Thread)
            CmpCompleteNotify(Post, STATUS_NOTIFY_CLEANUP);
    }
    ExReleaseFastMutex(&CmpNotifyMutex);
}

NTSTATUS
NTAPI
CmpNotifyChangeKey(PCM_KEY_BODY KeyBody,
                   PKEVENT Event,
                   ULONG Filter,
                   BOOLEAN WatchTree,
                   BOOLEAN Asynchronous,
                   KPROCESSOR_MODE PreviousMode)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PCM_NOTIFY_BLOCK Notify, NewNotify;
    PCMP_NOTIFY_POST Post;
    NTSTATUS Status;

    PAGED_CODE();

    Post = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Post), TAG_CM_POST);
    if (!Post) return STATUS_INSUFFICIENT_RESOURCES;
    NewNotify = ExAllocatePoolWithTag(PagedPool, sizeof(*NewNotify), TAG_CM_NOTIFY);
    if (!NewNotify)
    {
        ExFreePoolWithTag(Post, TAG_CM_POST);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    InitializeListHead(&Post->KeyList);
    InitializeListHead(&Post->ThreadList);
    KeInitializeEvent(&Post->WakeEvent, NotificationEvent, FALSE);
    Post->Event = Event;
    Post->Thread = PsGetCurrentThread();
    Post->Status = STATUS_PENDING;
    Post->Asynchronous = Asynchronous;

    CmpLockRegistry();
    CmpAcquireKcbLockShared(Kcb);
    ExAcquireFastMutex(&CmpNotifyMutex);
    if (Kcb->Delete || KeyBody->NotifyClosed)
    {
        Status = Kcb->Delete ? STATUS_KEY_DELETED : STATUS_NOTIFY_CLEANUP;
        goto Unlock;
    }

    Notify = KeyBody->NotifyBlock;
    if (!Notify)
    {
        Notify = NewNotify;
        NewNotify = NULL;
        InitializeListHead(&Notify->PostList);
        Notify->KeyControlBlock = Kcb;
        Notify->KeyBody = KeyBody;
        Notify->Filter = Filter & REG_LEGAL_CHANGE_FILTER;
        Notify->WatchTree = !!WatchTree;
        Notify->NotifyPending = FALSE;
        InsertTailList(&CmpNotifyList, &Notify->HiveList);
        KeyBody->NotifyBlock = Notify;
    }
    if (Asynchronous)
    {
        ObReferenceObject(Event);
        KeClearEvent(Event);
    }
    InsertTailList(&Notify->PostList, &Post->KeyList);
    if (!(Filter & REG_NOTIFY_THREAD_AGNOSTIC))
        InsertTailList(&CmpNotifyThreadList, &Post->ThreadList);
    Status = STATUS_PENDING;

Unlock:
    ExReleaseFastMutex(&CmpNotifyMutex);
    CmpReleaseKcbLock(Kcb);
    CmpUnlockRegistry();
    if (NewNotify) ExFreePoolWithTag(NewNotify, TAG_CM_NOTIFY);
    if (Status != STATUS_PENDING)
    {
        ExFreePoolWithTag(Post, TAG_CM_POST);
        return Status;
    }
    if (Asynchronous) return STATUS_PENDING;

    /* Never wait while holding registry or KCB locks: the writer needs them. */
    Status = KeWaitForSingleObject(&Post->WakeEvent, Executive, PreviousMode, TRUE, NULL);
    ExAcquireFastMutex(&CmpNotifyMutex);
    if (Post->Status != STATUS_PENDING)
        Status = Post->Status;
    else
        CmpCompleteNotify(Post, Status);
    ExReleaseFastMutex(&CmpNotifyMutex);
    ExFreePoolWithTag(Post, TAG_CM_POST);
    return Status;
}

VOID
NTAPI
CmpReportNotify(IN PCM_KEY_CONTROL_BLOCK Kcb,
                IN PHHIVE Hive,
                IN HCELL_INDEX Cell,
                IN ULONG Filter)
{
    PLIST_ENTRY Entry;
    PCM_NOTIFY_BLOCK Notify;
    PCM_KEY_CONTROL_BLOCK Changed;
    PCMP_NOTIFY_POST Post;

    UNREFERENCED_PARAMETER(Hive);
    UNREFERENCED_PARAMETER(Cell);

    /* A key name change changes the contents of its parent. Value changes
     * belong to the key itself. Parent KCBs also span mounted hive roots. */
    if (Filter & REG_NOTIFY_CHANGE_NAME) Kcb = Kcb->ParentKcb;
    if (!Kcb) return;

    ExAcquireFastMutex(&CmpNotifyMutex);
    for (Entry = CmpNotifyList.Flink; Entry != &CmpNotifyList; Entry = Entry->Flink)
    {
        Notify = CONTAINING_RECORD(Entry, CM_NOTIFY_BLOCK, HiveList);
        if (!(Notify->Filter & Filter)) continue;
        Changed = Kcb;
        if (Notify->WatchTree)
        {
            while (Changed && Changed != Notify->KeyControlBlock)
                Changed = Changed->ParentKcb;
        }
        if (Changed != Notify->KeyControlBlock) continue;
        while (!IsListEmpty(&Notify->PostList))
        {
            Post = CONTAINING_RECORD(Notify->PostList.Flink, CMP_NOTIFY_POST, KeyList);
            CmpCompleteNotify(Post, STATUS_NOTIFY_ENUM_DIR);
        }
    }
    ExReleaseFastMutex(&CmpNotifyMutex);
}

VOID
NTAPI
CmpFlushNotify(IN PCM_KEY_BODY KeyBody,
               IN BOOLEAN LockHeld)
{
    UNREFERENCED_PARAMETER(LockHeld);

    ExAcquireFastMutex(&CmpNotifyMutex);
    CmpFlushNotifyLocked(KeyBody);
    ExReleaseFastMutex(&CmpNotifyMutex);
}

VOID
NTAPI
CmpFlushNotifyOnKcb(IN PCM_KEY_CONTROL_BLOCK Kcb)
{
    PLIST_ENTRY Entry, Next;
    PCM_NOTIFY_BLOCK Notify;

    ExAcquireFastMutex(&CmpNotifyMutex);
    for (Entry = CmpNotifyList.Flink; Entry != &CmpNotifyList; Entry = Next)
    {
        Next = Entry->Flink;
        Notify = CONTAINING_RECORD(Entry, CM_NOTIFY_BLOCK, HiveList);
        if (Notify->KeyControlBlock == Kcb)
            CmpFlushNotifyLocked(Notify->KeyBody);
    }
    ExReleaseFastMutex(&CmpNotifyMutex);
}
