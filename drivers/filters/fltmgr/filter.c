/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minifilter registration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

static
PFLT_FILTER
FltpReferenceFilterByDriver(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PFLT_FILTER Filter, Found = NULL;
    PLIST_ENTRY Link;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);
    for (Link = FltGlobals.FilterList.Flink; Link != &FltGlobals.FilterList; Link = Link->Flink)
    {
        Filter = CONTAINING_RECORD(Link, FLT_FILTER, Base.PrimaryLink);
        if (Filter->DriverObject == DriverObject)
        {
            FltpReferencePointer(&Filter->Base);
            Found = Filter;
            break;
        }
    }
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();
    return Found;
}

PFLT_FILTER
FltpReferenceFilterByName(
    _In_ PCUNICODE_STRING Name)
{
    PFLT_FILTER Filter, Found = NULL;
    PLIST_ENTRY Link;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);
    for (Link = FltGlobals.FilterList.Flink; Link != &FltGlobals.FilterList; Link = Link->Flink)
    {
        Filter = CONTAINING_RECORD(Link, FLT_FILTER, Base.PrimaryLink);
        if (RtlEqualUnicodeString(&Filter->Name, Name, TRUE) &&
            ExAcquireRundownProtection(&Filter->Base.RundownRef))
        {
            Found = Filter;
            break;
        }
    }
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();
    return Found;
}

static
VOID
NTAPI
FltpMiniFilterDriverUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PFLT_FILTER Filter;
    PDRIVER_UNLOAD OldUnload = NULL;

    Filter = FltpReferenceFilterByDriver(DriverObject);
    if (Filter != NULL)
    {
        OldUnload = Filter->OldDriverUnload;
        if (Filter->FilterUnload != NULL)
        {
            Filter->FilterUnload(FLTFL_FILTER_UNLOAD_MANDATORY);
        }
        FltpDereferencePointer(&Filter->Base);
    }

    if (OldUnload != NULL)
    {
        OldUnload(DriverObject);
    }
}

NTSTATUS
FLTAPI
FltRegisterFilter(
    _In_ PDRIVER_OBJECT Driver,
    _In_ CONST FLT_REGISTRATION *Registration,
    _Outptr_ PFLT_FILTER *RetFilter)
{
    CONST FLT_OPERATION_REGISTRATION *Operation;
    UNICODE_STRING InstanceName, Altitude;
    PFLT_FILTER Filter;
    ULONG Index, Flags;
    HANDLE Key;
    NTSTATUS Status;
    PAGED_CODE();

    *RetFilter = NULL;

    if (FltGlobals.DriverObject == NULL)
    {
        return STATUS_FLT_NOT_INITIALIZED;
    }

    if (Registration->Version < FLT_REGISTRATION_VERSION_0200 ||
        Registration->Version > FLT_REGISTRATION_VERSION_0203)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Filter = FltpReferenceFilterByDriver(Driver);
    if (Filter != NULL)
    {
        FltpDereferencePointer(&Filter->Base);
        return STATUS_FLT_INSTANCE_ALTITUDE_COLLISION;
    }

    Status = FltpOpenServiceKey(&Driver->DriverExtension->ServiceKeyName, NULL, &Key);
    if (!NT_SUCCESS(Status))
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    ZwClose(Key);

    Filter = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Filter), FLT_TAG_FILTER);
    if (Filter == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Filter, sizeof(*Filter));
    FltpInitializeObject(&Filter->Base, FLT_SIGNATURE_FILTER);
    InitializeListHead(&Filter->InstanceList);
    InitializeListHead(&Filter->PortList);
    ExInitializeFastMutex(&Filter->PortLock);
    FltpInitializeLock(&Filter->ContextLock);
    InitializeListHead(&Filter->ContextList);
    Filter->DriverObject = Driver;
    Filter->RegistrationFlags = Registration->Flags;
    Filter->FilterUnload = Registration->FilterUnloadCallback;
    Filter->InstanceSetup = Registration->InstanceSetupCallback;
    Filter->InstanceQueryTeardown = Registration->InstanceQueryTeardownCallback;
    Filter->InstanceTeardownStart = Registration->InstanceTeardownStartCallback;
    Filter->InstanceTeardownComplete = Registration->InstanceTeardownCompleteCallback;
    Filter->GenerateFileName = Registration->GenerateFileNameCallback;
    Filter->NormalizeNameComponent = Registration->NormalizeNameComponentCallback;
    Filter->NormalizeContextCleanup = Registration->NormalizeContextCleanupCallback;
    if (Registration->Version >= FLT_REGISTRATION_VERSION_0202)
    {
        Filter->TransactionNotification = Registration->TransactionNotificationCallback;
        Filter->NormalizeNameComponentEx = Registration->NormalizeNameComponentExCallback;
    }
    if (Registration->Version >= FLT_REGISTRATION_VERSION_0203)
    {
        Filter->SectionNotification = Registration->SectionNotificationCallback;
    }

    Status = FltpDuplicateString(&Filter->Name, &Driver->DriverExtension->ServiceKeyName);
    if (NT_SUCCESS(Status))
    {
        Status = FltpReadInstanceRegistration(Filter, NULL, &InstanceName, &Altitude, &Flags);
        if (NT_SUCCESS(Status))
        {
            Filter->DefaultAltitude = Altitude;
            FltpFreeString(&InstanceName);
        }
        else
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        }
    }
    if (NT_SUCCESS(Status))
    {
        Status = FltpInitializeContexts(Filter, Registration->ContextRegistration);
    }
    if (!NT_SUCCESS(Status))
    {
        FltpDereferencePointer(&Filter->Base);
        return Status;
    }

    for (Operation = Registration->OperationRegistration;
         Operation != NULL && Operation->MajorFunction != IRP_MJ_OPERATION_END;
         Operation++)
    {
        Index = FltpOperationIndex(Operation->MajorFunction);
        if (Index == MAXULONG)
        {
            FltpDereferencePointer(&Filter->Base);
            return STATUS_INVALID_PARAMETER;
        }
        Filter->Operations[Index] = *Operation;
    }

    if (Filter->FilterUnload != NULL &&
        !(Filter->RegistrationFlags & FLTFL_REGISTRATION_DO_NOT_SUPPORT_SERVICE_STOP))
    {
        Filter->OldDriverUnload = Driver->DriverUnload;
        Driver->DriverUnload = FltpMiniFilterDriverUnload;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&FltGlobals.Lock, TRUE);
    InsertTailList(&FltGlobals.FilterList, &Filter->Base.PrimaryLink);
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    *RetFilter = Filter;
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltStartFiltering(
    _In_ PFLT_FILTER Filter)
{
    PAGED_CODE();

    if (!ExAcquireRundownProtection(&Filter->Base.RundownRef))
    {
        return STATUS_FLT_DELETING_OBJECT;
    }

    if (InterlockedOr((PLONG)&Filter->Flags, FLTP_FILTER_STARTED) & FLTP_FILTER_STARTED)
    {
        ExReleaseRundownProtection(&Filter->Base.RundownRef);
        return STATUS_INVALID_PARAMETER;
    }

    FltpAttachFilterToVolumes(Filter);
    ExReleaseRundownProtection(&Filter->Base.RundownRef);
    return STATUS_SUCCESS;
}

VOID
FLTAPI
FltUnregisterFilter(
    _In_ PFLT_FILTER Filter)
{
    PFLT_INSTANCE Instance;
    PLIST_ENTRY Link;
    PAGED_CODE();

    if (InterlockedOr((PLONG)&Filter->Flags, FLTP_FILTER_UNREGISTERING) & FLTP_FILTER_UNREGISTERING)
    {
        return;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&FltGlobals.Lock, TRUE);
    RemoveEntryList(&Filter->Base.PrimaryLink);
    InitializeListHead(&Filter->Base.PrimaryLink);
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    FltpCloseFilterPorts(Filter);

    for (;;)
    {
        Instance = NULL;

        KeEnterCriticalRegion();
        ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);
        Link = Filter->InstanceList.Flink;
        if (Link != &Filter->InstanceList)
        {
            Instance = CONTAINING_RECORD(Link, FLT_INSTANCE, FilterLink);
            FltpReferencePointer(&Instance->Base);
        }
        ExReleaseResourceLite(&FltGlobals.Lock);
        KeLeaveCriticalRegion();

        if (Instance == NULL)
        {
            break;
        }

        FltpTeardownInstance(Instance, FLTFL_INSTANCE_TEARDOWN_FILTER_UNLOAD);
        FltpDereferencePointer(&Instance->Base);
    }

    ExWaitForRundownProtectionRelease(&Filter->Base.RundownRef);
    FltpDeleteFilterContexts(Filter);

    if (Filter->DriverObject->DriverUnload == FltpMiniFilterDriverUnload)
    {
        Filter->DriverObject->DriverUnload = Filter->OldDriverUnload;
    }

    FltpDereferencePointer(&Filter->Base);
}

static
NTSTATUS
FltpBuildServicePath(
    _In_ PCUNICODE_STRING FilterName,
    _Out_ PUNICODE_STRING Path)
{
    static CONST WCHAR Prefix[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";

    Path->Length = 0;
    Path->MaximumLength = (USHORT)(sizeof(Prefix) + FilterName->Length);
    Path->Buffer = ExAllocatePoolWithTag(PagedPool, Path->MaximumLength, FLT_TAG_NAME);
    if (Path->Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlAppendUnicodeToString(Path, Prefix);
    RtlAppendUnicodeStringToString(Path, FilterName);
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltLoadFilter(
    _In_ PCUNICODE_STRING FilterName)
{
    UNICODE_STRING Path;
    NTSTATUS Status;
    PAGED_CODE();

    Status = FltpBuildServicePath(FilterName, &Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ZwLoadDriver(&Path);
    ExFreePoolWithTag(Path.Buffer, FLT_TAG_NAME);
    return Status;
}

NTSTATUS
FLTAPI
FltUnloadFilter(
    _In_ PCUNICODE_STRING FilterName)
{
    PFLT_FILTER Filter;
    UNICODE_STRING Path;
    NTSTATUS Status;
    PAGED_CODE();

    Filter = FltpReferenceFilterByName(FilterName);
    if (Filter == NULL)
    {
        return STATUS_FLT_FILTER_NOT_FOUND;
    }

    if (Filter->FilterUnload == NULL)
    {
        ExReleaseRundownProtection(&Filter->Base.RundownRef);
        return STATUS_FLT_DO_NOT_DETACH;
    }
    ExReleaseRundownProtection(&Filter->Base.RundownRef);

    Status = FltpBuildServicePath(FilterName, &Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ZwUnloadDriver(&Path);
    ExFreePoolWithTag(Path.Buffer, FLT_TAG_NAME);
    return Status;
}

NTSTATUS
FLTAPI
FltGetFilterFromName(
    _In_ PCUNICODE_STRING FilterName,
    _Outptr_ PFLT_FILTER *RetFilter)
{
    *RetFilter = FltpReferenceFilterByName(FilterName);
    return (*RetFilter != NULL) ? STATUS_SUCCESS : STATUS_FLT_FILTER_NOT_FOUND;
}

NTSTATUS
FLTAPI
FltGetFilterFromInstance(
    _In_ PFLT_INSTANCE Instance,
    _Outptr_ PFLT_FILTER *RetFilter)
{
    *RetFilter = NULL;

    if (!ExAcquireRundownProtection(&Instance->Filter->Base.RundownRef))
    {
        return STATUS_FLT_DELETING_OBJECT;
    }

    *RetFilter = Instance->Filter;
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltEnumerateFilters(
    _Out_writes_to_opt_(FilterListSize, *NumberFiltersReturned) PFLT_FILTER *FilterList,
    _In_ ULONG FilterListSize,
    _Out_ PULONG NumberFiltersReturned)
{
    PFLT_FILTER Filter;
    PLIST_ENTRY Link;
    ULONG Count = 0, Index;
    NTSTATUS Status = STATUS_SUCCESS;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);
    for (Link = FltGlobals.FilterList.Flink; Link != &FltGlobals.FilterList; Link = Link->Flink)
    {
        Filter = CONTAINING_RECORD(Link, FLT_FILTER, Base.PrimaryLink);
        if (!ExAcquireRundownProtection(&Filter->Base.RundownRef))
        {
            continue;
        }

        if (FilterList != NULL && Count < FilterListSize)
        {
            FilterList[Count] = Filter;
        }
        else
        {
            ExReleaseRundownProtection(&Filter->Base.RundownRef);
        }
        Count++;
    }
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    if (FilterList != NULL && Count > FilterListSize)
    {
        for (Index = 0; Index < FilterListSize; Index++)
        {
            ExReleaseRundownProtection(&FilterList[Index]->Base.RundownRef);
        }
        Status = STATUS_BUFFER_TOO_SMALL;
    }

    *NumberFiltersReturned = Count;
    return Status;
}

static
NTSTATUS
FltpFillFilterInformation(
    _In_ PFLT_FILTER Filter,
    _In_ FILTER_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    PFILTER_FULL_INFORMATION Full = Buffer;
    PFILTER_AGGREGATE_BASIC_INFORMATION Basic = Buffer;
    PFILTER_AGGREGATE_STANDARD_INFORMATION Standard = Buffer;
    ULONG Needed, Instances = 0;
    PLIST_ENTRY Link;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);
    for (Link = Filter->InstanceList.Flink; Link != &Filter->InstanceList; Link = Link->Flink)
    {
        Instances++;
    }
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    switch (InformationClass)
    {
        case FilterFullInformation:
            Needed = FIELD_OFFSET(FILTER_FULL_INFORMATION, FilterNameBuffer) + Filter->Name.Length;
            *BytesReturned = Needed;
            if (Buffer == NULL || BufferSize < Needed)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            Full->NextEntryOffset = 0;
            Full->FrameID = 0;
            Full->NumberOfInstances = Instances;
            Full->FilterNameLength = Filter->Name.Length;
            RtlCopyMemory(Full->FilterNameBuffer, Filter->Name.Buffer, Filter->Name.Length);
            return STATUS_SUCCESS;

        case FilterAggregateBasicInformation:
            Needed = sizeof(FILTER_AGGREGATE_BASIC_INFORMATION) + Filter->Name.Length + Filter->DefaultAltitude.Length;
            *BytesReturned = Needed;
            if (Buffer == NULL || BufferSize < Needed)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            RtlZeroMemory(Basic, sizeof(*Basic));
            Basic->Flags = FLTFL_AGGREGATE_INFO_IS_MINIFILTER;
            Basic->Type.MiniFilter.FrameID = 0;
            Basic->Type.MiniFilter.NumberOfInstances = Instances;
            Basic->Type.MiniFilter.FilterNameLength = Filter->Name.Length;
            Basic->Type.MiniFilter.FilterNameBufferOffset = sizeof(FILTER_AGGREGATE_BASIC_INFORMATION);
            Basic->Type.MiniFilter.FilterAltitudeLength = Filter->DefaultAltitude.Length;
            Basic->Type.MiniFilter.FilterAltitudeBufferOffset =
                (USHORT)(sizeof(FILTER_AGGREGATE_BASIC_INFORMATION) + Filter->Name.Length);
            RtlCopyMemory((PUCHAR)Basic + Basic->Type.MiniFilter.FilterNameBufferOffset,
                          Filter->Name.Buffer,
                          Filter->Name.Length);
            RtlCopyMemory((PUCHAR)Basic + Basic->Type.MiniFilter.FilterAltitudeBufferOffset,
                          Filter->DefaultAltitude.Buffer,
                          Filter->DefaultAltitude.Length);
            return STATUS_SUCCESS;

        case FilterAggregateStandardInformation:
            Needed = sizeof(FILTER_AGGREGATE_STANDARD_INFORMATION) + Filter->Name.Length + Filter->DefaultAltitude.Length;
            *BytesReturned = Needed;
            if (Buffer == NULL || BufferSize < Needed)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            RtlZeroMemory(Standard, sizeof(*Standard));
            Standard->Flags = FLTFL_ASI_IS_MINIFILTER;
            Standard->Type.MiniFilter.Flags = 0;
            Standard->Type.MiniFilter.FrameID = 0;
            Standard->Type.MiniFilter.NumberOfInstances = Instances;
            Standard->Type.MiniFilter.FilterNameLength = Filter->Name.Length;
            Standard->Type.MiniFilter.FilterNameBufferOffset = sizeof(FILTER_AGGREGATE_STANDARD_INFORMATION);
            Standard->Type.MiniFilter.FilterAltitudeLength = Filter->DefaultAltitude.Length;
            Standard->Type.MiniFilter.FilterAltitudeBufferOffset =
                (USHORT)(sizeof(FILTER_AGGREGATE_STANDARD_INFORMATION) + Filter->Name.Length);
            RtlCopyMemory((PUCHAR)Standard + Standard->Type.MiniFilter.FilterNameBufferOffset,
                          Filter->Name.Buffer,
                          Filter->Name.Length);
            RtlCopyMemory((PUCHAR)Standard + Standard->Type.MiniFilter.FilterAltitudeBufferOffset,
                          Filter->DefaultAltitude.Buffer,
                          Filter->DefaultAltitude.Length);
            return STATUS_SUCCESS;

        default:
            *BytesReturned = 0;
            return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
FLTAPI
FltGetFilterInformation(
    _In_ PFLT_FILTER Filter,
    _In_ FILTER_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    return FltpFillFilterInformation(Filter, InformationClass, Buffer, BufferSize, BytesReturned);
}

NTSTATUS
FLTAPI
FltEnumerateFilterInformation(
    _In_ ULONG Index,
    _In_ FILTER_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_to_opt_(BufferSize, *BytesReturned) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesReturned)
{
    PFLT_FILTER Filter = NULL, Current;
    PLIST_ENTRY Link;
    ULONG Position = 0;
    NTSTATUS Status;

    *BytesReturned = 0;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);
    for (Link = FltGlobals.FilterList.Flink; Link != &FltGlobals.FilterList; Link = Link->Flink)
    {
        Current = CONTAINING_RECORD(Link, FLT_FILTER, Base.PrimaryLink);
        if (Position++ == Index)
        {
            if (ExAcquireRundownProtection(&Current->Base.RundownRef))
            {
                Filter = Current;
            }
            break;
        }
    }
    ExReleaseResourceLite(&FltGlobals.Lock);
    KeLeaveCriticalRegion();

    if (Filter == NULL)
    {
        return STATUS_NO_MORE_ENTRIES;
    }

    Status = FltpFillFilterInformation(Filter, InformationClass, Buffer, BufferSize, BytesReturned);
    ExReleaseRundownProtection(&Filter->Base.RundownRef);
    return Status;
}
