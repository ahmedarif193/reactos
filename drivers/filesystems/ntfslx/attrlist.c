/*
 * PROJECT:     ReactOS ntfslx driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     NTFS attribute-list extent parsing and resolution
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

#include <pshpack1.h>
typedef struct _NTFSLX_ATTRIBUTE_LIST_ENTRY
{
    ULONG Type;
    USHORT Length;
    UCHAR NameLength;
    UCHAR NameOffset;
    ULONGLONG LowestVcn;
    ULONGLONG MftReference;
    USHORT Instance;
    WCHAR Name[1];
} NTFSLX_ATTRIBUTE_LIST_ENTRY, *PNTFSLX_ATTRIBUTE_LIST_ENTRY;
#include <poppack.h>

typedef NTSTATUS
(*PNTFSLX_READ_MFT_RECORD_ROUTINE)(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG RecordNumber,
    _Out_ PNTFSLX_MFT_RECORD Record);

typedef NTSTATUS (NTAPI *PNTFSLX_ATTRIBUTE_LIST_ENUM_CALLBACK)(
    _In_ PNTFSLX_MFT_RECORD ExtentRecord,
    _In_ PNTFSLX_ATTR_RECORD ExtentAttribute,
    _In_opt_ PVOID Context);

typedef struct _NTFSLX_ATTRIBUTE_LIST_ENUM_CONTEXT
{
    PDEVICE_OBJECT StorageDevice;
    PNTFSLX_VOLUME_INFO VolumeInfo;
    PNTFSLX_RUNLIST_ELEMENT MftRunlist;
    PNTFSLX_MFT_RECORD BaseRecord;
    ULONG AttributeType;
    PCWSTR Name;
    ULONG NameLength;
    ULONGLONG LowestVcn;
    PNTFSLX_ATTRIBUTE_LIST_ENUM_CALLBACK Callback;
    PVOID CallbackContext;
    PULONGLONG SeenRecords;
    ULONG SeenCount;
    ULONG SeenCapacity;
} NTFSLX_ATTRIBUTE_LIST_ENUM_CONTEXT, *PNTFSLX_ATTRIBUTE_LIST_ENUM_CONTEXT;

typedef struct _NTFSLX_ATTRIBUTE_LIST_RESOLVE_CONTEXT
{
    PNTFSLX_MFT_RECORD ExtentRecord;
    PNTFSLX_ATTR_RECORD ExtentAttribute;
    ULONG RecordLength;
    BOOLEAN Found;
} NTFSLX_ATTRIBUTE_LIST_RESOLVE_CONTEXT, *PNTFSLX_ATTRIBUTE_LIST_RESOLVE_CONTEXT;

static
BOOLEAN
NtfslxIsAlignedEntryLength(
    _In_ USHORT Length)
{
    return (Length != 0) && ((Length & 7) == 0);
}

static
BOOLEAN
NtfslxAttributeListEntryNameMatches(
    _In_ PNTFSLX_ATTRIBUTE_LIST_ENTRY Entry,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength)
{
    ULONG NameBytes;

    if (Entry->NameLength != NameLength)
    {
        return FALSE;
    }

    NameBytes = NameLength * sizeof(WCHAR);
    if (NameBytes == 0)
    {
        return TRUE;
    }

    if (Name == NULL)
    {
        return FALSE;
    }

    return RtlCompareMemory(Entry->Name,
                            Name,
                            NameBytes) == NameBytes;
}

static
BOOLEAN
NtfslxAttributeListEntryValid(
    _In_reads_bytes_(RemainingLength) PNTFSLX_ATTRIBUTE_LIST_ENTRY Entry,
    _In_ ULONG RemainingLength)
{
    ULONG HeaderLength;
    ULONG NameBytes;

    HeaderLength = FIELD_OFFSET(NTFSLX_ATTRIBUTE_LIST_ENTRY, Name);
    if (RemainingLength < HeaderLength)
    {
        return FALSE;
    }

    if (!NtfslxIsAlignedEntryLength(Entry->Length) ||
        Entry->Length < HeaderLength ||
        Entry->Length > RemainingLength)
    {
        return FALSE;
    }

    if (Entry->NameOffset < HeaderLength ||
        (Entry->NameOffset & 1) != 0 ||
        Entry->NameOffset > Entry->Length)
    {
        return FALSE;
    }

    NameBytes = (ULONG)Entry->NameLength * sizeof(WCHAR);
    if ((NameBytes / sizeof(WCHAR)) != Entry->NameLength ||
        NameBytes > Entry->Length - Entry->NameOffset)
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
NtfslxSameMftReference(
    _In_ ULONGLONG Lhs,
    _In_ ULONGLONG Rhs)
{
    return MREF(Lhs) == MREF(Rhs);
}

static
BOOLEAN
NtfslxAttributeRecordNameMatches(
    _In_ PNTFSLX_ATTR_RECORD Attribute,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength)
{
    ULONG NameBytes;

    if (Attribute->NameLength != NameLength)
    {
        return FALSE;
    }

    NameBytes = NameLength * sizeof(WCHAR);
    if (NameBytes == 0)
    {
        return TRUE;
    }

    if (Name == NULL)
    {
        return FALSE;
    }

    if (Attribute->NameOffset < FIELD_OFFSET(NTFSLX_ATTR_RECORD, Data) ||
        NameBytes > Attribute->Length - Attribute->NameOffset)
    {
        return FALSE;
    }

    return RtlCompareMemory((PUCHAR)Attribute + Attribute->NameOffset,
                            Name,
                            NameBytes) == NameBytes;
}

static
BOOLEAN
NtfslxAttributeRecordValid(
    _In_ PNTFSLX_MFT_RECORD Record,
    _In_ PNTFSLX_ATTR_RECORD Attribute)
{
    ULONG AttributeOffset;
    ULONG HeaderLength;
    ULONG NameBytes;

    if (Record == NULL || Attribute == NULL)
    {
        return FALSE;
    }

    if (Record->AttributesOffset >= Record->BytesInUse)
    {
        return FALSE;
    }

    AttributeOffset = (ULONG)((PUCHAR)Attribute - (PUCHAR)Record);
    if (AttributeOffset < Record->AttributesOffset ||
        AttributeOffset >= Record->BytesInUse)
    {
        return FALSE;
    }

    HeaderLength = FIELD_OFFSET(NTFSLX_ATTR_RECORD, Data);
    if (Attribute->Length < HeaderLength ||
        Attribute->Length > Record->BytesInUse - AttributeOffset ||
        (Attribute->Length & 7) != 0)
    {
        return FALSE;
    }

    if (Attribute->NameOffset < HeaderLength ||
        (Attribute->NameOffset & 1) != 0 ||
        Attribute->NameOffset > Attribute->Length)
    {
        return FALSE;
    }

    NameBytes = (ULONG)Attribute->NameLength * sizeof(WCHAR);
    if ((NameBytes / sizeof(WCHAR)) != Attribute->NameLength ||
        NameBytes > Attribute->Length - Attribute->NameOffset)
    {
        return FALSE;
    }

    if (Attribute->NonResident != 0)
    {
        if (Attribute->Data.NonResident.MappingPairsOffset < HeaderLength ||
            Attribute->Data.NonResident.MappingPairsOffset >= Attribute->Length ||
            Attribute->Data.NonResident.HighestVcn < Attribute->Data.NonResident.LowestVcn)
        {
            return FALSE;
        }
    }
    else
    {
        if (Attribute->Data.Resident.ValueOffset < HeaderLength ||
            Attribute->Data.Resident.ValueOffset > Attribute->Length ||
            Attribute->Data.Resident.ValueLength > Attribute->Length - Attribute->Data.Resident.ValueOffset)
        {
            return FALSE;
        }
    }

    return TRUE;
}

static
NTSTATUS
NtfslxValidateAttributeList(
    _In_reads_bytes_(AttributeListLength) const void *AttributeList,
    _In_ ULONG AttributeListLength)
{
    const UCHAR *Cursor;
    const UCHAR *End;
    PNTFSLX_ATTRIBUTE_LIST_ENTRY Entry;
    ULONG RemainingLength;

    if (AttributeList == NULL || AttributeListLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Cursor = AttributeList;
    End = Cursor + AttributeListLength;
    while (Cursor < End)
    {
        RemainingLength = (ULONG)(End - Cursor);
        Entry = (PNTFSLX_ATTRIBUTE_LIST_ENTRY)Cursor;
        if (!NtfslxAttributeListEntryValid(Entry, RemainingLength))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Cursor += Entry->Length;
    }

    return (Cursor == End) ? STATUS_SUCCESS : STATUS_FILE_CORRUPT_ERROR;
}

static
NTSTATUS
NtfslxFindAttributeInRecordByExtent(
    _In_ PNTFSLX_MFT_RECORD Record,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG LowestVcn,
    _Outptr_ PNTFSLX_ATTR_RECORD *AttributeOut)
{
    PUCHAR RecordBase;
    PNTFSLX_ATTR_RECORD Attribute;
    ULONG Offset;
    ULONG HeaderLength;

    if (AttributeOut == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *AttributeOut = NULL;
    if (Record == NULL || Record->AttributesOffset >= Record->BytesInUse)
    {
        return STATUS_INVALID_PARAMETER;
    }

    HeaderLength = FIELD_OFFSET(NTFSLX_ATTR_RECORD, Data);
    RecordBase = (PUCHAR)Record;
    Offset = Record->AttributesOffset;
    while (Offset + HeaderLength <= Record->BytesInUse)
    {
        Attribute = (PNTFSLX_ATTR_RECORD)(RecordBase + Offset);
        if (Attribute->Type == NTFSLX_ATTRIBUTE_END)
        {
            break;
        }

        if (!NtfslxAttributeRecordValid(Record, Attribute))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Attribute->Type == AttributeType &&
            NtfslxAttributeRecordNameMatches(Attribute, Name, NameLength))
        {
            if (Attribute->NonResident != 0)
            {
                if (Attribute->Data.NonResident.LowestVcn == LowestVcn)
                {
                    *AttributeOut = Attribute;
                    return STATUS_SUCCESS;
                }
            }
            else if (LowestVcn == 0)
            {
                *AttributeOut = Attribute;
                return STATUS_SUCCESS;
            }
        }

        Offset += Attribute->Length;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

static
NTSTATUS
NtfslxReadExtentRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG RecordNumber,
    _Out_ PNTFSLX_MFT_RECORD Record)
{
    if (Record == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Record, VolumeInfo->BytesPerFileRecord);
    return NtfslxReadMftRecord(StorageDevice,
                               VolumeInfo,
                               MftRunlist,
                               RecordNumber,
                               Record);
}

static
BOOLEAN
NtfslxAttributeListRecordSeen(
    _In_reads_(SeenCount) const ULONGLONG *SeenRecords,
    _In_ ULONG SeenCount,
    _In_ ULONGLONG RecordNumber)
{
    ULONG Index;

    for (Index = 0; Index < SeenCount; ++Index)
    {
        if (NtfslxSameMftReference(SeenRecords[Index], RecordNumber))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static
NTSTATUS
NtfslxAttributeListRecordRemember(
    _Inout_ PNTFSLX_ATTRIBUTE_LIST_ENUM_CONTEXT Context,
    _In_ ULONGLONG RecordNumber)
{
    PULONGLONG NewSeenRecords;
    ULONG NewCapacity;

    if (NtfslxAttributeListRecordSeen(Context->SeenRecords,
                                      Context->SeenCount,
                                      RecordNumber))
    {
        return STATUS_SUCCESS;
    }

    if (Context->SeenCount == Context->SeenCapacity)
    {
        NewCapacity = (Context->SeenCapacity != 0) ? (Context->SeenCapacity * 2) : 8;
        if (NewCapacity < Context->SeenCapacity)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        NewSeenRecords = ExAllocatePoolWithTag(NonPagedPool,
                                               NewCapacity * sizeof(ULONGLONG),
                                               NTFSLX_TAG);
        if (NewSeenRecords == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(NewSeenRecords, NewCapacity * sizeof(ULONGLONG));
        if (Context->SeenRecords != NULL && Context->SeenCount != 0)
        {
            RtlCopyMemory(NewSeenRecords,
                          Context->SeenRecords,
                          Context->SeenCount * sizeof(ULONGLONG));
            ExFreePoolWithTag(Context->SeenRecords, NTFSLX_TAG);
        }

        Context->SeenRecords = NewSeenRecords;
        Context->SeenCapacity = NewCapacity;
    }

    Context->SeenRecords[Context->SeenCount++] = RecordNumber;
    return STATUS_SUCCESS;
}

static
VOID
NtfslxAttributeListReleaseSeenRecords(
    _Inout_ PNTFSLX_ATTRIBUTE_LIST_ENUM_CONTEXT Context)
{
    if (Context->SeenRecords != NULL)
    {
        ExFreePoolWithTag(Context->SeenRecords, NTFSLX_TAG);
        Context->SeenRecords = NULL;
    }

    Context->SeenCount = 0;
    Context->SeenCapacity = 0;
}

static
NTSTATUS
NtfslxEnumerateAttributeListEntries(
    _Inout_ PNTFSLX_ATTRIBUTE_LIST_ENUM_CONTEXT Context,
    _In_reads_bytes_(AttributeListLength) const void *AttributeList,
    _In_ ULONG AttributeListLength);

static
NTSTATUS
NtfslxEnumerateAttributeListEntryCallback(
    _In_ PNTFSLX_MFT_RECORD ExtentRecord,
    _In_ PNTFSLX_ATTR_RECORD ExtentAttribute,
    _In_opt_ PVOID Context)
{
    PNTFSLX_ATTRIBUTE_LIST_ENUM_CALLBACK Callback;
    PNTFSLX_ATTRIBUTE_LIST_ENUM_CONTEXT EnumContext;

    EnumContext = Context;
    if (EnumContext == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Callback = EnumContext->Callback;
    return Callback(ExtentRecord, ExtentAttribute, EnumContext->CallbackContext);
}

static
NTSTATUS
NtfslxEnumerateAttributeListEntries(
    _Inout_ PNTFSLX_ATTRIBUTE_LIST_ENUM_CONTEXT Context,
    _In_reads_bytes_(AttributeListLength) const void *AttributeList,
    _In_ ULONG AttributeListLength)
{
    const UCHAR *Cursor;
    const UCHAR *End;
    PNTFSLX_ATTRIBUTE_LIST_ENTRY Entry;
    PNTFSLX_MFT_RECORD ExtentRecord;
    PNTFSLX_ATTR_RECORD ExtentAttribute;
    ULONGLONG RecordNumber;
    ULONG RemainingLength;
    NTSTATUS Status;

    Status = NtfslxValidateAttributeList(AttributeList, AttributeListLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Cursor = AttributeList;
    End = Cursor + AttributeListLength;
    while (Cursor < End)
    {
        RemainingLength = (ULONG)(End - Cursor);
        Entry = (PNTFSLX_ATTRIBUTE_LIST_ENTRY)Cursor;
        if (!NtfslxAttributeListEntryValid(Entry, RemainingLength))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Entry->Type == Context->AttributeType &&
            Entry->LowestVcn == Context->LowestVcn &&
            NtfslxAttributeListEntryNameMatches(Entry, Context->Name, Context->NameLength))
        {
            RecordNumber = MREF(Entry->MftReference);
            Status = NtfslxAttributeListRecordRemember(Context, RecordNumber);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }

            if (Context->BaseRecord != NULL &&
                NtfslxSameMftReference(Context->BaseRecord->MftRecordNumber, RecordNumber))
            {
                ExtentRecord = Context->BaseRecord;
            }
            else
            {
                ExtentRecord = ExAllocatePoolWithTag(NonPagedPool,
                                                     Context->VolumeInfo->BytesPerFileRecord,
                                                     NTFSLX_TAG);
                if (ExtentRecord == NULL)
                {
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                Status = NtfslxReadExtentRecord(Context->StorageDevice,
                                                Context->VolumeInfo,
                                                Context->MftRunlist,
                                                RecordNumber,
                                                ExtentRecord);
                if (!NT_SUCCESS(Status))
                {
                    ExFreePoolWithTag(ExtentRecord, NTFSLX_TAG);
                    return Status;
                }
            }

            Status = NtfslxFindAttributeInRecordByExtent(ExtentRecord,
                                                         Context->AttributeType,
                                                         Context->Name,
                                                         Context->NameLength,
                                                         Entry->LowestVcn,
                                                         &ExtentAttribute);
            if (!NT_SUCCESS(Status))
            {
                if (ExtentRecord != Context->BaseRecord)
                {
                    ExFreePoolWithTag(ExtentRecord, NTFSLX_TAG);
                }
                return Status;
            }

            Status = NtfslxEnumerateAttributeListEntryCallback(ExtentRecord,
                                                               ExtentAttribute,
                                                               Context);
            if (ExtentRecord != Context->BaseRecord)
            {
                ExFreePoolWithTag(ExtentRecord, NTFSLX_TAG);
            }

            if (Status == STATUS_NO_MORE_FILES)
            {
                return STATUS_SUCCESS;
            }

            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
        }

        Cursor += Entry->Length;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxEnumerateAttributeListExtents(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_opt_ PNTFSLX_MFT_RECORD BaseRecord,
    _In_reads_bytes_(AttributeListLength) const void *AttributeList,
    _In_ ULONG AttributeListLength,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG LowestVcn,
    _In_ PNTFSLX_ATTRIBUTE_LIST_ENUM_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext)
{
    NTFSLX_ATTRIBUTE_LIST_ENUM_CONTEXT Context;
    NTSTATUS Status;

    if (StorageDevice == NULL ||
        VolumeInfo == NULL ||
        Callback == NULL ||
        AttributeList == NULL ||
        AttributeListLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ASSERT(VolumeInfo->BytesPerFileRecord >= sizeof(NTFSLX_MFT_RECORD));

    RtlZeroMemory(&Context, sizeof(Context));
    Context.StorageDevice = StorageDevice;
    Context.VolumeInfo = VolumeInfo;
    Context.MftRunlist = MftRunlist;
    Context.BaseRecord = BaseRecord;
    Context.AttributeType = AttributeType;
    Context.Name = Name;
    Context.NameLength = NameLength;
    Context.LowestVcn = LowestVcn;
    Context.Callback = Callback;
    Context.CallbackContext = CallbackContext;

    if (BaseRecord != NULL)
    {
        Status = NtfslxAttributeListRecordRemember(&Context, BaseRecord->MftRecordNumber);
        if (!NT_SUCCESS(Status))
        {
            NtfslxAttributeListReleaseSeenRecords(&Context);
            return Status;
        }
    }

    Status = NtfslxEnumerateAttributeListEntries(&Context,
                                                 AttributeList,
                                                 AttributeListLength);
    NtfslxAttributeListReleaseSeenRecords(&Context);
    return Status;
}

static
NTSTATUS
NTAPI
NtfslxResolveAttributeListExtentCallback(
    _In_ PNTFSLX_MFT_RECORD ExtentRecord,
    _In_ PNTFSLX_ATTR_RECORD ExtentAttribute,
    _In_opt_ PVOID Context)
{
    PNTFSLX_ATTRIBUTE_LIST_RESOLVE_CONTEXT ResolveContext;

    ResolveContext = Context;
    if (ResolveContext == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ResolveContext->ExtentAttribute = ExtentAttribute;
    RtlCopyMemory(ResolveContext->ExtentRecord,
                  ExtentRecord,
                  ResolveContext->RecordLength);
    ResolveContext->Found = TRUE;
    return STATUS_NO_MORE_FILES;
}

NTSTATUS
NtfslxResolveAttributeListExtent(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ PNTFSLX_MFT_RECORD BaseRecord,
    _In_reads_bytes_(AttributeListLength) const void *AttributeList,
    _In_ ULONG AttributeListLength,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG LowestVcn,
    _Out_ PNTFSLX_MFT_RECORD ExtentRecord,
    _Outptr_ PNTFSLX_ATTR_RECORD *ExtentAttribute)
{
    NTFSLX_ATTRIBUTE_LIST_RESOLVE_CONTEXT ResolveContext;
    NTSTATUS Status;

    if (ExtentRecord == NULL || ExtentAttribute == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (StorageDevice == NULL ||
        VolumeInfo == NULL ||
        AttributeList == NULL ||
        AttributeListLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(ExtentRecord, VolumeInfo->BytesPerFileRecord);
    *ExtentAttribute = NULL;

    Status = NtfslxValidateAttributeList(AttributeList, AttributeListLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(&ResolveContext, sizeof(ResolveContext));
    ResolveContext.ExtentRecord = ExtentRecord;
    ResolveContext.RecordLength = VolumeInfo->BytesPerFileRecord;

    Status = NtfslxEnumerateAttributeListExtents(StorageDevice,
                                                 VolumeInfo,
                                                 MftRunlist,
                                                 BaseRecord,
                                                 AttributeList,
                                                 AttributeListLength,
                                                 AttributeType,
                                                 Name,
                                                 NameLength,
                                                 LowestVcn,
                                                 NtfslxResolveAttributeListExtentCallback,
                                                 &ResolveContext);
    if (Status == STATUS_NO_MORE_FILES)
    {
        Status = STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!ResolveContext.Found)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    *ExtentAttribute = ResolveContext.ExtentAttribute;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxFindAttributeListEntryByTypeName(
    _In_reads_bytes_(AttributeListLength) const void *AttributeList,
    _In_ ULONG AttributeListLength,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength,
    _Outptr_ PNTFSLX_ATTRIBUTE_LIST_ENTRY *EntryOut)
{
    const UCHAR *Cursor;
    const UCHAR *End;
    PNTFSLX_ATTRIBUTE_LIST_ENTRY Entry;
    ULONG RemainingLength;

    if (EntryOut == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *EntryOut = NULL;
    if (AttributeList == NULL || AttributeListLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Cursor = AttributeList;
    End = Cursor + AttributeListLength;
    while (Cursor < End)
    {
        RemainingLength = (ULONG)(End - Cursor);
        Entry = (PNTFSLX_ATTRIBUTE_LIST_ENTRY)Cursor;
        if (!NtfslxAttributeListEntryValid(Entry, RemainingLength))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Entry->Type == AttributeType &&
            NtfslxAttributeListEntryNameMatches(Entry, Name, NameLength))
        {
            *EntryOut = Entry;
            return STATUS_SUCCESS;
        }

        Cursor += Entry->Length;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/* ============================================================================
 *
 *  $ATTRIBUTE_LIST WRITER (T2.2)
 *
 *  When a file's attributes outgrow the BytesAllocated of its base MFT
 *  record, NTFS spills the overflow into one or more "extension" MFT
 *  records and adds an $ATTRIBUTE_LIST attribute to the base. The list is
 *  the only way to find attributes that no longer live in the base, so
 *  any reader must consult it whenever the base record carries an
 *  $ATTRIBUTE_LIST. Extension records are flagged by setting their
 *  BaseMftRecord field to the base reference (sequence << 48 | number);
 *  their own MftRecordNumber is the extension slot.
 *
 *  Functions in this section:
 *    NtfslxAttributeListBuildEntry          - serialize one entry
 *    NtfslxAttributeListInsertEntry         - splice entry into a buffer
 *    NtfslxAttributeListRemoveEntry         - excise entry from a buffer
 *    NtfslxAttributeListLookupEntry         - find entry, return MFT ref
 *    NtfslxAllocateExtensionMftRecord       - allocate + flag as extension
 *    NtfslxAttributeListInstallAttribute    - high-level: insert attribute,
 *                                             promote to $ATTRIBUTE_LIST and
 *                                             extension records as needed
 *
 *  Scope of this V1 (per T2.2 task): correctness for create/extend (insert
 *  new attribute that may not fit the base record). Coalescing back into
 *  the base when attributes shrink, and non-resident $ATTRIBUTE_LIST, are
 *  deferred. The writer always allocates one extension record at a time;
 *  pathologically fragmented files that need 3+ extensions are handled via
 *  repeated allocation walks (the allocator will pick a fresh slot each
 *  time we run out of room in the previous extension).
 *
 * ============================================================================
 */

/*
 * Compute the exact 8-byte aligned size of an attribute-list entry that
 * represents an attribute of the given type with the given name.
 */
static
ULONG
NtfslxAttributeListEntrySize(
    _In_ ULONG NameLength)
{
    ULONG HeaderLength;
    ULONG NameBytes;

    HeaderLength = FIELD_OFFSET(NTFSLX_ATTRIBUTE_LIST_ENTRY, Name);
    NameBytes = NameLength * sizeof(WCHAR);
    return ROUND_UP(HeaderLength + NameBytes, 8);
}

/*
 * Serialize a single attribute-list entry into Buffer. Buffer must have at
 * least NtfslxAttributeListEntrySize(NameLength) bytes available.
 */
static
VOID
NtfslxAttributeListBuildEntry(
    _Out_writes_bytes_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG LowestVcn,
    _In_ ULONGLONG MftReference,
    _In_ USHORT Instance)
{
    PNTFSLX_ATTRIBUTE_LIST_ENTRY Entry;
    ULONG HeaderLength;
    ULONG NameBytes;
    ULONG EntryLength;

    HeaderLength = FIELD_OFFSET(NTFSLX_ATTRIBUTE_LIST_ENTRY, Name);
    NameBytes = NameLength * sizeof(WCHAR);
    EntryLength = ROUND_UP(HeaderLength + NameBytes, 8);

    ASSERT(BufferLength >= EntryLength);
    RtlZeroMemory(Buffer, EntryLength);

    Entry = (PNTFSLX_ATTRIBUTE_LIST_ENTRY)Buffer;
    Entry->Type = AttributeType;
    Entry->Length = (USHORT)EntryLength;
    Entry->NameLength = (UCHAR)NameLength;
    Entry->NameOffset = (UCHAR)HeaderLength;
    Entry->LowestVcn = LowestVcn;
    Entry->MftReference = MftReference;
    Entry->Instance = Instance;

    if (NameLength != 0 && Name != NULL)
    {
        RtlCopyMemory(Entry->Name, Name, NameBytes);
    }
}

/*
 * Compare two (Type, Name, LowestVcn) keys to determine sort order in an
 * attribute list. Returns negative/zero/positive in the usual sense.
 *
 * NOTE: NTFS sorts list entries by (Type, Name, LowestVcn) and the name
 * compare is case-INsensitive against the volume upcase table for $DATA
 * stream names. We use a binary compare here because (a) the existing
 * resolver (NtfslxFindAttributeListEntryByTypeName) does the same, and
 * (b) the writer creates entries that match the in-record attribute's
 * exact name bytes, so binary equality is correct for our writes. If a
 * future reader needs case-insensitive lookup against externally created
 * NTFS volumes, that's a search-path fix, not a writer fix.
 */
static
LONG
NtfslxAttributeListCompareKeys(
    _In_ ULONG TypeA,
    _In_reads_opt_(NameLengthA) PCWSTR NameA,
    _In_ ULONG NameLengthA,
    _In_ ULONGLONG LowestVcnA,
    _In_ ULONG TypeB,
    _In_reads_opt_(NameLengthB) PCWSTR NameB,
    _In_ ULONG NameLengthB,
    _In_ ULONGLONG LowestVcnB)
{
    ULONG MinLen;
    ULONG Index;
    WCHAR Cha, Chb;

    if (TypeA < TypeB)
    {
        return -1;
    }
    if (TypeA > TypeB)
    {
        return 1;
    }

    MinLen = (NameLengthA < NameLengthB) ? NameLengthA : NameLengthB;
    for (Index = 0; Index < MinLen; ++Index)
    {
        Cha = NameA[Index];
        Chb = NameB[Index];
        if (Cha < Chb)
        {
            return -1;
        }
        if (Cha > Chb)
        {
            return 1;
        }
    }

    if (NameLengthA < NameLengthB)
    {
        return -1;
    }
    if (NameLengthA > NameLengthB)
    {
        return 1;
    }

    if (LowestVcnA < LowestVcnB)
    {
        return -1;
    }
    if (LowestVcnA > LowestVcnB)
    {
        return 1;
    }

    return 0;
}

/*
 * Insert a new attribute-list entry into AttributeListBuffer at its sorted
 * position. *NewBufferOut receives a fresh allocation containing the merged
 * list; caller must ExFreePoolWithTag(*NewBufferOut, NTFSLX_TAG) on success.
 *
 * If the list already contains an entry for the same (Type, Name, LowestVcn)
 * key, returns STATUS_OBJECT_NAME_COLLISION and leaves *NewBufferOut NULL.
 */
NTSTATUS
NtfslxAttributeListInsertEntry(
    _In_reads_bytes_opt_(AttributeListLength) const void *AttributeList,
    _In_ ULONG AttributeListLength,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG LowestVcn,
    _In_ ULONGLONG MftReference,
    _In_ USHORT Instance,
    _Outptr_result_bytebuffer_(*NewBufferLengthOut) PUCHAR *NewBufferOut,
    _Out_ PULONG NewBufferLengthOut)
{
    const UCHAR *Cursor;
    const UCHAR *End;
    PNTFSLX_ATTRIBUTE_LIST_ENTRY Entry;
    ULONG RemainingLength;
    ULONG NewEntryLength;
    ULONG NewBufferLength;
    PUCHAR NewBuffer;
    ULONG InsertOffset;
    LONG Cmp;
    BOOLEAN InsertOffsetFound;

    *NewBufferOut = NULL;
    *NewBufferLengthOut = 0;

    if (NewBufferOut == NULL || NewBufferLengthOut == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (AttributeList == NULL && AttributeListLength != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (AttributeListLength != 0)
    {
        NTSTATUS ValidateStatus = NtfslxValidateAttributeList(AttributeList, AttributeListLength);
        if (!NT_SUCCESS(ValidateStatus))
        {
            return ValidateStatus;
        }
    }

    NewEntryLength = NtfslxAttributeListEntrySize(NameLength);
    NewBufferLength = AttributeListLength + NewEntryLength;
    NewBuffer = ExAllocatePoolWithTag(NonPagedPool, NewBufferLength, NTFSLX_TAG);
    if (NewBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Walk existing entries to find the sorted insertion offset. */
    InsertOffset = AttributeListLength;
    InsertOffsetFound = FALSE;
    Cursor = AttributeList;
    End = (const UCHAR *)AttributeList + AttributeListLength;
    while (Cursor < End)
    {
        RemainingLength = (ULONG)(End - Cursor);
        Entry = (PNTFSLX_ATTRIBUTE_LIST_ENTRY)Cursor;
        if (!NtfslxAttributeListEntryValid(Entry, RemainingLength))
        {
            ExFreePoolWithTag(NewBuffer, NTFSLX_TAG);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Cmp = NtfslxAttributeListCompareKeys(
                  AttributeType, Name, NameLength, LowestVcn,
                  Entry->Type,
                  (PCWSTR)Entry->Name,
                  Entry->NameLength,
                  Entry->LowestVcn);
        if (Cmp == 0)
        {
            ExFreePoolWithTag(NewBuffer, NTFSLX_TAG);
            return STATUS_OBJECT_NAME_COLLISION;
        }
        if (Cmp < 0 && !InsertOffsetFound)
        {
            InsertOffset = (ULONG)((const UCHAR *)Entry - (const UCHAR *)AttributeList);
            InsertOffsetFound = TRUE;
        }

        Cursor += Entry->Length;
    }

    /* Copy [0, InsertOffset) -> NewBuffer at offset 0
     * Then build new entry at offset InsertOffset
     * Then copy [InsertOffset, end) -> NewBuffer at offset InsertOffset+NewEntryLength
     */
    if (InsertOffset > 0)
    {
        RtlCopyMemory(NewBuffer, AttributeList, InsertOffset);
    }
    NtfslxAttributeListBuildEntry(NewBuffer + InsertOffset,
                                  NewEntryLength,
                                  AttributeType,
                                  Name,
                                  NameLength,
                                  LowestVcn,
                                  MftReference,
                                  Instance);
    if (InsertOffset < AttributeListLength)
    {
        RtlCopyMemory(NewBuffer + InsertOffset + NewEntryLength,
                      (const UCHAR *)AttributeList + InsertOffset,
                      AttributeListLength - InsertOffset);
    }

    *NewBufferOut = NewBuffer;
    *NewBufferLengthOut = NewBufferLength;
    return STATUS_SUCCESS;
}

/*
 * Remove the attribute-list entry matching (Type, Name, LowestVcn) from the
 * supplied buffer. *NewBufferOut receives a fresh allocation containing the
 * shrunken list; caller must ExFreePoolWithTag(*NewBufferOut, NTFSLX_TAG) on
 * success. *NewBufferLengthOut is 0 if the list became empty (and the
 * caller is responsible for removing the $ATTRIBUTE_LIST attribute itself).
 *
 * Returns STATUS_OBJECT_NAME_NOT_FOUND if no matching entry exists.
 */
NTSTATUS
NtfslxAttributeListRemoveEntry(
    _In_reads_bytes_(AttributeListLength) const void *AttributeList,
    _In_ ULONG AttributeListLength,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG LowestVcn,
    _Outptr_result_maybenull_ PUCHAR *NewBufferOut,
    _Out_ PULONG NewBufferLengthOut)
{
    const UCHAR *Cursor;
    const UCHAR *End;
    PNTFSLX_ATTRIBUTE_LIST_ENTRY Entry;
    ULONG RemainingLength;
    PUCHAR NewBuffer;
    ULONG NewBufferLength;
    ULONG RemoveOffset;
    ULONG RemoveLength;
    BOOLEAN Found;
    NTSTATUS Status;

    *NewBufferOut = NULL;
    *NewBufferLengthOut = 0;

    if (AttributeList == NULL || AttributeListLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxValidateAttributeList(AttributeList, AttributeListLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Locate the entry. */
    Found = FALSE;
    RemoveOffset = 0;
    RemoveLength = 0;
    Cursor = AttributeList;
    End = (const UCHAR *)AttributeList + AttributeListLength;
    while (Cursor < End)
    {
        RemainingLength = (ULONG)(End - Cursor);
        Entry = (PNTFSLX_ATTRIBUTE_LIST_ENTRY)Cursor;
        if (!NtfslxAttributeListEntryValid(Entry, RemainingLength))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Entry->Type == AttributeType &&
            Entry->LowestVcn == LowestVcn &&
            NtfslxAttributeListEntryNameMatches(Entry, Name, NameLength))
        {
            RemoveOffset = (ULONG)((const UCHAR *)Entry - (const UCHAR *)AttributeList);
            RemoveLength = Entry->Length;
            Found = TRUE;
            break;
        }

        Cursor += Entry->Length;
    }

    if (!Found)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    NewBufferLength = AttributeListLength - RemoveLength;
    if (NewBufferLength == 0)
    {
        return STATUS_SUCCESS;
    }

    NewBuffer = ExAllocatePoolWithTag(NonPagedPool, NewBufferLength, NTFSLX_TAG);
    if (NewBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (RemoveOffset > 0)
    {
        RtlCopyMemory(NewBuffer, AttributeList, RemoveOffset);
    }
    if (RemoveOffset + RemoveLength < AttributeListLength)
    {
        RtlCopyMemory(NewBuffer + RemoveOffset,
                      (const UCHAR *)AttributeList + RemoveOffset + RemoveLength,
                      AttributeListLength - RemoveOffset - RemoveLength);
    }

    *NewBufferOut = NewBuffer;
    *NewBufferLengthOut = NewBufferLength;
    return STATUS_SUCCESS;
}

/*
 * Find an attribute-list entry by (Type, Name, LowestVcn) and return the
 * MFT reference and instance it points at. Caller can use those to read
 * the extension record and locate the attribute within it.
 */
NTSTATUS
NtfslxAttributeListLookupEntry(
    _In_reads_bytes_(AttributeListLength) const void *AttributeList,
    _In_ ULONG AttributeListLength,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG LowestVcn,
    _Out_ PULONGLONG MftReferenceOut,
    _Out_ PUSHORT InstanceOut)
{
    const UCHAR *Cursor;
    const UCHAR *End;
    PNTFSLX_ATTRIBUTE_LIST_ENTRY Entry;
    ULONG RemainingLength;
    NTSTATUS Status;

    if (MftReferenceOut == NULL || InstanceOut == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *MftReferenceOut = 0;
    *InstanceOut = 0;

    if (AttributeList == NULL || AttributeListLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxValidateAttributeList(AttributeList, AttributeListLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Cursor = AttributeList;
    End = (const UCHAR *)AttributeList + AttributeListLength;
    while (Cursor < End)
    {
        RemainingLength = (ULONG)(End - Cursor);
        Entry = (PNTFSLX_ATTRIBUTE_LIST_ENTRY)Cursor;
        if (!NtfslxAttributeListEntryValid(Entry, RemainingLength))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Entry->Type == AttributeType &&
            Entry->LowestVcn == LowestVcn &&
            NtfslxAttributeListEntryNameMatches(Entry, Name, NameLength))
        {
            *MftReferenceOut = Entry->MftReference;
            *InstanceOut = Entry->Instance;
            return STATUS_SUCCESS;
        }

        Cursor += Entry->Length;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/*
 * Allocate a new MFT record, mark it as an extension of BaseRecord, and
 * persist a fresh empty extension shell to disk. *OutExtensionRecord is
 * a fresh pool buffer holding the in-memory image of the new record;
 * callers manipulate it in place (insert attributes), then commit it via
 * NtfslxWriteMftRecord. The pool buffer is freed by the caller via
 * ExFreePoolWithTag(*OutExtensionRecord, NTFSLX_TAG).
 *
 * The base record's BytesInUse / BytesAllocated are NOT touched by this
 * function; the caller is responsible for adding $ATTRIBUTE_LIST entries
 * to the base record after this returns.
 *
 * Crash safety: the bitmap commit happens inside NtfslxAllocateMftRecord;
 * if we crash between bitmap-commit and the caller's first write of the
 * extension record, the slot is leaked (bitmap shows allocated, no record
 * references it) but no corruption results. The leak is recoverable by
 * a future $MFT chkdsk.
 */
NTSTATUS
NtfslxAllocateExtensionMftRecord(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PNTFSLX_MFT_RECORD BaseRecord,
    _Out_ PULONGLONG OutExtensionRecordNumber,
    _Outptr_ PNTFSLX_MFT_RECORD *OutExtensionRecord)
{
    PNTFSLX_MFT_RECORD ExtensionRecord;
    ULONGLONG ExtRecordNumber;
    ULONGLONG BaseReference;
    ULONG RecordSize;
    USHORT UsaCount;
    PUSHORT UsaPos;
    NTSTATUS Status;

    *OutExtensionRecordNumber = 0;
    *OutExtensionRecord = NULL;

    if (DevExt == NULL || BaseRecord == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RecordSize = DevExt->VolumeInfo.BytesPerFileRecord;

    Status = NtfslxAllocateMftRecordEx(DevExt, &ExtRecordNumber);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Read back the freshly allocated record so we inherit its preserved
     * sequence number and the on-disk shape established by the allocator
     * (USA, AttributesOffset, end marker). */
    ExtensionRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (ExtensionRecord == NULL)
    {
        (VOID)NtfslxFreeMftRecord(DevExt, DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, ExtRecordNumber);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                 DevExt->MftRunlist, ExtRecordNumber,
                                 ExtensionRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(ExtensionRecord, NTFSLX_TAG);
        (VOID)NtfslxFreeMftRecord(DevExt, DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, ExtRecordNumber);
        return Status;
    }

    /*
     * Patch the BaseMftRecord field. NTFS uses MK_MREF(record, sequence)
     * encoding so the seqno is part of the reference; if the base record
     * is later deleted and reused, an orphaned extension's BaseMftRecord
     * won't false-match the new base.
     */
    BaseReference = MK_MREF(BaseRecord->MftRecordNumber, BaseRecord->SequenceNumber);
    ExtensionRecord->BaseMftRecord = BaseReference;

    /* Persist the patched extension shell so a crash before the first
     * attribute write doesn't leave us with a record that thinks it is a
     * standalone base. */
    Status = NtfslxWriteMftRecord(DevExt,
                                  DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, ExtRecordNumber,
                                  ExtensionRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(ExtensionRecord, NTFSLX_TAG);
        (VOID)NtfslxFreeMftRecord(DevExt, DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, ExtRecordNumber);
        return Status;
    }

    /* MST fixup damages the in-memory record on write; re-prime the USA so
     * the caller can keep editing the in-memory copy and call WriteMft again
     * later without tripping NtfslxPreWriteMstFixup invariants. */
    UsaCount = ExtensionRecord->Ntfs.UsaCount;
    if (UsaCount > 0 && ExtensionRecord->Ntfs.UsaOffset > 0)
    {
        UsaPos = (PUSHORT)((PUCHAR)ExtensionRecord + ExtensionRecord->Ntfs.UsaOffset);
        *UsaPos = 1;
    }

    *OutExtensionRecordNumber = ExtRecordNumber;
    *OutExtensionRecord = ExtensionRecord;
    return STATUS_SUCCESS;
}

/*
 * Build an attribute-list buffer that reflects every non-$STANDARD_INFORMATION
 * non-$ATTRIBUTE_LIST attribute currently living in BaseRecord. Used when
 * promoting a previously-flat base record to the multi-record form: every
 * attribute that stays in the base needs a list entry pointing at it,
 * otherwise readers walking $ATTRIBUTE_LIST will miss them.
 *
 * *OutBuffer receives a freshly allocated buffer; caller must
 * ExFreePoolWithTag(*OutBuffer, NTFSLX_TAG).
 */
static
NTSTATUS
NtfslxAttributeListBuildFromBaseRecord(
    _In_ PNTFSLX_MFT_RECORD BaseRecord,
    _Outptr_ PUCHAR *OutBuffer,
    _Out_ PULONG OutBufferLength)
{
    PUCHAR Base;
    PNTFSLX_ATTR_RECORD Attribute;
    ULONG Offset;
    ULONG TotalLength;
    PUCHAR ListBuffer;
    ULONG ListPos;
    ULONGLONG BaseReference;

    *OutBuffer = NULL;
    *OutBufferLength = 0;

    if (BaseRecord == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* First pass: total bytes needed. */
    Base = (PUCHAR)BaseRecord;
    Offset = BaseRecord->AttributesOffset;
    TotalLength = 0;
    while (Offset + FIELD_OFFSET(NTFSLX_ATTR_RECORD, Data) <= BaseRecord->BytesInUse)
    {
        Attribute = (PNTFSLX_ATTR_RECORD)(Base + Offset);
        if (Attribute->Type == NTFSLX_ATTRIBUTE_END)
        {
            break;
        }
        if (!NtfslxAttributeRecordValid(BaseRecord, Attribute))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Attribute->Type != NTFSLX_ATTRIBUTE_STANDARD_INFORMATION &&
            Attribute->Type != NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST)
        {
            TotalLength += NtfslxAttributeListEntrySize(Attribute->NameLength);
        }

        Offset += Attribute->Length;
    }

    if (TotalLength == 0)
    {
        /* Nothing to migrate; an empty list is invalid for this writer. */
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    ListBuffer = ExAllocatePoolWithTag(NonPagedPool, TotalLength, NTFSLX_TAG);
    if (ListBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Second pass: serialize. Attributes inside an MFT record are already in
     * Type-then-Name order, so the resulting list is sorted by construction
     * for entries pointing at the base record. */
    BaseReference = MK_MREF(BaseRecord->MftRecordNumber, BaseRecord->SequenceNumber);
    Offset = BaseRecord->AttributesOffset;
    ListPos = 0;
    while (Offset + FIELD_OFFSET(NTFSLX_ATTR_RECORD, Data) <= BaseRecord->BytesInUse)
    {
        ULONG EntrySize;
        ULONGLONG LowestVcn;
        PCWSTR AttrName;

        Attribute = (PNTFSLX_ATTR_RECORD)(Base + Offset);
        if (Attribute->Type == NTFSLX_ATTRIBUTE_END)
        {
            break;
        }

        if (Attribute->Type == NTFSLX_ATTRIBUTE_STANDARD_INFORMATION ||
            Attribute->Type == NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST)
        {
            Offset += Attribute->Length;
            continue;
        }

        EntrySize = NtfslxAttributeListEntrySize(Attribute->NameLength);
        if (ListPos + EntrySize > TotalLength)
        {
            ExFreePoolWithTag(ListBuffer, NTFSLX_TAG);
            return STATUS_INTERNAL_ERROR;
        }

        AttrName = (Attribute->NameLength == 0) ? NULL :
                   (PCWSTR)((PUCHAR)Attribute + Attribute->NameOffset);
        LowestVcn = (Attribute->NonResident == 0) ? 0
                    : Attribute->Data.NonResident.LowestVcn;

        NtfslxAttributeListBuildEntry(ListBuffer + ListPos,
                                      EntrySize,
                                      Attribute->Type,
                                      AttrName,
                                      Attribute->NameLength,
                                      LowestVcn,
                                      BaseReference,
                                      Attribute->Instance);
        ListPos += EntrySize;
        Offset += Attribute->Length;
    }

    *OutBuffer = ListBuffer;
    *OutBufferLength = ListPos;
    return STATUS_SUCCESS;
}

/*
 * Replace the value of an existing resident $ATTRIBUTE_LIST attribute with
 * a new buffer, or insert a fresh one if absent. Returns STATUS_DISK_FULL
 * if the base record cannot grow to fit the new list.
 *
 * The caller is responsible for committing the modified base record to
 * disk; this routine only edits the in-memory image.
 */
static
NTSTATUS
NtfslxAttributeListWriteResidentToBase(
    _Inout_ PNTFSLX_MFT_RECORD BaseRecord,
    _In_reads_bytes_(NewListLength) const void *NewList,
    _In_ ULONG NewListLength)
{
    PNTFSLX_ATTR_RECORD Existing;
    ULONG NewAttrLength;
    ULONG ValueOffset;
    NTSTATUS Status;

    if (BaseRecord == NULL || NewList == NULL || NewListLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Header (24) + value, 8-byte aligned. The list itself has no name. */
    ValueOffset = 0x18;
    NewAttrLength = ROUND_UP(ValueOffset + NewListLength, 8);

    Status = NtfslxFindAttributeInRecordByExtent(BaseRecord,
                                                 NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST,
                                                 NULL, 0, 0,
                                                 &Existing);
    if (NT_SUCCESS(Status))
    {
        /* Resize in place. Only resident lists are supported by this V1. */
        if (Existing->NonResident != 0)
        {
            return STATUS_NOT_SUPPORTED;
        }

        Status = NtfslxResizeAttributeRecord(BaseRecord, Existing, NewAttrLength);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        Existing->Data.Resident.ValueLength = NewListLength;
        Existing->Data.Resident.ValueOffset = (USHORT)ValueOffset;
        Existing->Data.Resident.Flags = 0;
        RtlCopyMemory((PUCHAR)Existing + ValueOffset, NewList, NewListLength);
        if (NewAttrLength > ValueOffset + NewListLength)
        {
            RtlZeroMemory((PUCHAR)Existing + ValueOffset + NewListLength,
                          NewAttrLength - ValueOffset - NewListLength);
        }
        return STATUS_SUCCESS;
    }

    if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return Status;
    }

    /* Create a new $ATTRIBUTE_LIST attribute. */
    return NtfslxInsertAttributeRecord(BaseRecord,
                                       0,
                                       NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST,
                                       NULL, 0,
                                       NewList, NewListLength,
                                       NULL);
}

/*
 * Move an attribute record from BaseRecord to ExtensionRecord. Used when
 * we promote a previously-flat base record to multi-record form and need
 * to make room for $ATTRIBUTE_LIST. The moved attribute keeps its Instance
 * (callers reference attributes by (Type, Name, Instance) tuple inside the
 * containing record). Returns STATUS_DISK_FULL if the destination cannot
 * accept the moved bytes.
 */
static
NTSTATUS
NtfslxAttributeListMoveAttribute(
    _Inout_ PNTFSLX_MFT_RECORD BaseRecord,
    _Inout_ PNTFSLX_MFT_RECORD ExtensionRecord,
    _In_ PNTFSLX_ATTR_RECORD SourceAttribute)
{
    ULONG AttributeLength;
    ULONG DestOffset;
    ULONG NewBytesInUse;
    PUCHAR DestBase;
    PUCHAR SrcBase;
    NTSTATUS Status;

    AttributeLength = SourceAttribute->Length;
    if (AttributeLength == 0 ||
        (AttributeLength & 7) != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /* Locate the END marker offset in the extension record. */
    DestBase = (PUCHAR)ExtensionRecord;
    DestOffset = ExtensionRecord->AttributesOffset;
    while (DestOffset + sizeof(ULONG) <= ExtensionRecord->BytesInUse)
    {
        PNTFSLX_ATTR_RECORD Cur = (PNTFSLX_ATTR_RECORD)(DestBase + DestOffset);
        if (Cur->Type == NTFSLX_ATTRIBUTE_END)
        {
            break;
        }
        if (Cur->Length == 0 ||
            DestOffset + Cur->Length > ExtensionRecord->BytesInUse)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        DestOffset += Cur->Length;
    }

    NewBytesInUse = DestOffset + AttributeLength + sizeof(ULONG) * 2;
    if (NewBytesInUse > ExtensionRecord->BytesAllocated)
    {
        return STATUS_DISK_FULL;
    }

    /* Copy the attribute bytes verbatim into the extension. */
    SrcBase = (PUCHAR)SourceAttribute;
    RtlCopyMemory(DestBase + DestOffset, SrcBase, AttributeLength);

    /* Re-place the END marker. */
    *(PULONG)(DestBase + DestOffset + AttributeLength) = NTFSLX_ATTRIBUTE_END;
    ExtensionRecord->BytesInUse = DestOffset + AttributeLength + sizeof(ULONG) * 2;

    /* Now drop the original attribute from the base. */
    Status = NtfslxRemoveAttributeRecord(BaseRecord, SourceAttribute);
    return Status;
}

/*
 * High-level: install a fresh resident attribute (Type, Name, Value) on
 * the file backed by BaseRecord. If the attribute fits in the base record,
 * insert it inline. Otherwise, allocate an extension MFT record, place the
 * attribute there, and update (or create) the base's $ATTRIBUTE_LIST.
 *
 * On success, *OutHostMftReference is the MFT reference of whichever
 * record now hosts the attribute, *OutHostInstance is the assigned
 * Instance, and the caller has committed both records to disk.
 *
 * Crash safety:
 *   1) Allocate extension (if needed)            - bitmap dirties first
 *   2) Place attribute in extension              - extension record on-disk
 *   3) Update $ATTRIBUTE_LIST in base, persist   - base record on-disk
 * If we crash between (1) and (2), the extension is shaped but empty -
 * unused but harmless until chkdsk. If we crash between (2) and (3), the
 * extension holds an orphaned attribute - again, unused until chkdsk
 * because the base record's $ATTRIBUTE_LIST does not reference it.
 *
 * All scope-V1 limitations apply: resident value only, single new
 * extension record per call, no coalescing.
 */
NTSTATUS
NtfslxAttributeListInstallResidentAttribute(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _Inout_ PNTFSLX_MFT_RECORD BaseRecord,
    _In_ ULONGLONG BaseRecordNumber,
    _In_ ULONG AttributeType,
    _In_reads_opt_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _In_reads_bytes_opt_(ValueLength) const VOID *Value,
    _In_ ULONG ValueLength,
    _Out_opt_ PULONGLONG OutHostMftReference,
    _Out_opt_ PUSHORT OutHostInstance)
{
    PNTFSLX_ATTR_RECORD InsertedAttr;
    PNTFSLX_MFT_RECORD ExtensionRecord;
    ULONGLONG ExtensionRecordNumber;
    ULONGLONG HostReference;
    USHORT HostInstance;
    PUCHAR ExistingList;
    ULONG ExistingListLength;
    PUCHAR NewList;
    ULONG NewListLength;
    PUCHAR PromotionList;
    ULONG PromotionListLength;
    PNTFSLX_ATTR_RECORD ExistingListAttr;
    NTSTATUS Status;
    NTSTATUS RetStatus;
    BOOLEAN HaveExtension;
    BOOLEAN ExtensionPersisted;

    if (DevExt == NULL || BaseRecord == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (NameLength != 0 && Name == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (ValueLength != 0 && Value == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    InsertedAttr = NULL;
    ExtensionRecord = NULL;
    ExtensionRecordNumber = 0;
    ExistingList = NULL;
    ExistingListLength = 0;
    NewList = NULL;
    NewListLength = 0;
    PromotionList = NULL;
    PromotionListLength = 0;
    HaveExtension = FALSE;
    ExtensionPersisted = FALSE;

    /*
     * Pre-flight: if $ATTRIBUTE_LIST already exists in the base, the
     * inline-insert path needs space for both the new attribute AND the
     * matching list entry. Worst-case for "list grows by one entry" is
     * the entry size that NtfslxAttributeListEntrySize would emit; the
     * resident header doesn't change. NtfslxResizeAttributeRecord
     * accommodates the value-length growth in place if room exists.
     *
     * If the list is missing, the inline path is straightforward and the
     * later StatusDiskFull from NtfslxInsertAttributeRecord handles the
     * promotion case.
     */
    Status = NtfslxFindAttributeInRecordByExtent(BaseRecord,
                                                 NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST,
                                                 NULL, 0, 0,
                                                 &ExistingListAttr);
    if (NT_SUCCESS(Status))
    {
        ULONG ListGrowBytes = NtfslxAttributeListEntrySize(NameLength);
        ULONG ProjectedBytesInUse = BaseRecord->BytesInUse + ListGrowBytes;

        if (ExistingListAttr->NonResident != 0)
        {
            /* V1 doesn't support non-resident $ATTRIBUTE_LIST. */
            return STATUS_NOT_SUPPORTED;
        }

        if (ProjectedBytesInUse > BaseRecord->BytesAllocated)
        {
            /* Forcing the inline path will leave the record over budget
             * by the time we update the list. Skip directly to extension
             * promotion. */
            Status = STATUS_DISK_FULL;
        }
        else
        {
            Status = STATUS_SUCCESS;
        }
    }
    else if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        Status = STATUS_SUCCESS;
        ExistingListAttr = NULL;
    }
    else
    {
        return Status;
    }

    /* First attempt: simple inline insert. The common case (small file
     * with few streams) hits this path and never touches $ATTRIBUTE_LIST. */
    if (NT_SUCCESS(Status))
    {
        Status = NtfslxInsertAttributeRecord(BaseRecord,
                                             DevExt->VolumeInfo.BytesPerFileRecord,
                                             AttributeType,
                                             Name, NameLength,
                                             Value, ValueLength,
                                             &InsertedAttr);
    }
    if (NT_SUCCESS(Status))
    {
        HostReference = MK_MREF(BaseRecord->MftRecordNumber, BaseRecord->SequenceNumber);
        HostInstance = InsertedAttr->Instance;

        /* Re-locate $ATTRIBUTE_LIST: the inline insert may have shifted it
         * within the record. */
        Status = NtfslxFindAttributeInRecordByExtent(BaseRecord,
                                                     NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST,
                                                     NULL, 0, 0,
                                                     &ExistingListAttr);
        if (NT_SUCCESS(Status))
        {
            ASSERT(ExistingListAttr->NonResident == 0);

            ExistingList = (PUCHAR)ExistingListAttr +
                           ExistingListAttr->Data.Resident.ValueOffset;
            ExistingListLength = ExistingListAttr->Data.Resident.ValueLength;

            Status = NtfslxAttributeListInsertEntry(ExistingList,
                                                    ExistingListLength,
                                                    AttributeType,
                                                    Name, NameLength,
                                                    0,  /* LowestVcn=0 for resident */
                                                    HostReference,
                                                    HostInstance,
                                                    &NewList,
                                                    &NewListLength);
            if (!NT_SUCCESS(Status))
            {
                /* Roll back the inline insert: list-splice failure leaves
                 * the base record consistent with its prior state. */
                (VOID)NtfslxRemoveAttributeRecord(BaseRecord, InsertedAttr);
                goto Cleanup;
            }

            Status = NtfslxAttributeListWriteResidentToBase(BaseRecord,
                                                            NewList,
                                                            NewListLength);
            if (!NT_SUCCESS(Status))
            {
                /* InsertedAttr may have been moved by the list resize
                 * attempt - re-find it by Type+Name+Instance to roll back. */
                PNTFSLX_ATTR_RECORD ToUndo;
                NTSTATUS UndoStatus =
                    NtfslxFindAttributeInRecordByExtent(BaseRecord,
                                                        AttributeType,
                                                        Name, NameLength,
                                                        0,
                                                        &ToUndo);
                if (NT_SUCCESS(UndoStatus))
                {
                    (VOID)NtfslxRemoveAttributeRecord(BaseRecord, ToUndo);
                }
                goto Cleanup;
            }
        }
        else if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
        {
            goto Cleanup;
        }
        Status = STATUS_SUCCESS;
        goto Done;
    }

    if (Status != STATUS_DISK_FULL)
    {
        return Status;
    }

    /* Inline insert failed for lack of room. Promote: allocate an
     * extension record and place the new attribute there. If the base
     * record does not yet carry $ATTRIBUTE_LIST, build one that lists
     * every existing attribute, then insert it and commit. */
    Status = NtfslxAllocateExtensionMftRecord(DevExt, BaseRecord,
                                              &ExtensionRecordNumber,
                                              &ExtensionRecord);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    HaveExtension = TRUE;

    Status = NtfslxInsertAttributeRecord(ExtensionRecord,
                                         DevExt->VolumeInfo.BytesPerFileRecord,
                                         AttributeType,
                                         Name, NameLength,
                                         Value, ValueLength,
                                         &InsertedAttr);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    HostReference = MK_MREF(ExtensionRecord->MftRecordNumber,
                            ExtensionRecord->SequenceNumber);
    HostInstance = InsertedAttr->Instance;

    /* Commit the extension record before mutating the base. A crash here
     * leaves an orphaned extension that is invisible from the base
     * (because $ATTRIBUTE_LIST is not yet pointing at it) - benign. */
    Status = NtfslxWriteMftRecord(DevExt, DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, ExtensionRecordNumber,
                                  ExtensionRecord);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    ExtensionPersisted = TRUE;

    /* Now stitch the base record's $ATTRIBUTE_LIST. */
    Status = NtfslxFindAttributeInRecordByExtent(BaseRecord,
                                                 NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST,
                                                 NULL, 0, 0,
                                                 &ExistingListAttr);
    if (NT_SUCCESS(Status))
    {
        /* Already promoted previously: just splice in the new entry. */
        if (ExistingListAttr->NonResident != 0)
        {
            Status = STATUS_NOT_SUPPORTED;
            goto Cleanup;
        }

        ExistingList = (PUCHAR)ExistingListAttr +
                       ExistingListAttr->Data.Resident.ValueOffset;
        ExistingListLength = ExistingListAttr->Data.Resident.ValueLength;

        Status = NtfslxAttributeListInsertEntry(ExistingList,
                                                ExistingListLength,
                                                AttributeType,
                                                Name, NameLength,
                                                0,
                                                HostReference,
                                                HostInstance,
                                                &NewList,
                                                &NewListLength);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        Status = NtfslxAttributeListWriteResidentToBase(BaseRecord,
                                                        NewList,
                                                        NewListLength);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
    }
    else if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        /* First-time promotion: build a list covering every existing
         * non-trivial attribute in the base record, plus the new one in
         * the extension. */
        Status = NtfslxAttributeListBuildFromBaseRecord(BaseRecord,
                                                        &PromotionList,
                                                        &PromotionListLength);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        Status = NtfslxAttributeListInsertEntry(PromotionList,
                                                PromotionListLength,
                                                AttributeType,
                                                Name, NameLength,
                                                0,
                                                HostReference,
                                                HostInstance,
                                                &NewList,
                                                &NewListLength);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        Status = NtfslxAttributeListWriteResidentToBase(BaseRecord,
                                                        NewList,
                                                        NewListLength);
        if (Status == STATUS_DISK_FULL)
        {
            /* Inserting $ATTRIBUTE_LIST itself doesn't fit. Free up
             * room by moving one of the larger non-trivial attributes
             * (other than $FILE_NAME / $STANDARD_INFORMATION which must
             * stay in the base) into the extension we just allocated. */
            PUCHAR Base = (PUCHAR)BaseRecord;
            ULONG Offset = BaseRecord->AttributesOffset;
            PNTFSLX_ATTR_RECORD MoveCandidate = NULL;
            ULONG MoveCandidateSize = 0;

            while (Offset + FIELD_OFFSET(NTFSLX_ATTR_RECORD, Data) <= BaseRecord->BytesInUse)
            {
                PNTFSLX_ATTR_RECORD Cur = (PNTFSLX_ATTR_RECORD)(Base + Offset);
                if (Cur->Type == NTFSLX_ATTRIBUTE_END)
                {
                    break;
                }
                if (Cur->Type != NTFSLX_ATTRIBUTE_STANDARD_INFORMATION &&
                    Cur->Type != NTFSLX_ATTRIBUTE_FILE_NAME &&
                    Cur->Type != NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST &&
                    Cur->Length > MoveCandidateSize)
                {
                    MoveCandidate = Cur;
                    MoveCandidateSize = Cur->Length;
                }
                Offset += Cur->Length;
            }

            if (MoveCandidate == NULL)
            {
                Status = STATUS_DISK_FULL;
                goto Cleanup;
            }

            /* Migrate the candidate attribute. The base entry that points
             * at it must update its MftReference; we'll rebuild the
             * promotion list after the move. */
            Status = NtfslxAttributeListMoveAttribute(BaseRecord,
                                                      ExtensionRecord,
                                                      MoveCandidate);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }

            /* Re-persist the extension because we appended a moved
             * attribute. Even if a crash happens after this, the new
             * attribute is still orphaned (base list not yet updated). */
            Status = NtfslxWriteMftRecord(DevExt,
                                          DevExt->StorageDevice, &DevExt->VolumeInfo,
                                          DevExt->MftRunlist, ExtensionRecordNumber,
                                          ExtensionRecord);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }

            /* Rebuild the list - candidate is no longer in base, and the
             * caller's new attribute lives in the extension. */
            ExFreePoolWithTag(PromotionList, NTFSLX_TAG);
            PromotionList = NULL;
            PromotionListLength = 0;
            ExFreePoolWithTag(NewList, NTFSLX_TAG);
            NewList = NULL;
            NewListLength = 0;

            Status = NtfslxAttributeListBuildFromBaseRecord(BaseRecord,
                                                            &PromotionList,
                                                            &PromotionListLength);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }

            Status = NtfslxAttributeListInsertEntry(PromotionList,
                                                    PromotionListLength,
                                                    AttributeType,
                                                    Name, NameLength,
                                                    0,
                                                    HostReference,
                                                    HostInstance,
                                                    &NewList,
                                                    &NewListLength);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }

            Status = NtfslxAttributeListWriteResidentToBase(BaseRecord,
                                                            NewList,
                                                            NewListLength);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }
        }
        else if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
    }
    else
    {
        goto Cleanup;
    }

    Status = STATUS_SUCCESS;

Done:
    if (OutHostMftReference != NULL)
    {
        *OutHostMftReference = HostReference;
    }
    if (OutHostInstance != NULL)
    {
        *OutHostInstance = HostInstance;
    }

    RetStatus = STATUS_SUCCESS;
    goto Exit;

Cleanup:
    RetStatus = Status;
    /*
     * Best-effort rollback. If we crashed mid-way, the extension may have
     * been persisted with a moved attribute already gone from the base;
     * we can still drop the bitmap bit so the slot is reusable. The base
     * record changes that the caller observes have not yet been
     * committed by us, so the caller's "leave on failure" pattern works.
     */
    if (HaveExtension)
    {
        if (ExtensionPersisted)
        {
            /* The extension lives but is unreferenced; bitmap-free it. */
            (VOID)NtfslxFreeMftRecord(DevExt, DevExt->StorageDevice, &DevExt->VolumeInfo,
                                      DevExt->MftRunlist, ExtensionRecordNumber);
        }
        else
        {
            (VOID)NtfslxFreeMftRecord(DevExt, DevExt->StorageDevice, &DevExt->VolumeInfo,
                                      DevExt->MftRunlist, ExtensionRecordNumber);
        }
    }

Exit:
    if (NewList != NULL)
    {
        ExFreePoolWithTag(NewList, NTFSLX_TAG);
    }
    if (PromotionList != NULL)
    {
        ExFreePoolWithTag(PromotionList, NTFSLX_TAG);
    }
    if (ExtensionRecord != NULL)
    {
        ExFreePoolWithTag(ExtensionRecord, NTFSLX_TAG);
    }
    return RetStatus;
}
