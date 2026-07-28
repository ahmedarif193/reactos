/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     C interface for FileRecord class
 * COPYRIGHT:   Copyright 2026 Carl Bialorucki <carl.bialorucki@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

PNtfsFileRecord
NtfsFileRecordCreate(
    _In_ PNtfsVolume DiskVolume,
    _In_ ULONG FileRecordSize)
{
    PVolume Vol = reinterpret_cast<PVolume>(DiskVolume);

    if (!Vol)
        return nullptr;

    if (FileRecordSize == 0)
        return reinterpret_cast<PNtfsFileRecord>(new(NonPagedPool, TAG_FILE_RECORD) FileRecord(Vol));

    return reinterpret_cast<PNtfsFileRecord>(new(NonPagedPool, TAG_FILE_RECORD) FileRecord(Vol, FileRecordSize));
}

void
NtfsFileRecordDestroy(
    _In_opt_ PNtfsFileRecord Fr)
{
    if (!Fr)
        return;
    delete reinterpret_cast<PFileRecord>(Fr);
}

PFileRecordHeader
NtfsFileRecordGetHeader(
    _In_ PNtfsFileRecord Fr)
{
    return reinterpret_cast<PFileRecord>(Fr)->Header;
}

PAttribute
NtfsFileRecordGetAttribute(
    _In_ PNtfsFileRecord Fr,
    _In_ AttributeType Type,
    _In_opt_ PWSTR Name)
{
    return reinterpret_cast<PFileRecord>(Fr)->GetAttribute(Type, Name);
}

UINT64
NtfsAttributeGetPhysicalAllocationSize(
    _In_opt_ PAttribute Attribute)
{
    if (!Attribute || !Attribute->IsNonResident)
        return 0;
    if ((Attribute->Flags &
         (ATTR_COMPRESSION_MASK | ATTR_SPARSE)) &&
        Attribute->Length >= 0x48)
    {
        return Attribute->
            NonResident.CompressedDataSize;
    }
    return Attribute->NonResident.AllocatedSize;
}

NTSTATUS
NtfsFileRecordGetAttributeData(
    _In_     PNtfsFileRecord Fr,
    _In_     AttributeType Type,
    _In_opt_ PWSTR Name,
    _Out_    PUCHAR *Data)
{
    return reinterpret_cast<PFileRecord>(Fr)->GetAttributeData(Type, Name, Data);
}

NTSTATUS
NtfsFileRecordQueryDataStreams(
    _In_ PNtfsFileRecord FileRecord,
    _Out_opt_ PNtfsDataStreamInformation Streams,
    _Inout_ PULONG StreamCount)
{
    if (!FileRecord)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(FileRecord)->
        QueryDataStreams(Streams, StreamCount);
}

NTSTATUS
NtfsFileRecordCopyData(
    _In_ PNtfsFileRecord Fr,
    _In_ AttributeType Type,
    _In_opt_ PWSTR Name,
    _In_ PUCHAR Buffer,
    _Inout_ PULONG Length,
    _In_ ULONGLONG Offset)
{
    return reinterpret_cast<PFileRecord>(Fr)->CopyData(Type, Name, Buffer, Length, Offset);
}

NTSTATUS
NtfsFileRecordGetBasicInformation(
    _In_ PNtfsFileRecord Fr,
    _Out_ PNtfsFileBasicInformation Information)
{
    if (!Fr || !Information)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        GetBasicInformation(Information);
}

NTSTATUS
NtfsFileRecordSetBasicInformation(
    _In_ PNtfsFileRecord Fr,
    _In_ const NtfsFileBasicInformation* Information)
{
    if (!Fr || !Information)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        SetBasicInformation(Information);
}

NTSTATUS
NtfsFileRecordSetAutomaticTimestampMask(
    _In_ PNtfsFileRecord Fr,
    _In_ UINT32 Fields)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        SetAutomaticTimestampMask(Fields);
}

UINT32
NtfsFileRecordGetAutomaticTimestampMask(
    _In_ PNtfsFileRecord Fr)
{
    if (!Fr)
        return 0;
    return reinterpret_cast<PFileRecord>(Fr)->
        GetAutomaticTimestampMask();
}

NTSTATUS
NtfsFileRecordUpdateAutomaticTimestamps(
    _In_ PNtfsFileRecord Fr,
    _In_ UINT32 Fields)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        UpdateAutomaticTimestamps(Fields);
}

NTSTATUS
NtfsFileRecordReadReparsePoint(
    _In_ PNtfsFileRecord Fr,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->ReadReparsePoint(
        Buffer,
        BufferLength);
}

NTSTATUS
NtfsFileRecordSetReparsePoint(
    _In_ PNtfsFileRecord Fr,
    _In_reads_bytes_(BufferLength)
        const UCHAR* Buffer,
    _In_ ULONG BufferLength)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        SetReparsePoint(Buffer, BufferLength);
}

NTSTATUS
NtfsFileRecordDeleteReparsePoint(
    _In_ PNtfsFileRecord Fr,
    _In_reads_bytes_(BufferLength)
        const UCHAR* Buffer,
    _In_ ULONG BufferLength)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        DeleteReparsePoint(Buffer, BufferLength);
}

NTSTATUS
NtfsFileRecordDeleteExternalBacking(
    _In_ PNtfsFileRecord Fr)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        DeleteExternalBacking();
}

NTSTATUS
NtfsFileRecordReadExtendedAttributes(
    _In_ PNtfsFileRecord Fr,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength,
    _Out_opt_ PEAInformationEx Information)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->ReadExtendedAttributes(
        Buffer,
        BufferLength,
        Information);
}

NTSTATUS
NtfsFileRecordUpdateExtendedAttributes(
    _In_ PNtfsFileRecord Fr,
    _In_reads_(UpdateCount)
        const NtfsExtendedAttributeUpdate* Updates,
    _In_ ULONG UpdateCount)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        UpdateExtendedAttributes(Updates,
                                 UpdateCount);
}

NTSTATUS
NtfsFileRecordReadSecurityDescriptor(
    _In_ PNtfsFileRecord Fr,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->ReadSecurityDescriptor(
        Buffer,
        BufferLength);
}

NTSTATUS
NtfsFileRecordSetSecurityDescriptor(
    _In_ PNtfsFileRecord Fr,
    _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
    _In_ ULONG BufferLength)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->SetSecurityDescriptor(
        Buffer,
        BufferLength);
}

NTSTATUS
NtfsFileRecordWriteFileData(
    _In_ PNtfsFileRecord Fr,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ PUCHAR Buffer,
    _Inout_ PULONG Length,
    _In_ PLARGE_INTEGER Offset)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->WriteFileData(AttrType, StreamName, Buffer, Length, Offset);
}

NTSTATUS
NtfsFileRecordSetFileDataSize(
    _In_ PNtfsFileRecord Fr,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG NewSize)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->SetFileDataSize(
        AttrType,
        StreamName,
        NewSize);
}

NTSTATUS NtfsFileRecordSetFileValidDataLength(_In_ PNtfsFileRecord Fr, _In_ AttributeType AttrType, _In_opt_ PWSTR StreamName, _In_ ULONGLONG NewValidDataLength)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->SetFileValidDataLength(AttrType, StreamName, NewValidDataLength);
}

NTSTATUS
NtfsFileRecordSetFileAllocationSize(
    _In_ PNtfsFileRecord Fr,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG NewAllocationSize)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        SetFileAllocationSize(
            AttrType,
            StreamName,
        NewAllocationSize);
}

NTSTATUS
NtfsFileRecordSetSparse(
    _In_ PNtfsFileRecord Fr,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ BOOLEAN SetSparse)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->SetSparse(
        AttrType,
        StreamName,
        SetSparse);
}

NTSTATUS
NtfsFileRecordSetZeroData(
    _In_ PNtfsFileRecord Fr,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG FileOffset,
    _In_ ULONGLONG BeyondFinalZero)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->SetZeroData(
        AttrType,
        StreamName,
        FileOffset,
        BeyondFinalZero);
}

NTSTATUS
NtfsFileRecordQueryAllocatedRanges(
    _In_ PNtfsFileRecord Fr,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG FileOffset,
    _In_ ULONGLONG Length,
    _Out_opt_ PNtfsAllocatedRange Ranges,
    _Inout_ PULONG RangeCount)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        QueryAllocatedRanges(
            AttrType,
            StreamName,
            FileOffset,
            Length,
            Ranges,
            RangeCount);
}

NTSTATUS
NtfsFileRecordQueryRetrievalPointers(
    _In_ PNtfsFileRecord Fr,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG StartingVcn,
    _Out_ PULONGLONG ReturnedStartingVcn,
    _Out_opt_ PNtfsRetrievalExtent Extents,
    _Inout_ PULONG ExtentCount)
{
    if (!Fr)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PFileRecord>(Fr)->
        QueryRetrievalPointers(
            AttrType,
            StreamName,
            StartingVcn,
            ReturnedStartingVcn,
            Extents,
            ExtentCount);
}

#ifdef __cplusplus
}
#endif
