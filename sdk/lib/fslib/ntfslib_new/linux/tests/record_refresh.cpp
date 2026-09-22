#include <cstdio>
#include <cstring>

#include <ntfslib_new.h>
#include <ntfslib_new_internal.h>

static void
InitializeRecord(FileRecord& Record, const char* Value)
{
    std::memset(Record.Data, 0, 1024);
    std::memcpy(Record.Header->Header.TypeID, "FILE", 4);
    Record.Header->AttributeOffset = 0x38;
    Record.Header->ActualSize = 0x60;
    Record.Header->AllocatedSize = 1024;
    Record.Header->MFTRecordNumber = 42;
    Record.Header->SequenceNumber = 7;

    PAttribute Attribute = reinterpret_cast<PAttribute>(Record.Data + 0x38);
    Attribute->AttributeType = TypeData;
    Attribute->Length = 0x20;
    Attribute->Resident.DataLength = 4;
    Attribute->Resident.DataOffset = 0x18;
    std::memcpy(GetResidentDataPointer(Attribute), Value, 4);
    *reinterpret_cast<PULONG>(Record.Data + 0x58) = TypeAttributeEndMarker;
}

static bool
Check(bool Condition, const char* Message)
{
    if (!Condition)
        std::fprintf(stderr, "record_refresh: %s\n", Message);
    return Condition;
}

int
main()
{
    FileRecord Shared(nullptr, 1024);
    FileRecord Fresh(nullptr, 1024);
    FileRecord* PagingHandle = &Shared;
    PUCHAR OriginalBuffer = Shared.Data;
    UCHAR Snapshot[1024];

    InitializeRecord(Shared, "old!");
    InitializeRecord(Fresh, "new!");
    Shared.SetAutomaticTimestampMask(0);

    if (!Check(NT_SUCCESS(Shared.RefreshFrom(Fresh)), "refresh failed") ||
        !Check(Shared.Data == OriginalBuffer, "refresh replaced the shared buffer") ||
        !Check(Shared.GetAutomaticTimestampMask() == 0, "refresh reset timestamp policy"))
    {
        return 1;
    }

    PAttribute Attribute = PagingHandle->GetAttribute(TypeData, nullptr);
    if (!Check(Attribute && std::memcmp(GetResidentDataPointer(Attribute), "new!", 4) == 0,
               "existing handle retained stale metadata") ||
        !Check(NT_SUCCESS(Shared.RefreshFrom(Shared)), "self refresh failed"))
    {
        return 1;
    }

    std::memcpy(Snapshot, Shared.Data, sizeof(Snapshot));
    Fresh.Header->SequenceNumber++;
    if (!Check(Shared.RefreshFrom(Fresh) == STATUS_INVALID_PARAMETER,
               "refresh accepted a reused MFT slot") ||
        !Check(std::memcmp(Snapshot, Shared.Data, sizeof(Snapshot)) == 0,
               "failed refresh modified the shared record"))
    {
        return 1;
    }

    Fresh.Header->SequenceNumber--;
    Fresh.Header->MFTRecordNumber++;
    if (!Check(Shared.RefreshFrom(Fresh) == STATUS_INVALID_PARAMETER,
               "refresh accepted a different file") ||
        !Check(std::memcmp(Snapshot, Shared.Data, sizeof(Snapshot)) == 0,
               "different-file refresh modified the shared record"))
    {
        return 1;
    }

    return 0;
}
