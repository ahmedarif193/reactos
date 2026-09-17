/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Object boundary descriptors for private namespaces
 */

#include <rtl.h>
#include <ndk/obtypes.h>

#define NDEBUG
#include <debug.h>

#define TAG_BOUNDARY 'dBtR'

static
NTSTATUS
RtlpAddBoundaryEntry(
    _Inout_ PVOID *BoundaryDescriptor,
    _In_ BOUNDARY_ENTRY_TYPE EntryType,
    _In_reads_bytes_(DataSize) PVOID Data,
    _In_ ULONG DataSize)
{
    POBJECT_BOUNDARY_DESCRIPTOR Old, New;
    POBJECT_BOUNDARY_ENTRY Entry;
    ULONG EntrySize, NewSize;

    if (!BoundaryDescriptor || !*BoundaryDescriptor)
        return STATUS_INVALID_PARAMETER;

    Old = *BoundaryDescriptor;
    EntrySize = sizeof(OBJECT_BOUNDARY_ENTRY) + ROUND_UP(DataSize, sizeof(ULONG));
    NewSize = Old->TotalSize + EntrySize;

    New = RtlpAllocateMemory(NewSize, TAG_BOUNDARY);
    if (!New)
        return STATUS_NO_MEMORY;

    RtlCopyMemory(New, Old, Old->TotalSize);
    Entry = (POBJECT_BOUNDARY_ENTRY)((PUCHAR)New + Old->TotalSize);
    RtlZeroMemory(Entry, EntrySize);
    Entry->EntryType = EntryType;
    Entry->EntrySize = EntrySize;
    RtlCopyMemory(Entry + 1, Data, DataSize);
    New->Items++;
    New->TotalSize = NewSize;

    RtlpFreeMemory(Old, TAG_BOUNDARY);
    *BoundaryDescriptor = New;
    return STATUS_SUCCESS;
}

PVOID
NTAPI
RtlCreateBoundaryDescriptor(
    _In_ PUNICODE_STRING Name,
    _In_ ULONG Flags)
{
    POBJECT_BOUNDARY_DESCRIPTOR Descriptor;
    POBJECT_BOUNDARY_ENTRY Entry;
    ULONG EntrySize;

    if (!Name || !Name->Buffer || !Name->Length || (Flags & ~BOUNDARY_DESCRIPTOR_ADD_APPCONTAINER_SID))
        return NULL;

    EntrySize = sizeof(OBJECT_BOUNDARY_ENTRY) + ROUND_UP(Name->Length, sizeof(ULONG));
    Descriptor = RtlpAllocateMemory(sizeof(OBJECT_BOUNDARY_DESCRIPTOR) + EntrySize, TAG_BOUNDARY);
    if (!Descriptor)
        return NULL;

    RtlZeroMemory(Descriptor, sizeof(OBJECT_BOUNDARY_DESCRIPTOR) + EntrySize);
    Descriptor->Version = OBJECT_BOUNDARY_DESCRIPTOR_VERSION;
    Descriptor->Items = 1;
    Descriptor->TotalSize = sizeof(OBJECT_BOUNDARY_DESCRIPTOR) + EntrySize;
    Descriptor->Flags = Flags;
    Entry = (POBJECT_BOUNDARY_ENTRY)(Descriptor + 1);
    Entry->EntryType = OBNS_Name;
    Entry->EntrySize = EntrySize;
    RtlCopyMemory(Entry + 1, Name->Buffer, Name->Length);
    return Descriptor;
}

NTSTATUS
NTAPI
RtlAddSIDToBoundaryDescriptor(
    _Inout_ PVOID *BoundaryDescriptor,
    _In_ PSID RequiredSid)
{
    if (!RequiredSid || !RtlValidSid(RequiredSid))
        return STATUS_INVALID_SID;
    return RtlpAddBoundaryEntry(BoundaryDescriptor, OBNS_SID, RequiredSid, RtlLengthSid(RequiredSid));
}

NTSTATUS
NTAPI
RtlAddIntegrityLabelToBoundaryDescriptor(
    _Inout_ PVOID *BoundaryDescriptor,
    _In_ PSID IntegrityLabel)
{
    static const SID_IDENTIFIER_AUTHORITY LabelAuthority = {SECURITY_MANDATORY_LABEL_AUTHORITY};
    POBJECT_BOUNDARY_DESCRIPTOR Descriptor;
    POBJECT_BOUNDARY_ENTRY Entry;
    ULONG Offset, Index;

    if (!IntegrityLabel || !RtlValidSid(IntegrityLabel) ||
        !RtlEqualMemory(RtlIdentifierAuthoritySid(IntegrityLabel), &LabelAuthority, sizeof(LabelAuthority)) ||
        *RtlSubAuthorityCountSid(IntegrityLabel) != 1)
    {
        return STATUS_INVALID_SID;
    }
    if (!BoundaryDescriptor || !*BoundaryDescriptor)
        return STATUS_INVALID_PARAMETER;

    Descriptor = *BoundaryDescriptor;
    Offset = sizeof(OBJECT_BOUNDARY_DESCRIPTOR);
    for (Index = 0; Index < Descriptor->Items; Index++)
    {
        Entry = (POBJECT_BOUNDARY_ENTRY)((PUCHAR)Descriptor + Offset);
        if (Entry->EntryType == OBNS_IL)
            return STATUS_INVALID_PARAMETER;
        Offset += Entry->EntrySize;
    }
    return RtlpAddBoundaryEntry(BoundaryDescriptor, OBNS_IL, IntegrityLabel, RtlLengthSid(IntegrityLabel));
}

VOID
NTAPI
RtlDeleteBoundaryDescriptor(
    _In_ PVOID BoundaryDescriptor)
{
    if (BoundaryDescriptor)
        RtlpFreeMemory(BoundaryDescriptor, TAG_BOUNDARY);
}
