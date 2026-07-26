/*
 * Focused in-memory tests for resident NTFS attribute repacking.
 */

#include <cstdio>
#include <cstring>

#include <ntfslib_new.h>
#include <ntfslib_new_internal.h>

static bool
Check(bool Condition, const char* Message)
{
    if (!Condition)
        std::fprintf(stderr, "resident_update: %s\n", Message);
    return Condition;
}

static PAttribute
InitializeRecord(FileRecord& Record)
{
    PAttribute Target;
    PAttribute Tail;
    PULONG EndMarker;

    std::memset(Record.Data, 0, 1024);
    std::memcpy(Record.Header->Header.TypeID, "FILE", 4);
    Record.Header->AttributeOffset = 0x38;
    Record.Header->ActualSize = 0x80;
    Record.Header->AllocatedSize = 1024;

    Target = reinterpret_cast<PAttribute>(
        Record.Data + Record.Header->AttributeOffset);
    Target->AttributeType = TypeData;
    Target->Length = 0x20;
    Target->Resident.DataLength = 5;
    Target->Resident.DataOffset = 0x18;
    std::memcpy(GetResidentDataPointer(Target), "hello", 5);

    Tail = reinterpret_cast<PAttribute>(
        reinterpret_cast<PUCHAR>(Target) + Target->Length);
    Tail->AttributeType = TypeEA;
    Tail->Length = 0x20;
    Tail->Resident.DataLength = 4;
    Tail->Resident.DataOffset = 0x18;
    std::memcpy(GetResidentDataPointer(Tail), "tail", 4);

    EndMarker = reinterpret_cast<PULONG>(
        reinterpret_cast<PUCHAR>(Tail) + Tail->Length);
    *EndMarker = TypeAttributeEndMarker;
    return Target;
}

static PAttribute
GetTailAttribute(PAttribute Target)
{
    return reinterpret_cast<PAttribute>(
        reinterpret_cast<PUCHAR>(Target) + Target->Length);
}

int
main()
{
    FileRecord Record(nullptr, 1024);
    PAttribute Target = InitializeRecord(Record);
    PAttribute Tail;
    UCHAR Snapshot[1024];
    ULONG Length;
    NTSTATUS Status;

    Length = 2;
    Status = Record.UpdateResidentData(
        Target,
        reinterpret_cast<PUCHAR>(const_cast<char*>("XY")),
        &Length,
        1);
    if (!Check(Status == STATUS_SUCCESS, "in-place update failed") ||
        !Check(Target->Resident.DataLength == 5,
               "in-place update changed the value length") ||
        !Check(std::memcmp(GetResidentDataPointer(Target),
                           "hXYlo",
                           5) == 0,
               "in-place update produced the wrong value"))
    {
        return 1;
    }

    Length = 3;
    Status = Record.UpdateResidentData(
        Target,
        reinterpret_cast<PUCHAR>(const_cast<char*>("XYZ")),
        &Length,
        8);
    Tail = GetTailAttribute(Target);
    if (!Check(Status == STATUS_SUCCESS, "resident growth failed") ||
        !Check(Target->Length == 0x28,
               "resident growth produced the wrong aligned record size") ||
        !Check(Target->Resident.DataLength == 11,
               "resident growth produced the wrong value length") ||
        !Check(Record.Header->ActualSize == 0x88,
               "resident growth produced the wrong file-record size") ||
        !Check(std::memcmp(GetResidentDataPointer(Target),
                           "hXYlo\0\0\0XYZ",
                           11) == 0,
               "resident growth did not zero the write gap") ||
        !Check(Tail->AttributeType == TypeEA &&
               std::memcmp(GetResidentDataPointer(Tail),
                           "tail",
                           4) == 0,
               "resident growth damaged the following attribute"))
    {
        return 1;
    }

    Status = Record.ReplaceResidentData(
        Target,
        reinterpret_cast<const UCHAR*>("Q"),
        1);
    Tail = GetTailAttribute(Target);
    if (!Check(Status == STATUS_SUCCESS, "resident replacement failed") ||
        !Check(Target->Length == 0x20,
               "resident replacement did not compact the attribute") ||
        !Check(Target->Resident.DataLength == 1,
               "resident replacement produced the wrong value length") ||
        !Check(Record.Header->ActualSize == 0x80,
               "resident replacement did not compact the file record") ||
        !Check(*GetResidentDataPointer(Target) == 'Q',
               "resident replacement produced the wrong value") ||
        !Check(Tail->AttributeType == TypeEA &&
               std::memcmp(GetResidentDataPointer(Tail),
                           "tail",
                           4) == 0,
               "resident replacement damaged the following attribute"))
    {
        return 1;
    }

    std::memcpy(Snapshot, Record.Data, sizeof(Snapshot));
    Status = Record.ReplaceResidentData(Target, Snapshot, 1000);
    if (!Check(Status == STATUS_BUFFER_TOO_SMALL,
               "oversized resident value returned the wrong status") ||
        !Check(std::memcmp(Snapshot,
                           Record.Data,
                           sizeof(Snapshot)) == 0,
               "failed resident growth modified the file record"))
    {
        return 1;
    }

    std::memcpy(Snapshot, Record.Data, sizeof(Snapshot));
    Length = 2;
    Status = Record.UpdateResidentData(
        Target,
        reinterpret_cast<PUCHAR>(const_cast<char*>("NO")),
        &Length,
        MAXULONG);
    if (!Check(Status == STATUS_FILE_TOO_LARGE,
               "overflowing resident write returned the wrong status") ||
        !Check(std::memcmp(Snapshot,
                           Record.Data,
                           sizeof(Snapshot)) == 0,
               "overflowing resident write modified the file record"))
    {
        return 1;
    }

    Status = Record.ReplaceResidentData(Target, nullptr, 0);
    Tail = GetTailAttribute(Target);
    if (!Check(Status == STATUS_SUCCESS, "empty replacement failed") ||
        !Check(Target->Length == 0x18 &&
               Target->Resident.DataLength == 0,
               "empty replacement did not produce a header-only value") ||
        !Check(Record.Header->ActualSize == 0x78,
               "empty replacement produced the wrong file-record size") ||
        !Check(Tail->AttributeType == TypeEA &&
               std::memcmp(GetResidentDataPointer(Tail),
                           "tail",
                           4) == 0,
               "empty replacement damaged the following attribute"))
    {
        return 1;
    }

    return 0;
}
