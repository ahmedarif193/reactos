/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minifilter instances
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
FltpReadInstanceKey(
    _In_ HANDLE InstancesKey,
    _In_ PCUNICODE_STRING InstanceName,
    _Out_ PUNICODE_STRING Altitude,
    _Out_ PULONG Flags)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(Altitude, NULL, 0);
    *Flags = 0;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)InstanceName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               InstancesKey,
                               NULL);
    Status = ZwOpenKey(&Key, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = FltpQueryStringValue(Key, L"Altitude", Altitude);
    if (NT_SUCCESS(Status))
    {
        (VOID)FltpQueryDwordValue(Key, L"Flags", Flags);
        if (!FltpIsValidAltitude(Altitude))
        {
            FltpFreeString(Altitude);
            Status = STATUS_INVALID_PARAMETER;
        }
    }

    ZwClose(Key);
    return Status;
}

static
NTSTATUS
FltpOpenInstancesKey(
    _In_ PFLT_FILTER Filter,
    _Out_ PHANDLE Key)
{
    NTSTATUS Status;

    Status = FltpOpenServiceKey(&Filter->Name, L"Parameters\\Instances", Key);
    if (!NT_SUCCESS(Status))
    {
        Status = FltpOpenServiceKey(&Filter->Name, L"Instances", Key);
    }
    return Status;
}

NTSTATUS
FltpReadInstanceRegistration(
    _In_ PFLT_FILTER Filter,
    _In_opt_ PCUNICODE_STRING InstanceName,
    _Out_ PUNICODE_STRING ResolvedName,
    _Out_ PUNICODE_STRING Altitude,
    _Out_ PULONG Flags)
{
    HANDLE Key;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(ResolvedName, NULL, 0);
    RtlInitEmptyUnicodeString(Altitude, NULL, 0);
    *Flags = 0;

    Status = FltpOpenInstancesKey(Filter, &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (InstanceName != NULL)
    {
        Status = FltpDuplicateString(ResolvedName, InstanceName);
    }
    else
    {
        Status = FltpQueryStringValue(Key, L"DefaultInstance", ResolvedName);
    }

    if (NT_SUCCESS(Status))
    {
        Status = FltpReadInstanceKey(Key, ResolvedName, Altitude, Flags);
        if (!NT_SUCCESS(Status))
        {
            FltpFreeString(ResolvedName);
        }
    }

    ZwClose(Key);
    return Status;
}

NTSTATUS
FltpCreateInstance(
    _In_ PFLT_FILTER Filter,
    _In_ PFLT_VOLUME Volume,
    _In_ PCUNICODE_STRING Altitude,
    _In_opt_ PCUNICODE_STRING InstanceName,
    _In_ FLT_INSTANCE_SETUP_FLAGS SetupFlags,
    _Out_opt_ PFLT_INSTANCE *RetInstance)
{
    FLTP_RELATED_OBJECTS FltObjects;
    PFLT_INSTANCE Instance, Other;
    PLIST_ENTRY Link;
    NTSTATUS Status;
    PAGED_CODE();

    if (RetInstance != NULL)
    {
        *RetInstance = NULL;
    }

    if (!(Filter->Flags & FLTP_FILTER_STARTED))
    {
        return STATUS_FLT_FILTER_NOT_READY;
    }
    if ((Filter->Flags & FLTP_FILTER_UNREGISTERING) || (Volume->Flags & FLTP_VOLUME_DISMOUNTED))
    {
        return STATUS_FLT_DELETING_OBJECT;
    }
    if (!FltpIsValidAltitude(Altitude))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Instance = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Instance), FLT_TAG_INSTANCE);
    if (Instance == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Instance, sizeof(*Instance));
    FltpInitializeObject(&Instance->Base, FLT_SIGNATURE_INSTANCE);
    InitializeListHead(&Instance->ContextList);
    InitializeListHead(&Instance->OwnContextList);
    Instance->Flags = FLTP_INSTANCE_INITIALIZING;
    Instance->Volume = Volume;
    Instance->Filter = Filter;
    FltpReferencePointer(&Volume->Base);
    FltpReferencePointer(&Filter->Base);

    Status = FltpDuplicateString(&Instance->Altitude, Altitude);
    if (NT_SUCCESS(Status))
    {
        if (InstanceName != NULL)
        {
            Status = FltpDuplicateString(&Instance->Name, InstanceName);
        }
        else
        {
            Instance->Name.MaximumLength = (USHORT)min(INSTANCE_NAME_MAX_BYTES,
                                                       Filter->Name.Length + Altitude->Length + 3 * sizeof(WCHAR));
            Instance->Name.Buffer = ExAllocatePoolWithTag(NonPagedPoolNx, Instance->Name.MaximumLength, FLT_TAG_NAME);
            if (Instance->Name.Buffer == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                RtlAppendUnicodeStringToString(&Instance->Name, &Filter->Name);
                RtlAppendUnicodeToString(&Instance->Name, L" ");
                RtlAppendUnicodeStringToString(&Instance->Name, Altitude);
            }
        }
    }
    if (!NT_SUCCESS(Status))
    {
        FltpDereferencePointer(&Instance->Base);
        return Status;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&FltGlobals.Lock, TRUE);
    ExAcquirePushLockExclusive(&Volume->InstanceLock);

    for (Link = Volume->InstanceList.Flink; Link != &Volume->InstanceList; Link = Link->Flink)
    {
        Other = CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink);
        if (FltpCompareAltitude(&Other->Altitude, &Instance->Altitude) == 0)
        {
            Status = STATUS_FLT_INSTANCE_ALTITUDE_COLLISION;
            break;
        }
        if (RtlEqualUnicodeString(&Other->Name, &Instance->Name, TRUE))
        {
            Status = STATUS_FLT_INSTANCE_NAME_COLLISION;
            break;
        }
    }

    if (NT_SUCCESS(Status))
    {
        for (Link = Volume->InstanceList.Flink; Link != &Volume->InstanceList; Link = Link->Flink)
        {
            Other = CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink);
            if (FltpCompareAltitude(&Other->Altitude, &Instance->Altitude) < 0)
            {
                break;
            }
        }
        InsertTailList(Link, &Instance->VolumeLink);
        Volume->InstanceCount++;
        InsertTailList(&Filter->InstanceList, &Instance->FilterLink);
        FltpReferencePointer(&Instance->Base);
    }

    ExReleasePushLockExclusive(&Volume->InstanceLock);
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    if (!NT_SUCCESS(Status))
    {
        FltpDereferencePointer(&Instance->Base);
        return Status;
    }

    if (Filter->InstanceSetup != NULL)
    {
        RtlZeroMemory(&FltObjects, sizeof(FltObjects));
        FltObjects.Size = sizeof(FltObjects);
        FltObjects.Filter = Filter;
        FltObjects.Volume = Volume;
        FltObjects.Instance = Instance;

        Status = Filter->InstanceSetup((PCFLT_RELATED_OBJECTS)&FltObjects, SetupFlags, Volume->DeviceType, Volume->FileSystemType);
        if (Status != STATUS_SUCCESS)
        {
            if (NT_SUCCESS(Status))
            {
                Status = STATUS_FLT_DO_NOT_ATTACH;
            }
        }
    }

    if (!NT_SUCCESS(Status))
    {
        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&FltGlobals.Lock, TRUE);
        ExAcquirePushLockExclusive(&Volume->InstanceLock);
        RemoveEntryList(&Instance->VolumeLink);
        Volume->InstanceCount--;
        RemoveEntryList(&Instance->FilterLink);
        ExReleasePushLockExclusive(&Volume->InstanceLock);
        ExReleaseResourceLite(&FltGlobals.Lock);
        KeLeaveCriticalRegion();

        FltpDeleteInstanceEnlistments(Instance);
        FltpDeleteInstanceContexts(Instance);
        FltpDereferencePointer(&Instance->Base);
        FltpDereferencePointer(&Instance->Base);
        return Status;
    }

    if (RetInstance != NULL)
    {
        if (ExAcquireRundownProtection(&Instance->Base.RundownRef))
        {
            *RetInstance = Instance;
        }
        else
        {
            Status = STATUS_FLT_DELETING_OBJECT;
        }
    }

    InterlockedAnd((PLONG)&Instance->Flags, ~FLTP_INSTANCE_INITIALIZING);
    FltpDereferencePointer(&Instance->Base);
    return Status;
}

VOID
FltpTeardownInstance(
    _In_ PFLT_INSTANCE Instance,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Reason)
{
    PFLT_FILTER Filter = Instance->Filter;
    PFLT_VOLUME Volume = Instance->Volume;
    FLTP_RELATED_OBJECTS FltObjects;
    PAGED_CODE();

    if (InterlockedOr((PLONG)&Instance->Flags, FLTP_INSTANCE_DETACHING) & FLTP_INSTANCE_DETACHING)
    {
        return;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&FltGlobals.Lock, TRUE);
    ExAcquirePushLockExclusive(&Volume->InstanceLock);
    RemoveEntryList(&Instance->VolumeLink);
    InitializeListHead(&Instance->VolumeLink);
    Volume->InstanceCount--;
    RemoveEntryList(&Instance->FilterLink);
    InitializeListHead(&Instance->FilterLink);
    ExReleasePushLockExclusive(&Volume->InstanceLock);
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    RtlZeroMemory(&FltObjects, sizeof(FltObjects));
    FltObjects.Size = sizeof(FltObjects);
    FltObjects.Filter = Filter;
    FltObjects.Volume = Volume;
    FltObjects.Instance = Instance;

    if (Filter->InstanceTeardownStart != NULL)
    {
        Filter->InstanceTeardownStart((PCFLT_RELATED_OBJECTS)&FltObjects, Reason);
    }

    FltpDrainInstance(Instance);
    ExWaitForRundownProtectionRelease(&Instance->Base.RundownRef);

    if (Filter->InstanceTeardownComplete != NULL)
    {
        Filter->InstanceTeardownComplete((PCFLT_RELATED_OBJECTS)&FltObjects, Reason);
    }

    FltpDeleteInstanceEnlistments(Instance);
    FltpDeleteInstanceContexts(Instance);
    FltpDereferencePointer(&Instance->Base);
}

VOID
FltpAutoAttach(
    _In_ PFLT_FILTER Filter,
    _In_ PFLT_VOLUME Volume,
    _In_ FLT_INSTANCE_SETUP_FLAGS SetupFlags)
{
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + INSTANCE_NAME_MAX_BYTES];
    PKEY_BASIC_INFORMATION Information = (PKEY_BASIC_INFORMATION)Buffer;
    UNICODE_STRING Name, Altitude;
    ULONG Index, Length, Flags;
    HANDLE Key;
    NTSTATUS Status;
    PAGED_CODE();

    Status = FltpOpenInstancesKey(Filter, &Key);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    for (Index = 0; ; Index++)
    {
        Status = ZwEnumerateKey(Key, Index, KeyBasicInformation, Information, sizeof(Buffer), &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        if (!NT_SUCCESS(Status))
        {
            continue;
        }

        Name.Buffer = Information->Name;
        Name.Length = Name.MaximumLength = (USHORT)Information->NameLength;

        Status = FltpReadInstanceKey(Key, &Name, &Altitude, &Flags);
        if (!NT_SUCCESS(Status))
        {
            continue;
        }

        if (!(Flags & FLTP_INSTANCE_FLAG_NO_AUTOMATIC))
        {
            (VOID)FltpCreateInstance(Filter, Volume, &Altitude, &Name, SetupFlags, NULL);
        }

        FltpFreeString(&Altitude);
    }

    ZwClose(Key);
}

VOID
FltpAttachFilterToVolumes(
    _In_ PFLT_FILTER Filter)
{
    PFLT_VOLUME *Volumes;
    ULONG Count = 0, Index;
    NTSTATUS Status;

    Status = FltEnumerateVolumes(Filter, NULL, 0, &Count);
    if (!NT_SUCCESS(Status) || Count == 0)
    {
        return;
    }

    Count += 4;
    Volumes = ExAllocatePoolWithTag(PagedPool, Count * sizeof(PFLT_VOLUME), FLT_TAG_GENERAL);
    if (Volumes == NULL)
    {
        return;
    }

    Status = FltEnumerateVolumes(Filter, Volumes, Count, &Count);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Count; Index++)
        {
            if (!(Volumes[Index]->Flags & FLTP_VOLUME_SETUP_PENDING))
            {
                FltpAutoAttach(Filter, Volumes[Index], FLTFL_INSTANCE_SETUP_AUTOMATIC_ATTACHMENT);
            }
            ExReleaseRundownProtection(&Volumes[Index]->Base.RundownRef);
        }
    }

    ExFreePoolWithTag(Volumes, FLT_TAG_GENERAL);
}

VOID
FltpAttachFiltersToVolume(
    _In_ PFLT_VOLUME Volume,
    _In_ BOOLEAN NewlyMounted)
{
    PFLT_FILTER *Filters;
    ULONG Count = 0, Index;
    NTSTATUS Status;
    FLT_INSTANCE_SETUP_FLAGS Flags = FLTFL_INSTANCE_SETUP_AUTOMATIC_ATTACHMENT;

    if (NewlyMounted)
    {
        Flags |= FLTFL_INSTANCE_SETUP_NEWLY_MOUNTED_VOLUME;
    }

    Status = FltEnumerateFilters(NULL, 0, &Count);
    if (!NT_SUCCESS(Status) || Count == 0)
    {
        return;
    }

    Count += 4;
    Filters = ExAllocatePoolWithTag(PagedPool, Count * sizeof(PFLT_FILTER), FLT_TAG_GENERAL);
    if (Filters == NULL)
    {
        return;
    }

    Status = FltEnumerateFilters(Filters, Count, &Count);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Count; Index++)
        {
            if (Filters[Index]->Flags & FLTP_FILTER_STARTED)
            {
                FltpAutoAttach(Filters[Index], Volume, Flags);
            }
            ExReleaseRundownProtection(&Filters[Index]->Base.RundownRef);
        }
    }

    ExFreePoolWithTag(Filters, FLT_TAG_GENERAL);
}

NTSTATUS
FLTAPI
FltAttachVolume(
    _Inout_ PFLT_FILTER Filter,
    _Inout_ PFLT_VOLUME Volume,
    _In_opt_ PCUNICODE_STRING InstanceName,
    _Outptr_opt_result_maybenull_ PFLT_INSTANCE *RetInstance)
{
    UNICODE_STRING Name, Altitude;
    ULONG Flags;
    NTSTATUS Status;
    PAGED_CODE();

    if (RetInstance != NULL)
    {
        *RetInstance = NULL;
    }

    Status = FltpReadInstanceRegistration(Filter, InstanceName, &Name, &Altitude, &Flags);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Flags & FLTP_INSTANCE_FLAG_NO_MANUAL)
    {
        Status = STATUS_FLT_DO_NOT_ATTACH;
    }
    else
    {
        Status = FltpCreateInstance(Filter,
                                    Volume,
                                    &Altitude,
                                    &Name,
                                    FLTFL_INSTANCE_SETUP_MANUAL_ATTACHMENT,
                                    RetInstance);
        if (Status == STATUS_FLT_INSTANCE_ALTITUDE_COLLISION)
        {
            Status = STATUS_OBJECT_NAME_COLLISION;
        }
    }

    FltpFreeString(&Name);
    FltpFreeString(&Altitude);
    return Status;
}

NTSTATUS
FLTAPI
FltAttachVolumeAtAltitude(
    _Inout_ PFLT_FILTER Filter,
    _Inout_ PFLT_VOLUME Volume,
    _In_ PCUNICODE_STRING Altitude,
    _In_opt_ PCUNICODE_STRING InstanceName,
    _Outptr_opt_result_maybenull_ PFLT_INSTANCE *RetInstance)
{
    PAGED_CODE();

    return FltpCreateInstance(Filter,
                              Volume,
                              Altitude,
                              InstanceName,
                              FLTFL_INSTANCE_SETUP_MANUAL_ATTACHMENT,
                              RetInstance);
}

NTSTATUS
FLTAPI
FltGetVolumeInstanceFromName(
    _In_opt_ PFLT_FILTER Filter,
    _In_ PFLT_VOLUME Volume,
    _In_opt_ PCUNICODE_STRING InstanceName,
    _Outptr_ PFLT_INSTANCE *RetInstance)
{
    PFLT_INSTANCE Instance;
    PLIST_ENTRY Link;
    NTSTATUS Status = STATUS_FLT_INSTANCE_NOT_FOUND;

    *RetInstance = NULL;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&Volume->InstanceLock);
    for (Link = Volume->InstanceList.Flink; Link != &Volume->InstanceList; Link = Link->Flink)
    {
        Instance = CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink);
        if ((Filter == NULL || Instance->Filter == Filter) &&
            (InstanceName == NULL || RtlEqualUnicodeString(&Instance->Name, InstanceName, TRUE)))
        {
            if (ExAcquireRundownProtection(&Instance->Base.RundownRef))
            {
                *RetInstance = Instance;
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = STATUS_FLT_DELETING_OBJECT;
            }
            break;
        }
    }
    ExReleasePushLockShared(&Volume->InstanceLock);
    KeLeaveCriticalRegion();
    return Status;
}

NTSTATUS
FLTAPI
FltDetachVolume(
    _Inout_ PFLT_FILTER Filter,
    _Inout_ PFLT_VOLUME Volume,
    _In_opt_ PCUNICODE_STRING InstanceName)
{
    FLTP_RELATED_OBJECTS FltObjects;
    PFLT_INSTANCE Instance;
    NTSTATUS Status;
    PAGED_CODE();

    Status = FltGetVolumeInstanceFromName(Filter, Volume, InstanceName, &Instance);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Filter->InstanceQueryTeardown == NULL)
    {
        ExReleaseRundownProtection(&Instance->Base.RundownRef);
        return STATUS_FLT_DO_NOT_DETACH;
    }

    RtlZeroMemory(&FltObjects, sizeof(FltObjects));
    FltObjects.Size = sizeof(FltObjects);
    FltObjects.Filter = Filter;
    FltObjects.Volume = Volume;
    FltObjects.Instance = Instance;

    Status = Filter->InstanceQueryTeardown((PCFLT_RELATED_OBJECTS)&FltObjects, 0);
    FltpReferencePointer(&Instance->Base);
    ExReleaseRundownProtection(&Instance->Base.RundownRef);
    if (Status == STATUS_SUCCESS)
    {
        FltpTeardownInstance(Instance, FLTFL_INSTANCE_TEARDOWN_MANUAL);
    }
    else if (NT_SUCCESS(Status))
    {
        Status = STATUS_FLT_DO_NOT_DETACH;
    }

    FltpDereferencePointer(&Instance->Base);
    return Status;
}

NTSTATUS
FLTAPI
FltEnumerateInstances(
    _In_opt_ PFLT_VOLUME Volume,
    _In_opt_ PFLT_FILTER Filter,
    _Out_writes_to_opt_(InstanceListSize, *NumberInstancesReturned) PFLT_INSTANCE *InstanceList,
    _In_ ULONG InstanceListSize,
    _Out_ PULONG NumberInstancesReturned)
{
    PFLT_INSTANCE Instance;
    PLIST_ENTRY Head, Link;
    ULONG Count = 0, Index;
    NTSTATUS Status = STATUS_SUCCESS;

    *NumberInstancesReturned = 0;

    if (Volume == NULL && Filter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);
    if (Volume != NULL)
    {
        ExAcquirePushLockShared(&Volume->InstanceLock);
        Head = &Volume->InstanceList;
    }
    else
    {
        Head = &Filter->InstanceList;
    }

    for (Link = Head->Flink; Link != Head; Link = Link->Flink)
    {
        Instance = (Volume != NULL) ? CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink)
                                    : CONTAINING_RECORD(Link, FLT_INSTANCE, FilterLink);
        if (Volume != NULL && Filter != NULL && Instance->Filter != Filter)
        {
            continue;
        }
        if (!ExAcquireRundownProtection(&Instance->Base.RundownRef))
        {
            continue;
        }

        if (InstanceList != NULL && Count < InstanceListSize)
        {
            InstanceList[Count] = Instance;
        }
        else
        {
            ExReleaseRundownProtection(&Instance->Base.RundownRef);
        }
        Count++;
    }

    if (Volume != NULL)
    {
        ExReleasePushLockShared(&Volume->InstanceLock);
    }
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    if (InstanceList != NULL && Count > InstanceListSize)
    {
        for (Index = 0; Index < InstanceListSize; Index++)
        {
            ExReleaseRundownProtection(&InstanceList[Index]->Base.RundownRef);
        }
        Status = STATUS_BUFFER_TOO_SMALL;
    }

    *NumberInstancesReturned = Count;
    return Status;
}

static
NTSTATUS
FltpGetNeighbourInstance(
    _In_ PFLT_VOLUME Volume,
    _In_opt_ PFLT_INSTANCE Current,
    _In_ BOOLEAN Lower,
    _Outptr_ PFLT_INSTANCE *RetInstance)
{
    PFLT_INSTANCE Instance;
    PLIST_ENTRY Link;
    NTSTATUS Status = STATUS_NO_MORE_ENTRIES;

    *RetInstance = NULL;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&Volume->InstanceLock);

    if (Current == NULL)
    {
        Link = Lower ? Volume->InstanceList.Flink : Volume->InstanceList.Blink;
    }
    else if (IsListEmpty(&Current->VolumeLink))
    {
        Link = &Volume->InstanceList;
    }
    else
    {
        Link = Lower ? Current->VolumeLink.Flink : Current->VolumeLink.Blink;
    }

    while (Link != &Volume->InstanceList)
    {
        Instance = CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink);
        if (ExAcquireRundownProtection(&Instance->Base.RundownRef))
        {
            *RetInstance = Instance;
            Status = STATUS_SUCCESS;
            break;
        }
        Link = Lower ? Link->Flink : Link->Blink;
    }

    ExReleasePushLockShared(&Volume->InstanceLock);
    KeLeaveCriticalRegion();
    return Status;
}

NTSTATUS
FLTAPI
FltGetTopInstance(
    _In_ PFLT_VOLUME Volume,
    _Outptr_ PFLT_INSTANCE *Instance)
{
    return FltpGetNeighbourInstance(Volume, NULL, TRUE, Instance);
}

NTSTATUS
FLTAPI
FltGetBottomInstance(
    _In_ PFLT_VOLUME Volume,
    _Outptr_ PFLT_INSTANCE *Instance)
{
    return FltpGetNeighbourInstance(Volume, NULL, FALSE, Instance);
}

NTSTATUS
FLTAPI
FltGetLowerInstance(
    _In_ PFLT_INSTANCE CurrentInstance,
    _Outptr_ PFLT_INSTANCE *LowerInstance)
{
    return FltpGetNeighbourInstance(CurrentInstance->Volume, CurrentInstance, TRUE, LowerInstance);
}

NTSTATUS
FLTAPI
FltGetUpperInstance(
    _In_ PFLT_INSTANCE CurrentInstance,
    _Outptr_ PFLT_INSTANCE *UpperInstance)
{
    return FltpGetNeighbourInstance(CurrentInstance->Volume, CurrentInstance, FALSE, UpperInstance);
}

NTSTATUS
FLTAPI
FltGetInstanceInformation(
    _In_ PFLT_INSTANCE Instance,
    _In_ INSTANCE_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    PINSTANCE_BASIC_INFORMATION Basic = Buffer;
    PINSTANCE_PARTIAL_INFORMATION Partial = Buffer;
    PINSTANCE_FULL_INFORMATION Full = Buffer;
    PINSTANCE_AGGREGATE_STANDARD_INFORMATION Standard = Buffer;
    PFLT_VOLUME Volume = Instance->Volume;
    PFLT_FILTER Filter = Instance->Filter;
    ULONG Needed, Offset;

    switch (InformationClass)
    {
        case InstanceBasicInformation:
            Needed = sizeof(*Basic) + Instance->Name.Length;
            *BytesReturned = Needed;
            if (Buffer == NULL || BufferSize < Needed)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            Basic->NextEntryOffset = 0;
            Basic->InstanceNameLength = Instance->Name.Length;
            Basic->InstanceNameBufferOffset = sizeof(*Basic);
            RtlCopyMemory(Basic + 1, Instance->Name.Buffer, Instance->Name.Length);
            return STATUS_SUCCESS;

        case InstancePartialInformation:
            Needed = sizeof(*Partial) + Instance->Name.Length + Instance->Altitude.Length;
            *BytesReturned = Needed;
            if (Buffer == NULL || BufferSize < Needed)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            Partial->NextEntryOffset = 0;
            Offset = sizeof(*Partial);
            Partial->InstanceNameLength = Instance->Name.Length;
            Partial->InstanceNameBufferOffset = (USHORT)Offset;
            RtlCopyMemory((PUCHAR)Buffer + Offset, Instance->Name.Buffer, Instance->Name.Length);
            Offset += Instance->Name.Length;
            Partial->AltitudeLength = Instance->Altitude.Length;
            Partial->AltitudeBufferOffset = (USHORT)Offset;
            RtlCopyMemory((PUCHAR)Buffer + Offset, Instance->Altitude.Buffer, Instance->Altitude.Length);
            return STATUS_SUCCESS;

        case InstanceFullInformation:
            Needed = sizeof(*Full) + Instance->Name.Length + Instance->Altitude.Length +
                     Volume->DeviceName.Length + Filter->Name.Length;
            *BytesReturned = Needed;
            if (Buffer == NULL || BufferSize < Needed)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            Full->NextEntryOffset = 0;
            Offset = sizeof(*Full);
            Full->InstanceNameLength = Instance->Name.Length;
            Full->InstanceNameBufferOffset = (USHORT)Offset;
            RtlCopyMemory((PUCHAR)Buffer + Offset, Instance->Name.Buffer, Instance->Name.Length);
            Offset += Instance->Name.Length;
            Full->AltitudeLength = Instance->Altitude.Length;
            Full->AltitudeBufferOffset = (USHORT)Offset;
            RtlCopyMemory((PUCHAR)Buffer + Offset, Instance->Altitude.Buffer, Instance->Altitude.Length);
            Offset += Instance->Altitude.Length;
            Full->VolumeNameLength = Volume->DeviceName.Length;
            Full->VolumeNameBufferOffset = (USHORT)Offset;
            RtlCopyMemory((PUCHAR)Buffer + Offset, Volume->DeviceName.Buffer, Volume->DeviceName.Length);
            Offset += Volume->DeviceName.Length;
            Full->FilterNameLength = Filter->Name.Length;
            Full->FilterNameBufferOffset = (USHORT)Offset;
            RtlCopyMemory((PUCHAR)Buffer + Offset, Filter->Name.Buffer, Filter->Name.Length);
            return STATUS_SUCCESS;

        case InstanceAggregateStandardInformation:
            Needed = sizeof(*Standard) + Instance->Name.Length + Instance->Altitude.Length +
                     Volume->DeviceName.Length + Filter->Name.Length;
            *BytesReturned = Needed;
            if (Buffer == NULL || BufferSize < Needed)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            RtlZeroMemory(Standard, sizeof(*Standard));
            Standard->Flags = FLTFL_IASI_IS_MINIFILTER;
            Standard->Type.MiniFilter.FrameID = 0;
            Standard->Type.MiniFilter.VolumeFileSystemType = Volume->FileSystemType;
            Offset = sizeof(*Standard);
            Standard->Type.MiniFilter.InstanceNameLength = Instance->Name.Length;
            Standard->Type.MiniFilter.InstanceNameBufferOffset = (USHORT)Offset;
            RtlCopyMemory((PUCHAR)Buffer + Offset, Instance->Name.Buffer, Instance->Name.Length);
            Offset += Instance->Name.Length;
            Standard->Type.MiniFilter.AltitudeLength = Instance->Altitude.Length;
            Standard->Type.MiniFilter.AltitudeBufferOffset = (USHORT)Offset;
            RtlCopyMemory((PUCHAR)Buffer + Offset, Instance->Altitude.Buffer, Instance->Altitude.Length);
            Offset += Instance->Altitude.Length;
            Standard->Type.MiniFilter.VolumeNameLength = Volume->DeviceName.Length;
            Standard->Type.MiniFilter.VolumeNameBufferOffset = (USHORT)Offset;
            RtlCopyMemory((PUCHAR)Buffer + Offset, Volume->DeviceName.Buffer, Volume->DeviceName.Length);
            Offset += Volume->DeviceName.Length;
            Standard->Type.MiniFilter.FilterNameLength = Filter->Name.Length;
            Standard->Type.MiniFilter.FilterNameBufferOffset = (USHORT)Offset;
            RtlCopyMemory((PUCHAR)Buffer + Offset, Filter->Name.Buffer, Filter->Name.Length);
            return STATUS_SUCCESS;

        default:
            *BytesReturned = 0;
            return STATUS_INVALID_PARAMETER;
    }
}

static
NTSTATUS
FltpEnumerateInstanceInformation(
    _In_opt_ PFLT_VOLUME Volume,
    _In_opt_ PFLT_FILTER Filter,
    _In_ ULONG Index,
    _In_ INSTANCE_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    PFLT_INSTANCE *Instances;
    ULONG Count = 0, Position;
    NTSTATUS Status;

    *BytesReturned = 0;

    Status = FltEnumerateInstances(Volume, Filter, NULL, 0, &Count);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Index >= Count)
    {
        return STATUS_NO_MORE_ENTRIES;
    }

    Count += 4;
    Instances = ExAllocatePoolWithTag(PagedPool, Count * sizeof(PFLT_INSTANCE), FLT_TAG_GENERAL);
    if (Instances == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = FltEnumerateInstances(Volume, Filter, Instances, Count, &Count);
    if (NT_SUCCESS(Status))
    {
        if (Index < Count)
        {
            Status = FltGetInstanceInformation(Instances[Index], InformationClass, Buffer, BufferSize, BytesReturned);
        }
        else
        {
            Status = STATUS_NO_MORE_ENTRIES;
        }

        for (Position = 0; Position < Count; Position++)
        {
            ExReleaseRundownProtection(&Instances[Position]->Base.RundownRef);
        }
    }

    ExFreePoolWithTag(Instances, FLT_TAG_GENERAL);
    return Status;
}

NTSTATUS
FLTAPI
FltEnumerateInstanceInformationByFilter(
    _In_ PFLT_FILTER Filter,
    _In_ ULONG Index,
    _In_ INSTANCE_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    return FltpEnumerateInstanceInformation(NULL, Filter, Index, InformationClass, Buffer, BufferSize, BytesReturned);
}

NTSTATUS
FLTAPI
FltEnumerateInstanceInformationByVolume(
    _In_ PFLT_VOLUME Volume,
    _In_ ULONG Index,
    _In_ INSTANCE_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    return FltpEnumerateInstanceInformation(Volume, NULL, Index, InformationClass, Buffer, BufferSize, BytesReturned);
}

NTSTATUS
FLTAPI
FltEnumerateInstanceInformationByVolumeName(
    _In_ PUNICODE_STRING VolumeName,
    _In_ ULONG Index,
    _In_ INSTANCE_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    PFLT_VOLUME Volume;
    NTSTATUS Status;

    Status = FltGetVolumeFromName(NULL, VolumeName, &Volume);
    if (!NT_SUCCESS(Status))
    {
        *BytesReturned = 0;
        return Status;
    }

    Status = FltpEnumerateInstanceInformation(Volume, NULL, Index, InformationClass, Buffer, BufferSize, BytesReturned);
    ExReleaseRundownProtection(&Volume->Base.RundownRef);
    return Status;
}

NTSTATUS
FLTAPI
FltEnumerateInstanceInformationByDeviceObject(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG Index,
    _In_ INSTANCE_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    PFLT_VOLUME Volume;
    NTSTATUS Status;

    Volume = FltpReferenceVolumeFromDevice(DeviceObject);
    if (Volume == NULL)
    {
        *BytesReturned = 0;
        return STATUS_FLT_VOLUME_NOT_FOUND;
    }

    Status = FltpEnumerateInstanceInformation(Volume, NULL, Index, InformationClass, Buffer, BufferSize, BytesReturned);
    ExReleaseRundownProtection(&Volume->Base.RundownRef);
    return Status;
}
