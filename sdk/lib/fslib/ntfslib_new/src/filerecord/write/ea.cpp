/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Atomic native NTFS extended-attribute list updates
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define NTFS_MAX_PACKED_EA_SIZE ((ULONG)0xfffb)

typedef struct _EA_BUILD_ENTRY
{
    UINT8 Flags;
    UINT8 NameLength;
    UINT16 ValueLength;
    const UCHAR* Name;
    const UCHAR* Value;
    BOOLEAN Active;
} EA_BUILD_ENTRY, *PEA_BUILD_ENTRY;

static UCHAR
UpperEaCharacter(_In_ UCHAR Character)
{
    if (Character >= 'a' && Character <= 'z')
        return Character - ('a' - 'A');
    return Character;
}

static BOOLEAN
EaNamesEqual(_In_ const UCHAR* Left,
             _In_ UINT8 LeftLength,
             _In_ const UCHAR* Right,
             _In_ UINT8 RightLength)
{
    if (LeftLength != RightLength)
        return FALSE;

    for (ULONG Index = 0; Index < LeftLength; Index++)
    {
        if (UpperEaCharacter(Left[Index]) !=
            UpperEaCharacter(Right[Index]))
        {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOLEAN
IsEaNameCharacterValid(_In_ UCHAR Character)
{
    if (Character < 0x20 || Character > 0x7e)
        return FALSE;

    switch (Character)
    {
        case '\\':
        case '/':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
        case ',':
        case '+':
        case '=':
        case '[':
        case ']':
        case ';':
            return FALSE;

        default:
            return TRUE;
    }
}

static NTSTATUS
ValidateEaUpdates(
    _In_reads_(UpdateCount)
        const NtfsExtendedAttributeUpdate* Updates,
    _In_ ULONG UpdateCount)
{
    if (UpdateCount != 0 && !Updates)
        return STATUS_INVALID_PARAMETER;

    for (ULONG UpdateIndex = 0;
         UpdateIndex < UpdateCount;
         UpdateIndex++)
    {
        const NtfsExtendedAttributeUpdate* Update =
            &Updates[UpdateIndex];

        if (Update->Operation != NTFS_EA_UPDATE_SET &&
            Update->Operation != NTFS_EA_UPDATE_REMOVE)
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (!Update->Name ||
            Update->NameLength == 0 ||
            Update->NameLength == 0xff)
        {
            return STATUS_INVALID_EA_NAME;
        }
        for (ULONG NameIndex = 0;
             NameIndex < Update->NameLength;
             NameIndex++)
        {
            if (!IsEaNameCharacterValid(
                    Update->Name[NameIndex]))
            {
                return STATUS_INVALID_EA_NAME;
            }
        }

        if (Update->Operation == NTFS_EA_UPDATE_SET)
        {
            if (Update->Flags &
                    ~NTFS_EA_FLAG_NEED_EA ||
                (Update->ValueLength != 0 &&
                 !Update->Value))
            {
                return STATUS_INVALID_EA_NAME;
            }
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
LoadExistingExtendedAttributes(
    _In_ PFileRecord File,
    _Outptr_result_bytebuffer_(*RawLength)
        PUCHAR* RawData,
    _Out_ PULONG RawLength,
    _Out_ PEAInformationEx Information,
    _Out_ PULONG EntryCount)
{
    NtfsExtendedAttributeView View;
    PUCHAR Buffer = NULL;
    ULONG Length = 0;
    ULONG Offset = 0;
    ULONG Count = 0;
    NTSTATUS Status;

    if (!File || !RawData || !RawLength ||
        !Information || !EntryCount)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *RawData = NULL;
    *RawLength = 0;
    *EntryCount = 0;
    RtlZeroMemory(Information, sizeof(*Information));

    Status = File->ReadExtendedAttributes(NULL,
                                          &Length,
                                          Information);
    if (Status == STATUS_NO_EAS_ON_FILE)
        return STATUS_SUCCESS;
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return Status;
    if (Length == 0)
        return STATUS_EA_CORRUPT_ERROR;

    Buffer = new(PagedPool, TAG_NTFS) UCHAR[Length];
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = File->ReadExtendedAttributes(Buffer,
                                          &Length,
                                          Information);
    if (!NT_SUCCESS(Status))
        goto Done;

    while (Offset < Length)
    {
        Status = NtfsGetNextExtendedAttribute(Buffer,
                                               Length,
                                               &Offset,
                                               &View);
        if (!NT_SUCCESS(Status))
            goto Done;
        if (Count == MAXULONG)
        {
            Status = STATUS_EA_CORRUPT_ERROR;
            goto Done;
        }
        Count++;
    }
    if (Offset != Length || Count == 0)
    {
        Status = STATUS_EA_CORRUPT_ERROR;
        goto Done;
    }

    *RawData = Buffer;
    *RawLength = Length;
    *EntryCount = Count;
    Buffer = NULL;
    Status = STATUS_SUCCESS;

Done:
    delete[] Buffer;
    return Status;
}

static ULONG
FindEaBuildEntry(_In_ PEA_BUILD_ENTRY Entries,
                 _In_ ULONG EntryCount,
                 _In_ const UCHAR* Name,
                 _In_ UINT8 NameLength)
{
    for (ULONG Index = 0; Index < EntryCount; Index++)
    {
        if (Entries[Index].Active &&
            EaNamesEqual(Entries[Index].Name,
                         Entries[Index].NameLength,
                         Name,
                         NameLength))
        {
            return Index;
        }
    }
    return MAXULONG;
}

static NTSTATUS
PopulateEaBuildEntries(
    _In_opt_ const UCHAR* ExistingData,
    _In_ ULONG ExistingLength,
    _In_ ULONG ExistingCount,
    _In_reads_(UpdateCount)
        const NtfsExtendedAttributeUpdate* Updates,
    _In_ ULONG UpdateCount,
    _Out_ PEA_BUILD_ENTRY Entries,
    _Out_ PULONG EntryCount)
{
    NtfsExtendedAttributeView View;
    ULONG Count = 0;
    ULONG ExistingOffset = 0;
    ULONG Match;
    NTSTATUS Status;

    if (!Entries || !EntryCount)
        return STATUS_INVALID_PARAMETER;
    *EntryCount = 0;

    while (ExistingOffset < ExistingLength)
    {
        Status = NtfsGetNextExtendedAttribute(
            ExistingData,
            ExistingLength,
            &ExistingOffset,
            &View);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Count >= ExistingCount ||
            FindEaBuildEntry(Entries,
                             Count,
                             View.Name,
                             View.NameLength) != MAXULONG)
        {
            return STATUS_EA_CORRUPT_ERROR;
        }

        Entries[Count].Flags = View.Flags;
        Entries[Count].NameLength = View.NameLength;
        Entries[Count].ValueLength = View.ValueLength;
        Entries[Count].Name = View.Name;
        Entries[Count].Value = View.Value;
        Entries[Count].Active = TRUE;
        Count++;
    }
    if (Count != ExistingCount)
        return STATUS_EA_CORRUPT_ERROR;

    /*
     * Windows applies a FILE_FULL_EA_INFORMATION list in order: remove the
     * old name, then add the supplied nonempty value. Keeping that ordering
     * also gives duplicate names in one batch their native "last wins"
     * behavior.
     */
    for (ULONG UpdateIndex = 0;
         UpdateIndex < UpdateCount;
         UpdateIndex++)
    {
        const NtfsExtendedAttributeUpdate* Update =
            &Updates[UpdateIndex];

        Match = FindEaBuildEntry(Entries,
                                 Count,
                                 Update->Name,
                                 Update->NameLength);
        if (Update->Operation == NTFS_EA_UPDATE_REMOVE)
        {
            if (Match != MAXULONG)
                Entries[Match].Active = FALSE;
            continue;
        }

        if (Match == MAXULONG)
        {
            Match = Count++;
            Entries[Match].Name = Update->Name;
            Entries[Match].NameLength =
                Update->NameLength;
            Entries[Match].Active = TRUE;
        }
        Entries[Match].Flags = Update->Flags;
        Entries[Match].ValueLength =
            Update->ValueLength;
        Entries[Match].Value = Update->Value;
    }

    *EntryCount = Count;
    return STATUS_SUCCESS;
}

static NTSTATUS
BuildRawExtendedAttributes(
    _In_ PEA_BUILD_ENTRY Entries,
    _In_ ULONG EntryCount,
    _Outptr_result_bytebuffer_(*RawLength)
        PUCHAR* RawData,
    _Out_ PULONG RawLength,
    _Out_ PEAInformationEx Information)
{
    EAEx Header;
    PUCHAR Buffer = NULL;
    ULONG EntryLength;
    ULONG NeedEaCount = 0;
    ULONG Offset = 0;
    ULONG PackedEntryLength;
    ULONG PackedLength = 0;
    ULONG RawSize = 0;

    if (!Entries || !RawData || !RawLength ||
        !Information)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *RawData = NULL;
    *RawLength = 0;
    RtlZeroMemory(Information, sizeof(*Information));

    for (ULONG Index = 0; Index < EntryCount; Index++)
    {
        if (!Entries[Index].Active)
            continue;

        PackedEntryLength =
            5UL +
            Entries[Index].NameLength +
            Entries[Index].ValueLength;
        if (PackedEntryLength >
                NTFS_MAX_PACKED_EA_SIZE ||
            PackedLength >
                NTFS_MAX_PACKED_EA_SIZE -
                    PackedEntryLength)
        {
            return STATUS_EA_TOO_LARGE;
        }
        PackedLength += PackedEntryLength;

        EntryLength = ALIGN_UP_BY(
            sizeof(EAEx) +
                Entries[Index].NameLength +
                1UL +
                Entries[Index].ValueLength,
            sizeof(ULONG));
        if (EntryLength >
                MAXULONG - RawSize)
        {
            return STATUS_EA_TOO_LARGE;
        }
        RawSize += EntryLength;

        if (Entries[Index].Flags &
            NTFS_EA_FLAG_NEED_EA)
        {
            if (NeedEaCount == MAXUSHORT)
                return STATUS_EA_TOO_LARGE;
            NeedEaCount++;
        }
    }

    if (RawSize == 0)
        return STATUS_SUCCESS;

    Buffer = new(PagedPool, TAG_NTFS) UCHAR[RawSize];
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Buffer, RawSize);

    for (ULONG Index = 0; Index < EntryCount; Index++)
    {
        if (!Entries[Index].Active)
            continue;

        EntryLength = ALIGN_UP_BY(
            sizeof(EAEx) +
                Entries[Index].NameLength +
                1UL +
                Entries[Index].ValueLength,
            sizeof(ULONG));
        RtlZeroMemory(&Header, sizeof(Header));
        Header.OffsetNextEA = EntryLength;
        Header.Flags = Entries[Index].Flags;
        Header.NameLength =
            Entries[Index].NameLength;
        Header.ValueLength =
            Entries[Index].ValueLength;
        RtlCopyMemory(Buffer + Offset,
                      &Header,
                      sizeof(Header));
        RtlCopyMemory(Buffer + Offset + sizeof(Header),
                      Entries[Index].Name,
                      Entries[Index].NameLength);
        if (Entries[Index].ValueLength != 0)
        {
            RtlCopyMemory(
                Buffer + Offset + sizeof(Header) +
                    Entries[Index].NameLength + 1,
                Entries[Index].Value,
                Entries[Index].ValueLength);
        }
        Offset += EntryLength;
    }

    if (Offset != RawSize)
    {
        delete[] Buffer;
        return STATUS_EA_CORRUPT_ERROR;
    }

    Information->PackedEASize =
        (USHORT)PackedLength;
    Information->NumEAWithNEED_EA =
        (USHORT)NeedEaCount;
    Information->UnpackedEASize = RawSize;
    *RawData = Buffer;
    *RawLength = RawSize;
    return STATUS_SUCCESS;
}

static BOOLEAN
ExtendedAttributesAreIdentical(
    _In_opt_ const UCHAR* LeftData,
    _In_ ULONG LeftLength,
    _In_ const EAInformationEx* LeftInformation,
    _In_opt_ const UCHAR* RightData,
    _In_ ULONG RightLength,
    _In_ const EAInformationEx* RightInformation)
{
    if (LeftLength != RightLength ||
        RtlCompareMemory(LeftInformation,
                         RightInformation,
                         sizeof(*LeftInformation)) !=
            sizeof(*LeftInformation))
    {
        return FALSE;
    }
    return LeftLength == 0 ||
           RtlCompareMemory(LeftData,
                            RightData,
                            LeftLength) == LeftLength;
}

static BOOLEAN
IsEaAttributeListEntryValid(
    _In_ PAttributeListEx Entry,
    _In_ ULONG Remaining)
{
    const ULONG MinimumEntrySize = 0x1a;
    ULONG NameBytes;

    if (!Entry ||
        Remaining < MinimumEntrySize ||
        Entry->RecordLength < MinimumEntrySize ||
        (Entry->RecordLength &
         (sizeof(ULONGLONG) - 1)) != 0 ||
        Entry->RecordLength > Remaining)
    {
        return FALSE;
    }

    NameBytes =
        Entry->NameLength * sizeof(WCHAR);
    return Entry->NameLength == 0
        ? Entry->NameOffset == 0 ||
          (Entry->NameOffset >= MinimumEntrySize &&
           Entry->NameOffset <= Entry->RecordLength)
        : (Entry->NameOffset >= MinimumEntrySize &&
           Entry->NameOffset <= Entry->RecordLength &&
           NameBytes <=
               (ULONG)Entry->RecordLength -
                   (ULONG)Entry->NameOffset);
}

static BOOLEAN
EaExtensionRecordIsEmpty(
    _In_ PFileRecord Record)
{
    PAttribute Attribute;

    if (!Record || !Record->Data ||
        !Record->Header ||
        Record->Header->ActualSize < sizeof(ULONG) ||
        Record->Header->AttributeOffset >
            Record->Header->ActualSize - sizeof(ULONG))
    {
        return FALSE;
    }

    Attribute =
        reinterpret_cast<PAttribute>(
            Record->Data +
                Record->Header->AttributeOffset);
    return Attribute->AttributeType ==
        TypeAttributeEndMarker;
}

NTSTATUS
FileRecord::ReplaceListedExtendedAttributes(
    _In_opt_ PAttribute InformationAttribute,
    _In_opt_ PAttribute EaAttribute,
    _In_ const EAInformationEx* FinalInformation,
    _In_reads_bytes_(FinalLength)
        const UCHAR* FinalData,
    _In_ ULONG FinalLength)
{
    const ULONG NewEntryLength =
        ALIGN_UP_BY(0x1a,
                    sizeof(ULONGLONG));
    PAttribute ListAttribute = NULL;
    PAttribute NewEaAttribute = NULL;
    PAttribute NewInformationAttribute = NULL;
    PAttribute OldAttribute;
    PAttribute StandardAttribute;
    PDataRun NewEaRuns = NULL;
    PDataRun OldEaRuns = NULL;
    PFileRecord EaOwner;
    PFileRecord InformationOwner;
    PFileRecord NewOwner = NULL;
    PFileRecord OldOwner = NULL;
    PStandardInformationEx Standard;
    PUCHAR BaseRecordBackup = NULL;
    PUCHAR NewList = NULL;
    PUCHAR OldList = NULL;
    PUCHAR OldOwnerBackup = NULL;
    ULONGLONG BaseFileReference;
    ULONGLONG NewOwnerReference = 0;
    ULONGLONG OldOwnerReference = 0;
    ULONG EaInsertionOffset;
    ULONG EaListOffset = MAXULONG;
    ULONG InformationInsertionOffset;
    ULONG InformationListOffset = MAXULONG;
    ULONG NewListLength;
    ULONG OldListLength = 0;
    ULONG Offset;
    ULONG PreviousType = 0;
    ULONG RestoreLength;
    ULONG WrittenLength;
    USHORT NewEaId = 0;
    USHORT NewInformationId = 0;
    USHORT OldEaId = 0;
    USHORT OldInformationId = 0;
    BOOLEAN BaseWriteAttempted = FALSE;
    BOOLEAN HavePreviousType = FALSE;
    BOOLEAN ListWriteAttempted = FALSE;
    BOOLEAN MetadataRestored = TRUE;
    BOOLEAN NewOwnerPublished = FALSE;
    BOOLEAN OldOwnerCommitted = FALSE;
    NTSTATUS CleanupStatus;
    NTSTATUS RestoreStatus;
    NTSTATUS Status;

    if (!FinalInformation ||
        (!FinalData && FinalLength != 0) ||
        !DiskVolume || !Header ||
        Header->BaseFileRecord != 0 ||
        Header->SequenceNumber == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!!InformationAttribute != !!EaAttribute)
        return STATUS_EA_CORRUPT_ERROR;

    if (EaAttribute)
    {
        InformationOwner =
            GetAttributeOwner(
                InformationAttribute);
        EaOwner =
            GetAttributeOwner(EaAttribute);
        if (!InformationOwner || !EaOwner)
            return STATUS_FILE_CORRUPT_ERROR;
        if (InformationOwner != EaOwner)
            return STATUS_NOT_IMPLEMENTED;
        OldOwner = EaOwner;

        Status = InformationOwner->
            ValidateResidentAttributeForUpdate(
                InformationAttribute,
                NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        if (InformationAttribute->AttributeType !=
                TypeEAInformation ||
            InformationAttribute->NameLength != 0 ||
            InformationAttribute->Flags != 0 ||
            EaAttribute->AttributeType != TypeEA ||
            EaAttribute->NameLength != 0 ||
            EaAttribute->Flags != 0)
        {
            return STATUS_EA_CORRUPT_ERROR;
        }
        Status = EaOwner->ValidateAttributeForUpdate(
            EaAttribute,
            !!EaAttribute->IsNonResident,
            NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        if (EaAttribute->IsNonResident)
        {
            if (EaAttribute->NonResident.FirstVCN != 0)
                return STATUS_NOT_IMPLEMENTED;
            OldEaRuns =
                FindNonResidentData(EaAttribute);
            if (!OldEaRuns)
                return STATUS_EA_CORRUPT_ERROR;
        }

        OldInformationId =
            InformationAttribute->AttributeID;
        OldEaId = EaAttribute->AttributeID;
        OldOwnerReference =
            ((ULONGLONG)OldOwner->
                Header->SequenceNumber << 48) |
            OldOwner->Header->MFTRecordNumber;
    }
    else if (FinalLength == 0)
    {
        return STATUS_SUCCESS;
    }

    Status = LoadAttributeList();
    if (!NT_SUCCESS(Status) ||
        !AttributeListData ||
        AttributeListLength == 0)
    {
        Status = NT_SUCCESS(Status)
            ? STATUS_FILE_CORRUPT_ERROR
            : Status;
        goto Done;
    }
    ListAttribute = FindAttributeInRecord(
        TypeAttributeList,
        NULL,
        NULL);
    if (!ListAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    OldListLength = AttributeListLength;

    InformationInsertionOffset =
        AttributeListLength;
    EaInsertionOffset =
        AttributeListLength;
    Offset = 0;
    while (Offset < AttributeListLength)
    {
        PAttributeListEx Entry =
            reinterpret_cast<PAttributeListEx>(
                AttributeListData + Offset);
        ULONG Remaining =
            AttributeListLength - Offset;

        if (!IsEaAttributeListEntryValid(
                Entry,
                Remaining) ||
            (HavePreviousType &&
             Entry->Type < PreviousType))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        HavePreviousType = TRUE;
        PreviousType = Entry->Type;

        if (!InformationAttribute &&
            InformationInsertionOffset ==
                AttributeListLength &&
            Entry->Type >
                (ULONG)TypeEAInformation)
        {
            InformationInsertionOffset = Offset;
        }
        if (!InformationAttribute &&
            EaInsertionOffset ==
                AttributeListLength &&
            Entry->Type > (ULONG)TypeEA)
        {
            EaInsertionOffset = Offset;
        }

        if (Entry->Type ==
                (ULONG)TypeEAInformation ||
            Entry->Type == (ULONG)TypeEA)
        {
            if (Entry->NameLength != 0 ||
                Entry->FirstVCN != 0)
            {
                Status = STATUS_NOT_IMPLEMENTED;
                goto Done;
            }

            if (Entry->Type ==
                    (ULONG)TypeEAInformation)
            {
                if (InformationListOffset !=
                    MAXULONG)
                {
                    Status =
                        STATUS_FILE_CORRUPT_ERROR;
                    goto Done;
                }
                InformationListOffset = Offset;
                if (InformationAttribute &&
                    (Entry->BaseFileRef !=
                         OldOwnerReference ||
                     Entry->AttributeId !=
                         OldInformationId))
                {
                    Status =
                        STATUS_FILE_CORRUPT_ERROR;
                    goto Done;
                }
            }
            else
            {
                if (EaListOffset != MAXULONG)
                {
                    Status =
                        STATUS_FILE_CORRUPT_ERROR;
                    goto Done;
                }
                EaListOffset = Offset;
                if (EaAttribute &&
                    (Entry->BaseFileRef !=
                         OldOwnerReference ||
                     Entry->AttributeId !=
                         OldEaId))
                {
                    Status =
                        STATUS_FILE_CORRUPT_ERROR;
                    goto Done;
                }
            }
        }

        Offset += Entry->RecordLength;
    }
    if (Offset != AttributeListLength)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    if (InformationAttribute)
    {
        if (InformationListOffset == MAXULONG ||
            EaListOffset == MAXULONG)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
    }
    else if (InformationListOffset != MAXULONG ||
             EaListOffset != MAXULONG)
    {
        Status = STATUS_EA_CORRUPT_ERROR;
        goto Done;
    }

    if (FinalLength == 0)
    {
        PAttributeListEx InformationEntry =
            reinterpret_cast<PAttributeListEx>(
                AttributeListData +
                    InformationListOffset);
        PAttributeListEx EaEntry =
            reinterpret_cast<PAttributeListEx>(
                AttributeListData +
                    EaListOffset);

        /*
         * The unnamed $EA pair normally terminates a file's sorted list.
         * Truncating that suffix publishes removal in one MFT update. Rare
         * $PROPERTY_SET/$LOGGED_UTILITY_STREAM layouts need a general
         * copy-on-write list shrink and remain deliberately rejected.
         */
        if (InformationListOffset >
                EaListOffset ||
            InformationListOffset +
                InformationEntry->RecordLength !=
                EaListOffset ||
            EaListOffset +
                EaEntry->RecordLength !=
                AttributeListLength)
        {
            Status = STATUS_NOT_IMPLEMENTED;
            goto Done;
        }
        NewListLength = InformationListOffset;
    }
    else if (InformationAttribute)
    {
        NewListLength = AttributeListLength;
    }
    else
    {
        if (AttributeListLength >
            MAXULONG - 2 * NewEntryLength)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }
        NewListLength =
            AttributeListLength +
            2 * NewEntryLength;
    }

    OldList =
        new(PagedPool, TAG_NTFS)
            UCHAR[AttributeListLength];
    BaseRecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!OldList || !BaseRecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(OldList,
                  AttributeListData,
                  AttributeListLength);
    RtlCopyMemory(BaseRecordBackup,
                  Data,
                  RecordBufferSize);

    BaseFileReference =
        ((ULONGLONG)Header->SequenceNumber << 48) |
        Header->MFTRecordNumber;
    if (FinalLength != 0)
    {
        Status = DiskVolume->MFT->
            AllocateExtensionFileRecord(
                BaseFileReference,
                &NewOwner);
        if (!NT_SUCCESS(Status))
            goto RestoreBase;
        Status = NewOwner->
            SetAutomaticTimestampMask(0);
        if (!NT_SUCCESS(Status))
            goto RestoreBase;

        Status = NewOwner->InsertResidentAttribute(
            TypeEAInformation,
            NULL,
            &NewInformationAttribute);
        if (!NT_SUCCESS(Status))
            goto RestoreBase;
        Status = NewOwner->ReplaceResidentData(
            NewInformationAttribute,
            reinterpret_cast<const UCHAR*>(
                FinalInformation),
            sizeof(*FinalInformation));
        if (!NT_SUCCESS(Status))
            goto RestoreBase;

        Status = NewOwner->InsertResidentAttribute(
            TypeEA,
            NULL,
            &NewEaAttribute);
        if (!NT_SUCCESS(Status))
            goto RestoreBase;
        Status = NewOwner->ReplaceResidentData(
            NewEaAttribute,
            FinalData,
            FinalLength);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            Status = NewOwner->PromoteResidentData(
                NewEaAttribute,
                const_cast<PUCHAR>(FinalData),
                FinalLength,
                0,
                FinalLength,
                FinalLength);
            if (!NT_SUCCESS(Status))
                goto RestoreBase;
            NewEaAttribute =
                NewOwner->FindAttributeInRecord(
                    TypeEA,
                    NULL,
                    NULL);
            if (!NewEaAttribute ||
                !NewEaAttribute->IsNonResident)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto RestoreBase;
            }
            NewEaRuns =
                NewOwner->FindNonResidentData(
                    NewEaAttribute);
            if (!NewEaRuns)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto RestoreBase;
            }
        }
        else if (NT_SUCCESS(Status))
        {
            Status = DiskVolume->MFT->
                WriteFileRecordToMFT(NewOwner);
            if (!NT_SUCCESS(Status))
                goto RestoreBase;
        }
        else
        {
            goto RestoreBase;
        }

        NewInformationAttribute =
            NewOwner->FindAttributeInRecord(
                TypeEAInformation,
                NULL,
                NULL);
        NewEaAttribute =
            NewOwner->FindAttributeInRecord(
                TypeEA,
                NULL,
                NULL);
        if (!NewInformationAttribute ||
            !NewEaAttribute)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto RestoreBase;
        }
        NewInformationId =
            NewInformationAttribute->AttributeID;
        NewEaId = NewEaAttribute->AttributeID;
        NewOwnerReference =
            ((ULONGLONG)NewOwner->
                Header->SequenceNumber << 48) |
            NewOwner->Header->MFTRecordNumber;

        NewList =
            new(PagedPool, TAG_NTFS)
                UCHAR[NewListLength];
        if (!NewList)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto RestoreBase;
        }

        if (InformationAttribute)
        {
            PAttributeListEx NewEntry;

            RtlCopyMemory(NewList,
                          OldList,
                          NewListLength);
            NewEntry =
                reinterpret_cast<PAttributeListEx>(
                    NewList +
                        InformationListOffset);
            NewEntry->BaseFileRef =
                NewOwnerReference;
            NewEntry->AttributeId =
                NewInformationId;
            NewEntry =
                reinterpret_cast<PAttributeListEx>(
                    NewList + EaListOffset);
            NewEntry->BaseFileRef =
                NewOwnerReference;
            NewEntry->AttributeId = NewEaId;
        }
        else
        {
            PAttributeListEx NewEntry;
            ULONG NewEaOffset =
                EaInsertionOffset +
                NewEntryLength;

            if (InformationInsertionOffset >
                    EaInsertionOffset)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto RestoreBase;
            }
            RtlCopyMemory(
                NewList,
                OldList,
                InformationInsertionOffset);
            RtlZeroMemory(
                NewList +
                    InformationInsertionOffset,
                NewEntryLength);
            NewEntry =
                reinterpret_cast<PAttributeListEx>(
                    NewList +
                        InformationInsertionOffset);
            NewEntry->Type =
                TypeEAInformation;
            NewEntry->RecordLength =
                (USHORT)NewEntryLength;
            NewEntry->FirstVCN = 0;
            NewEntry->BaseFileRef =
                NewOwnerReference;
            NewEntry->AttributeId =
                NewInformationId;

            RtlCopyMemory(
                NewList +
                    InformationInsertionOffset +
                    NewEntryLength,
                OldList +
                    InformationInsertionOffset,
                EaInsertionOffset -
                    InformationInsertionOffset);
            RtlZeroMemory(
                NewList + NewEaOffset,
                NewEntryLength);
            NewEntry =
                reinterpret_cast<PAttributeListEx>(
                    NewList + NewEaOffset);
            NewEntry->Type = TypeEA;
            NewEntry->RecordLength =
                (USHORT)NewEntryLength;
            NewEntry->FirstVCN = 0;
            NewEntry->BaseFileRef =
                NewOwnerReference;
            NewEntry->AttributeId = NewEaId;
            RtlCopyMemory(
                NewList + NewEaOffset +
                    NewEntryLength,
                OldList + EaInsertionOffset,
                AttributeListLength -
                    EaInsertionOffset);
        }
    }

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        goto RestoreBase;
    UNREFERENCED_PARAMETER(StandardAttribute);
    Standard->FilePermissions |= FILE_PERM_ARCHIVE;

    Status = SynchronizeFileNameInformation(
        NTFS_FILE_NAME_UPDATE_EA_SIZE |
            NTFS_FILE_NAME_UPDATE_ARCHIVE,
        0,
        0,
        FinalInformation->PackedEASize,
        0,
        0);
    if (!NT_SUCCESS(Status))
        goto RestoreBase;
    Status = PrepareAutomaticTimestamps(
        NTFS_BASIC_INFO_CHANGE_TIME,
        NULL);
    if (!NT_SUCCESS(Status))
        goto RestoreBase;

    if (FinalLength != 0)
    {
        LARGE_INTEGER ListOffset = {};

        WrittenLength = NewListLength;
        ListWriteAttempted = TRUE;
        Status = WriteFileData(
            TypeAttributeList,
            NULL,
            NewList,
            &WrittenLength,
            &ListOffset);
        if (!NT_SUCCESS(Status) ||
            WrittenLength != NewListLength)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_END_OF_FILE;
            goto RestoreBase;
        }
    }
    else
    {
        if (ListAttribute->IsNonResident)
        {
            Status = ValidateAttributeForUpdate(
                ListAttribute,
                TRUE,
                NULL);
            if (!NT_SUCCESS(Status) ||
                ListAttribute->
                    NonResident.FirstVCN != 0 ||
                ListAttribute->
                    NonResident.DataSize !=
                        AttributeListLength ||
                ListAttribute->
                    NonResident.InitalizedDataSize <
                        AttributeListLength)
            {
                if (NT_SUCCESS(Status))
                    Status =
                        STATUS_FILE_CORRUPT_ERROR;
                goto RestoreBase;
            }
            ListAttribute->
                NonResident.DataSize =
                    NewListLength;
            ListAttribute->
                NonResident.InitalizedDataSize =
                    NewListLength;
        }
        else
        {
            Status = ResizeResidentData(
                ListAttribute,
                NewListLength);
            if (!NT_SUCCESS(Status))
                goto RestoreBase;
        }

        BaseWriteAttempted = TRUE;
        Status = DiskVolume->MFT->
            WriteFileRecordToMFT(this);
        if (!NT_SUCCESS(Status))
            goto RestoreBase;
    }

    NewOwnerPublished = TRUE;
    delete[] AttributeListData;
    AttributeListData = NULL;
    AttributeListLength = 0;

    /*
     * The replacement list is now authoritative. Retiring the old pair is
     * post-publication cleanup: failure can leak unreachable metadata but
     * must not roll back a valid new layout without LFS.
     */
    if (OldOwner)
    {
        OldOwnerBackup =
            new(PagedPool, TAG_FILE_RECORD)
                UCHAR[OldOwner->RecordBufferSize];
        if (OldOwnerBackup)
        {
            RtlCopyMemory(
                OldOwnerBackup,
                OldOwner->Data,
                OldOwner->RecordBufferSize);
            OldAttribute =
                OldOwner->FindAttributeInRecord(
                    TypeEA,
                    NULL,
                    &OldEaId);
            if (OldAttribute &&
                NT_SUCCESS(
                    OldOwner->RemoveAttributeRecord(
                        OldAttribute)))
            {
                OldAttribute =
                    OldOwner->
                        FindAttributeInRecord(
                            TypeEAInformation,
                            NULL,
                            &OldInformationId);
                if (OldAttribute &&
                    NT_SUCCESS(
                        OldOwner->
                            RemoveAttributeRecord(
                                OldAttribute)) &&
                    NT_SUCCESS(
                        DiskVolume->MFT->
                            WriteFileRecordToMFT(
                                OldOwner)))
                {
                    OldOwnerCommitted = TRUE;
                }
            }
            if (!OldOwnerCommitted)
            {
                RtlCopyMemory(
                    OldOwner->Data,
                    OldOwnerBackup,
                    OldOwner->RecordBufferSize);
                OldOwner->Header =
                    reinterpret_cast<
                        PFileRecordHeader>(
                            OldOwner->Data);
                OldOwner->ClearDataRunCache();
            }
        }

        if (OldOwnerCommitted)
        {
            if (OldEaRuns)
                (void)DiskVolume->
                    ReleaseClusters(OldEaRuns);
            if (OldOwner != this &&
                EaExtensionRecordIsEmpty(
                    OldOwner))
            {
                (void)DiskVolume->MFT->
                    DeallocateExtensionFileRecord(
                        OldOwner);
            }
        }
    }

    ClearDataRunCache();
    ClearExtentCache();
    Status = STATUS_SUCCESS;
    goto Done;

RestoreBase:
    RtlCopyMemory(Data,
                  BaseRecordBackup,
                  RecordBufferSize);
    Header =
        reinterpret_cast<PFileRecordHeader>(Data);
    ClearDataRunCache();
    delete[] AttributeListData;
    AttributeListData = NULL;
    AttributeListLength = 0;

    if (ListWriteAttempted)
    {
        LARGE_INTEGER ListOffset = {};

        /*
         * Restore the original bytes first, then the backed-up MFT record so
         * a failed growing write cannot leave a larger logical list behind.
         */
        RestoreLength = OldListLength;
        if (!OldList || RestoreLength == 0)
        {
            RestoreStatus =
                STATUS_FILE_CORRUPT_ERROR;
        }
        else
        {
            RestoreStatus = WriteFileData(
                TypeAttributeList,
                NULL,
                OldList,
                &RestoreLength,
                &ListOffset);
        }
        RtlCopyMemory(Data,
                      BaseRecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(
                Data);
        ClearDataRunCache();
        delete[] AttributeListData;
        AttributeListData = NULL;
        AttributeListLength = 0;
        if (NT_SUCCESS(RestoreStatus) &&
            RestoreLength == OldListLength)
        {
            RestoreStatus = DiskVolume->MFT->
                WriteFileRecordToMFT(this);
        }
        else if (NT_SUCCESS(RestoreStatus))
        {
            RestoreStatus = STATUS_END_OF_FILE;
        }
        if (!NT_SUCCESS(RestoreStatus))
            MetadataRestored = FALSE;
    }
    else if (BaseWriteAttempted)
    {
        RestoreStatus = DiskVolume->MFT->
            WriteFileRecordToMFT(this);
        if (!NT_SUCCESS(RestoreStatus))
            MetadataRestored = FALSE;
    }

    if (NewOwner &&
        !NewOwnerPublished &&
        MetadataRestored)
    {
        CleanupStatus = DiskVolume->MFT->
            DeallocateExtensionFileRecord(
                NewOwner);
        if (NewEaRuns &&
            NT_SUCCESS(CleanupStatus))
        {
            (void)DiskVolume->
                ReleaseClusters(NewEaRuns);
        }
    }

Done:
    FreeDataRun(NewEaRuns);
    FreeDataRun(OldEaRuns);
    delete NewOwner;
    delete[] OldOwnerBackup;
    delete[] NewList;
    delete[] OldList;
    delete[] BaseRecordBackup;
    return Status;
}

NTSTATUS
FileRecord::UpdateExtendedAttributes(
    _In_reads_(UpdateCount)
        const NtfsExtendedAttributeUpdate* Updates,
    _In_ ULONG UpdateCount)
{
    PEA_BUILD_ENTRY Entries = NULL;
    PDataRun OldEaRuns = NULL;
    PAttribute EaAttribute;
    PAttribute InformationAttribute;
    PAttribute StandardAttribute;
    PStandardInformationEx Standard;
    PUCHAR ExistingData = NULL;
    PUCHAR FinalData = NULL;
    PUCHAR RecordBackup = NULL;
    EAInformationEx ExistingInformation;
    EAInformationEx FinalInformation;
    ULONG BuildEntryCount = 0;
    ULONG EntryCapacity;
    ULONG ExistingCount = 0;
    ULONG ExistingLength = 0;
    ULONG FinalLength = 0;
    BOOLEAN Committed = FALSE;
    BOOLEAN HasAttributeList;
    BOOLEAN Promote = FALSE;
    NTSTATUS Status;

    Status = ValidateEaUpdates(Updates,
                               UpdateCount);
    if (!NT_SUCCESS(Status) || UpdateCount == 0)
        return Status;
    if (!DiskVolume)
        return STATUS_INVALID_DEVICE_STATE;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    HasAttributeList =
        FindAttributeInRecord(TypeAttributeList,
                              NULL,
                              NULL) != NULL;

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(StandardAttribute);
    Status = LoadExistingExtendedAttributes(
        this,
        &ExistingData,
        &ExistingLength,
        &ExistingInformation,
        &ExistingCount);
    if (!NT_SUCCESS(Status))
        goto Done;

    if (ExistingCount > MAXULONG - UpdateCount)
    {
        Status = STATUS_EA_TOO_LARGE;
        goto Done;
    }
    EntryCapacity = ExistingCount + UpdateCount;
    if ((SIZE_T)EntryCapacity >
        (~(SIZE_T)0) / sizeof(*Entries))
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    Entries = new(PagedPool, TAG_NTFS)
        EA_BUILD_ENTRY[EntryCapacity];
    if (!Entries)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlZeroMemory(Entries,
                  sizeof(*Entries) * EntryCapacity);

    Status = PopulateEaBuildEntries(
        ExistingData,
        ExistingLength,
        ExistingCount,
        Updates,
        UpdateCount,
        Entries,
        &BuildEntryCount);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = BuildRawExtendedAttributes(
        Entries,
        BuildEntryCount,
        &FinalData,
        &FinalLength,
        &FinalInformation);
    if (!NT_SUCCESS(Status))
        goto Done;

    if (ExtendedAttributesAreIdentical(
            ExistingData,
            ExistingLength,
            &ExistingInformation,
            FinalData,
            FinalLength,
            &FinalInformation))
    {
        Status = STATUS_SUCCESS;
        goto Done;
    }

    InformationAttribute =
        GetAttribute(TypeEAInformation,
                     NULL);
    EaAttribute = GetAttribute(TypeEA,
                               NULL);
    if (!!InformationAttribute != !!EaAttribute ||
        (!!EaAttribute != (ExistingLength != 0)))
    {
        Status = STATUS_EA_CORRUPT_ERROR;
        goto Done;
    }
    if (HasAttributeList)
    {
        Status = ReplaceListedExtendedAttributes(
            InformationAttribute,
            EaAttribute,
            &FinalInformation,
            FinalData,
            FinalLength);
        goto Done;
    }
    if (EaAttribute)
    {
        if (InformationAttribute->NameLength != 0 ||
            InformationAttribute->Flags != 0 ||
            InformationAttribute->IsNonResident ||
            EaAttribute->NameLength != 0 ||
            EaAttribute->Flags != 0 ||
            GetAttributeOwner(InformationAttribute) !=
                this ||
            GetAttributeOwner(EaAttribute) != this)
        {
            Status = STATUS_NOT_IMPLEMENTED;
            goto Done;
        }
        if (EaAttribute->IsNonResident)
        {
            if (EaAttribute->NonResident.FirstVCN != 0)
            {
                Status = STATUS_NOT_IMPLEMENTED;
                goto Done;
            }
            OldEaRuns = FindNonResidentData(EaAttribute);
            if (!OldEaRuns)
            {
                Status = STATUS_EA_CORRUPT_ERROR;
                goto Done;
            }
        }
    }

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);

    if (EaAttribute)
    {
        Status = RemoveAttributeRecord(EaAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;
        InformationAttribute =
            FindAttributeInRecord(TypeEAInformation,
                                  NULL,
                                  NULL);
        if (!InformationAttribute)
        {
            Status = STATUS_EA_CORRUPT_ERROR;
            goto Restore;
        }
        Status =
            RemoveAttributeRecord(InformationAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }

    if (FinalLength != 0)
    {
        Status = InsertResidentAttribute(
            TypeEAInformation,
            NULL,
            &InformationAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;
        Status = ReplaceResidentData(
            InformationAttribute,
            reinterpret_cast<const UCHAR*>(
                &FinalInformation),
            sizeof(FinalInformation));
        if (!NT_SUCCESS(Status))
            goto Restore;

        Status = InsertResidentAttribute(TypeEA,
                                         NULL,
                                         &EaAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;
        Status = ReplaceResidentData(EaAttribute,
                                     FinalData,
                                     FinalLength);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            Promote = TRUE;
            Status = STATUS_SUCCESS;
        }
        else if (!NT_SUCCESS(Status))
        {
            goto Restore;
        }
    }

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        goto Restore;
    UNREFERENCED_PARAMETER(StandardAttribute);
    Standard->FilePermissions |= FILE_PERM_ARCHIVE;

    Status = SynchronizeFileNameInformation(
        NTFS_FILE_NAME_UPDATE_EA_SIZE |
            NTFS_FILE_NAME_UPDATE_ARCHIVE,
        0,
        0,
        FinalInformation.PackedEASize,
        0,
        0);
    if (!NT_SUCCESS(Status))
        goto Restore;

    if (Promote)
    {
        Status = PromoteResidentData(
            EaAttribute,
            FinalData,
            FinalLength,
            0,
            FinalLength,
            FinalLength);
        if (!NT_SUCCESS(Status))
            goto Restore;
        Committed = TRUE;
    }
    else
    {
        Status = PrepareAutomaticTimestamps(
            NTFS_BASIC_INFO_CHANGE_TIME,
            NULL);
        if (!NT_SUCCESS(Status))
            goto Restore;
        Status = DiskVolume->MFT->
            WriteFileRecordToMFT(this);
        if (!NT_SUCCESS(Status))
            goto Restore;
        Committed = TRUE;
    }

    if (OldEaRuns)
        Status = DiskVolume->ReleaseClusters(OldEaRuns);
    goto Done;

Restore:
    if (!Committed)
    {
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }

Done:
    FreeDataRun(OldEaRuns);
    delete[] RecordBackup;
    delete[] FinalData;
    delete[] Entries;
    delete[] ExistingData;
    return Status;
}
