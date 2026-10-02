/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object references, strings, altitudes and lock wrappers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

VOID
FltpInitializeObject(
    _Out_ PFLT_OBJECT Object,
    _In_ ULONG Signature)
{
    Object->Signature = Signature;
    Object->PointerCount = 1;
    ExInitializeRundownProtection(&Object->RundownRef);
    InitializeListHead(&Object->PrimaryLink);
}

VOID
FltpReferencePointer(
    _Inout_ PFLT_OBJECT Object)
{
    InterlockedIncrement(&Object->PointerCount);
}

VOID
FltpDereferencePointer(
    _Inout_ PFLT_OBJECT Object)
{
    PFLT_FILTER Filter;
    PFLT_VOLUME Volume;
    PFLT_INSTANCE Instance;

    if (InterlockedDecrement(&Object->PointerCount) != 0)
    {
        return;
    }

    switch (Object->Signature)
    {
        case FLT_SIGNATURE_FILTER:
            Filter = CONTAINING_RECORD(Object, FLT_FILTER, Base);
            FltpFreeString(&Filter->Name);
            FltpFreeString(&Filter->DefaultAltitude);
            Object->Signature = 0;
            ExFreePoolWithTag(Filter, FLT_TAG_FILTER);
            break;

        case FLT_SIGNATURE_VOLUME:
            Volume = CONTAINING_RECORD(Object, FLT_VOLUME, Base);
            FltpFreeString(&Volume->DeviceName);
            FltpFreeString(&Volume->GuidName);
            FltpFreeString(&Volume->FileSystemDriverName);
            if (Volume->DiskDeviceObject != NULL)
            {
                ObDereferenceObject(Volume->DiskDeviceObject);
            }
            if (Volume->BaseDeviceObject != NULL)
            {
                ObDereferenceObject(Volume->BaseDeviceObject);
            }
            Object->Signature = 0;
            ExFreePoolWithTag(Volume, FLT_TAG_VOLUME);
            break;

        case FLT_SIGNATURE_INSTANCE:
            Instance = CONTAINING_RECORD(Object, FLT_INSTANCE, Base);
            FltpFreeString(&Instance->Altitude);
            FltpFreeString(&Instance->Name);
            FltpDereferencePointer(&Instance->Volume->Base);
            FltpDereferencePointer(&Instance->Filter->Base);
            Object->Signature = 0;
            ExFreePoolWithTag(Instance, FLT_TAG_INSTANCE);
            break;

        default:
            break;
    }
}

NTSTATUS
FLTAPI
FltObjectReference(
    _Inout_ PVOID FltObject)
{
    PFLT_OBJECT Object = FltObject;

    if (!ExAcquireRundownProtection(&Object->RundownRef))
    {
        return STATUS_FLT_DELETING_OBJECT;
    }

    return STATUS_SUCCESS;
}

VOID
FLTAPI
FltObjectDereference(
    _Inout_ PVOID FltObject)
{
    PFLT_OBJECT Object = FltObject;

    ExReleaseRundownProtection(&Object->RundownRef);
}

NTSTATUS
FltpDuplicateString(
    _Out_ PUNICODE_STRING Destination,
    _In_ PCUNICODE_STRING Source)
{
    Destination->Length = 0;
    Destination->MaximumLength = Source->Length + sizeof(WCHAR);
    Destination->Buffer = ExAllocatePoolWithTag(NonPagedPoolNx, Destination->MaximumLength, FLT_TAG_NAME);
    if (Destination->Buffer == NULL)
    {
        Destination->MaximumLength = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Destination->Buffer, Source->Buffer, Source->Length);
    Destination->Length = Source->Length;
    Destination->Buffer[Source->Length / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

VOID
FltpFreeString(
    _Inout_ PUNICODE_STRING String)
{
    if (String->Buffer != NULL)
    {
        ExFreePoolWithTag(String->Buffer, FLT_TAG_NAME);
    }
    String->Buffer = NULL;
    String->Length = 0;
    String->MaximumLength = 0;
}

static
BOOLEAN
FltpSplitAltitude(
    _In_ PCUNICODE_STRING Altitude,
    _Out_ PUNICODE_STRING Integer,
    _Out_ PUNICODE_STRING Fraction)
{
    USHORT Count = Altitude->Length / sizeof(WCHAR);
    USHORT Index, Dot = Count;

    if (Count == 0 || Altitude->Buffer == NULL)
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

    if (Dot == Count)
    {
        Fraction->Buffer = NULL;
        Fraction->Length = 0;
    }
    else
    {
        Fraction->Buffer = Altitude->Buffer + Dot + 1;
        Fraction->Length = (Count - Dot - 1) * sizeof(WCHAR);
        while (Fraction->Length != 0 &&
               Fraction->Buffer[Fraction->Length / sizeof(WCHAR) - 1] == L'0')
        {
            Fraction->Length -= sizeof(WCHAR);
        }
    }
    Fraction->MaximumLength = Fraction->Length;
    return TRUE;
}

BOOLEAN
FltpIsValidAltitude(
    _In_ PCUNICODE_STRING Altitude)
{
    UNICODE_STRING Integer, Fraction;

    return Altitude != NULL && FltpSplitAltitude(Altitude, &Integer, &Fraction);
}

LONG
FltpCompareAltitude(
    _In_ PCUNICODE_STRING First,
    _In_ PCUNICODE_STRING Second)
{
    UNICODE_STRING FirstInteger, FirstFraction, SecondInteger, SecondFraction;
    LONG Result;

    if (!FltpSplitAltitude(First, &FirstInteger, &FirstFraction) ||
        !FltpSplitAltitude(Second, &SecondInteger, &SecondFraction))
    {
        return RtlCompareUnicodeString(First, Second, TRUE);
    }

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

LONG
FLTAPI
FltCompareInstanceAltitudes(
    _In_ PFLT_INSTANCE Instance1,
    _In_ PFLT_INSTANCE Instance2)
{
    return FltpCompareAltitude(&Instance1->Altitude, &Instance2->Altitude);
}

NTSTATUS
FltpOpenServiceKey(
    _In_ PCUNICODE_STRING ServiceName,
    _In_opt_ PCWSTR SubKey,
    _Out_ PHANDLE Key)
{
    static CONST WCHAR Prefix[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Path;
    NTSTATUS Status;
    SIZE_T Length;

    Length = sizeof(Prefix) + ServiceName->Length + sizeof(WCHAR);
    if (SubKey != NULL)
    {
        Length += (wcslen(SubKey) + 1) * sizeof(WCHAR);
    }
    if (Length > MAXUSHORT)
    {
        return STATUS_NAME_TOO_LONG;
    }

    Path.Buffer = ExAllocatePoolWithTag(PagedPool, Length, FLT_TAG_NAME);
    if (Path.Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Path.Length = 0;
    Path.MaximumLength = (USHORT)Length;

    RtlAppendUnicodeToString(&Path, Prefix);
    RtlAppendUnicodeStringToString(&Path, ServiceName);
    if (SubKey != NULL)
    {
        RtlAppendUnicodeToString(&Path, L"\\");
        RtlAppendUnicodeToString(&Path, SubKey);
    }

    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(Key, KEY_READ, &ObjectAttributes);
    ExFreePoolWithTag(Path.Buffer, FLT_TAG_NAME);
    return Status;
}

static
NTSTATUS
FltpQueryValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _Out_ PKEY_VALUE_PARTIAL_INFORMATION *Information)
{
    PKEY_VALUE_PARTIAL_INFORMATION Buffer;
    UNICODE_STRING Name;
    ULONG Length = 0;
    NTSTATUS Status;

    *Information = NULL;
    RtlInitUnicodeString(&Name, ValueName);

    Status = ZwQueryValueKey(Key, &Name, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
    {
        return NT_SUCCESS(Status) ? STATUS_OBJECT_NAME_NOT_FOUND : Status;
    }

    Buffer = ExAllocatePoolWithTag(PagedPool, Length, FLT_TAG_GENERAL);
    if (Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQueryValueKey(Key, &Name, KeyValuePartialInformation, Buffer, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, FLT_TAG_GENERAL);
        return Status;
    }

    *Information = Buffer;
    return STATUS_SUCCESS;
}

NTSTATUS
FltpQueryStringValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _Out_ PUNICODE_STRING Value)
{
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    UNICODE_STRING Source;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(Value, NULL, 0);

    Status = FltpQueryValue(Key, ValueName, &Information);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if ((Information->Type != REG_SZ && Information->Type != REG_EXPAND_SZ && Information->Type != REG_MULTI_SZ) ||
        Information->DataLength < sizeof(WCHAR) ||
        Information->DataLength > MAXUSHORT)
    {
        ExFreePoolWithTag(Information, FLT_TAG_GENERAL);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    Source.Buffer = (PWCHAR)Information->Data;
    Source.Length = 0;
    while (Source.Length + sizeof(WCHAR) <= Information->DataLength &&
           Source.Buffer[Source.Length / sizeof(WCHAR)] != UNICODE_NULL)
    {
        Source.Length += sizeof(WCHAR);
    }
    Source.MaximumLength = Source.Length;

    Status = FltpDuplicateString(Value, &Source);
    ExFreePoolWithTag(Information, FLT_TAG_GENERAL);
    return Status;
}

NTSTATUS
FltpQueryDwordValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _Out_ PULONG Value)
{
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    NTSTATUS Status;

    *Value = 0;

    Status = FltpQueryValue(Key, ValueName, &Information);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Information->Type != REG_DWORD || Information->DataLength != sizeof(ULONG))
    {
        ExFreePoolWithTag(Information, FLT_TAG_GENERAL);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    *Value = *(PULONG)Information->Data;
    ExFreePoolWithTag(Information, FLT_TAG_GENERAL);
    return STATUS_SUCCESS;
}

VOID
FLTAPI
FltInitializePushLock(
    _Out_ PEX_PUSH_LOCK PushLock)
{
    FltpInitializeLock(PushLock);
}

VOID
FLTAPI
FltDeletePushLock(
    _In_ PEX_PUSH_LOCK PushLock)
{
    UNREFERENCED_PARAMETER(PushLock);
}

VOID
FLTAPI
FltAcquirePushLockExclusive(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(PushLock);
}

VOID
FLTAPI
FltAcquirePushLockShared(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(PushLock);
}

VOID
FLTAPI
FltReleasePushLock(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    ExReleasePushLock(PushLock);
    KeLeaveCriticalRegion();
}

VOID
FLTAPI
FltAcquirePushLockExclusiveEx(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    FltAcquirePushLockExclusive(PushLock);
}

VOID
FLTAPI
FltAcquirePushLockSharedEx(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    FltAcquirePushLockShared(PushLock);
}

VOID
FLTAPI
FltReleasePushLockEx(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    FltReleasePushLock(PushLock);
}

VOID
FLTAPI
FltAcquireResourceExclusive(
    _Inout_ PERESOURCE Resource)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(Resource, TRUE);
}

VOID
FLTAPI
FltAcquireResourceShared(
    _Inout_ PERESOURCE Resource)
{
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(Resource, TRUE);
}

VOID
FLTAPI
FltReleaseResource(
    _Inout_ PERESOURCE Resource)
{
    ExReleaseResourceLite(Resource);
    KeLeaveCriticalRegion();
}
