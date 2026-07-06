/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Windows Notification Facility (WNF)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

#define TAG_WNF_INSTANCE 'IfnW'
#define TAG_WNF_DATA     'DfnW'
#define TAG_WNF_SUB      'SfnW'

static LIST_ENTRY ExpWnfNameListHead;
static KGUARDED_MUTEX ExpWnfLock;
static ULONG64 ExpWnfNextUnique = 1;
static ULONG64 ExpWnfNextSubscriptionId = 1;
static BOOLEAN ExpWnfInitialized;

/* PRIVATE FUNCTIONS *********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpWnfInitialization(VOID)
{
    InitializeListHead(&ExpWnfNameListHead);
    KeInitializeGuardedMutex(&ExpWnfLock);
    ExpWnfInitialized = TRUE;
    return TRUE;
}

static
ULONG64
ExpWnfEncodeStateName(ULONG64 InternalName)
{
    return InternalName ^ WNF_STATE_KEY;
}

static
VOID
ExpWnfEncodeToStateName(ULONG64 InternalName,
                        PWNF_STATE_NAME StateName)
{
    ULONG64 Encoded = ExpWnfEncodeStateName(InternalName);

    StateName->Data[0] = (ULONG)Encoded;
    StateName->Data[1] = (ULONG)(Encoded >> 32);
}

static
NTSTATUS
ExpWnfCaptureStateName(PCWNF_STATE_NAME StateName,
                       KPROCESSOR_MODE PreviousMode,
                       PWNF_STATE_NAME_INTERNAL InternalName)
{
    WNF_STATE_NAME CapturedName;

    if (!ExpWnfInitialized)
        return STATUS_UNSUCCESSFUL;

    if (StateName == NULL)
        return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(StateName, sizeof(*StateName), sizeof(ULONG));
            CapturedName = *StateName;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        CapturedName = *StateName;
    }

    /* The XOR encoding is its own inverse */
    InternalName->Value = ExpWnfEncodeStateName(((ULONG64)CapturedName.Data[1] << 32) |
                                                CapturedName.Data[0]);

    if (InternalName->Version != WNF_STATE_NAME_VERSION)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

static
NTSTATUS
ExpWnfCaptureTypeId(PCWNF_TYPE_ID TypeId,
                    KPROCESSOR_MODE PreviousMode,
                    PWNF_TYPE_ID CapturedTypeId,
                    PBOOLEAN HasTypeId)
{
    *HasTypeId = FALSE;

    if (TypeId == NULL)
        return STATUS_SUCCESS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(TypeId, sizeof(*TypeId), sizeof(ULONG));
            *CapturedTypeId = *TypeId;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        *CapturedTypeId = *TypeId;
    }

    *HasTypeId = TRUE;
    return STATUS_SUCCESS;
}

static
PWNF_NAME_INSTANCE
ExpWnfLookupNameInstance(ULONG64 InternalName)
{
    PLIST_ENTRY Entry;
    PWNF_NAME_INSTANCE Instance;

    for (Entry = ExpWnfNameListHead.Flink;
         Entry != &ExpWnfNameListHead;
         Entry = Entry->Flink)
    {
        Instance = CONTAINING_RECORD(Entry, WNF_NAME_INSTANCE, NameListEntry);
        if (Instance->StateName.Value == InternalName)
            return Instance;
    }

    return NULL;
}

static
NTSTATUS
ExpWnfCreateNameInstance(PWNF_STATE_NAME_INTERNAL InternalName,
                         PCWNF_TYPE_ID TypeId,
                         ULONG MaximumStateSize,
                         PSECURITY_DESCRIPTOR CapturedSd,
                         PWNF_NAME_INSTANCE *OutInstance)
{
    PWNF_NAME_INSTANCE Instance;

    Instance = ExAllocatePoolWithTag(PagedPool, sizeof(*Instance), TAG_WNF_INSTANCE);
    if (Instance == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Instance, sizeof(*Instance));
    Instance->StateName.Value = InternalName->Value;
    Instance->Lifetime = (WNF_STATE_NAME_LIFETIME)InternalName->NameLifetime;
    Instance->DataScope = (WNF_DATA_SCOPE)InternalName->DataScope;
    Instance->PersistData = (BOOLEAN)InternalName->PermanentData;
    if (TypeId != NULL)
    {
        Instance->HasTypeId = TRUE;
        Instance->TypeId = *TypeId;
    }
    Instance->MaximumStateSize = MaximumStateSize ? MaximumStateSize
                                                  : WNF_MAX_STATE_SIZE;
    Instance->SecurityDescriptor = CapturedSd;
    Instance->CreatorProcess = PsGetCurrentProcess();
    InitializeListHead(&Instance->SubscriptionListHead);
    InsertTailList(&ExpWnfNameListHead, &Instance->NameListEntry);

    *OutInstance = Instance;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ExpWnfLookupOrCreateNameInstance(PWNF_STATE_NAME_INTERNAL InternalName,
                                 PWNF_NAME_INSTANCE *OutInstance)
{
    PWNF_NAME_INSTANCE Instance;

    Instance = ExpWnfLookupNameInstance(InternalName->Value);
    if (Instance != NULL)
    {
        *OutInstance = Instance;
        return STATUS_SUCCESS;
    }

    /* Well-known names spring into existence on first use */
    if (InternalName->NameLifetime != WnfWellKnownStateName)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    return ExpWnfCreateNameInstance(InternalName, NULL, 0, NULL, OutInstance);
}

static
BOOLEAN
ExpWnfMatchTypeId(PWNF_NAME_INSTANCE Instance,
                  BOOLEAN HasTypeId,
                  PCWNF_TYPE_ID TypeId)
{
    if (!Instance->HasTypeId || !HasTypeId)
        return TRUE;

    return RtlCompareMemory(&Instance->TypeId, TypeId, sizeof(WNF_TYPE_ID)) ==
           sizeof(WNF_TYPE_ID);
}

static
VOID
ExpWnfDereferenceSubscription(PWNF_SUBSCRIPTION Subscription)
{
    if (InterlockedDecrement(&Subscription->RefCount) == 0)
        KeSetEvent(&Subscription->RundownEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
ExpWnfReapSubscription(PWNF_SUBSCRIPTION Subscription)
{
    ExpWnfDereferenceSubscription(Subscription);
    KeWaitForSingleObject(&Subscription->RundownEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
    ExFreePoolWithTag(Subscription, TAG_WNF_SUB);
}

static
VOID
ExpWnfReapSubscriptionList(PLIST_ENTRY ReapList)
{
    PLIST_ENTRY Entry;

    while (!IsListEmpty(ReapList))
    {
        Entry = RemoveHeadList(ReapList);
        ExpWnfReapSubscription(CONTAINING_RECORD(Entry,
                                                 WNF_SUBSCRIPTION,
                                                 SubscriptionListEntry));
    }
}

static
VOID
NTAPI
ExpWnfCallbackWorker(PVOID Parameter)
{
    PWNF_SUBSCRIPTION Subscription = Parameter;
    PWNF_NAME_INSTANCE Instance = Subscription->NameInstance;
    WNF_STATE_NAME EncodedName;
    WNF_TYPE_ID TypeId = { { 0 } };
    PVOID Data = NULL;
    ULONG DataSize = 0;
    WNF_CHANGE_STAMP ChangeStamp = 0;
    BOOLEAN HasTypeId = FALSE;
    BOOLEAN Deliver = FALSE;

    KeAcquireGuardedMutex(&ExpWnfLock);
    InterlockedExchange(&Subscription->WorkQueued, 0);
    if (!Subscription->Removed)
    {
        DataSize = Instance->DataSize;
        if (DataSize != 0)
        {
            Data = ExAllocatePoolWithTag(PagedPool, DataSize, TAG_WNF_DATA);
            if (Data != NULL)
                RtlCopyMemory(Data, Instance->DataBuffer, DataSize);
            else
                DataSize = 0;
        }
        ChangeStamp = Instance->ChangeStamp;
        HasTypeId = Instance->HasTypeId;
        TypeId = Instance->TypeId;
        ExpWnfEncodeToStateName(Instance->StateName.Value, &EncodedName);
        Deliver = TRUE;
    }
    KeReleaseGuardedMutex(&ExpWnfLock);

    if (Deliver)
    {
        Subscription->Callback(&EncodedName,
                               ChangeStamp,
                               HasTypeId ? &TypeId : NULL,
                               Subscription->CallbackContext,
                               Data,
                               DataSize);
        Subscription->ChangeStamp = ChangeStamp;
    }

    if (Data != NULL)
        ExFreePoolWithTag(Data, TAG_WNF_DATA);

    InterlockedDecrement(&Instance->DeliveriesInFlight);
    ExpWnfDereferenceSubscription(Subscription);
}

/* Called with the WNF lock held */
static
VOID
ExpWnfSignalSubscription(PWNF_SUBSCRIPTION Subscription)
{
    KeSetEvent(&Subscription->NotificationEvent, IO_NO_INCREMENT, FALSE);

    if ((Subscription->Callback != NULL) &&
        (InterlockedCompareExchange(&Subscription->WorkQueued, 1, 0) == 0))
    {
        InterlockedIncrement(&Subscription->RefCount);
        InterlockedIncrement(&Subscription->NameInstance->DeliveriesInFlight);
        ExQueueWorkItem(&Subscription->WorkItem, DelayedWorkQueue);
    }
}

static
PWNF_SUBSCRIPTION
ExpWnfAllocateSubscription(WNF_CHANGE_STAMP ChangeStamp,
                           ULONG EventMask,
                           PWNF_KERNEL_CALLBACK Callback,
                           PVOID CallbackContext)
{
    PWNF_SUBSCRIPTION Subscription;

    Subscription = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(*Subscription),
                                         TAG_WNF_SUB);
    if (Subscription == NULL)
        return NULL;

    RtlZeroMemory(Subscription, sizeof(*Subscription));
    KeInitializeEvent(&Subscription->NotificationEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Subscription->RundownEvent, NotificationEvent, FALSE);
    Subscription->RefCount = 1;
    Subscription->ChangeStamp = ChangeStamp;
    Subscription->EventMask = EventMask;
    Subscription->Callback = Callback;
    Subscription->CallbackContext = CallbackContext;
    ExInitializeWorkItem(&Subscription->WorkItem,
                         ExpWnfCallbackWorker,
                         Subscription);
    return Subscription;
}

/* Called with the WNF lock held; reap the returned list after unlocking */
static
VOID
ExpWnfDetachSubscription(PWNF_SUBSCRIPTION Subscription,
                         PLIST_ENTRY ReapList)
{
    RemoveEntryList(&Subscription->SubscriptionListEntry);
    Subscription->Removed = TRUE;
    Subscription->NameInstance->SubscriptionCount--;
    InsertTailList(ReapList, &Subscription->SubscriptionListEntry);
}

static
VOID
ExpWnfFreeNameInstance(PWNF_NAME_INSTANCE Instance)
{
    if (Instance->DataBuffer != NULL)
        ExFreePoolWithTag(Instance->DataBuffer, TAG_WNF_DATA);

    if (Instance->SecurityDescriptor != NULL)
        SeReleaseSecurityDescriptor(Instance->SecurityDescriptor, KernelMode, TRUE);

    ExFreePoolWithTag(Instance, TAG_WNF_INSTANCE);
}

static
NTSTATUS
ExpWnfQuerySnapshot(ULONG64 InternalName,
                    BOOLEAN HasTypeId,
                    PCWNF_TYPE_ID TypeId,
                    PWNF_CHANGE_STAMP ChangeStamp,
                    PVOID *Data,
                    PULONG DataSize)
{
    PWNF_NAME_INSTANCE Instance;
    PVOID Snapshot = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    KeAcquireGuardedMutex(&ExpWnfLock);
    Instance = ExpWnfLookupNameInstance(InternalName);
    if (Instance == NULL)
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    else if (!ExpWnfMatchTypeId(Instance, HasTypeId, TypeId))
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        *ChangeStamp = Instance->ChangeStamp;
        *DataSize = Instance->DataSize;
        if (Instance->DataSize != 0)
        {
            Snapshot = ExAllocatePoolWithTag(PagedPool,
                                             Instance->DataSize,
                                             TAG_WNF_DATA);
            if (Snapshot != NULL)
                RtlCopyMemory(Snapshot, Instance->DataBuffer, Instance->DataSize);
            else
                Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    KeReleaseGuardedMutex(&ExpWnfLock);

    *Data = Snapshot;
    return Status;
}

/* SYSTEM CALLS **************************************************************/

NTSTATUS
NTAPI
NtCreateWnfStateName(PWNF_STATE_NAME StateName,
                     WNF_STATE_NAME_LIFETIME NameLifetime,
                     WNF_DATA_SCOPE DataScope,
                     BOOLEAN PersistData,
                     PCWNF_TYPE_ID TypeId,
                     ULONG MaximumStateSize,
                     PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    WNF_TYPE_ID CapturedTypeId;
    BOOLEAN HasTypeId;
    PSECURITY_DESCRIPTOR CapturedSd = NULL;
    PWNF_NAME_INSTANCE Instance;
    NTSTATUS Status;

    PAGED_CODE();

    if (!ExpWnfInitialized)
        return STATUS_UNSUCCESSFUL;

    if (StateName == NULL)
        return STATUS_INVALID_PARAMETER;

    if ((NameLifetime != WnfPermanentStateName) &&
        (NameLifetime != WnfPersistentStateName) &&
        (NameLifetime != WnfTemporaryStateName))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DataScope > WnfDataScopePhysicalMachine)
        return STATUS_INVALID_PARAMETER;

    if (MaximumStateSize > WNF_MAX_STATE_SIZE)
        return STATUS_INVALID_PARAMETER;

    if (PersistData && (NameLifetime == WnfTemporaryStateName))
        return STATUS_INVALID_PARAMETER;

    if ((NameLifetime != WnfTemporaryStateName) &&
        !SeSinglePrivilegeCheck(SeCreatePermanentPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(StateName, sizeof(*StateName), sizeof(ULONG));
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ExpWnfCaptureTypeId(TypeId, PreviousMode, &CapturedTypeId, &HasTypeId);
    if (!NT_SUCCESS(Status))
        return Status;

    if (SecurityDescriptor != NULL)
    {
        Status = SeCaptureSecurityDescriptor(SecurityDescriptor,
                                             PreviousMode,
                                             PagedPool,
                                             TRUE,
                                             &CapturedSd);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    InternalName.Value = 0;
    InternalName.Version = WNF_STATE_NAME_VERSION;
    InternalName.NameLifetime = NameLifetime;
    InternalName.DataScope = DataScope;
    InternalName.PermanentData = PersistData ? 1 : 0;

    KeAcquireGuardedMutex(&ExpWnfLock);
    InternalName.Unique = ExpWnfNextUnique++;
    Status = ExpWnfCreateNameInstance(&InternalName,
                                      HasTypeId ? &CapturedTypeId : NULL,
                                      MaximumStateSize,
                                      CapturedSd,
                                      &Instance);
    KeReleaseGuardedMutex(&ExpWnfLock);

    if (!NT_SUCCESS(Status))
    {
        if (CapturedSd != NULL)
            SeReleaseSecurityDescriptor(CapturedSd, KernelMode, TRUE);
        return Status;
    }

    _SEH2_TRY
    {
        ExpWnfEncodeToStateName(InternalName.Value, StateName);
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        KeAcquireGuardedMutex(&ExpWnfLock);
        RemoveEntryList(&Instance->NameListEntry);
        KeReleaseGuardedMutex(&ExpWnfLock);
        ExpWnfFreeNameInstance(Instance);
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtDeleteWnfStateName(PCWNF_STATE_NAME StateName)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    PWNF_SUBSCRIPTION Subscription;
    LIST_ENTRY ReapList;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeListHead(&ReapList);

    KeAcquireGuardedMutex(&ExpWnfLock);

    Instance = ExpWnfLookupNameInstance(InternalName.Value);
    if (Instance == NULL)
    {
        KeReleaseGuardedMutex(&ExpWnfLock);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if ((Instance->CreatorProcess != PsGetCurrentProcess()) &&
        !SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
    {
        KeReleaseGuardedMutex(&ExpWnfLock);
        return STATUS_ACCESS_DENIED;
    }

    RemoveEntryList(&Instance->NameListEntry);

    while (!IsListEmpty(&Instance->SubscriptionListHead))
    {
        Entry = Instance->SubscriptionListHead.Flink;
        Subscription = CONTAINING_RECORD(Entry,
                                         WNF_SUBSCRIPTION,
                                         SubscriptionListEntry);
        KeSetEvent(&Subscription->NotificationEvent, IO_NO_INCREMENT, FALSE);
        ExpWnfDetachSubscription(Subscription, &ReapList);
    }

    KeReleaseGuardedMutex(&ExpWnfLock);

    ExpWnfReapSubscriptionList(&ReapList);

    ExpWnfFreeNameInstance(Instance);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtUpdateWnfStateData(PCWNF_STATE_NAME StateName,
                     const VOID *Buffer,
                     ULONG Length,
                     PCWNF_TYPE_ID TypeId,
                     const VOID *ExplicitScope,
                     WNF_CHANGE_STAMP MatchingChangeStamp,
                     LOGICAL CheckStamp)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    WNF_TYPE_ID CapturedTypeId;
    BOOLEAN HasTypeId;
    PWNF_NAME_INSTANCE Instance;
    PLIST_ENTRY Entry;
    PVOID NewData = NULL;
    PVOID OldData;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ExplicitScope);

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((Length != 0) && (Buffer == NULL))
        return STATUS_INVALID_PARAMETER;

    if (Length > WNF_MAX_STATE_SIZE)
        return STATUS_BUFFER_OVERFLOW;

    Status = ExpWnfCaptureTypeId(TypeId, PreviousMode, &CapturedTypeId, &HasTypeId);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length != 0)
    {
        NewData = ExAllocatePoolWithTag(PagedPool, Length, TAG_WNF_DATA);
        if (NewData == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        _SEH2_TRY
        {
            if (PreviousMode != KernelMode)
                ProbeForRead(Buffer, Length, 1);
            RtlCopyMemory(NewData, Buffer, Length);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            ExFreePoolWithTag(NewData, TAG_WNF_DATA);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    KeAcquireGuardedMutex(&ExpWnfLock);

    Status = ExpWnfLookupOrCreateNameInstance(&InternalName, &Instance);
    if (!NT_SUCCESS(Status))
        goto Fail;

    if (Length > Instance->MaximumStateSize)
    {
        Status = STATUS_BUFFER_OVERFLOW;
        goto Fail;
    }

    if (!ExpWnfMatchTypeId(Instance, HasTypeId, &CapturedTypeId))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Fail;
    }

    if (CheckStamp && (Instance->ChangeStamp != MatchingChangeStamp))
    {
        Status = STATUS_UNSUCCESSFUL;
        goto Fail;
    }

    OldData = Instance->DataBuffer;
    Instance->DataBuffer = NewData;
    Instance->DataSize = Length;
    Instance->ChangeStamp++;

    for (Entry = Instance->SubscriptionListHead.Flink;
         Entry != &Instance->SubscriptionListHead;
         Entry = Entry->Flink)
    {
        ExpWnfSignalSubscription(CONTAINING_RECORD(Entry,
                                                   WNF_SUBSCRIPTION,
                                                   SubscriptionListEntry));
    }

    KeReleaseGuardedMutex(&ExpWnfLock);

    if (OldData != NULL)
        ExFreePoolWithTag(OldData, TAG_WNF_DATA);

    return STATUS_SUCCESS;

Fail:
    KeReleaseGuardedMutex(&ExpWnfLock);
    if (NewData != NULL)
        ExFreePoolWithTag(NewData, TAG_WNF_DATA);
    return Status;
}

NTSTATUS
NTAPI
NtDeleteWnfStateData(PCWNF_STATE_NAME StateName,
                     const VOID *ExplicitScope)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    PLIST_ENTRY Entry;
    PVOID OldData;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ExplicitScope);

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    KeAcquireGuardedMutex(&ExpWnfLock);

    Instance = ExpWnfLookupNameInstance(InternalName.Value);
    if (Instance == NULL)
    {
        KeReleaseGuardedMutex(&ExpWnfLock);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    OldData = Instance->DataBuffer;
    Instance->DataBuffer = NULL;
    Instance->DataSize = 0;
    Instance->ChangeStamp = 0;

    for (Entry = Instance->SubscriptionListHead.Flink;
         Entry != &Instance->SubscriptionListHead;
         Entry = Entry->Flink)
    {
        ExpWnfSignalSubscription(CONTAINING_RECORD(Entry,
                                                   WNF_SUBSCRIPTION,
                                                   SubscriptionListEntry));
    }

    KeReleaseGuardedMutex(&ExpWnfLock);

    if (OldData != NULL)
        ExFreePoolWithTag(OldData, TAG_WNF_DATA);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtQueryWnfStateData(PCWNF_STATE_NAME StateName,
                    PCWNF_TYPE_ID TypeId,
                    const VOID *ExplicitScope,
                    PWNF_CHANGE_STAMP ChangeStamp,
                    PVOID Buffer,
                    PULONG BufferSize)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    WNF_TYPE_ID CapturedTypeId;
    BOOLEAN HasTypeId;
    WNF_CHANGE_STAMP Stamp = 0;
    PVOID Snapshot = NULL;
    ULONG CallerSize = 0;
    ULONG Required = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ExplicitScope);

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((ChangeStamp == NULL) || (BufferSize == NULL))
        return STATUS_INVALID_PARAMETER;

    Status = ExpWnfCaptureTypeId(TypeId, PreviousMode, &CapturedTypeId, &HasTypeId);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWriteUlong((PULONG)ChangeStamp);
            ProbeForWriteUlong(BufferSize);
        }
        CallerSize = *BufferSize;
        if ((PreviousMode != KernelMode) && (Buffer != NULL) && (CallerSize != 0))
            ProbeForWrite(Buffer, CallerSize, 1);
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Buffer == NULL)
        CallerSize = 0;

    Status = ExpWnfQuerySnapshot(InternalName.Value,
                                 HasTypeId,
                                 &CapturedTypeId,
                                 &Stamp,
                                 &Snapshot,
                                 &Required);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *ChangeStamp = Stamp;
        *BufferSize = Required;
        if (CallerSize >= Required)
        {
            if (Required != 0)
                RtlCopyMemory(Buffer, Snapshot, Required);
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (Snapshot != NULL)
        ExFreePoolWithTag(Snapshot, TAG_WNF_DATA);

    return Status;
}

NTSTATUS
NTAPI
NtQueryWnfStateNameInformation(PCWNF_STATE_NAME StateName,
                               ULONG NameInfoClass,
                               const VOID *ExplicitScope,
                               PVOID InfoBuffer,
                               ULONG InfoBufferSize)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    ULONG Value = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ExplicitScope);

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    if (NameInfoClass > WnfInfoIsQuiescent)
        return STATUS_INVALID_INFO_CLASS;

    if ((InfoBuffer == NULL) || (InfoBufferSize != sizeof(ULONG)))
        return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(InfoBuffer);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    KeAcquireGuardedMutex(&ExpWnfLock);

    Instance = ExpWnfLookupNameInstance(InternalName.Value);
    switch (NameInfoClass)
    {
        case WnfInfoStateNameExist:
            Value = (Instance != NULL);
            Status = STATUS_SUCCESS;
            break;

        case WnfInfoSubscribersPresent:
            if (Instance == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
            }
            else
            {
                Value = (Instance->SubscriptionCount != 0);
                Status = STATUS_SUCCESS;
            }
            break;

        case WnfInfoIsQuiescent:
            if (Instance == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
            }
            else
            {
                Value = (Instance->DeliveriesInFlight == 0);
                Status = STATUS_SUCCESS;
            }
            break;
    }

    KeReleaseGuardedMutex(&ExpWnfLock);

    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *(PULONG)InfoBuffer = Value;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtSubscribeWnfStateChange(PCWNF_STATE_NAME StateName,
                          WNF_CHANGE_STAMP ChangeStamp,
                          ULONG EventMask,
                          PULONG64 SubscriptionId)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    PWNF_SUBSCRIPTION Subscription;
    LIST_ENTRY ReapList;
    ULONG64 Id;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((PreviousMode != KernelMode) && (SubscriptionId != NULL))
    {
        _SEH2_TRY
        {
            ProbeForWrite(SubscriptionId, sizeof(*SubscriptionId), sizeof(ULONG64));
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Subscription = ExpWnfAllocateSubscription(ChangeStamp, EventMask, NULL, NULL);
    if (Subscription == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Subscription->Process = PsGetCurrentProcess();
    Subscription->Thread = PsGetCurrentThread();

    KeAcquireGuardedMutex(&ExpWnfLock);

    Status = ExpWnfLookupOrCreateNameInstance(&InternalName, &Instance);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseGuardedMutex(&ExpWnfLock);
        ExFreePoolWithTag(Subscription, TAG_WNF_SUB);
        return Status;
    }

    Subscription->NameInstance = Instance;
    Subscription->SubscriptionId = Id = ExpWnfNextSubscriptionId++;
    InsertTailList(&Instance->SubscriptionListHead,
                   &Subscription->SubscriptionListEntry);
    Instance->SubscriptionCount++;

    if (Instance->ChangeStamp > ChangeStamp)
        KeSetEvent(&Subscription->NotificationEvent, IO_NO_INCREMENT, FALSE);

    KeReleaseGuardedMutex(&ExpWnfLock);

    if (SubscriptionId != NULL)
    {
        _SEH2_TRY
        {
            *SubscriptionId = Id;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            InitializeListHead(&ReapList);
            KeAcquireGuardedMutex(&ExpWnfLock);
            ExpWnfDetachSubscription(Subscription, &ReapList);
            KeReleaseGuardedMutex(&ExpWnfLock);
            ExpWnfReapSubscription(Subscription);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtUnsubscribeWnfStateChange(PCWNF_STATE_NAME StateName)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    PWNF_SUBSCRIPTION Subscription;
    PEPROCESS Process = PsGetCurrentProcess();
    LIST_ENTRY ReapList;
    PLIST_ENTRY Entry, NextEntry;
    BOOLEAN Found = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeListHead(&ReapList);

    KeAcquireGuardedMutex(&ExpWnfLock);

    Instance = ExpWnfLookupNameInstance(InternalName.Value);
    if (Instance == NULL)
    {
        KeReleaseGuardedMutex(&ExpWnfLock);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    for (Entry = Instance->SubscriptionListHead.Flink;
         Entry != &Instance->SubscriptionListHead;
         Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        Subscription = CONTAINING_RECORD(Entry,
                                         WNF_SUBSCRIPTION,
                                         SubscriptionListEntry);
        if ((Subscription->Process == Process) &&
            (Subscription->Callback == NULL))
        {
            ExpWnfDetachSubscription(Subscription, &ReapList);
            Found = TRUE;
        }
    }

    KeReleaseGuardedMutex(&ExpWnfLock);

    ExpWnfReapSubscriptionList(&ReapList);

    return Found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

/* KERNEL EXPORTS ************************************************************/

NTSTATUS
NTAPI
ExQueryWnfStateData(PCWNF_STATE_NAME StateName,
                    PWNF_CHANGE_STAMP ChangeStamp,
                    PVOID Buffer,
                    PULONG BufferSize)
{
    WNF_STATE_NAME_INTERNAL InternalName;
    WNF_CHANGE_STAMP Stamp = 0;
    PVOID Snapshot = NULL;
    ULONG Required = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if ((ChangeStamp == NULL) || (BufferSize == NULL))
        return STATUS_INVALID_PARAMETER;

    Status = ExpWnfCaptureStateName(StateName, KernelMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ExpWnfQuerySnapshot(InternalName.Value,
                                 FALSE,
                                 NULL,
                                 &Stamp,
                                 &Snapshot,
                                 &Required);
    if (!NT_SUCCESS(Status))
        return Status;

    *ChangeStamp = Stamp;
    if (Required == 0)
    {
        Status = STATUS_SUCCESS;
    }
    else if ((Buffer != NULL) && (*BufferSize >= Required))
    {
        RtlCopyMemory(Buffer, Snapshot, Required);
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    *BufferSize = Required;

    if (Snapshot != NULL)
        ExFreePoolWithTag(Snapshot, TAG_WNF_DATA);

    return Status;
}

NTSTATUS
NTAPI
ExSubscribeWnfStateChange(PVOID *SubscriptionHandle,
                          PCWNF_STATE_NAME StateName,
                          WNF_CHANGE_STAMP ChangeStamp,
                          PWNF_KERNEL_CALLBACK Callback,
                          PVOID CallbackContext)
{
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    PWNF_SUBSCRIPTION Subscription;
    NTSTATUS Status;

    PAGED_CODE();

    if ((SubscriptionHandle == NULL) || (Callback == NULL))
        return STATUS_INVALID_PARAMETER;

    Status = ExpWnfCaptureStateName(StateName, KernelMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    Subscription = ExpWnfAllocateSubscription(ChangeStamp,
                                              0,
                                              Callback,
                                              CallbackContext);
    if (Subscription == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeAcquireGuardedMutex(&ExpWnfLock);

    Status = ExpWnfLookupOrCreateNameInstance(&InternalName, &Instance);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseGuardedMutex(&ExpWnfLock);
        ExFreePoolWithTag(Subscription, TAG_WNF_SUB);
        return Status;
    }

    Subscription->NameInstance = Instance;
    Subscription->SubscriptionId = ExpWnfNextSubscriptionId++;
    InsertTailList(&Instance->SubscriptionListHead,
                   &Subscription->SubscriptionListEntry);
    Instance->SubscriptionCount++;

    if (Instance->ChangeStamp > ChangeStamp)
        ExpWnfSignalSubscription(Subscription);

    KeReleaseGuardedMutex(&ExpWnfLock);

    *SubscriptionHandle = Subscription;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ExUnsubscribeWnfStateChange(PVOID SubscriptionHandle)
{
    PWNF_NAME_INSTANCE Instance;
    PWNF_SUBSCRIPTION Subscription;
    PLIST_ENTRY InstanceEntry, SubEntry;
    LIST_ENTRY ReapList;
    BOOLEAN Found = FALSE;

    PAGED_CODE();

    if ((SubscriptionHandle == NULL) || !ExpWnfInitialized)
        return;

    InitializeListHead(&ReapList);

    KeAcquireGuardedMutex(&ExpWnfLock);

    /*
     * Walk the registry instead of trusting the handle: NtDeleteWnfStateName
     * reaps and frees all of a name's subscriptions, so a stale handle must
     * quietly not match rather than be dereferenced.
     */
    for (InstanceEntry = ExpWnfNameListHead.Flink;
         (InstanceEntry != &ExpWnfNameListHead) && !Found;
         InstanceEntry = InstanceEntry->Flink)
    {
        Instance = CONTAINING_RECORD(InstanceEntry,
                                     WNF_NAME_INSTANCE,
                                     NameListEntry);
        for (SubEntry = Instance->SubscriptionListHead.Flink;
             SubEntry != &Instance->SubscriptionListHead;
             SubEntry = SubEntry->Flink)
        {
            Subscription = CONTAINING_RECORD(SubEntry,
                                             WNF_SUBSCRIPTION,
                                             SubscriptionListEntry);
            if (Subscription == SubscriptionHandle)
            {
                ExpWnfDetachSubscription(Subscription, &ReapList);
                Found = TRUE;
                break;
            }
        }
    }

    KeReleaseGuardedMutex(&ExpWnfLock);

    ExpWnfReapSubscriptionList(&ReapList);
}
