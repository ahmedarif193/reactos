/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/config/cmhook.c
 * PURPOSE:         Configuration Manager - Registry Notifications/Callbacks
 * PROGRAMMERS:     Thomas Weidenmueller (w3seek@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include "ntoskrnl.h"
#define NDEBUG
#include "debug.h"

#define TAG_CM_CALLBACK 'bcMC'

typedef struct _CM_CALLBACK_ENTRY
{
    LIST_ENTRY ListEntry;
    LIST_ENTRY ContextList;
    EX_RUNDOWN_REF RundownRef;
    PEX_CALLBACK_FUNCTION Function;
    PVOID Context;
    LARGE_INTEGER Cookie;
    UNICODE_STRING Altitude;
    BOOLEAN Unregistering;
} CM_CALLBACK_ENTRY, *PCM_CALLBACK_ENTRY;

typedef struct _CM_CALLBACK_CONTEXT
{
    LIST_ENTRY KeyBodyLink;
    LIST_ENTRY CallbackLink;
    PCM_CALLBACK_ENTRY Callback;
    PVOID KeyBody;
    PVOID Context;
    PUNICODE_STRING Name;
} CM_CALLBACK_CONTEXT, *PCM_CALLBACK_CONTEXT;

typedef struct _CM_CALLBACK_FRAME
{
    SINGLE_LIST_ENTRY Link;
    PCM_CALLBACK_ENTRY Entry;
} CM_CALLBACK_FRAME, *PCM_CALLBACK_FRAME;

ULONG CmpCallBackCount = 0;

static LIST_ENTRY CmpCallbackListHead;
static EX_PUSH_LOCK CmpCallbackListLock;
static EX_PUSH_LOCK CmpCallbackContextLock;
static LONG64 CmpCallbackCookie;

CODE_SEG("INIT")
VOID
NTAPI
CmpInitCallback(VOID)
{
    PAGED_CODE();

    CmpCallBackCount = 0;
    InitializeListHead(&CmpCallbackListHead);
    ExInitializePushLock(&CmpCallbackListLock);
    ExInitializePushLock(&CmpCallbackContextLock);
}

static
NTSTATUS
CmpInvokeCallback(
    _In_ PCM_CALLBACK_ENTRY Entry,
    _In_ REG_NOTIFY_CLASS NotifyClass,
    _In_ PVOID Information)
{
    PETHREAD Thread = PsGetCurrentThread();
    CM_CALLBACK_FRAME Frame;
    NTSTATUS Status;

    Frame.Entry = Entry;
    PushEntryList(&Thread->CmCallbackListHead, &Frame.Link);
    Status = Entry->Function(Entry->Context, (PVOID)(ULONG_PTR)NotifyClass, Information);
    PopEntryList(&Thread->CmCallbackListHead);
    return Status;
}

static
PCM_CALLBACK_ENTRY
CmpFindCallbackLocked(
    _In_ LARGE_INTEGER Cookie)
{
    PLIST_ENTRY Link;
    PCM_CALLBACK_ENTRY Entry;

    for (Link = CmpCallbackListHead.Flink; Link != &CmpCallbackListHead; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, CM_CALLBACK_ENTRY, ListEntry);
        if (Entry->Cookie.QuadPart == Cookie.QuadPart)
        {
            return Entry;
        }
    }

    return NULL;
}

static
PVOID
CmpGetObjectContext(
    _In_opt_ PCM_KEY_BODY KeyBody,
    _In_ PCM_CALLBACK_ENTRY Entry)
{
    PLIST_ENTRY Link;
    PCM_CALLBACK_CONTEXT Node;
    PVOID Context = NULL;

    if (KeyBody == NULL || KeyBody->Type != CM_KEY_BODY_TYPE)
    {
        return NULL;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&CmpCallbackContextLock);
    for (Link = KeyBody->ContextListHead.Flink; Link != &KeyBody->ContextListHead; Link = Link->Flink)
    {
        Node = CONTAINING_RECORD(Link, CM_CALLBACK_CONTEXT, KeyBodyLink);
        if (Node->Callback == Entry)
        {
            Context = Node->Context;
            break;
        }
    }
    ExReleasePushLockShared(&CmpCallbackContextLock);
    KeLeaveCriticalRegion();
    return Context;
}

static
VOID
CmpFreeCallItems(
    _Inout_ PCMP_CALLBACK_CALL Call)
{
    if (Call->Items != Call->InlineItems)
    {
        ExFreePoolWithTag(Call->Items, TAG_CM_CALLBACK);
        Call->Items = Call->InlineItems;
    }
    Call->Count = 0;
}

NTSTATUS
NTAPI
CmpPostCallbacks(
    _Inout_ PCMP_CALLBACK_CALL Call,
    _In_ NTSTATUS Status)
{
    REG_POST_OPERATION_INFORMATION PostInformation;
    PCM_CALLBACK_ENTRY Entry;
    NTSTATUS CallbackStatus;
    ULONG Index;

    if (Call->Count == 0)
    {
        return Status;
    }

    for (Index = Call->Count; Index-- > 0;)
    {
        Entry = Call->Items[Index].Entry;

        if (Call->ResultObject != NULL)
        {
            PostInformation.Object = NT_SUCCESS(Status) ? *Call->ResultObject : NULL;
        }
        else
        {
            PostInformation.Object = Call->KeyBody;
        }
        PostInformation.Status = Status;
        PostInformation.PreInformation = Call->PreInformation;
        PostInformation.ReturnStatus = Status;
        PostInformation.CallContext = Call->Items[Index].CallContext;
        PostInformation.ObjectContext = CmpGetObjectContext(PostInformation.Object, Entry);
        PostInformation.Reserved = NULL;
        if (Call->CallContext != NULL)
        {
            *Call->CallContext = NULL;
        }
        if (Call->ObjectContext != NULL)
        {
            *Call->ObjectContext = CmpGetObjectContext(Call->KeyBody, Entry);
        }

        CallbackStatus = CmpInvokeCallback(Entry, Call->PostClass, &PostInformation);
        if (CallbackStatus == STATUS_CALLBACK_BYPASS)
        {
            Status = PostInformation.ReturnStatus;
        }

        ExReleaseRundownProtection(&Entry->RundownRef);
    }

    CmpFreeCallItems(Call);
    return Status;
}

BOOLEAN
NTAPI
CmpPreCallbacks(
    _Out_ PCMP_CALLBACK_CALL Call,
    _In_ REG_NOTIFY_CLASS PreClass,
    _In_ REG_NOTIFY_CLASS PostClass,
    _In_ PVOID PreInformation,
    _In_opt_ PVOID *CallContext,
    _In_opt_ PVOID *ObjectContext,
    _In_opt_ PCM_KEY_BODY KeyBody,
    _In_opt_ PVOID *ResultObject,
    _Out_ PNTSTATUS Status)
{
    PETHREAD Thread = PsGetCurrentThread();
    PLIST_ENTRY Start, Link;
    PCM_CALLBACK_ENTRY Entry;
    NTSTATUS CallbackStatus;
    ULONG Count, Total, Index;

    Call->Count = 0;
    Call->PostClass = PostClass;
    Call->PreInformation = PreInformation;
    Call->CallContext = CallContext;
    Call->ObjectContext = ObjectContext;
    Call->KeyBody = KeyBody;
    Call->ResultObject = ResultObject;
    Call->Items = Call->InlineItems;
    *Status = STATUS_SUCCESS;

    if (CmpCallBackCount == 0)
    {
        return TRUE;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&CmpCallbackListLock);

    Start = CmpCallbackListHead.Flink;
    if (Thread->CmCallbackListHead.Next != NULL)
    {
        Start = CONTAINING_RECORD(Thread->CmCallbackListHead.Next,
                                  CM_CALLBACK_FRAME,
                                  Link)->Entry->ListEntry.Flink;
    }

    Count = 0;
    for (Link = Start; Link != &CmpCallbackListHead; Link = Link->Flink)
    {
        Count++;
    }

    if (Count > RTL_NUMBER_OF(Call->InlineItems))
    {
        Call->Items = ExAllocatePoolWithTag(NonPagedPoolNx,
                                            Count * sizeof(Call->Items[0]),
                                            TAG_CM_CALLBACK);
        if (Call->Items == NULL)
        {
            ExReleasePushLockShared(&CmpCallbackListLock);
            KeLeaveCriticalRegion();
            Call->Items = Call->InlineItems;
            *Status = STATUS_INSUFFICIENT_RESOURCES;
            return FALSE;
        }
    }

    Total = 0;
    for (Link = Start; Link != &CmpCallbackListHead; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, CM_CALLBACK_ENTRY, ListEntry);
        if (!Entry->Unregistering && ExAcquireRundownProtection(&Entry->RundownRef))
        {
            Call->Items[Total].Entry = Entry;
            Call->Items[Total].CallContext = NULL;
            Total++;
        }
    }

    ExReleasePushLockShared(&CmpCallbackListLock);
    KeLeaveCriticalRegion();

    for (Index = 0; Index < Total; Index++)
    {
        Entry = Call->Items[Index].Entry;
        if (CallContext != NULL)
        {
            *CallContext = NULL;
        }
        if (ObjectContext != NULL)
        {
            *ObjectContext = CmpGetObjectContext(KeyBody, Entry);
        }

        CallbackStatus = CmpInvokeCallback(Entry, PreClass, PreInformation);
        if (CallbackStatus == STATUS_CALLBACK_BYPASS || !NT_SUCCESS(CallbackStatus))
        {
            break;
        }

        if (CallContext != NULL)
        {
            Call->Items[Index].CallContext = *CallContext;
        }
        Call->Count = Index + 1;
    }

    if (Index == Total)
    {
        if (Total == 0)
        {
            CmpFreeCallItems(Call);
        }
        return TRUE;
    }

    for (Count = Index; Count < Total; Count++)
    {
        ExReleaseRundownProtection(&Call->Items[Count].Entry->RundownRef);
    }

    if (CallbackStatus == STATUS_CALLBACK_BYPASS)
    {
        CallbackStatus = STATUS_SUCCESS;
    }

    if (Call->Count == 0)
    {
        CmpFreeCallItems(Call);
        *Status = CallbackStatus;
    }
    else
    {
        *Status = CmpPostCallbacks(Call, CallbackStatus);
    }
    return FALSE;
}

VOID
NTAPI
CmpCleanupKeyBodyContexts(
    _In_ PCM_KEY_BODY KeyBody)
{
    REG_CALLBACK_CONTEXT_CLEANUP_INFORMATION Information;
    PCM_CALLBACK_CONTEXT Node;
    PCM_CALLBACK_ENTRY Entry;
    PLIST_ENTRY Link;

    for (;;)
    {
        Node = NULL;
        Entry = NULL;

        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&CmpCallbackContextLock);
        if (!IsListEmpty(&KeyBody->ContextListHead))
        {
            Link = RemoveHeadList(&KeyBody->ContextListHead);
            Node = CONTAINING_RECORD(Link, CM_CALLBACK_CONTEXT, KeyBodyLink);
            InitializeListHead(&Node->KeyBodyLink);
            Entry = Node->Callback;
            if (Entry != NULL)
            {
                if (ExAcquireRundownProtection(&Entry->RundownRef))
                {
                    RemoveEntryList(&Node->CallbackLink);
                }
                else
                {
                    Node = NULL;
                    Entry = NULL;
                    ExReleasePushLockExclusive(&CmpCallbackContextLock);
                    KeLeaveCriticalRegion();
                    continue;
                }
            }
        }
        ExReleasePushLockExclusive(&CmpCallbackContextLock);
        KeLeaveCriticalRegion();

        if (Node == NULL)
        {
            break;
        }

        if (Entry != NULL)
        {
            Information.Object = KeyBody;
            Information.ObjectContext = Node->Context;
            Information.Reserved = NULL;
            CmpInvokeCallback(Entry, RegNtCallbackObjectContextCleanup, &Information);
            ExReleaseRundownProtection(&Entry->RundownRef);
        }

        if (Node->Name != NULL)
        {
            ExFreePool(Node->Name);
        }
        ExFreePoolWithTag(Node, TAG_CM_CALLBACK);
    }
}

static
NTSTATUS
CmpRegisterCallbackInternal(
    _In_ PEX_CALLBACK_FUNCTION Function,
    _In_opt_ PCUNICODE_STRING Altitude,
    _In_opt_ PVOID Context,
    _Out_ PLARGE_INTEGER Cookie)
{
    PCM_CALLBACK_ENTRY Entry, Other;
    PLIST_ENTRY Link;
    SIZE_T Size;

    Size = sizeof(CM_CALLBACK_ENTRY) + (Altitude ? Altitude->Length : 0);
    Entry = ExAllocatePoolWithTag(NonPagedPoolNx, Size, TAG_CM_CALLBACK);
    if (Entry == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Entry, Size);
    InitializeListHead(&Entry->ContextList);
    ExInitializeRundownProtection(&Entry->RundownRef);
    Entry->Function = Function;
    Entry->Context = Context;
    Entry->Cookie.QuadPart = InterlockedIncrement64(&CmpCallbackCookie);
    if (Altitude != NULL)
    {
        Entry->Altitude.Buffer = (PWCHAR)(Entry + 1);
        Entry->Altitude.Length = Altitude->Length;
        Entry->Altitude.MaximumLength = Altitude->Length;
        RtlCopyMemory(Entry->Altitude.Buffer, Altitude->Buffer, Altitude->Length);
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&CmpCallbackListLock);

    for (Link = CmpCallbackListHead.Flink; Link != &CmpCallbackListHead; Link = Link->Flink)
    {
        Other = CONTAINING_RECORD(Link, CM_CALLBACK_ENTRY, ListEntry);
        if (Other->Altitude.Buffer == NULL)
        {
            continue;
        }
        if (Altitude == NULL)
        {
            break;
        }
        if (ObpCompareAltitude(&Other->Altitude, &Entry->Altitude) == 0)
        {
            ExReleasePushLockExclusive(&CmpCallbackListLock);
            KeLeaveCriticalRegion();
            ExFreePoolWithTag(Entry, TAG_CM_CALLBACK);
            return STATUS_FLT_INSTANCE_ALTITUDE_COLLISION;
        }
        if (ObpCompareAltitude(&Other->Altitude, &Entry->Altitude) < 0)
        {
            break;
        }
    }

    InsertTailList(Link, &Entry->ListEntry);
    CmpCallBackCount++;

    ExReleasePushLockExclusive(&CmpCallbackListLock);
    KeLeaveCriticalRegion();

    *Cookie = Entry->Cookie;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmRegisterCallback(
    _In_ PEX_CALLBACK_FUNCTION Function,
    _In_opt_ PVOID Context,
    _Out_ PLARGE_INTEGER Cookie)
{
    PAGED_CODE();

    return CmpRegisterCallbackInternal(Function, NULL, Context, Cookie);
}

NTSTATUS
NTAPI
CmRegisterCallbackEx(
    _In_ PEX_CALLBACK_FUNCTION Function,
    _In_ PCUNICODE_STRING Altitude,
    _In_ PVOID Driver,
    _In_opt_ PVOID Context,
    _Out_ PLARGE_INTEGER Cookie,
    _Reserved_ PVOID Reserved)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Driver);
    UNREFERENCED_PARAMETER(Reserved);

    if (!ObpIsValidAltitude(Altitude))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return CmpRegisterCallbackInternal(Function, Altitude, Context, Cookie);
}

NTSTATUS
NTAPI
CmUnRegisterCallback(
    _In_ LARGE_INTEGER Cookie)
{
    REG_CALLBACK_CONTEXT_CLEANUP_INFORMATION Information;
    PCM_CALLBACK_CONTEXT Node;
    PCM_CALLBACK_ENTRY Entry;
    PLIST_ENTRY Link;
    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&CmpCallbackListLock);
    Entry = CmpFindCallbackLocked(Cookie);
    if (Entry == NULL || Entry->Unregistering)
    {
        ExReleasePushLockExclusive(&CmpCallbackListLock);
        KeLeaveCriticalRegion();
        return STATUS_INVALID_PARAMETER;
    }
    Entry->Unregistering = TRUE;
    ExReleasePushLockExclusive(&CmpCallbackListLock);
    KeLeaveCriticalRegion();

    ExWaitForRundownProtectionRelease(&Entry->RundownRef);

    for (;;)
    {
        Node = NULL;

        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&CmpCallbackContextLock);
        if (!IsListEmpty(&Entry->ContextList))
        {
            Link = RemoveHeadList(&Entry->ContextList);
            Node = CONTAINING_RECORD(Link, CM_CALLBACK_CONTEXT, CallbackLink);
            RemoveEntryList(&Node->KeyBodyLink);
        }
        ExReleasePushLockExclusive(&CmpCallbackContextLock);
        KeLeaveCriticalRegion();

        if (Node == NULL)
        {
            break;
        }

        Information.Object = Node->KeyBody;
        Information.ObjectContext = Node->Context;
        Information.Reserved = NULL;
        CmpInvokeCallback(Entry, RegNtCallbackObjectContextCleanup, &Information);
        ExFreePoolWithTag(Node, TAG_CM_CALLBACK);
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&CmpCallbackListLock);
    RemoveEntryList(&Entry->ListEntry);
    CmpCallBackCount--;
    ExReleasePushLockExclusive(&CmpCallbackListLock);
    KeLeaveCriticalRegion();

    ExFreePoolWithTag(Entry, TAG_CM_CALLBACK);
    return STATUS_SUCCESS;
}

VOID
NTAPI
CmGetCallbackVersion(
    _Out_opt_ PULONG Major,
    _Out_opt_ PULONG Minor)
{
    if (Major != NULL)
    {
        *Major = 1;
    }
    if (Minor != NULL)
    {
        *Minor = 1;
    }
}

NTSTATUS
NTAPI
CmSetCallbackObjectContext(
    _Inout_ PVOID Object,
    _In_ PLARGE_INTEGER Cookie,
    _In_ PVOID NewContext,
    _Out_opt_ PVOID *OldContext)
{
    PCM_KEY_BODY KeyBody = Object;
    PCM_CALLBACK_CONTEXT Node, NewNode;
    PCM_CALLBACK_ENTRY Entry;
    PLIST_ENTRY Link;
    NTSTATUS Status = STATUS_SUCCESS;
    PAGED_CODE();

    if (OldContext != NULL)
    {
        *OldContext = NULL;
    }

    if (KeyBody == NULL || KeyBody->Type != CM_KEY_BODY_TYPE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    NewNode = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*NewNode), TAG_CM_CALLBACK);
    if (NewNode == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&CmpCallbackListLock);
    ExAcquirePushLockExclusive(&CmpCallbackContextLock);

    Entry = CmpFindCallbackLocked(*Cookie);
    if (Entry == NULL || Entry->Unregistering)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        for (Link = KeyBody->ContextListHead.Flink; Link != &KeyBody->ContextListHead; Link = Link->Flink)
        {
            Node = CONTAINING_RECORD(Link, CM_CALLBACK_CONTEXT, KeyBodyLink);
            if (Node->Callback == Entry)
            {
                if (OldContext != NULL)
                {
                    *OldContext = Node->Context;
                }
                Node->Context = NewContext;
                break;
            }
        }

        if (Link == &KeyBody->ContextListHead)
        {
            NewNode->Callback = Entry;
            NewNode->KeyBody = KeyBody;
            NewNode->Context = NewContext;
            NewNode->Name = NULL;
            InsertTailList(&KeyBody->ContextListHead, &NewNode->KeyBodyLink);
            InsertTailList(&Entry->ContextList, &NewNode->CallbackLink);
            NewNode = NULL;
        }
    }

    ExReleasePushLockExclusive(&CmpCallbackContextLock);
    ExReleasePushLockShared(&CmpCallbackListLock);
    KeLeaveCriticalRegion();

    if (NewNode != NULL)
    {
        ExFreePoolWithTag(NewNode, TAG_CM_CALLBACK);
    }
    return Status;
}

static
BOOLEAN
CmpIsRegisteredCookie(
    _In_ PLARGE_INTEGER Cookie)
{
    BOOLEAN Found;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&CmpCallbackListLock);
    Found = (CmpFindCallbackLocked(*Cookie) != NULL);
    ExReleasePushLockShared(&CmpCallbackListLock);
    KeLeaveCriticalRegion();
    return Found;
}

NTSTATUS
NTAPI
CmCallbackGetKeyObjectIDEx(
    _In_ PLARGE_INTEGER Cookie,
    _In_ PVOID Object,
    _Out_opt_ PULONG_PTR ObjectID,
    _Outptr_opt_ PCUNICODE_STRING *ObjectName,
    _In_ ULONG Flags)
{
    PCM_KEY_BODY KeyBody = Object;
    PUNICODE_STRING Name;
    PAGED_CODE();

    if (Flags != 0 ||
        KeyBody == NULL ||
        KeyBody->Type != CM_KEY_BODY_TYPE ||
        KeyBody->KeyControlBlock == NULL ||
        !CmpIsRegisteredCookie(Cookie))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ObjectName != NULL)
    {
        CmpLockRegistry();
        Name = CmpConstructName(KeyBody->KeyControlBlock);
        CmpUnlockRegistry();
        if (Name == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        *ObjectName = Name;
    }

    if (ObjectID != NULL)
    {
        *ObjectID = (ULONG_PTR)KeyBody->KeyControlBlock;
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
CmCallbackReleaseKeyObjectIDEx(
    _In_ PCUNICODE_STRING ObjectName)
{
    PAGED_CODE();

    ExFreePool((PVOID)ObjectName);
}

NTSTATUS
NTAPI
CmCallbackGetKeyObjectID(
    _In_ PLARGE_INTEGER Cookie,
    _In_ PVOID Object,
    _Out_opt_ PULONG_PTR ObjectID,
    _Outptr_opt_ PCUNICODE_STRING *ObjectName)
{
    PCM_KEY_BODY KeyBody = Object;
    PCM_CALLBACK_CONTEXT Node, NewNode;
    PCUNICODE_STRING Name;
    PLIST_ENTRY Link;
    NTSTATUS Status;
    PAGED_CODE();

    if (ObjectName == NULL)
    {
        return CmCallbackGetKeyObjectIDEx(Cookie, Object, ObjectID, NULL, 0);
    }

    Status = CmCallbackGetKeyObjectIDEx(Cookie, Object, ObjectID, &Name, 0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    NewNode = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*NewNode), TAG_CM_CALLBACK);
    if (NewNode == NULL)
    {
        CmCallbackReleaseKeyObjectIDEx(Name);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&CmpCallbackContextLock);
    for (Link = KeyBody->ContextListHead.Flink; Link != &KeyBody->ContextListHead; Link = Link->Flink)
    {
        Node = CONTAINING_RECORD(Link, CM_CALLBACK_CONTEXT, KeyBodyLink);
        if (Node->Callback == NULL)
        {
            break;
        }
    }
    if (Link == &KeyBody->ContextListHead)
    {
        NewNode->Callback = NULL;
        NewNode->KeyBody = KeyBody;
        NewNode->Context = NULL;
        NewNode->Name = (PUNICODE_STRING)Name;
        InitializeListHead(&NewNode->CallbackLink);
        InsertTailList(&KeyBody->ContextListHead, &NewNode->KeyBodyLink);
        Node = NewNode;
        NewNode = NULL;
    }
    *ObjectName = Node->Name;
    ExReleasePushLockExclusive(&CmpCallbackContextLock);
    KeLeaveCriticalRegion();

    if (NewNode != NULL)
    {
        ExFreePoolWithTag(NewNode, TAG_CM_CALLBACK);
        CmCallbackReleaseKeyObjectIDEx(Name);
    }
    return STATUS_SUCCESS;
}

PVOID
NTAPI
CmGetBoundTransaction(
    _In_ PLARGE_INTEGER Cookie,
    _In_ PVOID Object)
{
    PCM_KEY_BODY KeyBody = Object;

    UNREFERENCED_PARAMETER(Cookie);

    if (KeyBody == NULL || KeyBody->Type != CM_KEY_BODY_TYPE)
    {
        return NULL;
    }

    return (PVOID)((ULONG_PTR)KeyBody->Trans.TransPtr & ~(ULONG_PTR)1);
}
