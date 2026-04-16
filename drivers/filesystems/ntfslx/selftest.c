/*
 * PROJECT:     ReactOS NTFS driver (ntfslx)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     RAM-only self-test. Exercises internal helpers against an
 *              in-memory 32 MB buffer, no storage device, no drive letter.
 *
 * Triggered via IOCTL_NTFSLX_SELFTEST on the control device.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

#define STLOG(fmt, ...) DbgPrint("NTFSLX-SELFTEST: " fmt, ##__VA_ARGS__)

#define ST_FAIL(Code) \
    do { \
        Result->FailedChecks++; \
        if (Result->FirstFailCode == 0) Result->FirstFailCode = (Code); \
    } while (0)

#define ST_CHECK(Cond, Code, fmt, ...) \
    do { \
        Result->TotalChecks++; \
        if (Cond) { \
            Result->PassedChecks++; \
            STLOG("OK   [%04lx] " fmt "\n", (ULONG)(Code), ##__VA_ARGS__); \
        } else { \
            ST_FAIL(Code); \
            STLOG("FAIL [%04lx] " fmt "\n", (ULONG)(Code), ##__VA_ARGS__); \
        } \
    } while (0)

/*
 * Scratch MFT record template: valid NTFSLX_MFT_RECORD header with an END
 * attribute, suitable for feeding to NtfslxInsertAttributeRecord.
 */
static VOID
InitBlankMftRecord(
    _Out_writes_bytes_(RecordSize) PNTFSLX_MFT_RECORD Record,
    _In_ ULONG RecordSize,
    _In_ ULONGLONG MftIndex,
    _In_ BOOLEAN IsDirectory)
{
    ULONG AttributesOffset;
    PNTFSLX_ATTR_RECORD EndAttr;

    RtlZeroMemory(Record, RecordSize);
    Record->Ntfs.Magic = NTFSLX_RECORD_MAGIC_FILE;
    Record->Ntfs.UsaOffset = (USHORT)FIELD_OFFSET(NTFSLX_MFT_RECORD, Reserved);
    Record->Ntfs.UsaCount = 1 + (USHORT)(RecordSize / 512);
    Record->Lsn = 0;
    Record->SequenceNumber = 1;
    Record->LinkCount = 1;

    /* Place attributes after the MFT record header + USA area, rounded up. */
    AttributesOffset = ROUND_UP(sizeof(NTFSLX_MFT_RECORD) +
                                (ULONG)Record->Ntfs.UsaCount * sizeof(USHORT), 8);
    Record->AttributesOffset = (USHORT)AttributesOffset;
    Record->Flags = NTFSLX_MFT_RECORD_IN_USE |
                    (IsDirectory ? NTFSLX_MFT_RECORD_IS_DIRECTORY : 0);
    Record->BytesAllocated = RecordSize;
    Record->MftRecordNumber = (ULONG)MftIndex;
    Record->NextAttributeInstance = 0;

    /* Initial END attribute just after AttributesOffset. */
    EndAttr = (PNTFSLX_ATTR_RECORD)((PUCHAR)Record + AttributesOffset);
    EndAttr->Type = NTFSLX_ATTRIBUTE_END;
    EndAttr->Length = 0;
    Record->BytesInUse = AttributesOffset + sizeof(ULONG) * 2;
}

/*
 * Walk an MFT record's attribute chain, count attributes, verify headers.
 */
static ULONG
CountAttributes(
    _In_ PNTFSLX_MFT_RECORD Record,
    _Out_opt_ PBOOLEAN Corrupt)
{
    PUCHAR Base = (PUCHAR)Record;
    ULONG Offset = Record->AttributesOffset;
    ULONG Count = 0;

    if (Corrupt) *Corrupt = FALSE;

    while (Offset + sizeof(ULONG) * 2 <= Record->BytesInUse)
    {
        PNTFSLX_ATTR_RECORD Attr = (PNTFSLX_ATTR_RECORD)(Base + Offset);

        if (Attr->Type == NTFSLX_ATTRIBUTE_END)
            return Count;

        if (Attr->Length == 0 || Offset + Attr->Length > Record->BytesInUse)
        {
            if (Corrupt) *Corrupt = TRUE;
            return Count;
        }

        Count++;
        Offset += Attr->Length;
    }

    if (Corrupt) *Corrupt = TRUE;
    return Count;
}

/* ----------------------------------------------------------------------
 * Test A: attribute insert / resize / remove cycle.
 *
 * Builds a blank 1024-byte MFT record, inserts $STANDARD_INFORMATION,
 * $FILE_NAME, and $DATA attributes, then grows $DATA, shrinks it, and
 * removes it. After each step the attribute chain must remain parseable
 * with the expected number of attributes.
 * ---------------------------------------------------------------------- */
static VOID
TestAttribLifecycle(
    _Inout_ PNTFSLX_SELFTEST_RESULT Result)
{
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_ATTR_RECORD Attr;
    NTFSLX_STANDARD_INFORMATION StdInfo;
    ULONG FnAttrValue[32];
    PNTFSLX_FILE_NAME_ATTRIBUTE Fn;
    NTSTATUS Status;
    BOOLEAN Corrupt;
    ULONG Count;

    STLOG("--- section A: attribute lifecycle ---\n");

    Record = ExAllocatePoolWithTag(NonPagedPool, 1024, 'TTSN');
    if (Record == NULL)
    {
        ST_FAIL(0xA000);
        return;
    }

    InitBlankMftRecord(Record, 1024, 16, FALSE);

    /* Insert $STANDARD_INFORMATION */
    RtlZeroMemory(&StdInfo, sizeof(StdInfo));
    StdInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    Status = NtfslxInsertAttributeRecord(Record, 1024,
                                         NTFSLX_ATTRIBUTE_STANDARD_INFORMATION,
                                         NULL, 0,
                                         &StdInfo, sizeof(StdInfo),
                                         NULL);
    ST_CHECK(NT_SUCCESS(Status), 0xA001, "insert $STANDARD_INFORMATION 0x%08lx", Status);

    Count = CountAttributes(Record, &Corrupt);
    ST_CHECK(Count == 1 && !Corrupt, 0xA002, "after $SI: count=%lu corrupt=%u", Count, Corrupt);

    /* Insert $FILE_NAME */
    RtlZeroMemory(FnAttrValue, sizeof(FnAttrValue));
    Fn = (PNTFSLX_FILE_NAME_ATTRIBUTE)FnAttrValue;
    Fn->ParentDirectory = MK_MREF(NTFSLX_FILE_ROOT, 0);
    Fn->FileAttributes = FILE_ATTRIBUTE_NORMAL;
    Fn->FileNameLength = 4;
    Fn->FileNameType = NTFSLX_FILE_NAME_WIN32_AND_DOS;
    ((PWCHAR)((PUCHAR)Fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)))[0] = L'T';
    ((PWCHAR)((PUCHAR)Fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)))[1] = L'e';
    ((PWCHAR)((PUCHAR)Fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)))[2] = L's';
    ((PWCHAR)((PUCHAR)Fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)))[3] = L't';
    Status = NtfslxInsertAttributeRecord(Record, 1024,
                                         NTFSLX_ATTRIBUTE_FILE_NAME,
                                         NULL, 0,
                                         FnAttrValue,
                                         sizeof(NTFSLX_FILE_NAME_ATTRIBUTE) + 4 * sizeof(WCHAR),
                                         NULL);
    ST_CHECK(NT_SUCCESS(Status), 0xA003, "insert $FILE_NAME 0x%08lx", Status);

    Count = CountAttributes(Record, &Corrupt);
    ST_CHECK(Count == 2 && !Corrupt, 0xA004, "after $FN: count=%lu corrupt=%u", Count, Corrupt);

    /* Insert empty $DATA, capture the attribute pointer. */
    Status = NtfslxInsertAttributeRecord(Record, 1024,
                                         NTFSLX_ATTRIBUTE_DATA,
                                         NULL, 0,
                                         NULL, 0,
                                         &Attr);
    ST_CHECK(NT_SUCCESS(Status) && Attr != NULL, 0xA005,
             "insert $DATA 0x%08lx attr=%p", Status, Attr);

    Count = CountAttributes(Record, &Corrupt);
    ST_CHECK(Count == 3 && !Corrupt, 0xA006, "after $DATA: count=%lu corrupt=%u", Count, Corrupt);

    /* Grow $DATA to 256 bytes value length. */
    if (Attr != NULL)
    {
        ULONG OldLength = Attr->Length;
        ULONG NewLength = ROUND_UP(Attr->Data.Resident.ValueOffset + 256, 8);

        Status = NtfslxResizeAttributeRecord(Record, Attr, NewLength);
        ST_CHECK(NT_SUCCESS(Status), 0xA007, "grow $DATA %lu->%lu 0x%08lx",
                 OldLength, NewLength, Status);
        Attr->Data.Resident.ValueLength = 256;

        Count = CountAttributes(Record, &Corrupt);
        ST_CHECK(Count == 3 && !Corrupt, 0xA008, "after grow: count=%lu corrupt=%u",
                 Count, Corrupt);

        /* Shrink back to 8 bytes. */
        NewLength = ROUND_UP(Attr->Data.Resident.ValueOffset + 8, 8);
        Status = NtfslxResizeAttributeRecord(Record, Attr, NewLength);
        ST_CHECK(NT_SUCCESS(Status), 0xA009, "shrink $DATA 0x%08lx", Status);

        Count = CountAttributes(Record, &Corrupt);
        ST_CHECK(Count == 3 && !Corrupt, 0xA00A, "after shrink: count=%lu corrupt=%u",
                 Count, Corrupt);

        /* Remove the $DATA attribute. */
        Status = NtfslxRemoveAttributeRecord(Record, Attr);
        ST_CHECK(NT_SUCCESS(Status), 0xA00B, "remove $DATA 0x%08lx", Status);

        Count = CountAttributes(Record, &Corrupt);
        ST_CHECK(Count == 2 && !Corrupt, 0xA00C, "after remove: count=%lu corrupt=%u",
                 Count, Corrupt);
    }

    ExFreePoolWithTag(Record, 'TTSN');
}

/* ----------------------------------------------------------------------
 * Test B: FCB header layout (catches the offset-8 resource crash).
 *
 * Verifies that NTFSLX_FILE_CONTEXT starts with an FSRTL_COMMON_FCB_HEADER
 * and that Resource / PagingIoResource are NULL after zero-init.
 * ---------------------------------------------------------------------- */
static VOID
TestFcbHeaderLayout(
    _Inout_ PNTFSLX_SELFTEST_RESULT Result)
{
    NTFSLX_FILE_CONTEXT Ctx;

    STLOG("--- section B: FCB header layout ---\n");

    RtlZeroMemory(&Ctx, sizeof(Ctx));
    Ctx.FcbHeader.NodeTypeCode = 0x0502;
    Ctx.FcbHeader.NodeByteSize = sizeof(Ctx);

    /* The kernel's fast-I/O path dereferences offset 8 as PERESOURCE.
     * Verify Resource is at offset 8 and NULL, and that our Signature
     * lives after the header rather than at offset 0. */
    ST_CHECK(FIELD_OFFSET(NTFSLX_FILE_CONTEXT, FcbHeader) == 0,
             0xB001, "FcbHeader offset = %lu (expect 0)",
             (ULONG)FIELD_OFFSET(NTFSLX_FILE_CONTEXT, FcbHeader));

    ST_CHECK(FIELD_OFFSET(FSRTL_COMMON_FCB_HEADER, Resource) == 8,
             0xB002, "Resource offset in FSRTL header = %lu (expect 8)",
             (ULONG)FIELD_OFFSET(FSRTL_COMMON_FCB_HEADER, Resource));

    ST_CHECK(Ctx.FcbHeader.Resource == NULL,
             0xB003, "FcbHeader.Resource=%p (expect NULL)",
             Ctx.FcbHeader.Resource);

    ST_CHECK(Ctx.FcbHeader.PagingIoResource == NULL,
             0xB004, "FcbHeader.PagingIoResource=%p (expect NULL)",
             Ctx.FcbHeader.PagingIoResource);

    ST_CHECK(FIELD_OFFSET(NTFSLX_FILE_CONTEXT, Signature) >= sizeof(FSRTL_COMMON_FCB_HEADER),
             0xB005, "Signature offset=%lu (must be >= %lu)",
             (ULONG)FIELD_OFFSET(NTFSLX_FILE_CONTEXT, Signature),
             (ULONG)sizeof(FSRTL_COMMON_FCB_HEADER));
}

/* ----------------------------------------------------------------------
 * Test C: Attribute ordering.
 *
 * Inserts attributes out of type-code order and verifies that the final
 * record has them in ascending type order (a canonical NTFS invariant).
 * ---------------------------------------------------------------------- */
static VOID
TestAttribOrdering(
    _Inout_ PNTFSLX_SELFTEST_RESULT Result)
{
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_ATTR_RECORD Attr;
    PUCHAR Base;
    ULONG Offset;
    ULONG Prev;
    BOOLEAN Ordered = TRUE;
    NTFSLX_STANDARD_INFORMATION StdInfo;
    NTSTATUS Status;

    STLOG("--- section C: attribute ordering ---\n");

    Record = ExAllocatePoolWithTag(NonPagedPool, 1024, 'TTSN');
    if (Record == NULL)
    {
        ST_FAIL(0xC000);
        return;
    }

    InitBlankMftRecord(Record, 1024, 16, FALSE);

    /* Insert in reverse type order: $DATA (0x80), $FILE_NAME (0x30), $SI (0x10). */
    Status = NtfslxInsertAttributeRecord(Record, 1024,
                                         NTFSLX_ATTRIBUTE_DATA,
                                         NULL, 0, NULL, 0, NULL);
    ST_CHECK(NT_SUCCESS(Status), 0xC001, "insert $DATA 0x%08lx", Status);

    {
        ULONG FnValue[8] = { 0 };
        PNTFSLX_FILE_NAME_ATTRIBUTE F = (PNTFSLX_FILE_NAME_ATTRIBUTE)FnValue;
        F->ParentDirectory = MK_MREF(NTFSLX_FILE_ROOT, 0);
        F->FileAttributes = FILE_ATTRIBUTE_NORMAL;
        F->FileNameLength = 1;
        F->FileNameType = NTFSLX_FILE_NAME_WIN32_AND_DOS;
        ((PWCHAR)((PUCHAR)F + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)))[0] = L'X';
        Status = NtfslxInsertAttributeRecord(Record, 1024,
                                             NTFSLX_ATTRIBUTE_FILE_NAME,
                                             NULL, 0,
                                             FnValue,
                                             sizeof(NTFSLX_FILE_NAME_ATTRIBUTE) + sizeof(WCHAR),
                                             NULL);
    }
    ST_CHECK(NT_SUCCESS(Status), 0xC002, "insert $FILE_NAME 0x%08lx", Status);

    RtlZeroMemory(&StdInfo, sizeof(StdInfo));
    Status = NtfslxInsertAttributeRecord(Record, 1024,
                                         NTFSLX_ATTRIBUTE_STANDARD_INFORMATION,
                                         NULL, 0,
                                         &StdInfo, sizeof(StdInfo),
                                         NULL);
    ST_CHECK(NT_SUCCESS(Status), 0xC003, "insert $STANDARD_INFORMATION 0x%08lx", Status);

    /* Walk and verify ascending type order. */
    Base = (PUCHAR)Record;
    Offset = Record->AttributesOffset;
    Prev = 0;
    while (Offset + sizeof(ULONG) * 2 <= Record->BytesInUse)
    {
        Attr = (PNTFSLX_ATTR_RECORD)(Base + Offset);
        if (Attr->Type == NTFSLX_ATTRIBUTE_END) break;
        if (Attr->Type < Prev) { Ordered = FALSE; break; }
        Prev = Attr->Type;
        if (Attr->Length == 0) break;
        Offset += Attr->Length;
    }
    ST_CHECK(Ordered, 0xC004, "attribute ordering after reverse insert");

    ExFreePoolWithTag(Record, 'TTSN');
}

/* ----------------------------------------------------------------------
 * Test D: 32 MB scratch-buffer allocation sanity.
 *
 * Confirms the driver can take a 32 MB NonPagedPool allocation, write
 * patterns into it, and read them back. This is the "RAM buffer works"
 * precondition for more elaborate layered tests in a future pass.
 * ---------------------------------------------------------------------- */
static VOID
TestScratchBuffer(
    _Inout_ PNTFSLX_SELFTEST_RESULT Result)
{
    PUCHAR Buf;
    ULONG I;
    BOOLEAN AllMatch = TRUE;
    const ULONG Size = NTFSLX_SELFTEST_BUFFER_SIZE;
    const ULONG Probe = 4096;

    STLOG("--- section D: %lu MB scratch buffer ---\n", Size / (1024 * 1024));

    Buf = ExAllocatePoolWithTag(NonPagedPool, Size, 'TTSN');
    ST_CHECK(Buf != NULL, 0xD001, "alloc %lu MB NonPagedPool", Size / (1024 * 1024));
    if (Buf == NULL) return;

    /* Write a probe pattern every 4 KB across the buffer. */
    for (I = 0; I < Size; I += Probe)
    {
        Buf[I] = (UCHAR)(I >> 12);
        Buf[I + 1] = (UCHAR)(I >> 20);
    }

    /* Read back and verify. */
    for (I = 0; I < Size; I += Probe)
    {
        if (Buf[I] != (UCHAR)(I >> 12) || Buf[I + 1] != (UCHAR)(I >> 20))
        {
            AllMatch = FALSE;
            break;
        }
    }
    ST_CHECK(AllMatch, 0xD002, "readback of %lu MB", Size / (1024 * 1024));

    ExFreePoolWithTag(Buf, 'TTSN');
}

NTSTATUS
NtfslxSelfTestRunAll(
    _Out_ PNTFSLX_SELFTEST_RESULT Result)
{
    if (Result == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Result, sizeof(*Result));

    STLOG("=== SELF-TEST START ===\n");

    TestScratchBuffer(Result);
    TestFcbHeaderLayout(Result);
    TestAttribLifecycle(Result);
    TestAttribOrdering(Result);

    STLOG("=== SELF-TEST END pass=%lu fail=%lu total=%lu firstFail=0x%04lx ===\n",
          Result->PassedChecks, Result->FailedChecks, Result->TotalChecks,
          Result->FirstFailCode);

    return Result->FailedChecks == 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
