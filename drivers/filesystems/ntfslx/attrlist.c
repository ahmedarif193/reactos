/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
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
