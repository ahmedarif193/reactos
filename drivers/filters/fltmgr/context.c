/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minifilter contexts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

#define FLTP_CONTEXT_SIGNATURE 'xtCF'
#define FLTP_CONTEXT_LINKED 0x0001
#define FLTP_CONTEXT_SECTION 0x0002

typedef struct _FLTP_CONTEXT
{
    ULONG Signature;
    volatile LONG ReferenceCount;
    FLT_CONTEXT_TYPE Type;
    USHORT Flags;
    PFLT_FILTER Filter;
    PFLT_INSTANCE Instance;
    PFLT_CONTEXT_TYPE_INFO Info;
    LIST_ENTRY OwnerLink;
    LIST_ENTRY ScopeLink;
    PVOID Key;
    PVOID Owner;
} FLTP_CONTEXT, *PFLTP_CONTEXT;

#define FLTP_CONTEXT_HEADER_SIZE ALIGN_UP_BY(sizeof(FLTP_CONTEXT), MEMORY_ALLOCATION_ALIGNMENT)
#define FLTP_CONTEXT_TO_USER(Context) ((PFLT_CONTEXT)((PUCHAR)(Context) + FLTP_CONTEXT_HEADER_SIZE))
#define FLTP_USER_TO_CONTEXT(User) ((PFLTP_CONTEXT)((PUCHAR)(User) - FLTP_CONTEXT_HEADER_SIZE))

typedef struct _FLTP_OWNER_CTRL
{
    union
    {
        FSRTL_PER_STREAM_CONTEXT Stream;
        FSRTL_PER_FILEOBJECT_CONTEXT FileObject;
        FSRTL_PER_FILE_CONTEXT File;
    } Fsrtl;
    LIST_ENTRY Contexts;
    LIST_ENTRY FileContexts;
    LIST_ENTRY SectionContexts;
    LIST_ENTRY Names;
    KEVENT SectionEvent;
} FLTP_OWNER_CTRL, *PFLTP_OWNER_CTRL;

#define FLTP_SECTION_SNAPSHOT 8

static
ULONG
FltpContextTypeIndex(
    _In_ FLT_CONTEXT_TYPE ContextType)
{
    switch (ContextType)
    {
        case FLT_VOLUME_CONTEXT: return 0;
        case FLT_INSTANCE_CONTEXT: return 1;
        case FLT_FILE_CONTEXT: return 2;
        case FLT_STREAM_CONTEXT: return 3;
        case FLT_STREAMHANDLE_CONTEXT: return 4;
        case FLT_TRANSACTION_CONTEXT: return 5;
        case FLT_SECTION_CONTEXT: return 6;
        default: return MAXULONG;
    }
}

NTSTATUS
FltpInitializeContexts(
    _Inout_ PFLT_FILTER Filter,
    _In_opt_ CONST FLT_CONTEXT_REGISTRATION *Registration)
{
    PFLT_CONTEXT_TYPE_INFO Info;
    ULONG Type, Slot;

    for (; Registration != NULL && Registration->ContextType != FLT_CONTEXT_END; Registration++)
    {
        Type = FltpContextTypeIndex(Registration->ContextType);
        if (Type == MAXULONG)
        {
            return STATUS_INVALID_PARAMETER;
        }

        for (Slot = 0; Slot < FLT_CONTEXT_REGISTRATIONS_PER_TYPE; Slot++)
        {
            if (!Filter->Contexts[Type][Slot].Registered)
            {
                break;
            }
        }
        if (Slot == FLT_CONTEXT_REGISTRATIONS_PER_TYPE)
        {
            return STATUS_INVALID_PARAMETER;
        }

        Info = &Filter->Contexts[Type][Slot];
        Info->Registered = TRUE;
        Info->VariableSize = (Registration->Size == FLT_VARIABLE_SIZED_CONTEXTS);
        Info->Flags = Registration->Flags;
        Info->Size = Registration->Size;
        Info->PoolTag = Registration->PoolTag;
        Info->CleanupCallback = Registration->ContextCleanupCallback;
        Info->AllocateCallback = Registration->ContextAllocateCallback;
        Info->FreeCallback = Registration->ContextFreeCallback;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltAllocateContext(
    _In_ PFLT_FILTER Filter,
    _In_ FLT_CONTEXT_TYPE ContextType,
    _In_ SIZE_T ContextSize,
    _In_ POOL_TYPE PoolType,
    _Outptr_result_bytebuffer_(ContextSize) PFLT_CONTEXT *ReturnedContext)
{
    PFLT_CONTEXT_TYPE_INFO Info = NULL, Candidate;
    PFLTP_CONTEXT Context;
    ULONG Type = FltpContextTypeIndex(ContextType), Slot;
    SIZE_T Size;

    *ReturnedContext = NULL;

    if (Type == MAXULONG)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (ContextType == FLT_VOLUME_CONTEXT && PoolType == PagedPool)
    {
        PoolType = NonPagedPoolNx;
    }

    for (Slot = 0; Slot < FLT_CONTEXT_REGISTRATIONS_PER_TYPE; Slot++)
    {
        Candidate = &Filter->Contexts[Type][Slot];
        if (!Candidate->Registered)
        {
            continue;
        }
        if (Candidate->AllocateCallback != NULL || Candidate->VariableSize || Candidate->Size == ContextSize)
        {
            Info = Candidate;
            break;
        }
        if ((Candidate->Flags & FLTFL_CONTEXT_REGISTRATION_NO_EXACT_SIZE_MATCH) &&
            Candidate->Size >= ContextSize &&
            (Info == NULL || Candidate->Size < Info->Size))
        {
            Info = Candidate;
        }
    }

    if (Info == NULL)
    {
        return STATUS_FLT_CONTEXT_ALLOCATION_NOT_FOUND;
    }

    Size = FLTP_CONTEXT_HEADER_SIZE + ContextSize;
    if (Info->AllocateCallback != NULL)
    {
        Context = Info->AllocateCallback(PoolType, Size, ContextType);
    }
    else
    {
        Context = ExAllocatePoolWithTag(PoolType, Size, Info->PoolTag ? Info->PoolTag : FLT_TAG_CONTEXT);
    }
    if (Context == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context, FLTP_CONTEXT_HEADER_SIZE);
    Context->Signature = FLTP_CONTEXT_SIGNATURE;
    Context->ReferenceCount = 1;
    Context->Type = ContextType;
    Context->Filter = Filter;
    Context->Info = Info;
    InitializeListHead(&Context->OwnerLink);
    InitializeListHead(&Context->ScopeLink);
    FltpReferencePointer(&Filter->Base);

    *ReturnedContext = FLTP_CONTEXT_TO_USER(Context);
    return STATUS_SUCCESS;
}

VOID
FLTAPI
FltReferenceContext(
    _In_ PFLT_CONTEXT Context)
{
    InterlockedIncrement(&FLTP_USER_TO_CONTEXT(Context)->ReferenceCount);
}

VOID
FLTAPI
FltReleaseContext(
    _In_ PFLT_CONTEXT Context)
{
    PFLTP_CONTEXT Header = FLTP_USER_TO_CONTEXT(Context);
    PFLT_FILTER Filter = Header->Filter;
    PFLT_INSTANCE Instance = Header->Instance;
    PFLT_CONTEXT_TYPE_INFO Info = Header->Info;
    FLT_CONTEXT_TYPE Type = Header->Type;

    if (InterlockedDecrement(&Header->ReferenceCount) != 0)
    {
        return;
    }

    if (Info->CleanupCallback != NULL)
    {
        Info->CleanupCallback(Context, Type);
    }

    Header->Signature = 0;
    if (Info->FreeCallback != NULL)
    {
        Info->FreeCallback(Header, Type);
    }
    else
    {
        ExFreePool(Header);
    }

    if (Instance != NULL)
    {
        FltpDereferencePointer(&Instance->Base);
    }
    FltpDereferencePointer(&Filter->Base);
}

static
VOID
FltpUnlinkContextLocked(
    _Inout_ PFLTP_CONTEXT Context)
{
    PFLTP_OWNER_CTRL Ctrl = Context->Owner;

    RemoveEntryList(&Context->OwnerLink);
    InitializeListHead(&Context->OwnerLink);
    RemoveEntryList(&Context->ScopeLink);
    InitializeListHead(&Context->ScopeLink);
    Context->Flags &= ~FLTP_CONTEXT_LINKED;
    Context->Owner = NULL;

    if (Context->Type == FLT_SECTION_CONTEXT)
    {
        InterlockedDecrement(&FltGlobals.SectionCount);
        if (IsListEmpty(&Ctrl->SectionContexts))
        {
            KeSetEvent(&Ctrl->SectionEvent, IO_NO_INCREMENT, FALSE);
        }
    }
}

static
PFLTP_CONTEXT
FltpFindContextLocked(
    _In_ PLIST_ENTRY OwnerList,
    _In_ PVOID Key)
{
    PFLTP_CONTEXT Context;
    PLIST_ENTRY Link;

    for (Link = OwnerList->Flink; Link != OwnerList; Link = Link->Flink)
    {
        Context = CONTAINING_RECORD(Link, FLTP_CONTEXT, OwnerLink);
        if (Context->Key == Key)
        {
            return Context;
        }
    }
    return NULL;
}

static
NTSTATUS
FltpSetContextLocked(
    _In_ PLIST_ENTRY OwnerList,
    _In_ PVOID Owner,
    _In_ PVOID Key,
    _In_ PLIST_ENTRY ScopeList,
    _In_opt_ PFLT_INSTANCE Instance,
    _In_ FLT_SET_CONTEXT_OPERATION Operation,
    _In_ PFLTP_CONTEXT NewContext,
    _Out_ PFLTP_CONTEXT *Replaced,
    _Out_opt_ PFLT_CONTEXT *OldContext)
{
    PFLTP_CONTEXT Existing;

    *Replaced = NULL;

    if (NewContext->Flags & FLTP_CONTEXT_LINKED)
    {
        return STATUS_FLT_CONTEXT_ALREADY_LINKED;
    }

    Existing = FltpFindContextLocked(OwnerList, Key);
    if (Existing != NULL)
    {
        if (Operation == FLT_SET_CONTEXT_KEEP_IF_EXISTS)
        {
            if (OldContext != NULL)
            {
                InterlockedIncrement(&Existing->ReferenceCount);
                *OldContext = FLTP_CONTEXT_TO_USER(Existing);
            }
            return STATUS_FLT_CONTEXT_ALREADY_DEFINED;
        }

        FltpUnlinkContextLocked(Existing);
        *Replaced = Existing;
    }

    NewContext->Flags |= FLTP_CONTEXT_LINKED;
    NewContext->Key = Key;
    NewContext->Owner = Owner;
    if (Instance != NULL && NewContext->Instance == NULL)
    {
        NewContext->Instance = Instance;
        FltpReferencePointer(&Instance->Base);
    }
    InterlockedIncrement(&NewContext->ReferenceCount);
    InsertTailList(OwnerList, &NewContext->OwnerLink);
    InsertTailList(ScopeList, &NewContext->ScopeLink);
    if (NewContext->Type == FLT_SECTION_CONTEXT)
    {
        InterlockedIncrement(&FltGlobals.SectionCount);
        KeClearEvent(&((PFLTP_OWNER_CTRL)Owner)->SectionEvent);
    }
    return STATUS_SUCCESS;
}

static
VOID
FltpFinishSet(
    _In_opt_ PFLTP_CONTEXT Replaced,
    _Out_opt_ PFLT_CONTEXT *OldContext)
{
    if (Replaced == NULL)
    {
        return;
    }

    if (OldContext != NULL)
    {
        *OldContext = FLTP_CONTEXT_TO_USER(Replaced);
    }
    else
    {
        FltReleaseContext(FLTP_CONTEXT_TO_USER(Replaced));
    }
}

static
NTSTATUS
FltpGetContext(
    _In_ PLIST_ENTRY OwnerList,
    _In_ PVOID Key,
    _Outptr_ PFLT_CONTEXT *Context)
{
    PFLTP_CONTEXT Found;

    *Context = NULL;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&FltGlobals.ContextLock);
    Found = FltpFindContextLocked(OwnerList, Key);
    if (Found != NULL)
    {
        InterlockedIncrement(&Found->ReferenceCount);
    }
    ExReleasePushLockShared(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    if (Found == NULL)
    {
        return STATUS_NOT_FOUND;
    }

    *Context = FLTP_CONTEXT_TO_USER(Found);
    return STATUS_SUCCESS;
}

static
NTSTATUS
FltpDeleteContextByKey(
    _In_ PLIST_ENTRY OwnerList,
    _In_ PVOID Key,
    _Out_opt_ PFLT_CONTEXT *OldContext)
{
    PFLTP_CONTEXT Found;

    if (OldContext != NULL)
    {
        *OldContext = NULL;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    Found = FltpFindContextLocked(OwnerList, Key);
    if (Found != NULL)
    {
        FltpUnlinkContextLocked(Found);
    }
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    if (Found == NULL)
    {
        return STATUS_NOT_FOUND;
    }

    FltpFinishSet(Found, OldContext);
    return STATUS_SUCCESS;
}

VOID
FLTAPI
FltDeleteContext(
    _In_ PFLT_CONTEXT Context)
{
    PFLTP_CONTEXT Header = FLTP_USER_TO_CONTEXT(Context);
    BOOLEAN Linked;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    Linked = (Header->Flags & FLTP_CONTEXT_LINKED) != 0;
    if (Linked)
    {
        FltpUnlinkContextLocked(Header);
    }
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    if (Linked)
    {
        FltReleaseContext(Context);
    }
}

static
VOID
FltpReleaseList(
    _In_ PLIST_ENTRY List,
    _In_ BOOLEAN ScopeList,
    _In_opt_ PVOID Key)
{
    PFLTP_CONTEXT Context;
    PLIST_ENTRY Link;

    for (;;)
    {
        Context = NULL;

        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
        for (Link = List->Flink; Link != List; Link = Link->Flink)
        {
            Context = ScopeList ? CONTAINING_RECORD(Link, FLTP_CONTEXT, ScopeLink)
                                : CONTAINING_RECORD(Link, FLTP_CONTEXT, OwnerLink);
            if (Key == NULL || Context->Key == Key)
            {
                FltpUnlinkContextLocked(Context);
                break;
            }
            Context = NULL;
        }
        ExReleasePushLockExclusive(&FltGlobals.ContextLock);
        KeLeaveCriticalRegion();

        if (Context == NULL)
        {
            break;
        }

        FltReleaseContext(FLTP_CONTEXT_TO_USER(Context));
    }
}

VOID
FltpDeleteInstanceContexts(
    _In_ PFLT_INSTANCE Instance)
{
    FltpReleaseList(&Instance->ContextList, TRUE, NULL);
}

VOID
FltpDeleteVolumeContexts(
    _In_ PFLT_VOLUME Volume,
    _In_opt_ PFLT_FILTER Filter)
{
    FltpReleaseList(&Volume->ContextList, FALSE, Filter);
}

VOID
FltpDeleteFilterContexts(
    _In_ PFLT_FILTER Filter)
{
    FltpReleaseList(&Filter->ContextList, TRUE, NULL);
}

VOID
FltpDeleteTransactionContexts(
    _In_ PKTRANSACTION Transaction)
{
    FltpReleaseList(&FltGlobals.TransactionContextList, FALSE, Transaction);
}

static
VOID
FltpFreeOwnerCtrl(
    _In_ PFLTP_OWNER_CTRL Ctrl)
{
    FltpReleaseList(&Ctrl->Contexts, FALSE, NULL);
    FltpReleaseList(&Ctrl->FileContexts, FALSE, NULL);
    FltpReleaseList(&Ctrl->SectionContexts, FALSE, NULL);
    FltpReleaseNameList(&Ctrl->Names);
    ExFreePoolWithTag(Ctrl, FLT_TAG_CONTEXT);
}

static
VOID
NTAPI
FltpStreamCtrlFree(
    _In_ PVOID Buffer)
{
    FltpFreeOwnerCtrl(CONTAINING_RECORD(Buffer, FLTP_OWNER_CTRL, Fsrtl.Stream));
}

static
VOID
NTAPI
FltpFileCtrlFree(
    _In_ PVOID Buffer)
{
    FltpFreeOwnerCtrl(CONTAINING_RECORD(Buffer, FLTP_OWNER_CTRL, Fsrtl.File));
}

static
PFLTP_OWNER_CTRL
FltpAllocateOwnerCtrl(VOID)
{
    PFLTP_OWNER_CTRL Ctrl;

    Ctrl = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Ctrl), FLT_TAG_CONTEXT);
    if (Ctrl != NULL)
    {
        RtlZeroMemory(Ctrl, sizeof(*Ctrl));
        InitializeListHead(&Ctrl->Contexts);
        InitializeListHead(&Ctrl->FileContexts);
        InitializeListHead(&Ctrl->SectionContexts);
        InitializeListHead(&Ctrl->Names);
        KeInitializeEvent(&Ctrl->SectionEvent, NotificationEvent, TRUE);
    }
    return Ctrl;
}

static
PFLTP_OWNER_CTRL
FltpStreamCtrl(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Create)
{
    PFSRTL_ADVANCED_FCB_HEADER Header = FsRtlGetPerStreamContextPointer(FileObject);
    PFSRTL_PER_STREAM_CONTEXT Entry;
    PFLTP_OWNER_CTRL Ctrl;

    Entry = FsRtlLookupPerStreamContext(Header, &FltGlobals, NULL);
    if (Entry != NULL)
    {
        return CONTAINING_RECORD(Entry, FLTP_OWNER_CTRL, Fsrtl.Stream);
    }
    if (!Create)
    {
        return NULL;
    }

    Ctrl = FltpAllocateOwnerCtrl();
    if (Ctrl == NULL)
    {
        return NULL;
    }

    FsRtlInitPerStreamContext(&Ctrl->Fsrtl.Stream, &FltGlobals, NULL, FltpStreamCtrlFree);
    if (!NT_SUCCESS(FsRtlInsertPerStreamContext(Header, &Ctrl->Fsrtl.Stream)))
    {
        ExFreePoolWithTag(Ctrl, FLT_TAG_CONTEXT);
        return NULL;
    }
    return Ctrl;
}

static
PFLTP_OWNER_CTRL
FltpFileObjectCtrl(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Create)
{
    PFSRTL_PER_FILEOBJECT_CONTEXT Entry;
    PFLTP_OWNER_CTRL Ctrl;

    Entry = FsRtlLookupPerFileObjectContext(FileObject, &FltGlobals, NULL);
    if (Entry != NULL)
    {
        return CONTAINING_RECORD(Entry, FLTP_OWNER_CTRL, Fsrtl.FileObject);
    }
    if (!Create)
    {
        return NULL;
    }

    Ctrl = FltpAllocateOwnerCtrl();
    if (Ctrl == NULL)
    {
        return NULL;
    }

    FsRtlInitPerFileObjectContext(&Ctrl->Fsrtl.FileObject, &FltGlobals, NULL);
    if (!NT_SUCCESS(FsRtlInsertPerFileObjectContext(FileObject, &Ctrl->Fsrtl.FileObject)))
    {
        ExFreePoolWithTag(Ctrl, FLT_TAG_CONTEXT);
        return NULL;
    }
    return Ctrl;
}

PLIST_ENTRY
FltpFileObjectNameList(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Create)
{
    PFLTP_OWNER_CTRL Ctrl = FltpFileObjectCtrl(FileObject, Create);

    return Ctrl != NULL ? &Ctrl->Names : NULL;
}

VOID
FltpCleanupFileObjectContexts(
    _In_ PFILE_OBJECT FileObject)
{
    PFSRTL_PER_FILEOBJECT_CONTEXT Entry;

    if (FileObject == NULL)
    {
        return;
    }

    Entry = FsRtlRemovePerFileObjectContext(FileObject, &FltGlobals, NULL);
    if (Entry != NULL)
    {
        FltpFreeOwnerCtrl(CONTAINING_RECORD(Entry, FLTP_OWNER_CTRL, Fsrtl.FileObject));
    }
}

static
BOOLEAN
FltpSingleStreamFileSystem(
    _In_opt_ PFLT_INSTANCE Instance)
{
    if (Instance == NULL)
    {
        return FALSE;
    }

    switch (Instance->Volume->FileSystemType)
    {
        case FLT_FSTYPE_FAT:
        case FLT_FSTYPE_EXFAT:
        case FLT_FSTYPE_CDFS:
        case FLT_FSTYPE_UDFS:
            return TRUE;

        default:
            return FALSE;
    }
}

BOOLEAN
FLTAPI
FltSupportsStreamContexts(
    _In_ PFILE_OBJECT FileObject)
{
    return FileObject != NULL && FsRtlSupportsPerStreamContexts(FileObject);
}

BOOLEAN
FLTAPI
FltSupportsStreamHandleContexts(
    _In_ PFILE_OBJECT FileObject)
{
    return FltSupportsStreamContexts(FileObject);
}

BOOLEAN
FLTAPI
FltSupportsFileContextsEx(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PFLT_INSTANCE Instance)
{
    if (FileObject == NULL || FileObject->FsContext == NULL)
    {
        return FALSE;
    }
    if (FsRtlSupportsPerFileContexts(FileObject))
    {
        return TRUE;
    }
    return FltpSingleStreamFileSystem(Instance) && FsRtlSupportsPerStreamContexts(FileObject);
}

BOOLEAN
FLTAPI
FltSupportsFileContexts(
    _In_ PFILE_OBJECT FileObject)
{
    return FltSupportsFileContextsEx(FileObject, NULL);
}

static
NTSTATUS
FltpFileObjectOwnerList(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ FLT_CONTEXT_TYPE Type,
    _In_ BOOLEAN Create,
    _Out_ PLIST_ENTRY *OwnerList,
    _Out_ PVOID *Owner)
{
    PFSRTL_PER_FILE_CONTEXT FileEntry;
    PFLTP_OWNER_CTRL Ctrl = NULL;
    PVOID *FilePointer;

    *OwnerList = NULL;
    *Owner = NULL;

    if (FileObject == NULL || FileObject->FsContext == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    switch (Type)
    {
        case FLT_STREAM_CONTEXT:
            if (!FsRtlSupportsPerStreamContexts(FileObject))
            {
                return STATUS_NOT_SUPPORTED;
            }
            Ctrl = FltpStreamCtrl(FileObject, Create);
            if (Ctrl != NULL)
            {
                *OwnerList = &Ctrl->Contexts;
            }
            break;

        case FLT_SECTION_CONTEXT:
            if (!FsRtlSupportsPerStreamContexts(FileObject))
            {
                return STATUS_NOT_SUPPORTED;
            }
            Ctrl = FltpStreamCtrl(FileObject, Create);
            if (Ctrl != NULL)
            {
                *OwnerList = &Ctrl->SectionContexts;
            }
            break;

        case FLT_STREAMHANDLE_CONTEXT:
            if (!FsRtlSupportsPerStreamContexts(FileObject))
            {
                return STATUS_NOT_SUPPORTED;
            }
            Ctrl = FltpFileObjectCtrl(FileObject, Create);
            if (Ctrl != NULL)
            {
                *OwnerList = &Ctrl->Contexts;
            }
            break;

        case FLT_FILE_CONTEXT:
            if (FsRtlSupportsPerFileContexts(FileObject))
            {
                FilePointer = FsRtlGetPerFileContextPointer(FileObject);
                FileEntry = FsRtlLookupPerFileContext(FilePointer, &FltGlobals, NULL);
                if (FileEntry != NULL)
                {
                    Ctrl = CONTAINING_RECORD(FileEntry, FLTP_OWNER_CTRL, Fsrtl.File);
                }
                else if (Create)
                {
                    Ctrl = FltpAllocateOwnerCtrl();
                    if (Ctrl != NULL)
                    {
                        FsRtlInitPerFileContext(&Ctrl->Fsrtl.File, &FltGlobals, NULL, FltpFileCtrlFree);
                        if (!NT_SUCCESS(FsRtlInsertPerFileContext(FilePointer, &Ctrl->Fsrtl.File)))
                        {
                            ExFreePoolWithTag(Ctrl, FLT_TAG_CONTEXT);
                            Ctrl = NULL;
                        }
                    }
                }
                if (Ctrl != NULL)
                {
                    *OwnerList = &Ctrl->Contexts;
                }
            }
            else if (FltpSingleStreamFileSystem(Instance) && FsRtlSupportsPerStreamContexts(FileObject))
            {
                Ctrl = FltpStreamCtrl(FileObject, Create);
                if (Ctrl != NULL)
                {
                    *OwnerList = &Ctrl->FileContexts;
                }
            }
            else
            {
                return STATUS_NOT_SUPPORTED;
            }
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    if (Ctrl == NULL)
    {
        return Create ? STATUS_INSUFFICIENT_RESOURCES : STATUS_NOT_FOUND;
    }

    *Owner = Ctrl;
    return STATUS_SUCCESS;
}

static
NTSTATUS
FltpSetFileObjectContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ FLT_CONTEXT_TYPE Type,
    _In_ FLT_SET_CONTEXT_OPERATION Operation,
    _In_ PFLT_CONTEXT NewContext,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    PFLTP_CONTEXT Header = FLTP_USER_TO_CONTEXT(NewContext), Replaced = NULL;
    PLIST_ENTRY OwnerList;
    PVOID Owner;
    NTSTATUS Status;

    if (OldContext != NULL)
    {
        *OldContext = NULL;
    }

    if (Header->Type != Type)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Instance->Flags & FLTP_INSTANCE_DETACHING)
    {
        return STATUS_FLT_DELETING_OBJECT;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    Status = FltpFileObjectOwnerList(Instance, FileObject, Type, TRUE, &OwnerList, &Owner);
    if (NT_SUCCESS(Status))
    {
        Status = FltpSetContextLocked(OwnerList,
                                      Owner,
                                      Instance,
                                      &Instance->ContextList,
                                      Instance,
                                      Operation,
                                      Header,
                                      &Replaced,
                                      OldContext);
    }
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    FltpFinishSet(Replaced, OldContext);
    return Status;
}

static
NTSTATUS
FltpGetFileObjectContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ FLT_CONTEXT_TYPE Type,
    _Outptr_ PFLT_CONTEXT *Context)
{
    PFLTP_CONTEXT Found = NULL;
    PLIST_ENTRY OwnerList;
    PVOID Owner;
    NTSTATUS Status;

    *Context = NULL;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&FltGlobals.ContextLock);
    Status = FltpFileObjectOwnerList(Instance, FileObject, Type, FALSE, &OwnerList, &Owner);
    if (NT_SUCCESS(Status))
    {
        Found = FltpFindContextLocked(OwnerList, Instance);
        if (Found != NULL)
        {
            InterlockedIncrement(&Found->ReferenceCount);
        }
        else
        {
            Status = STATUS_NOT_FOUND;
        }
    }
    ExReleasePushLockShared(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    if (Found != NULL)
    {
        *Context = FLTP_CONTEXT_TO_USER(Found);
    }
    return Status;
}

static
NTSTATUS
FltpDeleteFileObjectContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ FLT_CONTEXT_TYPE Type,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    PFLTP_CONTEXT Found = NULL;
    PLIST_ENTRY OwnerList;
    PVOID Owner;
    NTSTATUS Status;

    if (OldContext != NULL)
    {
        *OldContext = NULL;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    Status = FltpFileObjectOwnerList(Instance, FileObject, Type, FALSE, &OwnerList, &Owner);
    if (NT_SUCCESS(Status))
    {
        Found = FltpFindContextLocked(OwnerList, Instance);
        if (Found != NULL)
        {
            FltpUnlinkContextLocked(Found);
        }
        else
        {
            Status = STATUS_NOT_FOUND;
        }
    }
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    FltpFinishSet(Found, OldContext);
    return Status;
}

NTSTATUS
FLTAPI
FltSetVolumeContext(
    _In_ PFLT_VOLUME Volume,
    _In_ FLT_SET_CONTEXT_OPERATION Operation,
    _In_ PFLT_CONTEXT NewContext,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    PFLTP_CONTEXT Header = FLTP_USER_TO_CONTEXT(NewContext), Replaced = NULL;
    NTSTATUS Status;

    if (OldContext != NULL)
    {
        *OldContext = NULL;
    }
    if (Header->Type != FLT_VOLUME_CONTEXT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Volume->Flags & FLTP_VOLUME_DISMOUNTED)
    {
        return STATUS_FLT_DELETING_OBJECT;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    Status = FltpSetContextLocked(&Volume->ContextList,
                                  Volume,
                                  Header->Filter,
                                  &Header->Filter->ContextList,
                                  NULL,
                                  Operation,
                                  Header,
                                  &Replaced,
                                  OldContext);
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    FltpFinishSet(Replaced, OldContext);
    return Status;
}

NTSTATUS
FLTAPI
FltGetVolumeContext(
    _In_ PFLT_FILTER Filter,
    _In_ PFLT_VOLUME Volume,
    _Outptr_ PFLT_CONTEXT *Context)
{
    return FltpGetContext(&Volume->ContextList, Filter, Context);
}

NTSTATUS
FLTAPI
FltDeleteVolumeContext(
    _In_ PFLT_FILTER Filter,
    _In_ PFLT_VOLUME Volume,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    return FltpDeleteContextByKey(&Volume->ContextList, Filter, OldContext);
}

NTSTATUS
FLTAPI
FltSetInstanceContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ FLT_SET_CONTEXT_OPERATION Operation,
    _In_ PFLT_CONTEXT NewContext,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    PFLTP_CONTEXT Header = FLTP_USER_TO_CONTEXT(NewContext), Replaced = NULL;
    NTSTATUS Status;

    if (OldContext != NULL)
    {
        *OldContext = NULL;
    }
    if (Header->Type != FLT_INSTANCE_CONTEXT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Instance->Flags & FLTP_INSTANCE_DETACHING)
    {
        return STATUS_FLT_DELETING_OBJECT;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    Status = FltpSetContextLocked(&Instance->OwnContextList,
                                  Instance,
                                  Instance,
                                  &Instance->ContextList,
                                  Instance,
                                  Operation,
                                  Header,
                                  &Replaced,
                                  OldContext);
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    FltpFinishSet(Replaced, OldContext);
    return Status;
}

NTSTATUS
FLTAPI
FltGetInstanceContext(
    _In_ PFLT_INSTANCE Instance,
    _Outptr_ PFLT_CONTEXT *Context)
{
    return FltpGetContext(&Instance->OwnContextList, Instance, Context);
}

NTSTATUS
FLTAPI
FltDeleteInstanceContext(
    _In_ PFLT_INSTANCE Instance,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    return FltpDeleteContextByKey(&Instance->OwnContextList, Instance, OldContext);
}

NTSTATUS
FLTAPI
FltSetStreamContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ FLT_SET_CONTEXT_OPERATION Operation,
    _In_ PFLT_CONTEXT NewContext,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    return FltpSetFileObjectContext(Instance, FileObject, FLT_STREAM_CONTEXT, Operation, NewContext, OldContext);
}

NTSTATUS
FLTAPI
FltGetStreamContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ PFLT_CONTEXT *Context)
{
    return FltpGetFileObjectContext(Instance, FileObject, FLT_STREAM_CONTEXT, Context);
}

NTSTATUS
FLTAPI
FltDeleteStreamContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    return FltpDeleteFileObjectContext(Instance, FileObject, FLT_STREAM_CONTEXT, OldContext);
}

NTSTATUS
FLTAPI
FltSetStreamHandleContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ FLT_SET_CONTEXT_OPERATION Operation,
    _In_ PFLT_CONTEXT NewContext,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    return FltpSetFileObjectContext(Instance, FileObject, FLT_STREAMHANDLE_CONTEXT, Operation, NewContext, OldContext);
}

NTSTATUS
FLTAPI
FltGetStreamHandleContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ PFLT_CONTEXT *Context)
{
    return FltpGetFileObjectContext(Instance, FileObject, FLT_STREAMHANDLE_CONTEXT, Context);
}

NTSTATUS
FLTAPI
FltDeleteStreamHandleContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    return FltpDeleteFileObjectContext(Instance, FileObject, FLT_STREAMHANDLE_CONTEXT, OldContext);
}

NTSTATUS
FLTAPI
FltSetFileContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ FLT_SET_CONTEXT_OPERATION Operation,
    _In_ PFLT_CONTEXT NewContext,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    return FltpSetFileObjectContext(Instance, FileObject, FLT_FILE_CONTEXT, Operation, NewContext, OldContext);
}

NTSTATUS
FLTAPI
FltGetFileContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ PFLT_CONTEXT *Context)
{
    return FltpGetFileObjectContext(Instance, FileObject, FLT_FILE_CONTEXT, Context);
}

NTSTATUS
FLTAPI
FltDeleteFileContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Outptr_opt_result_maybenull_ PFLT_CONTEXT *OldContext)
{
    return FltpDeleteFileObjectContext(Instance, FileObject, FLT_FILE_CONTEXT, OldContext);
}

typedef struct _FLTP_TRANSACTION_KEY
{
    PFLT_INSTANCE Instance;
    PKTRANSACTION Transaction;
} FLTP_TRANSACTION_KEY;

static
PFLTP_CONTEXT
FltpFindTransactionContextLocked(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction)
{
    PFLTP_CONTEXT Context;
    PLIST_ENTRY Link;

    for (Link = FltGlobals.TransactionContextList.Flink;
         Link != &FltGlobals.TransactionContextList;
         Link = Link->Flink)
    {
        Context = CONTAINING_RECORD(Link, FLTP_CONTEXT, OwnerLink);
        if (Context->Key == Transaction && Context->Instance == Instance)
        {
            return Context;
        }
    }
    return NULL;
}

NTSTATUS
FLTAPI
FltSetTransactionContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _In_ FLT_SET_CONTEXT_OPERATION Operation,
    _In_ PFLT_CONTEXT NewContext,
    _Outptr_opt_ PFLT_CONTEXT *OldContext)
{
    PFLTP_CONTEXT Header = FLTP_USER_TO_CONTEXT(NewContext), Existing, Replaced = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    if (OldContext != NULL)
    {
        *OldContext = NULL;
    }
    if (Header->Type != FLT_TRANSACTION_CONTEXT || Transaction == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = FltpTrackTransaction(Transaction);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    if (Header->Flags & FLTP_CONTEXT_LINKED)
    {
        Status = STATUS_FLT_CONTEXT_ALREADY_LINKED;
    }
    else
    {
        Existing = FltpFindTransactionContextLocked(Instance, Transaction);
        if (Existing != NULL && Operation == FLT_SET_CONTEXT_KEEP_IF_EXISTS)
        {
            if (OldContext != NULL)
            {
                InterlockedIncrement(&Existing->ReferenceCount);
                *OldContext = FLTP_CONTEXT_TO_USER(Existing);
            }
            Status = STATUS_FLT_CONTEXT_ALREADY_DEFINED;
        }
        else
        {
            if (Existing != NULL)
            {
                FltpUnlinkContextLocked(Existing);
                Replaced = Existing;
            }
            Header->Flags |= FLTP_CONTEXT_LINKED;
            Header->Key = Transaction;
            Header->Owner = Transaction;
            if (Header->Instance == NULL)
            {
                Header->Instance = Instance;
                FltpReferencePointer(&Instance->Base);
            }
            InterlockedIncrement(&Header->ReferenceCount);
            InsertTailList(&FltGlobals.TransactionContextList, &Header->OwnerLink);
            InsertTailList(&Instance->ContextList, &Header->ScopeLink);
        }
    }
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    FltpFinishSet(Replaced, OldContext);
    return Status;
}

NTSTATUS
FLTAPI
FltGetTransactionContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _Outptr_ PFLT_CONTEXT *Context)
{
    PFLTP_CONTEXT Found;

    *Context = NULL;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&FltGlobals.ContextLock);
    Found = FltpFindTransactionContextLocked(Instance, Transaction);
    if (Found != NULL)
    {
        InterlockedIncrement(&Found->ReferenceCount);
    }
    ExReleasePushLockShared(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    if (Found == NULL)
    {
        return STATUS_NOT_FOUND;
    }
    *Context = FLTP_CONTEXT_TO_USER(Found);
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltDeleteTransactionContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _Outptr_opt_ PFLT_CONTEXT *OldContext)
{
    PFLTP_CONTEXT Found;

    if (OldContext != NULL)
    {
        *OldContext = NULL;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    Found = FltpFindTransactionContextLocked(Instance, Transaction);
    if (Found != NULL)
    {
        FltpUnlinkContextLocked(Found);
    }
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    if (Found == NULL)
    {
        return STATUS_NOT_FOUND;
    }
    FltpFinishSet(Found, OldContext);
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltGetSectionContext(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ PFLT_CONTEXT *Context)
{
    return FltpGetFileObjectContext(Instance, FileObject, FLT_SECTION_CONTEXT, Context);
}

NTSTATUS
FLTAPI
FltRegisterForDataScan(
    _In_ PFLT_INSTANCE Instance)
{
    if (Instance->Volume->FileSystemType == FLT_FSTYPE_NPFS || Instance->Volume->FileSystemType == FLT_FSTYPE_MSFS)
    {
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltCreateSectionForDataScan(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ PFLT_CONTEXT SectionContext,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PLARGE_INTEGER MaximumSize,
    _In_ ULONG SectionPageProtection,
    _In_ ULONG AllocationAttributes,
    _In_ ULONG Flags,
    _Out_ PHANDLE SectionHandle,
    _Outptr_ PVOID *SectionObject,
    _Out_opt_ PLARGE_INTEGER SectionFileSize)
{
    PFLTP_CONTEXT Header = FLTP_USER_TO_CONTEXT(SectionContext);
    BOOLEAN Linked;
    NTSTATUS Status;

    *SectionHandle = NULL;
    *SectionObject = NULL;

    if (Header->Type != FLT_SECTION_CONTEXT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Header->Flags & FLTP_CONTEXT_SECTION)
    {
        return STATUS_INVALID_PARAMETER_3;
    }

    Status = FltpSetFileObjectContext(Instance,
                                      FileObject,
                                      FLT_SECTION_CONTEXT,
                                      FLT_SET_CONTEXT_KEEP_IF_EXISTS,
                                      SectionContext,
                                      NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = FsRtlCreateSectionForDataScan(SectionHandle,
                                           SectionObject,
                                           SectionFileSize,
                                           FileObject,
                                           DesiredAccess,
                                           ObjectAttributes,
                                           MaximumSize,
                                           SectionPageProtection,
                                           AllocationAttributes,
                                           Flags);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    Linked = (Header->Flags & FLTP_CONTEXT_LINKED) != 0;
    if (NT_SUCCESS(Status))
    {
        Header->Flags |= FLTP_CONTEXT_SECTION;
        Linked = FALSE;
    }
    else if (Linked)
    {
        FltpUnlinkContextLocked(Header);
    }
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    if (Linked)
    {
        FltReleaseContext(SectionContext);
    }
    return Status;
}

NTSTATUS
FLTAPI
FltCloseSectionForDataScan(
    _In_ PFLT_CONTEXT SectionContext)
{
    PFLTP_CONTEXT Header = FLTP_USER_TO_CONTEXT(SectionContext);
    NTSTATUS Status = STATUS_SUCCESS;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&FltGlobals.ContextLock);
    if (Header->Type != FLT_SECTION_CONTEXT || !(Header->Flags & FLTP_CONTEXT_SECTION))
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else if (!(Header->Flags & FLTP_CONTEXT_LINKED))
    {
        Status = STATUS_NOT_FOUND;
    }
    else
    {
        FltpUnlinkContextLocked(Header);
    }
    ExReleasePushLockExclusive(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    if (NT_SUCCESS(Status))
    {
        FltReleaseContext(SectionContext);
    }
    return Status;
}

NTSTATUS
FLTAPI
FltGetContextsEx(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_CONTEXT_TYPE DesiredContexts,
    _In_ SIZE_T ContextsSize,
    _Out_ PFLT_RELATED_CONTEXTS_EX Contexts)
{
    FLT_RELATED_CONTEXTS_EX Local;

    RtlZeroMemory(&Local, sizeof(Local));

    if ((DesiredContexts & FLT_VOLUME_CONTEXT) && FltObjects->Volume != NULL && FltObjects->Filter != NULL)
    {
        (VOID)FltGetVolumeContext(FltObjects->Filter, FltObjects->Volume, &Local.VolumeContext);
    }
    if ((DesiredContexts & FLT_INSTANCE_CONTEXT) && FltObjects->Instance != NULL)
    {
        (VOID)FltGetInstanceContext(FltObjects->Instance, &Local.InstanceContext);
    }
    if (FltObjects->Instance != NULL && FltObjects->FileObject != NULL)
    {
        if (DesiredContexts & FLT_FILE_CONTEXT)
        {
            (VOID)FltGetFileContext(FltObjects->Instance, FltObjects->FileObject, &Local.FileContext);
        }
        if (DesiredContexts & FLT_STREAM_CONTEXT)
        {
            (VOID)FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject, &Local.StreamContext);
        }
        if (DesiredContexts & FLT_STREAMHANDLE_CONTEXT)
        {
            (VOID)FltGetStreamHandleContext(FltObjects->Instance, FltObjects->FileObject, &Local.StreamHandleContext);
        }
        if (DesiredContexts & FLT_SECTION_CONTEXT)
        {
            (VOID)FltGetSectionContext(FltObjects->Instance, FltObjects->FileObject, &Local.SectionContext);
        }
    }
    if ((DesiredContexts & FLT_TRANSACTION_CONTEXT) && FltObjects->Instance != NULL && FltObjects->Transaction != NULL)
    {
        (VOID)FltGetTransactionContext(FltObjects->Instance, FltObjects->Transaction, &Local.TransactionContext);
    }

    RtlCopyMemory(Contexts, &Local, min(ContextsSize, sizeof(Local)));
    return STATUS_SUCCESS;
}

VOID
FLTAPI
FltGetContexts(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_CONTEXT_TYPE DesiredContexts,
    _Out_ PFLT_RELATED_CONTEXTS Contexts)
{
    if (!NT_SUCCESS(FltGetContextsEx(FltObjects, DesiredContexts & ~FLT_SECTION_CONTEXT, sizeof(*Contexts), (PFLT_RELATED_CONTEXTS_EX)Contexts)))
    {
        RtlZeroMemory(Contexts, sizeof(*Contexts));
    }
}

VOID
FLTAPI
FltReleaseContextsEx(
    _In_ SIZE_T ContextsSize,
    _In_ PFLT_RELATED_CONTEXTS_EX Contexts)
{
    PFLT_CONTEXT *Array = (PFLT_CONTEXT *)Contexts;
    SIZE_T Index;

    for (Index = 0; Index < ContextsSize / sizeof(PFLT_CONTEXT); Index++)
    {
        if (Array[Index] != NULL)
        {
            FltReleaseContext(Array[Index]);
            Array[Index] = NULL;
        }
    }
}

VOID
FLTAPI
FltReleaseContexts(
    _In_ PFLT_RELATED_CONTEXTS Contexts)
{
    FltReleaseContextsEx(sizeof(*Contexts), (PFLT_RELATED_CONTEXTS_EX)Contexts);
}

BOOLEAN
FltpHasSections(
    _In_opt_ PFILE_OBJECT FileObject)
{
    PFLTP_OWNER_CTRL Ctrl;
    BOOLEAN Result = FALSE;

    if (FltGlobals.SectionCount == 0 ||
        KeGetCurrentIrql() != PASSIVE_LEVEL ||
        FileObject == NULL ||
        FileObject->FsContext == NULL ||
        !FsRtlSupportsPerStreamContexts(FileObject))
    {
        return FALSE;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&FltGlobals.ContextLock);
    Ctrl = FltpStreamCtrl(FileObject, FALSE);
    if (Ctrl != NULL)
    {
        Result = !IsListEmpty(&Ctrl->SectionContexts);
    }
    ExReleasePushLockShared(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();
    return Result;
}

static
BOOLEAN
FltpIsSectionConflict(
    _In_ PFLT_CALLBACK_DATA Data)
{
    PFLT_IO_PARAMETER_BLOCK Iopb = Data->Iopb;
    PFSRTL_COMMON_FCB_HEADER Header = Iopb->TargetFileObject->FsContext;
    LONGLONG NewSize;

    if (!(Data->Flags & FLTFL_CALLBACK_DATA_IRP_OPERATION) ||
        (Iopb->IrpFlags & (IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO)))
    {
        return FALSE;
    }

    if (Iopb->MajorFunction == IRP_MJ_WRITE)
    {
        return (Iopb->IrpFlags & IRP_NOCACHE) != 0;
    }
    if (Iopb->MajorFunction != IRP_MJ_SET_INFORMATION)
    {
        return FALSE;
    }

    switch (Iopb->Parameters.SetFileInformation.FileInformationClass)
    {
        case FileEndOfFileInformation:
            if (Iopb->Parameters.SetFileInformation.Length < sizeof(FILE_END_OF_FILE_INFORMATION) ||
                Iopb->Parameters.SetFileInformation.AdvanceOnly)
            {
                return FALSE;
            }
            NewSize = ((PFILE_END_OF_FILE_INFORMATION)Iopb->Parameters.SetFileInformation.InfoBuffer)->EndOfFile.QuadPart;
            break;

        case FileAllocationInformation:
            if (Iopb->Parameters.SetFileInformation.Length < sizeof(FILE_ALLOCATION_INFORMATION))
            {
                return FALSE;
            }
            NewSize = ((PFILE_ALLOCATION_INFORMATION)Iopb->Parameters.SetFileInformation.InfoBuffer)->AllocationSize.QuadPart;
            break;

        default:
            return FALSE;
    }

    return NewSize < Header->FileSize.QuadPart;
}

VOID
FltpSynchronizeSections(
    _In_ PFLT_CALLBACK_DATA Data)
{
    PFILE_OBJECT FileObject = Data->Iopb->TargetFileObject;
    PFLTP_CONTEXT Snapshot[FLTP_SECTION_SNAPSHOT];
    PFLTP_OWNER_CTRL Ctrl;
    PLIST_ENTRY Link;
    ULONG Count = 0, Index;

    if (!FltpHasSections(FileObject) || !FltpIsSectionConflict(Data))
    {
        return;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&FltGlobals.ContextLock);
    Ctrl = FltpStreamCtrl(FileObject, FALSE);
    if (Ctrl != NULL)
    {
        for (Link = Ctrl->SectionContexts.Flink;
             Link != &Ctrl->SectionContexts && Count < FLTP_SECTION_SNAPSHOT;
             Link = Link->Flink)
        {
            Snapshot[Count] = CONTAINING_RECORD(Link, FLTP_CONTEXT, OwnerLink);
            InterlockedIncrement(&Snapshot[Count]->ReferenceCount);
            Count++;
        }
    }
    ExReleasePushLockShared(&FltGlobals.ContextLock);
    KeLeaveCriticalRegion();

    if (Count == 0)
    {
        return;
    }

    for (Index = 0; Index < Count; Index++)
    {
        if (Snapshot[Index]->Filter->SectionNotification != NULL &&
            ExAcquireRundownProtection(&Snapshot[Index]->Instance->Base.RundownRef))
        {
            (VOID)Snapshot[Index]->Filter->SectionNotification(Snapshot[Index]->Instance,
                                                               FLTP_CONTEXT_TO_USER(Snapshot[Index]),
                                                               Data);
            ExReleaseRundownProtection(&Snapshot[Index]->Instance->Base.RundownRef);
        }
        FltReleaseContext(FLTP_CONTEXT_TO_USER(Snapshot[Index]));
    }

    KeWaitForSingleObject(&Ctrl->SectionEvent, Executive, KernelMode, FALSE, NULL);
}
