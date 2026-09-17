/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Private namespaces (boundary descriptor scoped directories)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define TAG_OB_NAMESPACE 'sNbO'
#define OBP_MAX_BOUNDARY_SIZE 0x10000
#define OBP_MAX_BOUNDARY_ITEMS 64

typedef struct _OBP_PRIVATE_NAMESPACE
{
    LIST_ENTRY Link;
    POBJECT_DIRECTORY Directory;
    ULONG BoundaryLength;
    OBJECT_BOUNDARY_DESCRIPTOR Boundary;
} OBP_PRIVATE_NAMESPACE, *POBP_PRIVATE_NAMESPACE;

static LIST_ENTRY ObpPrivateNamespaceList;
static EX_PUSH_LOCK ObpPrivateNamespaceLock;

VOID
NTAPI
ObpInitializePrivateNamespaces(VOID)
{
    InitializeListHead(&ObpPrivateNamespaceList);
    ExInitializePushLock(&ObpPrivateNamespaceLock);
}

static
VOID
ObpLockPrivateNamespaces(VOID)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&ObpPrivateNamespaceLock);
}

static
VOID
ObpUnlockPrivateNamespaces(VOID)
{
    ExReleasePushLockExclusive(&ObpPrivateNamespaceLock);
    KeLeaveCriticalRegion();
}

static
BOOLEAN
ObpValidateBoundaryDescriptor(
    _In_ POBJECT_BOUNDARY_DESCRIPTOR Descriptor)
{
    POBJECT_BOUNDARY_ENTRY Entry;
    ULONG Offset = sizeof(OBJECT_BOUNDARY_DESCRIPTOR), Index;
    PSID Sid;

    if (Descriptor->Version != OBJECT_BOUNDARY_DESCRIPTOR_VERSION ||
        Descriptor->Items > OBP_MAX_BOUNDARY_ITEMS ||
        (Descriptor->Flags & ~BOUNDARY_DESCRIPTOR_ADD_APPCONTAINER_SID))
    {
        return FALSE;
    }

    for (Index = 0; Index < Descriptor->Items; Index++)
    {
        if (Offset + sizeof(OBJECT_BOUNDARY_ENTRY) > Descriptor->TotalSize)
            return FALSE;
        Entry = (POBJECT_BOUNDARY_ENTRY)((PUCHAR)Descriptor + Offset);
        if (Entry->EntrySize < sizeof(OBJECT_BOUNDARY_ENTRY) ||
            (Entry->EntrySize & (sizeof(ULONG) - 1)) ||
            Offset + Entry->EntrySize > Descriptor->TotalSize)
        {
            return FALSE;
        }
        switch (Entry->EntryType)
        {
            case OBNS_Name:
                break;
            case OBNS_SID:
            case OBNS_IL:
                Sid = (PSID)(Entry + 1);
                if (Entry->EntrySize < sizeof(OBJECT_BOUNDARY_ENTRY) + FIELD_OFFSET(SID, SubAuthority) ||
                    !RtlValidSid(Sid) ||
                    RtlLengthSid(Sid) > Entry->EntrySize - sizeof(OBJECT_BOUNDARY_ENTRY))
                {
                    return FALSE;
                }
                break;
            default:
                return FALSE;
        }
        Offset += Entry->EntrySize;
    }
    return Offset <= Descriptor->TotalSize;
}

static
NTSTATUS
ObpCaptureBoundaryDescriptor(
    _In_ PVOID UserDescriptor,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ POBJECT_BOUNDARY_DESCRIPTOR *Captured)
{
    OBJECT_BOUNDARY_DESCRIPTOR Header;
    POBJECT_BOUNDARY_DESCRIPTOR Copy = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    *Captured = NULL;
    if (!UserDescriptor) return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead(UserDescriptor, sizeof(Header), sizeof(ULONG));
        Header = *(POBJECT_BOUNDARY_DESCRIPTOR)UserDescriptor;
        if (Header.TotalSize < sizeof(Header) || Header.TotalSize > OBP_MAX_BOUNDARY_SIZE)
        {
            Status = STATUS_INVALID_PARAMETER;
            _SEH2_LEAVE;
        }
        Copy = ExAllocatePoolWithTag(PagedPool, Header.TotalSize, TAG_OB_NAMESPACE);
        if (!Copy)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            _SEH2_LEAVE;
        }
        if (PreviousMode != KernelMode)
            ProbeForRead(UserDescriptor, Header.TotalSize, sizeof(ULONG));
        RtlCopyMemory(Copy, UserDescriptor, Header.TotalSize);
        Copy->TotalSize = Header.TotalSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status) && !ObpValidateBoundaryDescriptor(Copy))
        Status = STATUS_INVALID_PARAMETER;

    if (!NT_SUCCESS(Status))
    {
        if (Copy) ExFreePoolWithTag(Copy, TAG_OB_NAMESPACE);
        return Status;
    }

    *Captured = Copy;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpCheckBoundaryAccess(
    _In_ POBJECT_BOUNDARY_DESCRIPTOR Descriptor)
{
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    PTOKEN Token;
    POBJECT_BOUNDARY_ENTRY Entry;
    ULONG Offset = sizeof(OBJECT_BOUNDARY_DESCRIPTOR), Index, Rid;
    PSID Sid;
    NTSTATUS Status = STATUS_SUCCESS;

    SeCaptureSubjectContext(&SubjectContext);
    Token = SeQuerySubjectContextToken(&SubjectContext);

    for (Index = 0; Index < Descriptor->Items && NT_SUCCESS(Status); Index++)
    {
        Entry = (POBJECT_BOUNDARY_ENTRY)((PUCHAR)Descriptor + Offset);
        Sid = (PSID)(Entry + 1);
        if (Entry->EntryType == OBNS_SID)
        {
            if (!SepSidInToken(Token, Sid))
                Status = STATUS_ACCESS_DENIED;
        }
        else if (Entry->EntryType == OBNS_IL)
        {
            Rid = *RtlSubAuthoritySid(Sid, *RtlSubAuthorityCountSid(Sid) - 1);
            if (SepGetTokenIntegrityRid(Token) < Rid)
                Status = STATUS_ACCESS_DENIED;
        }
        Offset += Entry->EntrySize;
    }

    if (NT_SUCCESS(Status) && (Descriptor->Flags & BOUNDARY_DESCRIPTOR_ADD_APPCONTAINER_SID) && !Token->LowBoxInfo)
        Status = STATUS_ACCESS_DENIED;

    SeReleaseSubjectContext(&SubjectContext);
    return Status;
}

static
POBP_PRIVATE_NAMESPACE
ObpFindPrivateNamespace(
    _In_ POBJECT_BOUNDARY_DESCRIPTOR Descriptor)
{
    PLIST_ENTRY Entry;
    POBP_PRIVATE_NAMESPACE Namespace;

    for (Entry = ObpPrivateNamespaceList.Flink; Entry != &ObpPrivateNamespaceList; Entry = Entry->Flink)
    {
        Namespace = CONTAINING_RECORD(Entry, OBP_PRIVATE_NAMESPACE, Link);
        if (Namespace->BoundaryLength == Descriptor->TotalSize &&
            RtlEqualMemory(&Namespace->Boundary, Descriptor, Descriptor->TotalSize))
        {
            return Namespace;
        }
    }
    return NULL;
}

VOID
NTAPI
ObpRemovePrivateNamespace(
    _In_ POBJECT_DIRECTORY Directory)
{
    POBP_PRIVATE_NAMESPACE Namespace;

    ObpLockPrivateNamespaces();
    Namespace = Directory->NamespaceEntry;
    if (Namespace)
    {
        RemoveEntryList(&Namespace->Link);
        Directory->NamespaceEntry = NULL;
    }
    ObpUnlockPrivateNamespaces();

    if (Namespace)
        ExFreePoolWithTag(Namespace, TAG_OB_NAMESPACE);
}

NTSTATUS
NTAPI
NtCreatePrivateNamespace(
    _Out_ PHANDLE NamespaceHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ PVOID BoundaryDescriptor)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_BOUNDARY_DESCRIPTOR Boundary = NULL;
    POBP_PRIVATE_NAMESPACE Namespace = NULL;
    POBJECT_DIRECTORY Directory = NULL;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(NamespaceHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObpCaptureBoundaryDescriptor(BoundaryDescriptor, PreviousMode, &Boundary);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObpCheckBoundaryAccess(Boundary);
    if (!NT_SUCCESS(Status)) goto Quit;

    Namespace = ExAllocatePoolWithTag(PagedPool,
                                      FIELD_OFFSET(OBP_PRIVATE_NAMESPACE, Boundary) + Boundary->TotalSize,
                                      TAG_OB_NAMESPACE);
    if (!Namespace)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Quit;
    }
    Namespace->BoundaryLength = Boundary->TotalSize;
    RtlCopyMemory(&Namespace->Boundary, Boundary, Boundary->TotalSize);

    Status = ObCreateObject(PreviousMode,
                            ObpDirectoryObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(OBJECT_DIRECTORY),
                            0,
                            0,
                            (PVOID*)&Directory);
    if (!NT_SUCCESS(Status)) goto Quit;

    RtlZeroMemory(Directory, sizeof(OBJECT_DIRECTORY));
    ExInitializePushLock(&Directory->Lock);
    Directory->SessionId = -1;
    Namespace->Directory = Directory;

    ObpLockPrivateNamespaces();
    if (ObpFindPrivateNamespace(Boundary))
    {
        Status = STATUS_OBJECT_NAME_COLLISION;
    }
    else
    {
        InsertTailList(&ObpPrivateNamespaceList, &Namespace->Link);
        Directory->NamespaceEntry = Namespace;
        Namespace = NULL;
    }
    ObpUnlockPrivateNamespaces();

    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Directory);
        goto Quit;
    }

    Status = ObInsertObject(Directory, NULL, DesiredAccess, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status)) goto Quit;

    _SEH2_TRY
    {
        *NamespaceHandle = Handle;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

Quit:
    if (Namespace) ExFreePoolWithTag(Namespace, TAG_OB_NAMESPACE);
    if (Boundary) ExFreePoolWithTag(Boundary, TAG_OB_NAMESPACE);
    return Status;
}

NTSTATUS
NTAPI
NtOpenPrivateNamespace(
    _Out_ PHANDLE NamespaceHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ PVOID BoundaryDescriptor)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_BOUNDARY_DESCRIPTOR Boundary = NULL;
    POBP_PRIVATE_NAMESPACE Namespace;
    POBJECT_DIRECTORY Directory = NULL;
    ULONG Attributes = 0;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWriteHandle(NamespaceHandle);
            if (ObjectAttributes)
            {
                ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), sizeof(ULONG));
                Attributes = ObjectAttributes->Attributes;
            }
        }
        else if (ObjectAttributes)
        {
            Attributes = ObjectAttributes->Attributes;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = ObpCaptureBoundaryDescriptor(BoundaryDescriptor, PreviousMode, &Boundary);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObpCheckBoundaryAccess(Boundary);
    if (!NT_SUCCESS(Status)) goto Quit;

    ObpLockPrivateNamespaces();
    Namespace = ObpFindPrivateNamespace(Boundary);
    if (Namespace)
    {
        Directory = Namespace->Directory;
        ObReferenceObject(Directory);
    }
    ObpUnlockPrivateNamespaces();

    if (!Directory)
    {
        Status = STATUS_OBJECT_PATH_NOT_FOUND;
        goto Quit;
    }

    Status = ObOpenObjectByPointer(Directory,
                                   Attributes & (OBJ_INHERIT | OBJ_KERNEL_HANDLE),
                                   NULL,
                                   DesiredAccess,
                                   ObpDirectoryObjectType,
                                   PreviousMode,
                                   &Handle);
    ObDereferenceObject(Directory);
    if (!NT_SUCCESS(Status)) goto Quit;

    _SEH2_TRY
    {
        *NamespaceHandle = Handle;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

Quit:
    if (Boundary) ExFreePoolWithTag(Boundary, TAG_OB_NAMESPACE);
    return Status;
}

NTSTATUS
NTAPI
NtDeletePrivateNamespace(
    _In_ HANDLE NamespaceHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_DIRECTORY Directory;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(NamespaceHandle,
                                       0,
                                       ObpDirectoryObjectType,
                                       PreviousMode,
                                       (PVOID*)&Directory,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (!Directory->NamespaceEntry)
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    else
        ObpRemovePrivateNamespace(Directory);

    ObDereferenceObject(Directory);
    return Status;
}
