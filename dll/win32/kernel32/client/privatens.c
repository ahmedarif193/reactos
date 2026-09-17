/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Boundary descriptors and private namespaces
 */

#include <k32.h>

#define NDEBUG
#include <debug.h>

typedef struct _BASE_PRIVATE_NAMESPACE
{
    LIST_ENTRY Link;
    HANDLE Handle;
    UNICODE_STRING Alias;
} BASE_PRIVATE_NAMESPACE, *PBASE_PRIVATE_NAMESPACE;

static LIST_ENTRY BasePrivateNamespaceList = {&BasePrivateNamespaceList, &BasePrivateNamespaceList};

static
BOOL
BasepAddPrivateNamespace(
    _In_ HANDLE Handle,
    _In_ PCWSTR Alias)
{
    PBASE_PRIVATE_NAMESPACE Entry;
    SIZE_T Length = wcslen(Alias) * sizeof(WCHAR);

    if (!Length || Length > MAXUSHORT - sizeof(WCHAR))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Entry = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(*Entry) + Length + sizeof(WCHAR));
    if (!Entry)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    Entry->Handle = Handle;
    Entry->Alias.Buffer = (PWSTR)(Entry + 1);
    Entry->Alias.Length = (USHORT)Length;
    Entry->Alias.MaximumLength = (USHORT)(Length + sizeof(WCHAR));
    RtlCopyMemory(Entry->Alias.Buffer, Alias, Length + sizeof(WCHAR));

    RtlAcquirePebLock();
    InsertTailList(&BasePrivateNamespaceList, &Entry->Link);
    RtlReleasePebLock();
    return TRUE;
}

static
VOID
BasepRemovePrivateNamespace(
    _In_ HANDLE Handle)
{
    PLIST_ENTRY Link;
    PBASE_PRIVATE_NAMESPACE Entry = NULL;

    RtlAcquirePebLock();
    for (Link = BasePrivateNamespaceList.Flink; Link != &BasePrivateNamespaceList; Link = Link->Flink)
    {
        if (CONTAINING_RECORD(Link, BASE_PRIVATE_NAMESPACE, Link)->Handle == Handle)
        {
            Entry = CONTAINING_RECORD(Link, BASE_PRIVATE_NAMESPACE, Link);
            RemoveEntryList(&Entry->Link);
            break;
        }
    }
    RtlReleasePebLock();
    if (Entry) RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
}

VOID
WINAPI
BasepAdjustObjectAttributesForPrivateNamespace(
    _Inout_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    PUNICODE_STRING Name = ObjectAttributes->ObjectName;
    UNICODE_STRING Prefix;
    PLIST_ENTRY Link;
    PBASE_PRIVATE_NAMESPACE Entry;
    USHORT Index, Chars;

    if (!Name || !Name->Buffer || IsListEmpty(&BasePrivateNamespaceList))
        return;

    Chars = Name->Length / sizeof(WCHAR);
    for (Index = 0; Index < Chars; Index++)
    {
        if (Name->Buffer[Index] == L'\\')
            break;
    }
    if (Index == 0 || Index >= Chars)
        return;

    Prefix.Buffer = Name->Buffer;
    Prefix.Length = Prefix.MaximumLength = Index * sizeof(WCHAR);

    RtlAcquirePebLock();
    for (Link = BasePrivateNamespaceList.Flink; Link != &BasePrivateNamespaceList; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, BASE_PRIVATE_NAMESPACE, Link);
        if (RtlEqualUnicodeString(&Entry->Alias, &Prefix, TRUE))
        {
            ObjectAttributes->RootDirectory = Entry->Handle;
            Name->Buffer += Index + 1;
            Name->Length -= (Index + 1) * sizeof(WCHAR);
            Name->MaximumLength -= (Index + 1) * sizeof(WCHAR);
            break;
        }
    }
    RtlReleasePebLock();
}

HANDLE
WINAPI
CreateBoundaryDescriptorW(
    _In_ LPCWSTR Name,
    _In_ ULONG Flags)
{
    UNICODE_STRING NameString;
    PVOID Descriptor;

    if (!Name || (Flags & ~CREATE_BOUNDARY_DESCRIPTOR_ADD_APPCONTAINER_SID))
        return NULL;
    RtlInitUnicodeString(&NameString, Name);
    Descriptor = RtlCreateBoundaryDescriptor(&NameString, Flags);
    if (!Descriptor)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return Descriptor;
}

HANDLE
WINAPI
CreateBoundaryDescriptorA(
    _In_ LPCSTR Name,
    _In_ ULONG Flags)
{
    UNICODE_STRING NameString;
    HANDLE Descriptor;

    if (!Name || !Basep8BitStringToDynamicUnicodeString(&NameString, Name))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    Descriptor = CreateBoundaryDescriptorW(NameString.Buffer, Flags);
    RtlFreeUnicodeString(&NameString);
    return Descriptor;
}

BOOL
WINAPI
AddSIDToBoundaryDescriptor(
    _Inout_ HANDLE *BoundaryDescriptor,
    _In_ PSID RequiredSid)
{
    NTSTATUS Status = RtlAddSIDToBoundaryDescriptor(BoundaryDescriptor, RequiredSid);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
AddIntegrityLabelToBoundaryDescriptor(
    _Inout_ HANDLE *BoundaryDescriptor,
    _In_ PSID IntegrityLabel)
{
    NTSTATUS Status = RtlAddIntegrityLabelToBoundaryDescriptor(BoundaryDescriptor, IntegrityLabel);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

VOID
WINAPI
DeleteBoundaryDescriptor(
    _In_ HANDLE BoundaryDescriptor)
{
    RtlDeleteBoundaryDescriptor(BoundaryDescriptor);
}

HANDLE
WINAPI
CreatePrivateNamespaceW(
    _In_opt_ LPSECURITY_ATTRIBUTES lpPrivateNamespaceAttributes,
    _In_ LPVOID lpBoundaryDescriptor,
    _In_ LPCWSTR lpAliasPrefix)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Handle;
    NTSTATUS Status;

    if (!lpBoundaryDescriptor || !lpAliasPrefix)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               (lpPrivateNamespaceAttributes && lpPrivateNamespaceAttributes->bInheritHandle) ? OBJ_INHERIT : 0,
                               NULL,
                               lpPrivateNamespaceAttributes ? lpPrivateNamespaceAttributes->lpSecurityDescriptor : NULL);
    Status = NtCreatePrivateNamespace(&Handle, MAXIMUM_ALLOWED, &ObjectAttributes, lpBoundaryDescriptor);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return NULL;
    }
    if (!BasepAddPrivateNamespace(Handle, lpAliasPrefix))
    {
        NtDeletePrivateNamespace(Handle);
        NtClose(Handle);
        return NULL;
    }
    return Handle;
}

HANDLE
WINAPI
CreatePrivateNamespaceA(
    _In_opt_ LPSECURITY_ATTRIBUTES lpPrivateNamespaceAttributes,
    _In_ LPVOID lpBoundaryDescriptor,
    _In_ LPCSTR lpAliasPrefix)
{
    UNICODE_STRING Alias;
    HANDLE Handle;

    if (!lpAliasPrefix || !Basep8BitStringToDynamicUnicodeString(&Alias, lpAliasPrefix))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    Handle = CreatePrivateNamespaceW(lpPrivateNamespaceAttributes, lpBoundaryDescriptor, Alias.Buffer);
    RtlFreeUnicodeString(&Alias);
    return Handle;
}

HANDLE
WINAPI
OpenPrivateNamespaceW(
    _In_ LPVOID lpBoundaryDescriptor,
    _In_ LPCWSTR lpAliasPrefix)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Handle;
    NTSTATUS Status;

    if (!lpBoundaryDescriptor || !lpAliasPrefix)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenPrivateNamespace(&Handle, MAXIMUM_ALLOWED, &ObjectAttributes, lpBoundaryDescriptor);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return NULL;
    }
    if (!BasepAddPrivateNamespace(Handle, lpAliasPrefix))
    {
        NtClose(Handle);
        return NULL;
    }
    return Handle;
}

HANDLE
WINAPI
OpenPrivateNamespaceA(
    _In_ LPVOID lpBoundaryDescriptor,
    _In_ LPCSTR lpAliasPrefix)
{
    UNICODE_STRING Alias;
    HANDLE Handle;

    if (!lpAliasPrefix || !Basep8BitStringToDynamicUnicodeString(&Alias, lpAliasPrefix))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    Handle = OpenPrivateNamespaceW(lpBoundaryDescriptor, Alias.Buffer);
    RtlFreeUnicodeString(&Alias);
    return Handle;
}

BOOLEAN
WINAPI
ClosePrivateNamespace(
    _In_ HANDLE Handle,
    _In_ ULONG Flags)
{
    NTSTATUS Status;

    if (Flags & ~PRIVATE_NAMESPACE_FLAG_DESTROY)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Flags & PRIVATE_NAMESPACE_FLAG_DESTROY)
        NtDeletePrivateNamespace(Handle);
    BasepRemovePrivateNamespace(Handle);
    Status = NtClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}
