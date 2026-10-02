/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     File name information
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

typedef struct _FLTP_NAME_INFORMATION
{
    FLT_FILE_NAME_INFORMATION Info;
    volatile LONG ReferenceCount;
    LIST_ENTRY CacheLink;
    LONG Generation;
    WCHAR Buffer[ANYSIZE_ARRAY];
} FLTP_NAME_INFORMATION, *PFLTP_NAME_INFORMATION;

static
NTSTATUS
FltpAllocatePath(
    _Out_ PUNICODE_STRING Path,
    _In_ ULONG Size)
{
    if (Size > MAXUSHORT)
    {
        return STATUS_NAME_TOO_LONG;
    }

    Path->Length = 0;
    Path->MaximumLength = (USHORT)Size;
    Path->Buffer = ExAllocatePoolWithTag(PagedPool, max(Size, sizeof(WCHAR)), FLT_TAG_NAME);
    return Path->Buffer ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static
NTSTATUS
FltpAppendPath(
    _Inout_ PUNICODE_STRING Path,
    _In_ PCUNICODE_STRING Tail)
{
    UNICODE_STRING Grown;
    NTSTATUS Status;

    if ((ULONG)Path->Length + Tail->Length > Path->MaximumLength)
    {
        Status = FltpAllocatePath(&Grown, (ULONG)Path->Length + Tail->Length + 64 * sizeof(WCHAR));
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        RtlCopyMemory(Grown.Buffer, Path->Buffer, Path->Length);
        Grown.Length = Path->Length;
        ExFreePoolWithTag(Path->Buffer, FLT_TAG_NAME);
        *Path = Grown;
    }

    RtlCopyMemory((PUCHAR)Path->Buffer + Path->Length, Tail->Buffer, Tail->Length);
    Path->Length += Tail->Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
FltpQueryNameClass(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ FILE_INFORMATION_CLASS InformationClass,
    _Out_ PUNICODE_STRING Path)
{
    PFILE_NAME_INFORMATION Information;
    ULONG Length = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + 260 * sizeof(WCHAR);
    ULONG Returned;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(Path, NULL, 0);

    for (;;)
    {
        Information = ExAllocatePoolWithTag(PagedPool, Length, FLT_TAG_NAME);
        if (Information == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Status = FltQueryInformationFile(Instance, FileObject, Information, Length, InformationClass, &Returned);
        if (Status != STATUS_BUFFER_OVERFLOW)
        {
            break;
        }

        Length = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + Information->FileNameLength;
        ExFreePoolWithTag(Information, FLT_TAG_NAME);
    }

    if (NT_SUCCESS(Status))
    {
        Status = FltpAllocatePath(Path, Information->FileNameLength + 64 * sizeof(WCHAR));
        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(Path->Buffer, Information->FileName, Information->FileNameLength);
            Path->Length = (USHORT)Information->FileNameLength;
        }
    }

    ExFreePoolWithTag(Information, FLT_TAG_NAME);
    return Status;
}

static
NTSTATUS
FltpQueryOpenName(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ FLT_FILE_NAME_OPTIONS Format,
    _Out_ PUNICODE_STRING Path)
{
    NTSTATUS Status;

    if (Format == FLT_FILE_NAME_SHORT)
    {
        return FltpQueryNameClass(Instance, FileObject, FileAlternateNameInformation, Path);
    }

    if (Format == FLT_FILE_NAME_NORMALIZED)
    {
        Status = FltpQueryNameClass(Instance, FileObject, FileNormalizedNameInformation, Path);
        if (NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    return FltpQueryNameClass(Instance, FileObject, FileNameInformation, Path);
}

static
VOID
FltpSplitFinal(
    _In_ PCUNICODE_STRING Path,
    _Out_ PUNICODE_STRING Parent,
    _Out_ PUNICODE_STRING Final)
{
    USHORT Count = Path->Length / sizeof(WCHAR);
    USHORT Index = Count;

    while (Index > 0 && Path->Buffer[Index - 1] != L'\\')
    {
        Index--;
    }

    Parent->Buffer = Path->Buffer;
    Parent->Length = Parent->MaximumLength = Index * sizeof(WCHAR);
    Final->Buffer = Path->Buffer + Index;
    Final->Length = Final->MaximumLength = (Count - Index) * sizeof(WCHAR);
}

static
NTSTATUS
FltpNormalizePath(
    _In_ PFLT_INSTANCE Instance,
    _Inout_ PFLT_VOLUME *Volume,
    _Inout_ PUNICODE_STRING Path)
{
    UCHAR Buffer[FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName) + 256 * sizeof(WCHAR)];
    PFILE_NAMES_INFORMATION Names = (PFILE_NAMES_INFORMATION)Buffer;
    UNICODE_STRING Parent, Final, Stream, Search, FullParent, Normalized, Expanded, Separator;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    PFILE_OBJECT ParentObject;
    PFLT_VOLUME ParentVolume;
    HANDLE ParentHandle;
    NTSTATUS Status;
    USHORT Index;

    FltpSplitFinal(Path, &Parent, &Final);
    if (Parent.Length == 0)
    {
        return STATUS_SUCCESS;
    }

    Stream.Buffer = NULL;
    Stream.Length = Stream.MaximumLength = 0;
    Search = Final;
    for (Index = 0; Index < Final.Length / sizeof(WCHAR); Index++)
    {
        if (Final.Buffer[Index] == L':')
        {
            Stream.Buffer = Final.Buffer + Index;
            Stream.Length = Stream.MaximumLength = Final.Length - Index * sizeof(WCHAR);
            Search.Length = Search.MaximumLength = Index * sizeof(WCHAR);
            break;
        }
    }

    Status = FltpAllocatePath(&FullParent, (ULONG)(*Volume)->DeviceName.Length + Parent.Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    (VOID)FltpAppendPath(&FullParent, &(*Volume)->DeviceName);
    (VOID)FltpAppendPath(&FullParent, &Parent);

    InitializeObjectAttributes(&ObjectAttributes,
                               &FullParent,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = FltCreateFileEx2(Instance->Filter,
                              Instance,
                              &ParentHandle,
                              &ParentObject,
                              FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                              &ObjectAttributes,
                              &IoStatusBlock,
                              NULL,
                              0,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              FILE_OPEN,
                              FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT,
                              NULL,
                              0,
                              IO_IGNORE_SHARE_ACCESS_CHECK,
                              NULL);
    ExFreePoolWithTag(FullParent.Buffer, FLT_TAG_NAME);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ParentVolume = FltpReferenceVolumeFromDevice(IoGetRelatedDeviceObject(ParentObject));
    if (ParentVolume == NULL || ParentVolume != *Volume)
    {
        if (ParentVolume != NULL)
        {
            ExReleaseRundownProtection(&ParentVolume->Base.RundownRef);
        }
        ObDereferenceObject(ParentObject);
        ZwClose(ParentHandle);
        return STATUS_NOT_SAME_DEVICE;
    }
    ExReleaseRundownProtection(&ParentVolume->Base.RundownRef);

    Status = FltpQueryOpenName(Instance, ParentObject, FLT_FILE_NAME_NORMALIZED, &Normalized);
    if (NT_SUCCESS(Status))
    {
        Expanded = Search;
        if (Search.Length != 0 &&
            NT_SUCCESS(FltQueryDirectoryFile(Instance,
                                             ParentObject,
                                             Names,
                                             sizeof(Buffer),
                                             FileNamesInformation,
                                             TRUE,
                                             &Search,
                                             TRUE,
                                             NULL)))
        {
            Expanded.Buffer = Names->FileName;
            Expanded.Length = Expanded.MaximumLength = (USHORT)Names->FileNameLength;
        }

        RtlInitUnicodeString(&Separator, L"\\");
        if (Normalized.Length == 0 || Normalized.Buffer[Normalized.Length / sizeof(WCHAR) - 1] != L'\\')
        {
            Status = FltpAppendPath(&Normalized, &Separator);
        }
        if (NT_SUCCESS(Status))
        {
            Status = FltpAppendPath(&Normalized, &Expanded);
        }
        if (NT_SUCCESS(Status) && Stream.Length != 0)
        {
            Status = FltpAppendPath(&Normalized, &Stream);
        }

        if (NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Path->Buffer, FLT_TAG_NAME);
            *Path = Normalized;
        }
        else
        {
            ExFreePoolWithTag(Normalized.Buffer, FLT_TAG_NAME);
        }
    }

    ObDereferenceObject(ParentObject);
    ZwClose(ParentHandle);
    return Status;
}

static
NTSTATUS
FltpUnopenedName(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN TargetDirectory,
    _Out_ PUNICODE_STRING Path)
{
    UNICODE_STRING Separator, Parent, Final;
    NTSTATUS Status;

    RtlInitUnicodeString(&Separator, L"\\");

    if (FileObject->RelatedFileObject != NULL)
    {
        Status = FltpQueryNameClass(Instance, FileObject->RelatedFileObject, FileNameInformation, Path);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (FileObject->FileName.Length != 0)
        {
            if (FileObject->FileName.Buffer[0] != L':' &&
                (Path->Length == 0 || Path->Buffer[Path->Length / sizeof(WCHAR) - 1] != L'\\'))
            {
                Status = FltpAppendPath(Path, &Separator);
            }
            if (NT_SUCCESS(Status))
            {
                Status = FltpAppendPath(Path, &FileObject->FileName);
            }
        }
    }
    else
    {
        Status = FltpAllocatePath(Path, (ULONG)FileObject->FileName.Length + 64 * sizeof(WCHAR));
        if (NT_SUCCESS(Status))
        {
            Status = FltpAppendPath(Path, &FileObject->FileName);
        }
    }

    if (!NT_SUCCESS(Status))
    {
        if (Path->Buffer != NULL)
        {
            ExFreePoolWithTag(Path->Buffer, FLT_TAG_NAME);
            Path->Buffer = NULL;
        }
        return Status;
    }

    if (TargetDirectory)
    {
        FltpSplitFinal(Path, &Parent, &Final);
        Path->Length = Parent.Length;
        if (Path->Length > sizeof(WCHAR))
        {
            Path->Length -= sizeof(WCHAR);
        }
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
FltpBuildNameInformation(
    _In_ PFLT_VOLUME Volume,
    _In_ PCUNICODE_STRING Path,
    _In_ FLT_FILE_NAME_OPTIONS Format,
    _In_ BOOLEAN FullName,
    _Outptr_ PFLT_FILE_NAME_INFORMATION *FileNameInformation)
{
    PFLTP_NAME_INFORMATION Name;
    ULONG VolumeLength = (Format == FLT_FILE_NAME_SHORT || FullName) ? 0 : Volume->DeviceName.Length;
    ULONG Length = VolumeLength + Path->Length;

    if (Length > MAXUSHORT)
    {
        return STATUS_NAME_TOO_LONG;
    }

    Name = ExAllocatePoolWithTag(NonPagedPoolNx, FIELD_OFFSET(FLTP_NAME_INFORMATION, Buffer) + Length, FLT_TAG_NAME);
    if (Name == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Name, FIELD_OFFSET(FLTP_NAME_INFORMATION, Buffer));
    Name->ReferenceCount = 1;
    InitializeListHead(&Name->CacheLink);
    Name->Info.Size = sizeof(FLT_FILE_NAME_INFORMATION);
    Name->Info.Format = Format;
    Name->Info.Name.Buffer = Name->Buffer;
    Name->Info.Name.Length = Name->Info.Name.MaximumLength = (USHORT)Length;
    RtlCopyMemory(Name->Buffer, Volume->DeviceName.Buffer, VolumeLength);
    RtlCopyMemory((PUCHAR)Name->Buffer + VolumeLength, Path->Buffer, Path->Length);

    if (Format != FLT_FILE_NAME_SHORT)
    {
        Name->Info.Volume.Buffer = Name->Buffer;
        Name->Info.Volume.Length = Name->Info.Volume.MaximumLength =
            (USHORT)min((ULONG)Volume->DeviceName.Length, Length);
        Name->Info.Share.Buffer = Name->Buffer + Name->Info.Volume.Length / sizeof(WCHAR);
    }

    *FileNameInformation = &Name->Info;
    return STATUS_SUCCESS;
}

VOID
FltpReleaseNameList(
    _Inout_ PLIST_ENTRY List)
{
    PFLTP_NAME_INFORMATION Name;
    PLIST_ENTRY Link;

    while (!IsListEmpty(List))
    {
        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&FltGlobals.NameLock);
        Link = IsListEmpty(List) ? NULL : RemoveHeadList(List);
        if (Link != NULL)
        {
            InitializeListHead(Link);
        }
        ExReleasePushLockExclusive(&FltGlobals.NameLock);
        KeLeaveCriticalRegion();

        if (Link == NULL)
        {
            break;
        }

        Name = CONTAINING_RECORD(Link, FLTP_NAME_INFORMATION, CacheLink);
        FltReleaseFileNameInformation(&Name->Info);
    }
}

static
VOID
FltpReleaseStaleNames(
    _Inout_ PLIST_ENTRY Stale)
{
    PFLTP_NAME_INFORMATION Name;
    PLIST_ENTRY Link;

    while (!IsListEmpty(Stale))
    {
        Link = RemoveHeadList(Stale);
        InitializeListHead(Link);
        Name = CONTAINING_RECORD(Link, FLTP_NAME_INFORMATION, CacheLink);
        FltReleaseFileNameInformation(&Name->Info);
    }
}

static
PFLTP_NAME_INFORMATION
FltpFindNameLocked(
    _In_opt_ PLIST_ENTRY List,
    _In_ FLT_FILE_NAME_OPTIONS Format,
    _Inout_ PLIST_ENTRY Stale)
{
    PFLTP_NAME_INFORMATION Name;
    PLIST_ENTRY Link, Next;

    if (List == NULL)
    {
        return NULL;
    }

    for (Link = List->Flink; Link != List; Link = Next)
    {
        Next = Link->Flink;
        Name = CONTAINING_RECORD(Link, FLTP_NAME_INFORMATION, CacheLink);
        if (Name->Generation != FltGlobals.NameGeneration)
        {
            RemoveEntryList(Link);
            InsertTailList(Stale, Link);
            continue;
        }
        if (Name->Info.Format == Format)
        {
            return Name;
        }
    }
    return NULL;
}

static
PLIST_ENTRY
FltpNameListForInsert(
    _In_ PFILE_OBJECT FileObject)
{
    PLIST_ENTRY List;

    if (FileObject->Flags & FO_CLEANUP_COMPLETE)
    {
        return NULL;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    List = FltpFileObjectNameList(FileObject, TRUE);
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();
    return List;
}

static
PFLT_FILE_NAME_INFORMATION
FltpLookupCachedName(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PFLT_CALLBACK_DATA CallbackData,
    _In_ BOOLEAN Unopened,
    _In_ FLT_FILE_NAME_OPTIONS Format)
{
    PFLTP_NAME_INFORMATION Found = NULL;
    PLIST_ENTRY CreateList = NULL, List;
    BOOLEAN Promote = FALSE;
    LIST_ENTRY Stale;

    if (CallbackData != NULL && CallbackData->Iopb->MajorFunction == IRP_MJ_CREATE)
    {
        CreateList = &FLTP_DATA_TO_IRP_CTRL(CallbackData)->Names;
    }

    InitializeListHead(&Stale);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.NameLock);
    if (!Unopened)
    {
        Found = FltpFindNameLocked(FltpFileObjectNameList(FileObject, FALSE), Format, &Stale);
    }
    if (Found == NULL)
    {
        Found = FltpFindNameLocked(CreateList, Format, &Stale);
        Promote = (Found != NULL && !Unopened);
    }
    if (Found != NULL)
    {
        InterlockedIncrement(&Found->ReferenceCount);
    }
    ExReleasePushLockExclusive(&FltGlobals.NameLock);
    KeLeaveCriticalRegion();

    List = Promote ? FltpNameListForInsert(FileObject) : NULL;
    if (List != NULL)
    {
        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&FltGlobals.NameLock);
        if (!IsListEmpty(&Found->CacheLink) && FltpFindNameLocked(List, Format, &Stale) == NULL)
        {
            RemoveEntryList(&Found->CacheLink);
            InsertTailList(List, &Found->CacheLink);
        }
        ExReleasePushLockExclusive(&FltGlobals.NameLock);
        KeLeaveCriticalRegion();
    }

    FltpReleaseStaleNames(&Stale);
    return Found != NULL ? &Found->Info : NULL;
}

static
VOID
FltpInsertCachedName(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PFLT_CALLBACK_DATA CallbackData,
    _In_ BOOLEAN Unopened,
    _In_ PFLT_FILE_NAME_INFORMATION Info,
    _In_ LONG Generation)
{
    PFLTP_NAME_INFORMATION Name = CONTAINING_RECORD(Info, FLTP_NAME_INFORMATION, Info);
    LIST_ENTRY Stale;
    PLIST_ENTRY List;

    List = Unopened ? &FLTP_DATA_TO_IRP_CTRL(CallbackData)->Names : FltpNameListForInsert(FileObject);
    if (List == NULL)
    {
        return;
    }

    InitializeListHead(&Stale);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.NameLock);
    if (Generation == FltGlobals.NameGeneration && FltpFindNameLocked(List, Info->Format, &Stale) == NULL)
    {
        Name->Generation = Generation;
        InterlockedIncrement(&Name->ReferenceCount);
        InsertTailList(List, &Name->CacheLink);
    }
    ExReleasePushLockExclusive(&FltGlobals.NameLock);
    KeLeaveCriticalRegion();

    FltpReleaseStaleNames(&Stale);
}

static
PFLT_INSTANCE
FltpFindNameProvider(
    _In_ PFLT_INSTANCE Instance,
    _In_ BOOLEAN IncludeSelf)
{
    PFLT_VOLUME Volume = Instance->Volume;
    PFLT_INSTANCE Candidate, Found = NULL;
    PLIST_ENTRY Link;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&Volume->InstanceLock);
    for (Link = Volume->InstanceList.Flink; Link != &Volume->InstanceList; Link = Link->Flink)
    {
        Candidate = CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink);
        if (Candidate->Filter->GenerateFileName == NULL ||
            (Candidate->Flags & (FLTP_INSTANCE_DETACHING | FLTP_INSTANCE_INITIALIZING)))
        {
            continue;
        }
        if (Candidate == Instance)
        {
            if (!IncludeSelf)
            {
                continue;
            }
        }
        else if (FltpCompareAltitude(&Candidate->Altitude, &Instance->Altitude) >= 0)
        {
            continue;
        }

        if (ExAcquireRundownProtection(&Candidate->Base.RundownRef))
        {
            Found = Candidate;
            break;
        }
    }
    ExReleasePushLockShared(&Volume->InstanceLock);
    KeLeaveCriticalRegion();
    return Found;
}

static
NTSTATUS
FltpGetName(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PFLT_CALLBACK_DATA CallbackData,
    _In_ FLT_FILE_NAME_OPTIONS NameOptions,
    _In_ BOOLEAN Safe,
    _Outptr_ PFLT_FILE_NAME_INFORMATION *FileNameInformation)
{
    FLT_FILE_NAME_OPTIONS Format = NameOptions & FLT_VALID_FILE_NAME_FORMATS;
    FLT_FILE_NAME_OPTIONS Method = NameOptions & FLT_VALID_FILE_NAME_QUERY_METHODS;
    NTSTATUS Unavailable = (Method == FLT_FILE_NAME_QUERY_CACHE_ONLY ||
                            Method == FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP)
                               ? STATUS_FLT_NAME_CACHE_MISS
                               : STATUS_FLT_INVALID_NAME_REQUEST;
    PFLT_VOLUME Volume = Instance->Volume;
    PFLT_INSTANCE Provider;
    FLT_NAME_CONTROL NameControl;
    BOOLEAN Unopened = FALSE, Cache = FALSE;
    UNICODE_STRING Path;
    NTSTATUS Status;
    LONG Generation;

    Provider = FltpFindNameProvider(Instance, (NameOptions & FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER) != 0);
    if (Provider != NULL)
    {
        if (Method == FLT_FILE_NAME_QUERY_CACHE_ONLY || !Safe)
        {
            ExReleaseRundownProtection(&Provider->Base.RundownRef);
            return Unavailable;
        }

        RtlInitEmptyUnicodeString(&NameControl.Name, NULL, 0);
        Status = Provider->Filter->GenerateFileName(Provider,
                                                    FileObject,
                                                    CallbackData,
                                                    NameOptions & ~FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER,
                                                    &Cache,
                                                    &NameControl);
        ExReleaseRundownProtection(&Provider->Base.RundownRef);
        if (NT_SUCCESS(Status))
        {
            Status = FltpBuildNameInformation(Volume, &NameControl.Name, Format, TRUE, FileNameInformation);
        }
        if (NameControl.Name.Buffer != NULL)
        {
            ExFreePoolWithTag(NameControl.Name.Buffer, FLT_TAG_NAME);
        }
        return Status;
    }

    if (CallbackData != NULL && CallbackData->Iopb->MajorFunction == IRP_MJ_CREATE)
    {
        if (!(CallbackData->Flags & FLTFL_CALLBACK_DATA_POST_OPERATION))
        {
            Unopened = TRUE;
        }
        else if (CallbackData->IoStatus.Status == STATUS_REPARSE)
        {
            if (!(NameOptions & FLT_FILE_NAME_ALLOW_QUERY_ON_REPARSE))
            {
                return STATUS_FLT_INVALID_NAME_REQUEST;
            }
            Unopened = TRUE;
        }
        else if (!NT_SUCCESS(CallbackData->IoStatus.Status))
        {
            Unopened = TRUE;
        }
    }

    Generation = FltGlobals.NameGeneration;
    if (Method != FLT_FILE_NAME_QUERY_FILESYSTEM_ONLY)
    {
        *FileNameInformation = FltpLookupCachedName(FileObject, CallbackData, Unopened, Format);
        if (*FileNameInformation != NULL)
        {
            return STATUS_SUCCESS;
        }
    }
    if (Method == FLT_FILE_NAME_QUERY_CACHE_ONLY || !Safe)
    {
        return Unavailable;
    }

    if (Unopened)
    {
        if (Format == FLT_FILE_NAME_SHORT ||
            (CallbackData->Iopb->Parameters.Create.Options & FILE_OPEN_BY_FILE_ID))
        {
            return STATUS_FLT_INVALID_NAME_REQUEST;
        }

        Status = FltpUnopenedName(Instance,
                                  FileObject,
                                  (CallbackData->Iopb->OperationFlags & SL_OPEN_TARGET_DIRECTORY) != 0,
                                  &Path);
        if (NT_SUCCESS(Status) && Format == FLT_FILE_NAME_NORMALIZED)
        {
            Status = FltpNormalizePath(Instance, &Volume, &Path);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Path.Buffer, FLT_TAG_NAME);
            }
        }
    }
    else
    {
        Status = FltpQueryOpenName(Instance, FileObject, Format, &Path);
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = FltpBuildNameInformation(Volume, &Path, Format, FALSE, FileNameInformation);
    ExFreePoolWithTag(Path.Buffer, FLT_TAG_NAME);
    if (NT_SUCCESS(Status) &&
        Method != FLT_FILE_NAME_QUERY_FILESYSTEM_ONLY &&
        !(NameOptions & FLT_FILE_NAME_DO_NOT_CACHE))
    {
        FltpInsertCachedName(FileObject, CallbackData, Unopened, *FileNameInformation, Generation);
    }
    return Status;
}

static
NTSTATUS
FltpCheckNameOptions(
    _In_ FLT_FILE_NAME_OPTIONS NameOptions)
{
    FLT_FILE_NAME_OPTIONS Format = NameOptions & FLT_VALID_FILE_NAME_FORMATS;
    FLT_FILE_NAME_OPTIONS Method = NameOptions & FLT_VALID_FILE_NAME_QUERY_METHODS;

    if (Format != FLT_FILE_NAME_NORMALIZED && Format != FLT_FILE_NAME_OPENED && Format != FLT_FILE_NAME_SHORT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    switch (Method)
    {
        case FLT_FILE_NAME_QUERY_CACHE_ONLY:
        case FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP:
        case FLT_FILE_NAME_QUERY_DEFAULT:
        case FLT_FILE_NAME_QUERY_FILESYSTEM_ONLY:
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
FLTAPI
FltGetFileNameInformation(
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _In_ FLT_FILE_NAME_OPTIONS NameOptions,
    _Outptr_ PFLT_FILE_NAME_INFORMATION *FileNameInformation)
{
    PFLT_IO_PARAMETER_BLOCK Iopb;
    BOOLEAN Safe = TRUE;
    NTSTATUS Status;

    if (CallbackData == NULL || FileNameInformation == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *FileNameInformation = NULL;
    Iopb = CallbackData->Iopb;

    if (Iopb->TargetFileObject == NULL || Iopb->TargetInstance == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((Iopb->IrpFlags & IRP_PAGING_IO) ||
        IoGetTopLevelIrp() != NULL ||
        KeAreAllApcsDisabled() ||
        (Iopb->TargetFileObject->Flags & FO_CLEANUP_COMPLETE))
    {
        Safe = FALSE;
    }

    switch (Iopb->MajorFunction)
    {
        case (UCHAR)IRP_MJ_ACQUIRE_FOR_CC_FLUSH:
        case (UCHAR)IRP_MJ_ACQUIRE_FOR_MOD_WRITE:
        case (UCHAR)IRP_MJ_RELEASE_FOR_CC_FLUSH:
        case (UCHAR)IRP_MJ_RELEASE_FOR_MOD_WRITE:
        case (UCHAR)IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION:
            Safe = FALSE;
            break;

        case (UCHAR)IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION:
            if (CallbackData->Flags & FLTFL_CALLBACK_DATA_POST_OPERATION)
            {
                Safe = FALSE;
            }
            break;

        default:
            break;
    }

    Status = FltpCheckNameOptions(NameOptions);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return FltpGetName(Iopb->TargetInstance,
                       Iopb->TargetFileObject,
                       CallbackData,
                       NameOptions,
                       Safe,
                       FileNameInformation);
}

NTSTATUS
FLTAPI
FltGetFileNameInformationUnsafe(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PFLT_INSTANCE Instance,
    _In_ FLT_FILE_NAME_OPTIONS NameOptions,
    _Outptr_ PFLT_FILE_NAME_INFORMATION *FileNameInformation)
{
    PFLT_INSTANCE Bottom = NULL;
    PFLT_VOLUME Volume;
    NTSTATUS Status;

    *FileNameInformation = NULL;

    Status = FltpCheckNameOptions(NameOptions);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Instance != NULL)
    {
        return FltpGetName(Instance, FileObject, NULL, NameOptions, TRUE, FileNameInformation);
    }

    Status = FltGetVolumeFromFileObject(NULL, FileObject, &Volume);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = FltGetTopInstance(Volume, &Bottom);
    FltObjectDereference(Volume);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = FltpGetName(Bottom,
                         FileObject,
                         NULL,
                         NameOptions | FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER,
                         TRUE,
                         FileNameInformation);
    FltObjectDereference(Bottom);
    return Status;
}

VOID
FLTAPI
FltReferenceFileNameInformation(
    _In_ PFLT_FILE_NAME_INFORMATION FileNameInformation)
{
    InterlockedIncrement(&CONTAINING_RECORD(FileNameInformation, FLTP_NAME_INFORMATION, Info)->ReferenceCount);
}

VOID
FLTAPI
FltReleaseFileNameInformation(
    _In_ PFLT_FILE_NAME_INFORMATION FileNameInformation)
{
    PFLTP_NAME_INFORMATION Name = CONTAINING_RECORD(FileNameInformation, FLTP_NAME_INFORMATION, Info);

    if (InterlockedDecrement(&Name->ReferenceCount) == 0)
    {
        ExFreePoolWithTag(Name, FLT_TAG_NAME);
    }
}

NTSTATUS
FLTAPI
FltParseFileName(
    _In_ PCUNICODE_STRING FileName,
    _Inout_opt_ PUNICODE_STRING Extension,
    _Inout_opt_ PUNICODE_STRING Stream,
    _Inout_opt_ PUNICODE_STRING FinalComponent)
{
    USHORT Count = FileName->Length / sizeof(WCHAR);
    USHORT Start = Count, End = Count, Index;
    USHORT Dot = MAXUSHORT;

    while (Start > 0 && FileName->Buffer[Start - 1] != L'\\')
    {
        Start--;
    }

    for (Index = Start; Index < Count; Index++)
    {
        if (FileName->Buffer[Index] == L':')
        {
            End = Index;
            break;
        }
    }

    for (Index = Start; Index < End; Index++)
    {
        if (FileName->Buffer[Index] == L'.')
        {
            Dot = Index;
        }
    }

    if (FinalComponent != NULL)
    {
        FinalComponent->Buffer = FileName->Buffer + Start;
        FinalComponent->Length = FinalComponent->MaximumLength = (End - Start) * sizeof(WCHAR);
    }
    if (Stream != NULL)
    {
        Stream->Buffer = FileName->Buffer + End;
        Stream->Length = Stream->MaximumLength = (Count - End) * sizeof(WCHAR);
    }
    if (Extension != NULL)
    {
        if (Dot != MAXUSHORT && Dot + 1 < End)
        {
            Extension->Buffer = FileName->Buffer + Dot + 1;
            Extension->Length = Extension->MaximumLength = (End - Dot - 1) * sizeof(WCHAR);
        }
        else
        {
            Extension->Buffer = FileName->Buffer + End;
            Extension->Length = Extension->MaximumLength = 0;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltParseFileNameInformation(
    _Inout_ PFLT_FILE_NAME_INFORMATION FileNameInformation)
{
    PFLT_FILE_NAME_INFORMATION Info = FileNameInformation;
    UNICODE_STRING Remaining;
    USHORT Prefix;

    if (Info->NamesParsed == (FLTFL_FILE_NAME_PARSED_FINAL_COMPONENT |
                              FLTFL_FILE_NAME_PARSED_EXTENSION |
                              FLTFL_FILE_NAME_PARSED_STREAM |
                              FLTFL_FILE_NAME_PARSED_PARENT_DIR))
    {
        return STATUS_SUCCESS;
    }

    if (Info->Format == FLT_FILE_NAME_SHORT)
    {
        (VOID)FltParseFileName(&Info->Name, &Info->Extension, NULL, NULL);
    }
    else
    {
        Prefix = Info->Volume.Length + Info->Share.Length;
        Remaining.Buffer = Info->Name.Buffer + Prefix / sizeof(WCHAR);
        Remaining.Length = Remaining.MaximumLength = Info->Name.Length - Prefix;

        (VOID)FltParseFileName(&Remaining, &Info->Extension, &Info->Stream, &Info->FinalComponent);
        Info->ParentDir.Buffer = Remaining.Buffer;
        Info->ParentDir.Length = Info->ParentDir.MaximumLength =
            (USHORT)((PUCHAR)Info->FinalComponent.Buffer - (PUCHAR)Remaining.Buffer);
    }

    Info->NamesParsed = FLTFL_FILE_NAME_PARSED_FINAL_COMPONENT |
                        FLTFL_FILE_NAME_PARSED_EXTENSION |
                        FLTFL_FILE_NAME_PARSED_STREAM |
                        FLTFL_FILE_NAME_PARSED_PARENT_DIR;
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltGetTunneledName(
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _In_ PFLT_FILE_NAME_INFORMATION FileNameInformation,
    _Outptr_result_maybenull_ PFLT_FILE_NAME_INFORMATION *RetTunneledFileNameInformation)
{
    UNREFERENCED_PARAMETER(CallbackData);
    UNREFERENCED_PARAMETER(FileNameInformation);

    *RetTunneledFileNameInformation = NULL;
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltPurgeFileNameInformationCache(
    _In_ PFLT_INSTANCE Instance,
    _In_opt_ PFILE_OBJECT FileObject)
{
    PLIST_ENTRY List;

    UNREFERENCED_PARAMETER(Instance);

    if (FileObject == NULL)
    {
        InterlockedIncrement(&FltGlobals.NameGeneration);
        return STATUS_SUCCESS;
    }

    List = FltpFileObjectNameList(FileObject, FALSE);
    if (List != NULL)
    {
        FltpReleaseNameList(List);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltCheckAndGrowNameControl(
    _Inout_ PFLT_NAME_CONTROL NameCtrl,
    _In_ USHORT NewSize)
{
    PWCH Buffer;

    if (NewSize <= NameCtrl->Name.MaximumLength)
    {
        return STATUS_SUCCESS;
    }

    Buffer = ExAllocatePoolWithTag(PagedPool, NewSize, FLT_TAG_NAME);
    if (Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (NameCtrl->Name.Buffer != NULL)
    {
        RtlCopyMemory(Buffer, NameCtrl->Name.Buffer, NameCtrl->Name.Length);
        ExFreePoolWithTag(NameCtrl->Name.Buffer, FLT_TAG_NAME);
    }

    NameCtrl->Name.Buffer = Buffer;
    NameCtrl->Name.MaximumLength = NewSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
FltpResolveDeviceName(
    _In_ PCUNICODE_STRING Name,
    _Out_ PFLT_VOLUME *Volume,
    _Out_ PUNICODE_STRING Path)
{
    UNICODE_STRING Current, Target, Link, Rest;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PFLT_VOLUME Candidate, Found = NULL;
    PLIST_ENTRY Entry;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Depth, Index, Count;
    PWCH Buffer;

    *Volume = NULL;

    Status = FltpAllocatePath(&Current, (ULONG)Name->Length + 64 * sizeof(WCHAR));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    (VOID)FltpAppendPath(&Current, Name);

    for (Depth = 0; Depth < 8; Depth++)
    {
        KeEnterCriticalRegion();
        ExAcquireResourceSharedLite(&FltGlobals.Lock, TRUE);
        for (Entry = FltGlobals.VolumeList.Flink; Entry != &FltGlobals.VolumeList; Entry = Entry->Flink)
        {
            Candidate = CONTAINING_RECORD(Entry, FLT_VOLUME, Base.PrimaryLink);
            if (Candidate->DeviceName.Length != 0 &&
                RtlPrefixUnicodeString(&Candidate->DeviceName, &Current, TRUE) &&
                (Current.Length == Candidate->DeviceName.Length ||
                 Current.Buffer[Candidate->DeviceName.Length / sizeof(WCHAR)] == L'\\') &&
                ExAcquireRundownProtection(&Candidate->Base.RundownRef))
            {
                Found = Candidate;
                break;
            }
        }
        ExReleaseResourceLite(&FltGlobals.Lock);
        KeLeaveCriticalRegion();

        if (Found != NULL)
        {
            Rest.Buffer = Current.Buffer + Found->DeviceName.Length / sizeof(WCHAR);
            Rest.Length = Rest.MaximumLength = Current.Length - Found->DeviceName.Length;
            Status = FltpAllocatePath(Path, (ULONG)Rest.Length + 64 * sizeof(WCHAR));
            if (NT_SUCCESS(Status))
            {
                (VOID)FltpAppendPath(Path, &Rest);
                *Volume = Found;
            }
            else
            {
                ExReleaseRundownProtection(&Found->Base.RundownRef);
            }
            ExFreePoolWithTag(Current.Buffer, FLT_TAG_NAME);
            return Status;
        }

        Count = Current.Length / sizeof(WCHAR);
        Status = STATUS_OBJECT_PATH_NOT_FOUND;
        for (Index = Count; Index > 1; Index--)
        {
            if (Index != Count && Current.Buffer[Index] != L'\\')
            {
                continue;
            }

            Link.Buffer = Current.Buffer;
            Link.Length = Link.MaximumLength = (USHORT)(Index * sizeof(WCHAR));
            InitializeObjectAttributes(&ObjectAttributes,
                                       &Link,
                                       OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                       NULL,
                                       NULL);
            Status = ZwOpenSymbolicLinkObject(&Handle, SYMBOLIC_LINK_QUERY, &ObjectAttributes);
            if (NT_SUCCESS(Status))
            {
                break;
            }
        }
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        Buffer = ExAllocatePoolWithTag(PagedPool, MAXUSHORT, FLT_TAG_NAME);
        if (Buffer == NULL)
        {
            ZwClose(Handle);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Target.Buffer = Buffer;
        Target.Length = 0;
        Target.MaximumLength = MAXUSHORT - sizeof(WCHAR);
        Status = ZwQuerySymbolicLinkObject(Handle, &Target, NULL);
        ZwClose(Handle);
        if (NT_SUCCESS(Status))
        {
            Rest.Buffer = Current.Buffer + Link.Length / sizeof(WCHAR);
            Rest.Length = Rest.MaximumLength = Current.Length - Link.Length;
            Status = FltpAppendPath(&Target, &Rest);
        }
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Target.Buffer, FLT_TAG_NAME);
            break;
        }

        ExFreePoolWithTag(Current.Buffer, FLT_TAG_NAME);
        Current = Target;
        Status = STATUS_OBJECT_PATH_NOT_FOUND;
    }

    ExFreePoolWithTag(Current.Buffer, FLT_TAG_NAME);
    return NT_SUCCESS(Status) ? STATUS_OBJECT_PATH_NOT_FOUND : Status;
}

NTSTATUS
FLTAPI
FltGetDestinationFileNameInformation(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ HANDLE RootDirectory,
    _In_reads_bytes_(FileNameLength) PWSTR FileName,
    _In_ ULONG FileNameLength,
    _In_ FLT_FILE_NAME_OPTIONS NameOptions,
    _Outptr_ PFLT_FILE_NAME_INFORMATION *RetFileNameInformation)
{
    FLT_FILE_NAME_OPTIONS Format = NameOptions & FLT_VALID_FILE_NAME_FORMATS;
    PFLT_VOLUME Volume = Instance->Volume, Resolved = NULL;
    UNICODE_STRING Name, Path, Parent, Final, Separator;
    PFILE_OBJECT RootObject;
    NTSTATUS Status;
    PAGED_CODE();

    *RetFileNameInformation = NULL;

    if (FileNameLength > MAXUSHORT || (Format != FLT_FILE_NAME_NORMALIZED && Format != FLT_FILE_NAME_OPENED))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Name.Buffer = FileName;
    Name.Length = Name.MaximumLength = (USHORT)FileNameLength;
    RtlInitUnicodeString(&Separator, L"\\");

    if (RootDirectory != NULL)
    {
        Status = ObReferenceObjectByHandle(RootDirectory, 0, *IoFileObjectType, KernelMode, (PVOID *)&RootObject, NULL);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        Status = FltpQueryNameClass(Instance, RootObject, FileNameInformation, &Path);
        ObDereferenceObject(RootObject);
        if (NT_SUCCESS(Status) &&
            (Path.Length == 0 || Path.Buffer[Path.Length / sizeof(WCHAR) - 1] != L'\\'))
        {
            Status = FltpAppendPath(&Path, &Separator);
        }
        if (NT_SUCCESS(Status))
        {
            Status = FltpAppendPath(&Path, &Name);
        }
    }
    else if (FileNameLength >= sizeof(WCHAR) && FileName[0] == L'\\')
    {
        Status = FltpResolveDeviceName(&Name, &Resolved, &Path);
        if (NT_SUCCESS(Status))
        {
            Volume = Resolved;
        }
        else
        {
            Path.Buffer = NULL;
        }
    }
    else
    {
        Status = FltpQueryNameClass(Instance, FileObject, FileNameInformation, &Path);
        if (NT_SUCCESS(Status))
        {
            FltpSplitFinal(&Path, &Parent, &Final);
            Path.Length = Parent.Length;
            Status = FltpAppendPath(&Path, &Name);
        }
    }

    if (NT_SUCCESS(Status) && Format == FLT_FILE_NAME_NORMALIZED && Volume == Instance->Volume)
    {
        Status = FltpNormalizePath(Instance, &Volume, &Path);
    }

    if (NT_SUCCESS(Status))
    {
        Status = FltpBuildNameInformation(Volume, &Path, Format, FALSE, RetFileNameInformation);
    }

    if (Path.Buffer != NULL)
    {
        ExFreePoolWithTag(Path.Buffer, FLT_TAG_NAME);
    }
    if (Resolved != NULL)
    {
        ExReleaseRundownProtection(&Resolved->Base.RundownRef);
    }
    return Status;
}
