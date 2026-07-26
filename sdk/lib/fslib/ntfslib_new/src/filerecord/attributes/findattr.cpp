/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static BOOLEAN
IsAlwaysResidentAttribute(_In_ AttributeType Type)
{
    switch (Type)
    {
        case TypeStandardInformation:
        case TypeFileName:
        case TypeVolumeName:
        case TypeVolumeInformation:
        case TypeIndexRoot:
        case TypeEAInformation:
            return TRUE;

        default:
            return FALSE;
    }
}

static BOOLEAN
IsAttributeListEntryValid(_In_ PAttributeListEx Entry,
                          _In_ ULONG Remaining)
{
    const ULONG MinimumEntrySize = 0x1a;

    return Remaining >= MinimumEntrySize &&
           Entry->RecordLength >= MinimumEntrySize &&
           (Entry->RecordLength & 7) == 0 &&
           Entry->RecordLength <= Remaining &&
           (Entry->NameLength == 0 ||
            (Entry->NameOffset >= MinimumEntrySize &&
             Entry->NameOffset +
                 Entry->NameLength * sizeof(WCHAR) <=
                 Entry->RecordLength));
}

static BOOLEAN
IsAttributeNameValid(_In_ PAttribute Attribute)
{
    ULONG HeaderSize;
    ULONG NameBytes;

    if (!Attribute)
        return FALSE;
    if (Attribute->NameLength == 0)
        return TRUE;

    HeaderSize = Attribute->IsNonResident
        ? ((Attribute->Flags & (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
            ? 0x48
            : 0x40)
        : 0x18;
    NameBytes = Attribute->NameLength * sizeof(WCHAR);
    return Attribute->NameOffset >= HeaderSize &&
           Attribute->NameOffset <= Attribute->Length &&
           NameBytes <= Attribute->Length - Attribute->NameOffset;
}

static BOOLEAN
AttributeNameMatchesListEntry(_In_ PAttribute Attribute,
                              _In_ PAttributeListEx Entry)
{
    ULONG NameBytes;

    if (!IsAttributeNameValid(Attribute) ||
        Attribute->NameLength != Entry->NameLength)
    {
        return FALSE;
    }

    NameBytes = Attribute->NameLength * sizeof(WCHAR);
    return NameBytes == 0 ||
           RtlCompareMemory(GetNamePointer(Attribute),
                            (PUCHAR)Entry + Entry->NameOffset,
                            NameBytes) == NameBytes;
}

static BOOLEAN
AttributesHaveSameName(_In_ PAttribute Left,
                       _In_ PAttribute Right)
{
    ULONG NameBytes;

    if (!IsAttributeNameValid(Left) ||
        !IsAttributeNameValid(Right) ||
        Left->NameLength != Right->NameLength)
    {
        return FALSE;
    }

    NameBytes = Left->NameLength * sizeof(WCHAR);
    return NameBytes == 0 ||
           RtlCompareMemory(GetNamePointer(Left),
                            GetNamePointer(Right),
                            NameBytes) == NameBytes;
}

/* Find Attribute Functions */
PAttribute
FileRecord::GetAttribute(_In_     AttributeType Type,
                         _In_opt_ PWSTR Name)
{
    PAttribute Attribute = FindAttributeInRecord(Type, Name, NULL);

    /*
     * A logical nonresident stream starts at VCN zero. If this base record
     * only carries a continuation extent, use $ATTRIBUTE_LIST to locate the
     * first segment so size and initialized-length metadata come from the
     * authoritative header.
     */
    if (Attribute &&
        Attribute->IsNonResident &&
        Attribute->NonResident.FirstVCN != 0)
    {
        Attribute = NULL;
    }

    if (Attribute || Type == TypeAttributeList)
        return Attribute;

    return FindAttributeFromList(Type, Name);
}

PAttribute
FileRecord::FindAttributeInRecord(_In_ AttributeType Type,
                                  _In_opt_ PWSTR Name,
                                  _In_opt_ const USHORT *AttributeId)
{
    ULONG DataPtr;
    PAttribute TestAttr;
    ULONG NameLength = 0;

    // Validate header before using it
    if (!Header || !Data)
        return NULL;

    // Basic sanity: ensure pointers exist; detailed signature checks happen at load time

    if (Name)
        NameLength = NtfsWcsLen(Name) * sizeof(WCHAR);

    // Progress data pointer to attribute section.
    DataPtr = Header->AttributeOffset;

    // Ensure AttributeOffset is inside the record bounds
    if (Header->AttributeOffset < sizeof(FileRecordHeader) ||
        Header->AttributeOffset >= Header->ActualSize ||
        Header->ActualSize > Header->AllocatedSize)
    {
        return NULL;
    }

    while (DataPtr + 0x10 /* minimum fixed header */ <= Header->ActualSize)
    {
        // Test current attribute
        TestAttr = (PAttribute)(&Data[DataPtr]);

        // Guard against corrupted attributes and end marker
        if (TestAttr->AttributeType == TypeAttributeEndMarker)
            return NULL;

        // Validate attribute length (resident: >= 0x18, nonresident: >= 0x40)
        const ULONG minHeader = TestAttr->IsNonResident
            ? ((TestAttr->Flags & (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
                ? 0x48
                : 0x40)
            : 0x18;
        if (TestAttr->Length < minHeader ||
            (TestAttr->Length & 7) != 0 ||
            DataPtr + TestAttr->Length > Header->ActualSize)
        {
            return NULL;
        }

        if (TestAttr->AttributeType == Type)
        {
            if (AttributeId &&
                TestAttr->AttributeID != *AttributeId)
            {
                DataPtr += TestAttr->Length;
                continue;
            }

            // If no name is specified, return the first attribute with the target type.
            if (!Name)
            {
                return (PAttribute)&Data[DataPtr];
            }

            // Validate name fields before comparing
            if (TestAttr->NameLength &&
                TestAttr->NameOffset >= minHeader &&
                TestAttr->NameOffset + (TestAttr->NameLength * sizeof(WCHAR)) <= TestAttr->Length)
            {
                if ((TestAttr->NameLength) * sizeof(WCHAR) == NameLength &&
                    RtlCompareMemory(GetNamePointer(TestAttr),
                                     Name,
                                     NameLength) == NameLength)
                {
                    // We found the attribute!
                    return (PAttribute)&Data[DataPtr];
                }
            }

            // This one isn't it. Try again.
        }

        else if (!TestAttr->Length ||
                 TestAttr->AttributeType > (ULONG)Type)
        {
            /* Attributes are stored in ascending order according to its attribute type.
             * If we passed the attribute type, it's not going to show up. Fail early.
             *
             * TODO: Search $ATTRIBUTE_LIST (0x20) if we can't find it.
             */
            return NULL;
        }

        DataPtr += TestAttr->Length;
    }

    // We went through the whole record and didn't find it.
    return NULL;
}

PAttribute
FileRecord::FindAttributeFromList(_In_ AttributeType Type,
                                  _In_opt_ PWSTR Name)
{
    PUCHAR ListData;
    ULONG ListLength;
    ULONG Offset = 0;
    ULONG NameLength = Name ? (ULONG)NtfsWcsLen(Name) : 0;
    PAttribute Result = NULL;
    NTSTATUS Status;

    /* The list contents are immutable while the record is loaded, so decode
     * them once per record and serve later lookups from the cached buffer.
     */
    Status = LoadAttributeList();
    if (!NT_SUCCESS(Status))
        return NULL;

    ListData = AttributeListData;
    ListLength = AttributeListLength;

    while (Offset < ListLength)
    {
        PAttributeListEx Entry;
        ULONG Remaining = ListLength - Offset;
        FileRecord* TargetRecord;
        ULONGLONG FileRecordNumber;

        Entry = (PAttributeListEx)(ListData + Offset);
        if (!IsAttributeListEntryValid(Entry, Remaining))
            break;

        if (Entry->Type == (ULONG)Type &&
            Entry->FirstVCN == 0 &&
            (!Name ||
             (Entry->NameLength == NameLength &&
              RtlCompareMemory(ListData + Offset + Entry->NameOffset,
                               Name,
                               NameLength * sizeof(WCHAR)) ==
                  NameLength * sizeof(WCHAR))))
        {
            FileRecordNumber = GetFRNFromFileRef(Entry->BaseFileRef);
            TargetRecord = FileRecordNumber == Header->MFTRecordNumber
                ? this
                : GetExtentRecord(Entry->BaseFileRef);
            if (!TargetRecord)
                break;

            Result = TargetRecord->FindAttributeInRecord(
                Type,
                Name,
                &Entry->AttributeId);
            if (Result)
                break;
        }

        Offset += Entry->RecordLength;
    }

    return Result;
}

NTSTATUS
FileRecord::LoadAttributeList()
{
    PAttribute ListAttribute;

    if (AttributeListData)
        return STATUS_SUCCESS;

    ListAttribute = FindAttributeInRecord(TypeAttributeList, NULL, NULL);
    if (!ListAttribute)
        return STATUS_NOT_FOUND;

    return ReadAttributeAlloc(ListAttribute,
                              &AttributeListData,
                              &AttributeListLength);
}

FileRecord*
FileRecord::GetExtentRecord(_In_ ULONGLONG FileReference)
{
    PFileRecordExtentCacheEntry Entry;
    PFileRecord Record = NULL;
    ULONGLONG FileRecordNumber = GetFRNFromFileRef(FileReference);
    USHORT SequenceNumber = (USHORT)(FileReference >> 48);
    NTSTATUS Status;

    for (Entry = ExtentCache; Entry; Entry = Entry->Next)
    {
        if (Entry->FileReference == FileReference)
            return Entry->Record;
    }

    if (FileRecordNumber > MAXULONG)
        return NULL;

    Status = DiskVolume->MFT->GetFileRecord((ULONG)FileRecordNumber,
                                            &Record);
    if (!NT_SUCCESS(Status) || !Record)
        return NULL;

    if ((SequenceNumber != 0 &&
         Record->Header->SequenceNumber != SequenceNumber) ||
        GetFRNFromFileRef(Record->Header->BaseFileRecord) !=
            Header->MFTRecordNumber)
    {
        delete Record;
        return NULL;
    }

    Entry = new(PagedPool, TAG_FILE_RECORD) FileRecordExtentCacheEntry();
    if (!Entry)
    {
        delete Record;
        return NULL;
    }

    Entry->FileReference = FileReference;
    Entry->Record = Record;
    Entry->Next = ExtentCache;
    Record->BaseRecordOwner = this;
    ExtentCache = Entry;
    return Record;
}

FileRecord*
FileRecord::GetAttributeOwner(_In_ PAttribute Attribute)
{
    const ULONG AttributeHeaderSize = 0x10;
    PFileRecordExtentCacheEntry Entry;
    ULONG_PTR AttributeAddress;
    ULONG_PTR RecordAddress;

    if (!Attribute)
        return NULL;

    AttributeAddress = reinterpret_cast<ULONG_PTR>(Attribute);
    RecordAddress = reinterpret_cast<ULONG_PTR>(Data);
    if (Data && Header &&
        Header->ActualSize <= RecordBufferSize &&
        Header->ActualSize >= AttributeHeaderSize &&
        AttributeAddress >= RecordAddress &&
        AttributeAddress - RecordAddress <=
            Header->ActualSize -
                AttributeHeaderSize)
    {
        return this;
    }

    for (Entry = ExtentCache; Entry; Entry = Entry->Next)
    {
        FileRecord* Record = Entry->Record;

        if (!Record || !Record->Data ||
            !Record->Header ||
            Record->Header->ActualSize >
                Record->RecordBufferSize ||
            Record->Header->ActualSize <
                AttributeHeaderSize)
        {
            continue;
        }

        RecordAddress =
            reinterpret_cast<ULONG_PTR>(Record->Data);
        if (AttributeAddress >= RecordAddress &&
            AttributeAddress - RecordAddress <=
                Record->Header->ActualSize -
                    AttributeHeaderSize)
        {
            return Record;
        }
    }

    return NULL;
}

void
FileRecord::ClearExtentCache()
{
    while (ExtentCache)
    {
        PFileRecordExtentCacheEntry Entry = ExtentCache;
        ExtentCache = Entry->Next;
        delete Entry->Record;
        delete Entry;
    }
}

void
FileRecord::ClearExtentCacheExcept(_In_opt_ FileRecord* Record)
{
    PFileRecordExtentCacheEntry* Link = &ExtentCache;

    while (*Link)
    {
        PFileRecordExtentCacheEntry Entry = *Link;

        if (Entry->Record == Record)
        {
            Link = &Entry->Next;
            continue;
        }

        *Link = Entry->Next;
        delete Entry->Record;
        delete Entry;
    }
}

NTSTATUS
FileRecord::GetAttributeData(_In_     AttributeType Type,
                             _In_opt_ PWSTR Name,
                             _Out_    PUCHAR *Data)
{
    PAttribute Attr;

    Attr = GetAttribute(Type, Name);

    if (!Attr)
    {
        /* Every base file record must have $STANDARD_INFORMATION. Other
         * always-resident types are conditional on the kind of record.
         */
        return Type == TypeStandardInformation
            ? STATUS_FILE_CORRUPT_ERROR
            : STATUS_NOT_FOUND;
    }

    // These types are always resident. If they're not, this file is corrupt.
    if (Attr->IsNonResident && IsAlwaysResidentAttribute(Type))
        return STATUS_FILE_CORRUPT_ERROR;

    // Validate the resident data window before returning a pointer into it.
    if (Attr->Resident.DataOffset < 0x18 ||
        Attr->Resident.DataOffset > Attr->Length ||
        Attr->Resident.DataLength > Attr->Length - Attr->Resident.DataOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    *Data = reinterpret_cast<PUCHAR>(GetResidentDataPointer(Attr));

    return STATUS_SUCCESS;
}


PDataRun
FileRecord::DecodeDataRuns(_In_ PAttribute DataAttr)
{
    const ULONGLONG MaximumValue = ~(ULONGLONG)0;
    PUCHAR DataRunPtr;
    PUCHAR DataRunEnd;
    PDataRun Head = NULL;
    PDataRun Tail = NULL;
    ULONGLONG PreviousLCN = 0;
    ULONGLONG LogicalClusters = 0;

    ULONG MinimumHeader;

    MinimumHeader = DataAttr &&
                    (DataAttr->Flags & (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
        ? 0x48
        : 0x40;

    if (!DataAttr || !DataAttr->IsNonResident ||
        DataAttr->Length < MinimumHeader ||
        DataAttr->NonResident.DataRunsOffset < MinimumHeader ||
        DataAttr->NonResident.DataRunsOffset >= DataAttr->Length)
    {
        return NULL;
    }

    DataRunPtr = (PUCHAR)DataAttr + DataAttr->NonResident.DataRunsOffset;
    DataRunEnd = (PUCHAR)DataAttr + DataAttr->Length;

    while (DataRunPtr < DataRunEnd && *DataRunPtr != 0)
    {
        UINT8 LengthSize = *DataRunPtr & 0x0f;
        UINT8 OffsetSize = *DataRunPtr >> 4;
        ULONGLONG RunLength = 0;
        ULONGLONG RunLCN = 0;
        BOOLEAN IsSparse = OffsetSize == 0;
        PDataRun Run;

        if (LengthSize == 0 ||
            LengthSize > sizeof(ULONGLONG) ||
            OffsetSize > sizeof(LONGLONG) ||
            (SIZE_T)(DataRunEnd - DataRunPtr) <
                (SIZE_T)(1 + LengthSize + OffsetSize))
        {
            goto Corrupt;
        }

        RtlCopyMemory(&RunLength, DataRunPtr + 1, LengthSize);
        /*
         * Lengths share the signed variable-length encoding used by LCN
         * deltas; Windows and NTFS-3G treat a stored negative as corrupt.
         */
        if (LONGLONG_SIGN_EXTEND((LONGLONG)RunLength, LengthSize) <= 0 ||
            LogicalClusters > MaximumValue - RunLength)
        {
            goto Corrupt;
        }

        if (!IsSparse)
        {
            LONGLONG Delta = 0;

            RtlCopyMemory(&Delta,
                          DataRunPtr + 1 + LengthSize,
                          OffsetSize);
            Delta = LONGLONG_SIGN_EXTEND(Delta, OffsetSize);

            if ((Delta < 0 &&
                 PreviousLCN < (ULONGLONG)(-(Delta + 1)) + 1) ||
                (Delta >= 0 &&
                 PreviousLCN > MaximumValue - (ULONGLONG)Delta))
            {
                goto Corrupt;
            }

            RunLCN = Delta < 0
                ? PreviousLCN - ((ULONGLONG)(-(Delta + 1)) + 1)
                : PreviousLCN + (ULONGLONG)Delta;
            if (RunLCN >= DiskVolume->ClustersInVolume ||
                RunLength > DiskVolume->ClustersInVolume - RunLCN)
            {
                goto Corrupt;
            }
            PreviousLCN = RunLCN;
        }

        Run = new(PagedPool, TAG_DATA_RUN) DataRun();
        if (!Run)
            goto Corrupt;
        Run->NextRun = NULL;
        Run->LCN = RunLCN;
        Run->Length = RunLength;
        Run->IsSparse = IsSparse;

        if (Tail)
            Tail->NextRun = Run;
        else
            Head = Run;
        Tail = Run;

        LogicalClusters += RunLength;
        DataRunPtr += 1 + LengthSize + OffsetSize;
    }

    if (DataRunPtr >= DataRunEnd ||
        DataAttr->NonResident.LastVCN <
            DataAttr->NonResident.FirstVCN ||
        LogicalClusters !=
            DataAttr->NonResident.LastVCN -
            DataAttr->NonResident.FirstVCN + 1)
    {
        goto Corrupt;
    }

    return Head;

Corrupt:
    FreeDataRun(Head);
    return NULL;
}

PDataRun
FileRecord::BuildCompositeDataRuns(_In_ PAttribute Attribute)
{
    PDataRun Head = NULL;
    PDataRun Tail = NULL;
    PUCHAR ListData;
    ULONG ListLength;
    ULONG Offset = 0;
    ULONGLONG ExpectedVCN = 0;
    BOOLEAN FoundSegment = FALSE;
    NTSTATUS Status;

    /*
     * An extension record does not contain the base record's
     * $ATTRIBUTE_LIST. Delegate reconstruction to the base object that loaded
     * this extent so later mutations see every continuation segment rather
     * than silently decoding only VCN zero.
     */
    if (Header &&
        Header->BaseFileRecord != 0 &&
        BaseRecordOwner)
    {
        return BaseRecordOwner->
            BuildCompositeDataRuns(Attribute);
    }

    if (!Attribute ||
        !Attribute->IsNonResident ||
        !IsAttributeNameValid(Attribute) ||
        Attribute->NonResident.FirstVCN != 0 ||
        Attribute->NonResident.InitalizedDataSize >
            Attribute->NonResident.DataSize ||
        Attribute->NonResident.DataSize >
            Attribute->NonResident.AllocatedSize)
    {
        return NULL;
    }

    /*
     * Reading a nonresident $ATTRIBUTE_LIST may itself enter this cache.
     * Decode that stream directly; other stream extents are described by the
     * list once it has been read.
     */
    if (Attribute->AttributeType == TypeAttributeList)
    {
        ULONGLONG ClusterSize = BytesPerCluster(DiskVolume);
        ULONGLONG AllocatedClusters;

        /*
         * NTFS requires the mapping pairs for $ATTRIBUTE_LIST itself to fit
         * in its base MFT record. It may be nonresident, but it cannot use
         * continuation attribute extents because those extents could only be
         * located by first reading the list. Enforce complete coverage here
         * instead of trying to bootstrap an impossible recursive layout.
         */
        if (Attribute->Flags != 0 ||
            ClusterSize == 0 ||
            Attribute->NonResident.AllocatedSize % ClusterSize != 0 ||
            Attribute->NonResident.LastVCN == ~(ULONGLONG)0)
        {
            return NULL;
        }
        AllocatedClusters =
            Attribute->NonResident.AllocatedSize / ClusterSize;
        if (Attribute->NonResident.LastVCN + 1 !=
            AllocatedClusters)
        {
            return NULL;
        }
        return DecodeDataRuns(Attribute);
    }

    Status = LoadAttributeList();
    if (Status == STATUS_NOT_FOUND)
    {
        if (Attribute->NonResident.FirstVCN != 0)
            return NULL;
        return DecodeDataRuns(Attribute);
    }
    if (!NT_SUCCESS(Status) ||
        !AttributeListData ||
        AttributeListLength == 0)
    {
        return NULL;
    }

    ListData = AttributeListData;
    ListLength = AttributeListLength;

    while (Offset < ListLength)
    {
        PAttributeListEx ListEntry =
            (PAttributeListEx)(ListData + Offset);
        ULONG Remaining = ListLength - Offset;

        if (!IsAttributeListEntryValid(ListEntry, Remaining))
            goto Corrupt;

        if (ListEntry->Type == Attribute->AttributeType &&
            AttributeNameMatchesListEntry(Attribute, ListEntry))
        {
            ULONGLONG FileRecordNumber;
            FileRecord* TargetRecord;
            PAttribute Segment;
            PDataRun SegmentRuns;
            PDataRun SegmentTail;

            if (ListEntry->FirstVCN != ExpectedVCN)
                goto Corrupt;

            FileRecordNumber = GetFRNFromFileRef(ListEntry->BaseFileRef);
            TargetRecord = FileRecordNumber == Header->MFTRecordNumber
                ? this
                : GetExtentRecord(ListEntry->BaseFileRef);
            if (!TargetRecord)
                goto Corrupt;

            Segment = TargetRecord->FindAttributeInRecord(
                (AttributeType)ListEntry->Type,
                NULL,
                &ListEntry->AttributeId);
            if (!Segment ||
                !Segment->IsNonResident ||
                (Segment->Flags != Attribute->Flags &&
                 (Segment == Attribute || Segment->Flags != 0)) ||
                !AttributesHaveSameName(Attribute, Segment) ||
                Segment->NonResident.FirstVCN != ListEntry->FirstVCN ||
                Segment->NonResident.LastVCN <
                    Segment->NonResident.FirstVCN)
            {
                goto Corrupt;
            }

            if (!FoundSegment && Segment != Attribute)
                goto Corrupt;

            SegmentRuns = DecodeDataRuns(Segment);
            if (!SegmentRuns)
                goto Corrupt;

            SegmentTail = SegmentRuns;
            while (SegmentTail->NextRun)
                SegmentTail = SegmentTail->NextRun;

            if (Tail)
                Tail->NextRun = SegmentRuns;
            else
                Head = SegmentRuns;
            Tail = SegmentTail;

            if (Segment->NonResident.LastVCN == ~(ULONGLONG)0)
                goto Corrupt;
            ExpectedVCN = Segment->NonResident.LastVCN + 1;
            FoundSegment = TRUE;
        }

        Offset += ListEntry->RecordLength;
    }

    if (!FoundSegment)
        goto Corrupt;

    {
        ULONGLONG ClusterSize = BytesPerCluster(DiskVolume);
        ULONGLONG RequiredClusters;

        if (ClusterSize == 0)
            goto Corrupt;

        RequiredClusters =
            Attribute->NonResident.DataSize / ClusterSize;
        if (Attribute->NonResident.DataSize % ClusterSize)
            RequiredClusters++;
        if (ExpectedVCN < RequiredClusters)
            goto Corrupt;
    }

    return Head;

Corrupt:
    FreeDataRun(Head);
    return NULL;
}

PDataRun
FileRecord::FindNonResidentData(_In_ PAttribute Attribute)
{
    return BuildCompositeDataRuns(Attribute);
}

PDataRun
FileRecord::GetCachedDataRuns(_In_ PAttribute Attribute)
{
    PDataRunCacheEntry Entry;

    for (Entry = DataRunCache; Entry; Entry = Entry->Next)
    {
        if (Entry->Attribute == Attribute)
            return Entry->Runs;
    }

    Entry = new(PagedPool, TAG_DATA_RUN) DataRunCacheEntry();
    if (!Entry)
        return NULL;

    Entry->Runs = BuildCompositeDataRuns(Attribute);
    if (!Entry->Runs)
    {
        delete Entry;
        return NULL;
    }

    Entry->Attribute = Attribute;
    Entry->Next = DataRunCache;
    DataRunCache = Entry;
    return Entry->Runs;
}

void
FileRecord::ClearDataRunCache()
{
    while (DataRunCache)
    {
        PDataRunCacheEntry Entry = DataRunCache;
        DataRunCache = Entry->Next;
        FreeDataRun(Entry->Runs);
        delete Entry;
    }
}
