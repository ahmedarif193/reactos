/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Validated NTFS security descriptor lookup and reading
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE 20
#define NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE 20
#define NTFS_SDS_DUPLICATE_OFFSET 0x40000
#define NTFS_SECURITY_DESCRIPTOR_REVISION 1
#define NTFS_SECURITY_DESCRIPTOR_SELF_RELATIVE 0x8000
#define NTFS_SECURITY_DESCRIPTOR_DACL_PRESENT 0x0004
#define NTFS_SECURITY_DESCRIPTOR_SACL_PRESENT 0x0010
#define NTFS_ACL_REVISION 2
#define NTFS_ACL_REVISION_DS 4
#define NTFS_MAX_SID_SUB_AUTHORITIES 15
#define NTFS_INDEX_HEADER_LARGE 1

typedef struct _NTFS_SECURITY_LOCATION
{
    ULONG Hash;
    ULONG SecurityId;
    ULONGLONG Offset;
    ULONG Length;
} NTFS_SECURITY_LOCATION, *PNTFS_SECURITY_LOCATION;

static const WCHAR NtfsSiiName[] = L"$SII";
static const WCHAR NtfsSdsName[] = L"$SDS";

static USHORT
ReadUnalignedU16(_In_ const UCHAR* Data)
{
    USHORT Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static ULONG
ReadUnalignedU32(_In_ const UCHAR* Data)
{
    ULONG Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static ULONGLONG
ReadUnalignedU64(_In_ const UCHAR* Data)
{
    ULONGLONG Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static void
ReadSecurityLocation(_In_ const UCHAR* Data,
                     _Out_ PNTFS_SECURITY_LOCATION Location)
{
    Location->Hash = ReadUnalignedU32(Data);
    Location->SecurityId =
        ReadUnalignedU32(Data + sizeof(ULONG));
    Location->Offset =
        ReadUnalignedU64(Data + 2 * sizeof(ULONG));
    Location->Length =
        ReadUnalignedU32(Data + 2 * sizeof(ULONG) +
                         sizeof(ULONGLONG));
}

static BOOLEAN
ValidateSid(_In_ const UCHAR* Descriptor,
            _In_ ULONG DescriptorLength,
            _In_ ULONG Offset)
{
    ULONG SidLength;
    UCHAR SubAuthorityCount;

    if (Offset == 0)
        return TRUE;
    if ((Offset & (sizeof(ULONG) - 1)) != 0 ||
        Offset < NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE ||
        Offset > DescriptorLength ||
        DescriptorLength - Offset < 8)
    {
        return FALSE;
    }

    if (Descriptor[Offset] != NTFS_SECURITY_DESCRIPTOR_REVISION)
        return FALSE;
    SubAuthorityCount = Descriptor[Offset + 1];
    if (SubAuthorityCount > NTFS_MAX_SID_SUB_AUTHORITIES)
        return FALSE;

    SidLength = 8 + SubAuthorityCount * sizeof(ULONG);
    return SidLength <= DescriptorLength - Offset;
}

static BOOLEAN
ValidateAcl(_In_ const UCHAR* Descriptor,
            _In_ ULONG DescriptorLength,
            _In_ ULONG Offset)
{
    const UCHAR* Acl;
    ULONG AceOffset;
    ULONG AclSize;
    ULONG AceCount;

    if (Offset == 0)
        return TRUE;
    if ((Offset & (sizeof(ULONG) - 1)) != 0 ||
        Offset < NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE ||
        Offset > DescriptorLength ||
        DescriptorLength - Offset < 8)
    {
        return FALSE;
    }

    Acl = Descriptor + Offset;
    if (Acl[0] != NTFS_ACL_REVISION &&
        Acl[0] != NTFS_ACL_REVISION_DS)
    {
        return FALSE;
    }

    AclSize = ReadUnalignedU16(Acl + 2);
    AceCount = ReadUnalignedU16(Acl + 4);
    if (AclSize < 8 || AclSize > DescriptorLength - Offset)
        return FALSE;

    AceOffset = 8;
    for (ULONG Index = 0; Index < AceCount; Index++)
    {
        ULONG AceSize;

        if (AceOffset > AclSize ||
            AclSize - AceOffset < 4)
        {
            return FALSE;
        }

        AceSize = ReadUnalignedU16(Acl + AceOffset + 2);
        if (AceSize < 4 ||
            (AceSize & (sizeof(ULONG) - 1)) != 0 ||
            AceSize > AclSize - AceOffset)
        {
            return FALSE;
        }
        AceOffset += AceSize;
    }

    return AceOffset <= AclSize;
}

static BOOLEAN
ValidateSecurityDescriptor(_In_ const UCHAR* Descriptor,
                           _In_ ULONG DescriptorLength)
{
    ULONG OwnerOffset;
    ULONG GroupOffset;
    ULONG SaclOffset;
    ULONG DaclOffset;
    USHORT Control;

    if (!Descriptor ||
        DescriptorLength < NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE ||
        (DescriptorLength & (sizeof(ULONG) - 1)) != 0 ||
        Descriptor[0] != NTFS_SECURITY_DESCRIPTOR_REVISION ||
        Descriptor[1] != 0)
    {
        return FALSE;
    }

    Control = ReadUnalignedU16(Descriptor + 2);
    if (!(Control & NTFS_SECURITY_DESCRIPTOR_SELF_RELATIVE))
        return FALSE;

    OwnerOffset = ReadUnalignedU32(Descriptor + 4);
    GroupOffset = ReadUnalignedU32(Descriptor + 8);
    SaclOffset = ReadUnalignedU32(Descriptor + 12);
    DaclOffset = ReadUnalignedU32(Descriptor + 16);

    if (!ValidateSid(Descriptor, DescriptorLength, OwnerOffset) ||
        !ValidateSid(Descriptor, DescriptorLength, GroupOffset) ||
        (!(Control & NTFS_SECURITY_DESCRIPTOR_SACL_PRESENT) &&
         SaclOffset != 0) ||
        (!(Control & NTFS_SECURITY_DESCRIPTOR_DACL_PRESENT) &&
         DaclOffset != 0) ||
        !ValidateAcl(Descriptor, DescriptorLength, SaclOffset) ||
        !ValidateAcl(Descriptor, DescriptorLength, DaclOffset))
    {
        return FALSE;
    }

    return TRUE;
}

/*
 * Replace the file's security with a validated self-relative descriptor
 * stored in the legacy per-file $SECURITY_DESCRIPTOR attribute. Clearing
 * SecurityId makes the local value authoritative for every reader
 * (including NTFS-3G and this library's own lookup order), because the
 * volume-wide $Secure reference only applies while the id is nonzero.
 * Maintaining the NTFS 3.x $Secure SDS/SII/SDH indexes for a canonical
 * shared descriptor remains future work.
 */
NTSTATUS
FileRecord::SetSecurityDescriptor(
    _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
    _In_ ULONG BufferLength)
{
    PAttribute SecurityAttribute;
    PAttribute StandardAttribute;
    PStandardInformationEx Standard;
    PUCHAR RecordBackup = NULL;
    BOOLEAN Committed = FALSE;
    NTSTATUS Status;

    if (!Buffer || BufferLength == 0)
        return STATUS_INVALID_PARAMETER;
    if (!DiskVolume)
        return STATUS_INVALID_DEVICE_STATE;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (FindAttributeInRecord(TypeAttributeList, NULL, NULL))
        return STATUS_NOT_IMPLEMENTED;
    if (!ValidateSecurityDescriptor(Buffer, BufferLength))
        return STATUS_INVALID_PARAMETER;

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD) UCHAR[RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup, Data, RecordBufferSize);

    SecurityAttribute = FindAttributeInRecord(
        TypeSecurityDescriptor,
        NULL,
        NULL);
    if (SecurityAttribute &&
        SecurityAttribute->IsNonResident)
    {
        Status = STATUS_NOT_IMPLEMENTED;
        goto Restore;
    }
    if (!SecurityAttribute)
    {
        Status = InsertResidentAttribute(
            TypeSecurityDescriptor,
            NULL,
            &SecurityAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }
    Status = ReplaceResidentData(SecurityAttribute,
                                 Buffer,
                                 BufferLength);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        goto Restore;
    /* V1 $STANDARD_INFORMATION predates SecurityId; only the NTFS 3.x
     * form carries the field to clear.
     */
    if (StandardAttribute->Resident.DataLength >=
        FIELD_OFFSET(StandardInformationEx, SecurityId) +
            sizeof(UINT32))
    {
        Standard->SecurityId = 0;
    }

    Status = PrepareAutomaticTimestamps(
        NTFS_BASIC_INFO_CHANGE_TIME,
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;
    Status = DiskVolume->MFT->WriteFileRecordToMFT(this);
    if (!NT_SUCCESS(Status))
        goto Restore;
    Committed = TRUE;

Restore:
    if (!Committed)
    {
        RtlCopyMemory(Data, RecordBackup, RecordBufferSize);
        Header = reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }
    delete[] RecordBackup;
    return Status;
}

static ULONG
SecurityDescriptorHash(_In_ const UCHAR* Descriptor,
                       _In_ ULONG DescriptorLength)
{
    ULONG Hash = 0;

    for (ULONG Offset = 0;
         Offset < DescriptorLength;
         Offset += sizeof(ULONG))
    {
        Hash = ReadUnalignedU32(Descriptor + Offset) +
               ((Hash << 3) | (Hash >> 29));
    }
    return Hash;
}

static NTSTATUS
ReadExactAttribute(_In_ PFileRecord File,
                   _In_ PAttribute Attribute,
                   _Out_ PUCHAR Buffer,
                   _In_ ULONG Length,
                   _In_ ULONGLONG Offset)
{
    ULONG Remaining = Length;
    NTSTATUS Status;

    Status = File->CopyData(Attribute,
                            Buffer,
                            &Remaining,
                            Offset);
    if (!NT_SUCCESS(Status))
        return Status;
    return Remaining == 0 ? STATUS_SUCCESS : STATUS_END_OF_FILE;
}

static NTSTATUS
FindSecurityIdInNode(
    _In_ ULONG SecurityId,
    _In_ PIndexNodeHeader Header,
    _In_ ULONG HeaderBytes,
    _Out_ PNTFS_SECURITY_LOCATION Location,
    _Out_ PULONGLONG ChildVCN,
    _Out_ PBOOLEAN Descend)
{
    PIndexEntry Entry;
    ULONG_PTR End;
    BOOLEAN FoundEnd = FALSE;

    RtlZeroMemory(Location, sizeof(*Location));
    *ChildVCN = 0;
    *Descend = FALSE;

    if (HeaderBytes < sizeof(*Header) ||
        Header->IndexOffset < sizeof(*Header) ||
        Header->IndexOffset > Header->TotalIndexSize ||
        Header->TotalIndexSize > HeaderBytes ||
        (Header->Flags & ~NTFS_INDEX_HEADER_LARGE) != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Entry = reinterpret_cast<PIndexEntry>(
        reinterpret_cast<PUCHAR>(Header) +
        Header->IndexOffset);
    End = reinterpret_cast<ULONG_PTR>(Header) +
          Header->TotalIndexSize;

    while (reinterpret_cast<ULONG_PTR>(Entry) < End)
    {
        ULONG EffectiveLength;
        ULONG Remaining;
        ULONG Key;

        Remaining = (ULONG)(End -
                    reinterpret_cast<ULONG_PTR>(Entry));
        if (Remaining < FIELD_OFFSET(IndexEntry, IndexStream) ||
            Entry->EntryLength <
                FIELD_OFFSET(IndexEntry, IndexStream) ||
            (Entry->EntryLength & (sizeof(ULONGLONG) - 1)) != 0 ||
            Entry->EntryLength > Remaining ||
            (Entry->Flags & ~(INDEX_ENTRY_NODE |
                              INDEX_ENTRY_END)) != 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        EffectiveLength = Entry->EntryLength;
        if (Entry->Flags & INDEX_ENTRY_NODE)
        {
            if (EffectiveLength <
                FIELD_OFFSET(IndexEntry, IndexStream) +
                    sizeof(ULONGLONG))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            EffectiveLength -= sizeof(ULONGLONG);
        }

        if (Entry->Flags & INDEX_ENTRY_END)
        {
            if (Entry->StreamLength != 0 ||
                Entry->Data.ViewIndex.DataLength != 0)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            FoundEnd = TRUE;
            if (Entry->Flags & INDEX_ENTRY_NODE)
            {
                *ChildVCN = ReadUnalignedU64(
                    reinterpret_cast<PUCHAR>(Entry) +
                    Entry->EntryLength - sizeof(ULONGLONG));
                *Descend = TRUE;
            }
            break;
        }

        if (Entry->StreamLength != sizeof(ULONG) ||
            Entry->Data.ViewIndex.Reserved != 0 ||
            Entry->Data.ViewIndex.DataLength !=
                NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE ||
            Entry->Data.ViewIndex.DataOffset <
                FIELD_OFFSET(IndexEntry, IndexStream) +
                    sizeof(ULONG) ||
            Entry->Data.ViewIndex.DataOffset >
                EffectiveLength ||
            Entry->Data.ViewIndex.DataLength >
                EffectiveLength -
                    Entry->Data.ViewIndex.DataOffset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Key = ReadUnalignedU32(Entry->IndexStream);
        ReadSecurityLocation(
            reinterpret_cast<PUCHAR>(Entry) +
                Entry->Data.ViewIndex.DataOffset,
            Location);
        if (Location->SecurityId != Key ||
            Location->Length <
                NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE +
                NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE ||
            (Location->Offset &
                (sizeof(ULONGLONG) * 2 - 1)) != 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (SecurityId == Key)
            return STATUS_SUCCESS;

        if (SecurityId < Key)
        {
            if (Entry->Flags & INDEX_ENTRY_NODE)
            {
                *ChildVCN = ReadUnalignedU64(
                    reinterpret_cast<PUCHAR>(Entry) +
                    Entry->EntryLength - sizeof(ULONGLONG));
                *Descend = TRUE;
                return STATUS_SUCCESS;
            }
            return STATUS_NOT_FOUND;
        }

        Entry = reinterpret_cast<PIndexEntry>(
            reinterpret_cast<PUCHAR>(Entry) +
            Entry->EntryLength);
    }

    if (!FoundEnd)
        return STATUS_FILE_CORRUPT_ERROR;
    return *Descend ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

static NTSTATUS
FindSecurityDescriptorLocation(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord SecureFile,
    _In_ ULONG SecurityId,
    _Out_ PNTFS_SECURITY_LOCATION Location)
{
    const ULONGLONG MaximumValue = ~(ULONGLONG)0;
    PAttribute IndexRootAttribute;
    PAttribute IndexAllocationAttribute;
    PAttribute BitmapAttribute;
    PIndexRootEx IndexRoot;
    PUCHAR IndexBufferData = NULL;
    ULONGLONG BitmapLength;
    ULONGLONG ChildVCN;
    ULONGLONG VisitedVCNs[64];
    ULONG VisitedCount = 0;
    ULONG IndexRecordSize;
    BOOLEAN Descend;
    NTSTATUS Status;

    IndexRootAttribute = SecureFile->GetAttribute(
        TypeIndexRoot,
        const_cast<PWSTR>(NtfsSiiName));
    if (!IndexRootAttribute ||
        IndexRootAttribute->IsNonResident ||
        IndexRootAttribute->Resident.DataLength <
            FIELD_OFFSET(IndexRootEx, Header) +
                sizeof(IndexNodeHeader))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = reinterpret_cast<PIndexRootEx>(
        GetResidentDataPointer(IndexRootAttribute));
    IndexRecordSize = IndexRoot->BytesPerIndexRec;
    if (IndexRoot->AttributeType != 0 ||
        IndexRoot->CollationRule != ATTRDEF_COLLATION_ULONG ||
        IndexRecordSize != BytesPerIndexRecord(DiskVolume) ||
        IndexRecordSize < sizeof(IndexBuffer) ||
        IndexRecordSize % DiskVolume->BytesPerSector != 0 ||
        IndexRoot->ClusPerIndexRec !=
            (UCHAR)DiskVolume->ClustersPerIndexRecord)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = FindSecurityIdInNode(
        SecurityId,
        &IndexRoot->Header,
        IndexRootAttribute->Resident.DataLength -
            FIELD_OFFSET(IndexRootEx, Header),
        Location,
        &ChildVCN,
        &Descend);
    if (!NT_SUCCESS(Status) || !Descend)
        return Status;

    IndexAllocationAttribute = SecureFile->GetAttribute(
        TypeIndexAllocation,
        const_cast<PWSTR>(NtfsSiiName));
    BitmapAttribute = SecureFile->GetAttribute(
        TypeBitmap,
        const_cast<PWSTR>(NtfsSiiName));
    if (!IndexAllocationAttribute ||
        !IndexAllocationAttribute->IsNonResident ||
        !BitmapAttribute)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    BitmapLength = GetAttributeDataSize(BitmapAttribute);
    if (BitmapLength == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    IndexBufferData =
        new(PagedPool, TAG_NTFS) UCHAR[IndexRecordSize];
    if (!IndexBufferData)
        return STATUS_INSUFFICIENT_RESOURCES;

    while (Descend)
    {
        PIndexBuffer NodeBuffer;
        ULONGLONG AllocationUnit;
        ULONGLONG AllocationOffset;
        ULONGLONG IndexRecordNumber;
        ULONGLONG BitmapByte;
        ULONG BytesRemaining;
        UCHAR BitmapMask;
        UCHAR BitmapValue;

        if (VisitedCount == RTL_NUMBER_OF(VisitedVCNs))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        for (ULONG Index = 0; Index < VisitedCount; Index++)
        {
            if (VisitedVCNs[Index] == ChildVCN)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
        }
        VisitedVCNs[VisitedCount++] = ChildVCN;

        AllocationUnit =
            IndexRecordSize < BytesPerCluster(DiskVolume)
                ? DiskVolume->BytesPerSector
                : BytesPerCluster(DiskVolume);
        if (AllocationUnit == 0 ||
            ChildVCN > MaximumValue / AllocationUnit)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        AllocationOffset = ChildVCN * AllocationUnit;
        if (AllocationOffset % IndexRecordSize != 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        IndexRecordNumber = AllocationOffset / IndexRecordSize;
        if ((IndexRecordNumber >> 3) >= BitmapLength)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        BitmapByte = IndexRecordNumber >> 3;
        BitmapMask =
            (UCHAR)(1 << (IndexRecordNumber & 7));
        BytesRemaining = sizeof(BitmapValue);
        Status = SecureFile->CopyData(
            BitmapAttribute,
            &BitmapValue,
            &BytesRemaining,
            BitmapByte);
        if (!NT_SUCCESS(Status) ||
            BytesRemaining != 0 ||
            !(BitmapValue & BitmapMask))
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        BytesRemaining = IndexRecordSize;
        Status = SecureFile->CopyData(
            IndexAllocationAttribute,
            IndexBufferData,
            &BytesRemaining,
            AllocationOffset);
        if (!NT_SUCCESS(Status) || BytesRemaining != 0)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_END_OF_FILE;
            goto Done;
        }

        NodeBuffer =
            reinterpret_cast<PIndexBuffer>(IndexBufferData);
        Status = NtfsApplyFixup(
            &NodeBuffer->RecordHeader,
            IndexRecordSize,
            DiskVolume->BytesPerSector);
        if (!NT_SUCCESS(Status) ||
            RtlCompareMemory(NodeBuffer->RecordHeader.TypeID,
                             "INDX",
                             4) != 4 ||
            NodeBuffer->VCN != ChildVCN)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        Descend = FALSE;
        Status = FindSecurityIdInNode(
            SecurityId,
            &NodeBuffer->IndexHeader,
            IndexRecordSize -
                FIELD_OFFSET(IndexBuffer, IndexHeader),
            Location,
            &ChildVCN,
            &Descend);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

Done:
    delete[] IndexBufferData;
    return Status;
}

static NTSTATUS
ValidateSdsHeaderAt(
    _In_ PFileRecord SecureFile,
    _In_ PAttribute SdsAttribute,
    _In_ PNTFS_SECURITY_LOCATION Location,
    _In_ ULONGLONG PhysicalOffset)
{
    UCHAR Header[NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE];
    NTFS_SECURITY_LOCATION Stored;
    ULONGLONG SdsSize = GetAttributeDataSize(SdsAttribute);
    NTSTATUS Status;

    if (PhysicalOffset > SdsSize ||
        Location->Length > SdsSize - PhysicalOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = ReadExactAttribute(
        SecureFile,
        SdsAttribute,
        Header,
        sizeof(Header),
        PhysicalOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    ReadSecurityLocation(Header, &Stored);
    if (Stored.Hash != Location->Hash ||
        Stored.SecurityId != Location->SecurityId ||
        Stored.Offset != Location->Offset ||
        Stored.Length != Location->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
ReadAndValidateSdsDescriptor(
    _In_ PFileRecord SecureFile,
    _In_ PAttribute SdsAttribute,
    _In_ PNTFS_SECURITY_LOCATION Location,
    _In_ ULONGLONG PhysicalOffset,
    _Out_ PUCHAR Buffer)
{
    ULONG DescriptorLength =
        Location->Length -
        NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE;
    NTSTATUS Status;

    Status = ValidateSdsHeaderAt(
        SecureFile,
        SdsAttribute,
        Location,
        PhysicalOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ReadExactAttribute(
        SecureFile,
        SdsAttribute,
        Buffer,
        DescriptorLength,
        PhysicalOffset +
            NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!ValidateSecurityDescriptor(Buffer,
                                    DescriptorLength) ||
        SecurityDescriptorHash(Buffer,
                               DescriptorLength) !=
            Location->Hash)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
ReadDirectSecurityDescriptor(
    _In_ PFileRecord File,
    _In_ PAttribute Attribute,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    ULONGLONG DataSize;
    ULONG Capacity;
    ULONG Required;
    NTSTATUS Status;

    DataSize = GetAttributeDataSize(Attribute);
    if (DataSize < NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE)
        return STATUS_FILE_CORRUPT_ERROR;
    if (DataSize > MAXULONG)
        return STATUS_FILE_TOO_LARGE;

    Capacity = *BufferLength;
    Required = (ULONG)DataSize;
    *BufferLength = Required;
    if (!Buffer || Capacity < Required)
        return STATUS_BUFFER_TOO_SMALL;

    Status = ReadExactAttribute(
        File,
        Attribute,
        Buffer,
        Required,
        0);
    if (!NT_SUCCESS(Status))
        return Status;
    return ValidateSecurityDescriptor(Buffer, Required)
        ? STATUS_SUCCESS
        : STATUS_FILE_CORRUPT_ERROR;
}

NTSTATUS
Volume::ReadSecurityDescriptorById(
    _In_ ULONG SecurityId,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    PFileRecord SecureFile = NULL;
    PAttribute SdsAttribute;
    NTFS_SECURITY_LOCATION Location;
    ULONGLONG DuplicateOffset;
    ULONG Capacity;
    ULONG Required;
    NTSTATUS FirstStatus;
    NTSTATUS Status;

    if (!BufferLength || SecurityId == 0)
        return STATUS_INVALID_PARAMETER;

    Status = MFT->GetFileRecord(_Secure, &SecureFile);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FindSecurityDescriptorLocation(
        this,
        SecureFile,
        SecurityId,
        &Location);
    if (!NT_SUCCESS(Status))
        goto Done;

    SdsAttribute = SecureFile->GetAttribute(
        TypeData,
        const_cast<PWSTR>(NtfsSdsName));
    if (!SdsAttribute ||
        Location.Length <
            NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE +
            NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    Required =
        Location.Length -
        NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE;
    Capacity = *BufferLength;
    *BufferLength = Required;

    FirstStatus = ValidateSdsHeaderAt(
        SecureFile,
        SdsAttribute,
        &Location,
        Location.Offset);
    if (!NT_SUCCESS(FirstStatus))
    {
        if (Location.Offset >
            ~(ULONGLONG)0 - NTFS_SDS_DUPLICATE_OFFSET)
        {
            Status = FirstStatus;
            goto Done;
        }
        DuplicateOffset =
            Location.Offset + NTFS_SDS_DUPLICATE_OFFSET;
        Status = ValidateSdsHeaderAt(
            SecureFile,
            SdsAttribute,
            &Location,
            DuplicateOffset);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    else
    {
        DuplicateOffset = Location.Offset;
    }

    if (!Buffer || Capacity < Required)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Done;
    }

    Status = ReadAndValidateSdsDescriptor(
        SecureFile,
        SdsAttribute,
        &Location,
        DuplicateOffset,
        Buffer);
    if (!NT_SUCCESS(Status) &&
        DuplicateOffset == Location.Offset &&
        Location.Offset <=
            ~(ULONGLONG)0 - NTFS_SDS_DUPLICATE_OFFSET)
    {
        Status = ReadAndValidateSdsDescriptor(
            SecureFile,
            SdsAttribute,
            &Location,
            Location.Offset + NTFS_SDS_DUPLICATE_OFFSET,
            Buffer);
    }

Done:
    delete SecureFile;
    return Status;
}

NTSTATUS
FileRecord::ReadSecurityDescriptor(
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    PAttribute SecurityAttribute;
    PAttribute StandardAttribute;
    PUCHAR StandardData;
    ULONG SecurityId;

    if (!BufferLength)
        return STATUS_INVALID_PARAMETER;

    SecurityAttribute = GetAttribute(
        TypeSecurityDescriptor,
        NULL);
    if (SecurityAttribute)
    {
        return ReadDirectSecurityDescriptor(
            this,
            SecurityAttribute,
            Buffer,
            BufferLength);
    }

    StandardAttribute = GetAttribute(
        TypeStandardInformation,
        NULL);
    if (!StandardAttribute ||
        StandardAttribute->IsNonResident ||
        StandardAttribute->Resident.DataOffset < 0x18 ||
        StandardAttribute->Resident.DataOffset >
            StandardAttribute->Length ||
        StandardAttribute->Resident.DataLength >
            StandardAttribute->Length -
                StandardAttribute->Resident.DataOffset ||
        StandardAttribute->Resident.DataLength <
            FIELD_OFFSET(StandardInformationEx, SecurityId) +
                sizeof(ULONG))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    StandardData = reinterpret_cast<PUCHAR>(
        GetResidentDataPointer(StandardAttribute));
    SecurityId = ReadUnalignedU32(
        StandardData +
        FIELD_OFFSET(StandardInformationEx, SecurityId));
    if (SecurityId == 0)
        return STATUS_NOT_FOUND;

    return DiskVolume->ReadSecurityDescriptorById(
        SecurityId,
        Buffer,
        BufferLength);
}
