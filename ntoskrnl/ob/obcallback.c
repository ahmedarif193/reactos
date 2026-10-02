/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object handle operation callbacks (ObRegisterCallbacks)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define TAG_OB_CALLBACK 'bCbO'

typedef struct _OBP_CALLBACK_REGISTRATION *POBP_CALLBACK_REGISTRATION;

typedef struct _OBP_CALLBACK_ENTRY
{
    LIST_ENTRY ListEntry;
    POBP_CALLBACK_REGISTRATION Registration;
    POBJECT_TYPE ObjectType;
    OB_OPERATION Operations;
    POB_PRE_OPERATION_CALLBACK PreOperation;
    POB_POST_OPERATION_CALLBACK PostOperation;
    EX_RUNDOWN_REF RundownRef;
} OBP_CALLBACK_ENTRY, *POBP_CALLBACK_ENTRY;

typedef struct _OBP_CALLBACK_REGISTRATION
{
    LIST_ENTRY ListEntry;
    UNICODE_STRING Altitude;
    PVOID RegistrationContext;
    USHORT Count;
    OBP_CALLBACK_ENTRY Entries[ANYSIZE_ARRAY];
} OBP_CALLBACK_REGISTRATION;

static LIST_ENTRY ObpCallbackRegistrationList = {&ObpCallbackRegistrationList, &ObpCallbackRegistrationList};
static EX_PUSH_LOCK ObpCallbackLock;

static
BOOLEAN
ObpSplitAltitude(
    _In_ PCUNICODE_STRING Altitude,
    _Out_ PUNICODE_STRING Integer,
    _Out_ PUNICODE_STRING Fraction)
{
    USHORT Count = Altitude->Length / sizeof(WCHAR);
    USHORT Index, Dot = Count;

    if (Count == 0)
    {
        return FALSE;
    }

    for (Index = 0; Index < Count; Index++)
    {
        WCHAR Char = Altitude->Buffer[Index];

        if (Char == L'.')
        {
            if (Dot != Count)
            {
                return FALSE;
            }
            Dot = Index;
        }
        else if (Char < L'0' || Char > L'9')
        {
            return FALSE;
        }
    }

    if (Dot == 0)
    {
        return FALSE;
    }

    Integer->Buffer = Altitude->Buffer;
    Integer->Length = Dot * sizeof(WCHAR);
    while (Integer->Length > sizeof(WCHAR) && Integer->Buffer[0] == L'0')
    {
        Integer->Buffer++;
        Integer->Length -= sizeof(WCHAR);
    }
    Integer->MaximumLength = Integer->Length;

    Fraction->Buffer = Altitude->Buffer + Dot + 1;
    Fraction->Length = (Dot == Count) ? 0 : (Count - Dot - 1) * sizeof(WCHAR);
    if (Dot == Count)
    {
        Fraction->Buffer = NULL;
    }
    while (Fraction->Length != 0 &&
           Fraction->Buffer[Fraction->Length / sizeof(WCHAR) - 1] == L'0')
    {
        Fraction->Length -= sizeof(WCHAR);
    }
    Fraction->MaximumLength = Fraction->Length;
    return TRUE;
}

BOOLEAN
NTAPI
ObpIsValidAltitude(
    _In_ PCUNICODE_STRING Altitude)
{
    UNICODE_STRING Integer, Fraction;

    return Altitude != NULL &&
           Altitude->Buffer != NULL &&
           ObpSplitAltitude(Altitude, &Integer, &Fraction);
}

LONG
NTAPI
ObpCompareAltitude(
    _In_ PCUNICODE_STRING First,
    _In_ PCUNICODE_STRING Second)
{
    UNICODE_STRING FirstInteger, FirstFraction, SecondInteger, SecondFraction;
    LONG Result;

    ObpSplitAltitude(First, &FirstInteger, &FirstFraction);
    ObpSplitAltitude(Second, &SecondInteger, &SecondFraction);

    if (FirstInteger.Length != SecondInteger.Length)
    {
        return (FirstInteger.Length > SecondInteger.Length) ? 1 : -1;
    }

    Result = RtlCompareUnicodeString(&FirstInteger, &SecondInteger, FALSE);
    if (Result != 0)
    {
        return Result;
    }

    return RtlCompareUnicodeString(&FirstFraction, &SecondFraction, FALSE);
}

NTSTATUS
NTAPI
ObpBeginHandleCallbacks(
    _Out_ POBP_HANDLE_CALLBACK_STATE State,
    _In_ POBJECT_TYPE ObjectType,
    _In_ OB_OPERATION Operation)
{
    PLIST_ENTRY Link;
    POBP_CALLBACK_ENTRY Entry;
    ULONG Count = 0;

    State->Count = 0;
    State->PreCalled = FALSE;
    State->Operation = Operation;
    State->ObjectType = ObjectType;
    State->Items = State->InlineItems;

    if (!ObjectType->TypeInfo.SupportsObjectCallbacks ||
        IsListEmpty(&ObjectType->CallbackList))
    {
        return STATUS_SUCCESS;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&ObpCallbackLock);

    for (Link = ObjectType->CallbackList.Flink;
         Link != &ObjectType->CallbackList;
         Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, OBP_CALLBACK_ENTRY, ListEntry);
        if (Entry->Operations & Operation)
        {
            Count++;
        }
    }

    if (Count > RTL_NUMBER_OF(State->InlineItems))
    {
        State->Items = ExAllocatePoolWithTag(NonPagedPoolNx,
                                             Count * sizeof(State->Items[0]),
                                             TAG_OB_CALLBACK);
        if (State->Items == NULL)
        {
            ExReleasePushLockShared(&ObpCallbackLock);
            KeLeaveCriticalRegion();
            State->Items = State->InlineItems;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    for (Link = ObjectType->CallbackList.Flink;
         Link != &ObjectType->CallbackList;
         Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, OBP_CALLBACK_ENTRY, ListEntry);
        if ((Entry->Operations & Operation) &&
            ExAcquireRundownProtection(&Entry->RundownRef))
        {
            State->Items[State->Count].Entry = Entry;
            State->Items[State->Count].CallContext = NULL;
            State->Count++;
        }
    }

    ExReleasePushLockShared(&ObpCallbackLock);

    if (State->Count == 0)
    {
        if (State->Items != State->InlineItems)
        {
            ExFreePoolWithTag(State->Items, TAG_OB_CALLBACK);
            State->Items = State->InlineItems;
        }
        KeLeaveCriticalRegion();
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
ObpCallPreHandleCallbacks(
    _Inout_ POBP_HANDLE_CALLBACK_STATE State,
    _In_ PVOID Object,
    _In_ BOOLEAN KernelHandle,
    _Inout_ PACCESS_MASK DesiredAccess,
    _In_opt_ PEPROCESS SourceProcess,
    _In_opt_ PEPROCESS TargetProcess)
{
    OB_PRE_OPERATION_INFORMATION Information;
    OB_PRE_OPERATION_PARAMETERS Parameters;
    POBP_CALLBACK_ENTRY Entry;
    ACCESS_MASK Original = *DesiredAccess;
    PACCESS_MASK Access;
    ULONG Index;

    if (State->Count == 0)
    {
        return;
    }

    ObReferenceObject(Object);
    State->PreCalled = TRUE;
    State->Object = Object;
    State->KernelHandle = KernelHandle;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    if (State->Operation == OB_OPERATION_HANDLE_DUPLICATE)
    {
        Parameters.DuplicateHandleInformation.DesiredAccess = Original;
        Parameters.DuplicateHandleInformation.OriginalDesiredAccess = Original;
        Parameters.DuplicateHandleInformation.SourceProcess = SourceProcess;
        Parameters.DuplicateHandleInformation.TargetProcess = TargetProcess;
        Access = &Parameters.DuplicateHandleInformation.DesiredAccess;
    }
    else
    {
        Parameters.CreateHandleInformation.DesiredAccess = Original;
        Parameters.CreateHandleInformation.OriginalDesiredAccess = Original;
        Access = &Parameters.CreateHandleInformation.DesiredAccess;
    }

    for (Index = 0; Index < State->Count; Index++)
    {
        Entry = State->Items[Index].Entry;
        if (Entry->PreOperation == NULL)
        {
            continue;
        }

        RtlZeroMemory(&Information, sizeof(Information));
        Information.Operation = State->Operation;
        Information.KernelHandle = KernelHandle;
        Information.Object = Object;
        Information.ObjectType = State->ObjectType;
        Information.Parameters = &Parameters;

        Entry->PreOperation(Entry->Registration->RegistrationContext, &Information);

        State->Items[Index].CallContext = Information.CallContext;
        *Access &= *DesiredAccess;
        *DesiredAccess = *Access;
    }
}

VOID
NTAPI
ObpEndHandleCallbacks(
    _Inout_ POBP_HANDLE_CALLBACK_STATE State,
    _In_ NTSTATUS ReturnStatus,
    _In_ ACCESS_MASK GrantedAccess)
{
    OB_POST_OPERATION_INFORMATION Information;
    OB_POST_OPERATION_PARAMETERS Parameters;
    POBP_CALLBACK_ENTRY Entry;
    ULONG Index;

    if (State->Count == 0)
    {
        return;
    }

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    if (State->Operation == OB_OPERATION_HANDLE_DUPLICATE)
    {
        Parameters.DuplicateHandleInformation.GrantedAccess = GrantedAccess;
    }
    else
    {
        Parameters.CreateHandleInformation.GrantedAccess = GrantedAccess;
    }

    for (Index = State->Count; Index-- > 0;)
    {
        Entry = State->Items[Index].Entry;
        if (State->PreCalled && Entry->PostOperation != NULL)
        {
            RtlZeroMemory(&Information, sizeof(Information));
            Information.Operation = State->Operation;
            Information.KernelHandle = State->KernelHandle;
            Information.Object = State->Object;
            Information.ObjectType = State->ObjectType;
            Information.CallContext = State->Items[Index].CallContext;
            Information.ReturnStatus = ReturnStatus;
            Information.Parameters = &Parameters;

            Entry->PostOperation(Entry->Registration->RegistrationContext, &Information);
        }

        ExReleaseRundownProtection(&Entry->RundownRef);
    }

    if (State->Items != State->InlineItems)
    {
        ExFreePoolWithTag(State->Items, TAG_OB_CALLBACK);
        State->Items = State->InlineItems;
    }

    State->Count = 0;
    KeLeaveCriticalRegion();

    if (State->PreCalled)
    {
        State->PreCalled = FALSE;
        ObDereferenceObject(State->Object);
    }
}

USHORT
NTAPI
ObGetFilterVersion(VOID)
{
    return OB_FLT_REGISTRATION_VERSION;
}

NTSTATUS
NTAPI
ObRegisterCallbacks(
    _In_ POB_CALLBACK_REGISTRATION CallbackRegistration,
    _Outptr_ PVOID *RegistrationHandle)
{
    POBP_CALLBACK_REGISTRATION Registration, Other;
    POB_OPERATION_REGISTRATION Operation;
    POBP_CALLBACK_ENTRY Entry;
    PLIST_ENTRY Link;
    SIZE_T Size;
    USHORT Index;
    PAGED_CODE();

    *RegistrationHandle = NULL;

    if ((CallbackRegistration->Version & 0xFF00) != (OB_FLT_REGISTRATION_VERSION & 0xFF00) ||
        CallbackRegistration->OperationRegistrationCount == 0 ||
        CallbackRegistration->OperationRegistration == NULL ||
        !ObpIsValidAltitude(&CallbackRegistration->Altitude))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (Index = 0; Index < CallbackRegistration->OperationRegistrationCount; Index++)
    {
        Operation = &CallbackRegistration->OperationRegistration[Index];
        if (Operation->ObjectType == NULL ||
            *Operation->ObjectType == NULL ||
            !(*Operation->ObjectType)->TypeInfo.SupportsObjectCallbacks ||
            Operation->Operations == 0 ||
            (Operation->Operations & ~(OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE)) ||
            (Operation->PreOperation == NULL && Operation->PostOperation == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    Size = FIELD_OFFSET(OBP_CALLBACK_REGISTRATION, Entries) +
           CallbackRegistration->OperationRegistrationCount * sizeof(OBP_CALLBACK_ENTRY) +
           CallbackRegistration->Altitude.Length;
    Registration = ExAllocatePoolWithTag(NonPagedPoolNx, Size, TAG_OB_CALLBACK);
    if (Registration == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Registration, Size);
    Registration->RegistrationContext = CallbackRegistration->RegistrationContext;
    Registration->Count = CallbackRegistration->OperationRegistrationCount;
    Registration->Altitude.Buffer = (PWCHAR)&Registration->Entries[Registration->Count];
    Registration->Altitude.Length = CallbackRegistration->Altitude.Length;
    Registration->Altitude.MaximumLength = CallbackRegistration->Altitude.Length;
    RtlCopyMemory(Registration->Altitude.Buffer,
                  CallbackRegistration->Altitude.Buffer,
                  CallbackRegistration->Altitude.Length);

    for (Index = 0; Index < Registration->Count; Index++)
    {
        Operation = &CallbackRegistration->OperationRegistration[Index];
        Entry = &Registration->Entries[Index];
        Entry->Registration = Registration;
        Entry->ObjectType = *Operation->ObjectType;
        Entry->Operations = Operation->Operations;
        Entry->PreOperation = Operation->PreOperation;
        Entry->PostOperation = Operation->PostOperation;
        ExInitializeRundownProtection(&Entry->RundownRef);
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&ObpCallbackLock);

    for (Link = ObpCallbackRegistrationList.Flink;
         Link != &ObpCallbackRegistrationList;
         Link = Link->Flink)
    {
        Other = CONTAINING_RECORD(Link, OBP_CALLBACK_REGISTRATION, ListEntry);
        if (ObpCompareAltitude(&Other->Altitude, &Registration->Altitude) == 0)
        {
            ExReleasePushLockExclusive(&ObpCallbackLock);
            KeLeaveCriticalRegion();
            ExFreePoolWithTag(Registration, TAG_OB_CALLBACK);
            return STATUS_FLT_INSTANCE_ALTITUDE_COLLISION;
        }
    }

    InsertTailList(&ObpCallbackRegistrationList, &Registration->ListEntry);

    for (Index = 0; Index < Registration->Count; Index++)
    {
        Entry = &Registration->Entries[Index];
        for (Link = Entry->ObjectType->CallbackList.Flink;
             Link != &Entry->ObjectType->CallbackList;
             Link = Link->Flink)
        {
            Other = CONTAINING_RECORD(Link, OBP_CALLBACK_ENTRY, ListEntry)->Registration;
            if (ObpCompareAltitude(&Other->Altitude, &Registration->Altitude) < 0)
            {
                break;
            }
        }
        InsertTailList(Link, &Entry->ListEntry);
    }

    ExReleasePushLockExclusive(&ObpCallbackLock);
    KeLeaveCriticalRegion();

    *RegistrationHandle = Registration;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ObUnRegisterCallbacks(
    _In_ PVOID RegistrationHandle)
{
    POBP_CALLBACK_REGISTRATION Registration = RegistrationHandle;
    USHORT Index;
    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&ObpCallbackLock);

    RemoveEntryList(&Registration->ListEntry);
    for (Index = 0; Index < Registration->Count; Index++)
    {
        RemoveEntryList(&Registration->Entries[Index].ListEntry);
    }

    ExReleasePushLockExclusive(&ObpCallbackLock);
    KeLeaveCriticalRegion();

    for (Index = 0; Index < Registration->Count; Index++)
    {
        ExWaitForRundownProtectionRelease(&Registration->Entries[Index].RundownRef);
    }

    ExFreePoolWithTag(Registration, TAG_OB_CALLBACK);
}
