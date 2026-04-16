/*
 * PROJECT:     ReactOS NTFS driver (ntfslx)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Attribute record manipulation within MFT records
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NtfslxResizeAttributeRecord(
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _Inout_ PNTFSLX_ATTR_RECORD Attribute,
    _In_ ULONG NewSize)
{
    PUCHAR Base;
    ULONG OldSize;
    ULONG AttrOffset;
    ULONG TailStart;
    ULONG TailEnd;
    ULONG TailLength;
    LONG SizeDelta;
    ULONG NewBytesInUse;

    /* NewSize must be aligned to 8 bytes */
    NewSize = ROUND_UP(NewSize, 8);
    OldSize = Attribute->Length;

    if (NewSize == OldSize)
    {
        return STATUS_SUCCESS;
    }

    Base = (PUCHAR)MftRecord;
    AttrOffset = (ULONG)((PUCHAR)Attribute - Base);
    TailStart = AttrOffset + OldSize;
    TailEnd = MftRecord->BytesInUse;
    TailLength = TailEnd - TailStart;
    SizeDelta = (LONG)NewSize - (LONG)OldSize;
    NewBytesInUse = (ULONG)((LONG)MftRecord->BytesInUse + SizeDelta);

    if (NewBytesInUse > MftRecord->BytesAllocated)
    {
        return STATUS_DISK_FULL;
    }

    /* Move the tail (everything after this attribute) */
    if (TailLength > 0)
    {
        RtlMoveMemory(Base + AttrOffset + NewSize,
                      Base + TailStart,
                      TailLength);
    }

    /* Zero any newly exposed bytes if growing */
    if (SizeDelta > 0)
    {
        RtlZeroMemory(Base + AttrOffset + OldSize, (ULONG)SizeDelta);
    }

    Attribute->Length = NewSize;
    MftRecord->BytesInUse = NewBytesInUse;

    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxInsertAttributeRecord(
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _In_ ULONG RecordSize,
    _In_ ULONG Type,
    _In_reads_opt_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _In_reads_bytes_opt_(ValueLength) const VOID *Value,
    _In_ ULONG ValueLength,
    _Outptr_opt_ PNTFSLX_ATTR_RECORD *OutAttribute)
{
    PUCHAR Base;
    ULONG Offset;
    PNTFSLX_ATTR_RECORD CurrentAttr;
    PNTFSLX_ATTR_RECORD InsertPoint;
    ULONG InsertOffset;
    ULONG HeaderSize;
    ULONG NameBytes;
    ULONG ValueOffset;
    ULONG AttrLength;
    ULONG TailLength;
    ULONG NewBytesInUse;

    if (OutAttribute != NULL)
    {
        *OutAttribute = NULL;
    }

    Base = (PUCHAR)MftRecord;
    NameBytes = NameLength * sizeof(WCHAR);

    /* Calculate attribute record size:
     * Header (0x18 for resident) + name + value, all 8-byte aligned */
    HeaderSize = 0x18; /* FIELD_OFFSET of Data.Resident + sizeof(Resident) = 24 */
    ValueOffset = ROUND_UP(HeaderSize + NameBytes, 8);
    AttrLength = ROUND_UP(ValueOffset + ValueLength, 8);

    /* Find insertion point: attributes are sorted by type code */
    InsertPoint = NULL;
    InsertOffset = MftRecord->AttributesOffset;
    Offset = MftRecord->AttributesOffset;

    while (Offset + sizeof(ULONG) * 2 <= MftRecord->BytesInUse)
    {
        CurrentAttr = (PNTFSLX_ATTR_RECORD)(Base + Offset);
        if (CurrentAttr->Type == NTFSLX_ATTRIBUTE_END)
        {
            InsertOffset = Offset;
            break;
        }

        if (CurrentAttr->Length == 0 || Offset + CurrentAttr->Length > MftRecord->BytesInUse)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (CurrentAttr->Type > Type)
        {
            InsertOffset = Offset;
            break;
        }

        Offset += CurrentAttr->Length;
        InsertOffset = Offset;
    }

    /* Check if there is room: BytesInUse + AttrLength <= BytesAllocated
     * Note: we are inserting before the end marker, so the end marker + padding
     * is already accounted for in BytesInUse */
    NewBytesInUse = MftRecord->BytesInUse + AttrLength;
    if (NewBytesInUse > MftRecord->BytesAllocated)
    {
        return STATUS_DISK_FULL;
    }

    /* Make room: shift everything from InsertOffset to BytesInUse */
    TailLength = MftRecord->BytesInUse - InsertOffset;
    if (TailLength > 0)
    {
        RtlMoveMemory(Base + InsertOffset + AttrLength,
                      Base + InsertOffset,
                      TailLength);
    }

    /* Zero the new attribute record area */
    RtlZeroMemory(Base + InsertOffset, AttrLength);

    /* Fill in the attribute record header */
    InsertPoint = (PNTFSLX_ATTR_RECORD)(Base + InsertOffset);
    InsertPoint->Type = Type;
    InsertPoint->Length = AttrLength;
    InsertPoint->NonResident = 0;
    InsertPoint->NameLength = (UCHAR)NameLength;
    InsertPoint->NameOffset = (NameLength > 0) ? (USHORT)HeaderSize : 0;
    InsertPoint->Flags = 0;
    InsertPoint->Instance = MftRecord->NextAttributeInstance;
    InsertPoint->Data.Resident.ValueLength = ValueLength;
    InsertPoint->Data.Resident.ValueOffset = (USHORT)ValueOffset;
    InsertPoint->Data.Resident.Flags = 0;

    /* Copy attribute name if present */
    if (NameLength > 0 && Name != NULL)
    {
        RtlCopyMemory((PUCHAR)InsertPoint + HeaderSize, Name, NameBytes);
    }

    /* Copy attribute value if present */
    if (ValueLength > 0 && Value != NULL)
    {
        RtlCopyMemory((PUCHAR)InsertPoint + ValueOffset, Value, ValueLength);
    }

    MftRecord->BytesInUse = NewBytesInUse;
    MftRecord->NextAttributeInstance++;

    if (OutAttribute != NULL)
    {
        *OutAttribute = InsertPoint;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxAddNamedDataStream(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG MftIndex,
    _In_reads_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength)
{
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_ATTR_RECORD Existing;
    ULONG RecordSize;
    NTSTATUS Status;

    if (DevExt == NULL || Name == NULL || NameLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (NameLength > 255)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    RecordSize = DevExt->VolumeInfo.BytesPerFileRecord;
    Record = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (Record == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                 DevExt->MftRunlist, MftIndex, Record);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Record, NTFSLX_TAG);
        return Status;
    }

    /* If a $DATA with this exact name already exists, the stream is taken. */
    Status = NtfslxFindAttribute(Record, NTFSLX_ATTRIBUTE_DATA,
                                 Name, NameLength, &Existing);
    if (NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Record, NTFSLX_TAG);
        return STATUS_OBJECT_NAME_COLLISION;
    }
    if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        ExFreePoolWithTag(Record, NTFSLX_TAG);
        return Status;
    }

    /* Insert a fresh empty $DATA with the requested name. */
    Status = NtfslxInsertAttributeRecord(Record, RecordSize,
                                         NTFSLX_ATTRIBUTE_DATA,
                                         Name, NameLength,
                                         NULL, 0,
                                         NULL);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Record, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, MftIndex, Record);
    ExFreePoolWithTag(Record, NTFSLX_TAG);
    return Status;
}

NTSTATUS
NtfslxRemoveAttributeRecord(
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _In_ PNTFSLX_ATTR_RECORD Attribute)
{
    PUCHAR Base;
    ULONG AttrOffset;
    ULONG AttrLength;
    ULONG TailStart;
    ULONG TailLength;

    Base = (PUCHAR)MftRecord;
    AttrOffset = (ULONG)((PUCHAR)Attribute - Base);
    AttrLength = Attribute->Length;

    if (AttrLength == 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (AttrOffset + AttrLength > MftRecord->BytesInUse)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    TailStart = AttrOffset + AttrLength;
    TailLength = MftRecord->BytesInUse - TailStart;

    if (TailLength > 0)
    {
        RtlMoveMemory(Base + AttrOffset,
                      Base + TailStart,
                      TailLength);
    }

    /* Zero freed space */
    RtlZeroMemory(Base + AttrOffset + TailLength,
                  AttrLength);

    MftRecord->BytesInUse -= AttrLength;

    return STATUS_SUCCESS;
}
