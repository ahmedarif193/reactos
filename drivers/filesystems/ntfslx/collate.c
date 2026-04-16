/*
 * NTFS collation rule dispatch for the ntfslx ReactOS driver.
 *
 * Implements COLLATION_BINARY, COLLATION_FILE_NAME,
 * COLLATION_NTOFS_ULONG, and COLLATION_NTOFS_ULONGS.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

/*
 * Binary collation: memcmp with length tiebreaker.
 */
static LONG
NtfslxCollateBinary(
    _In_reads_bytes_(Data1Length) const VOID *Data1,
    _In_ ULONG Data1Length,
    _In_reads_bytes_(Data2Length) const VOID *Data2,
    _In_ ULONG Data2Length)
{
    LONG Result;
    ULONG MinLen = (Data1Length < Data2Length) ? Data1Length : Data2Length;

    Result = (LONG)memcmp(Data1, Data2, MinLen);
    if (Result == 0 && Data1Length != Data2Length)
    {
        Result = (Data1Length < Data2Length) ? -1 : 1;
    }
    return Result;
}

/*
 * NTOFS ULONG collation: compare two le32 values.
 */
static LONG
NtfslxCollateNtofsUlong(
    _In_reads_bytes_(Data1Length) const VOID *Data1,
    _In_ ULONG Data1Length,
    _In_reads_bytes_(Data2Length) const VOID *Data2,
    _In_ ULONG Data2Length)
{
    ULONG D1, D2;

    if (Data1Length != 4 || Data2Length != 4)
        return -2; /* error sentinel */

    D1 = *(const ULONG *)Data1;
    D2 = *(const ULONG *)Data2;

    if (D1 < D2)
        return -1;
    if (D1 > D2)
        return 1;
    return 0;
}

/*
 * NTOFS ULONGS collation: compare two le32 arrays element-by-element.
 */
static LONG
NtfslxCollateNtofsUlongs(
    _In_reads_bytes_(Data1Length) const VOID *Data1,
    _In_ ULONG Data1Length,
    _In_reads_bytes_(Data2Length) const VOID *Data2,
    _In_ ULONG Data2Length)
{
    const ULONG *P1 = (const ULONG *)Data1;
    const ULONG *P2 = (const ULONG *)Data2;
    ULONG D1, D2;
    LONG Remaining;

    if (Data1Length != Data2Length || (Data1Length & 3) != 0)
        return -1;

    Remaining = (LONG)Data1Length;
    do
    {
        D1 = *P1++;
        D2 = *P2++;
        Remaining -= 4;
    }
    while (D1 == D2 && Remaining > 0);

    if (D1 < D2)
        return -1;
    if (D1 > D2)
        return 1;
    return 0;
}

/*
 * FILE_NAME collation: compare two FILE_NAME attribute keys using the
 * existing NtfslxCollateNames function (case-insensitive first, then
 * case-sensitive as tiebreaker).
 *
 * The FILE_NAME attribute key layout:
 *   offset 0x40: FileNameLength (1 byte)
 *   offset 0x41: FileNameType (1 byte)
 *   offset 0x42: FileName (variable WCHAR[])
 */
static LONG
NtfslxCollateFileName(
    _In_reads_bytes_(Data1Length) const VOID *Data1,
    _In_ ULONG Data1Length,
    _In_reads_bytes_(Data2Length) const VOID *Data2,
    _In_ ULONG Data2Length,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength)
{
    const NTFSLX_FILE_NAME_ATTRIBUTE *Fn1;
    const NTFSLX_FILE_NAME_ATTRIBUTE *Fn2;
    LONG Result;

    UNREFERENCED_PARAMETER(Data1Length);
    UNREFERENCED_PARAMETER(Data2Length);

    Fn1 = (const NTFSLX_FILE_NAME_ATTRIBUTE *)Data1;
    Fn2 = (const NTFSLX_FILE_NAME_ATTRIBUTE *)Data2;

    /* First compare case-insensitive */
    Result = NtfslxCollateNames(
        (const USHORT *)((const UCHAR *)Fn1 + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)),
        Fn1->FileNameLength,
        (const USHORT *)((const UCHAR *)Fn2 + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)),
        Fn2->FileNameLength,
        -2, /* InvalidCharReturn */
        TRUE, /* IgnoreCase */
        UpcaseTable,
        UpcaseLength);

    if (Result != 0)
        return Result;

    /* Tiebreaker: case-sensitive */
    return NtfslxCollateNames(
        (const USHORT *)((const UCHAR *)Fn1 + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)),
        Fn1->FileNameLength,
        (const USHORT *)((const UCHAR *)Fn2 + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)),
        Fn2->FileNameLength,
        -2,
        FALSE, /* CaseSensitive */
        UpcaseTable,
        UpcaseLength);
}

/*
 * NtfslxCollate - generic collation dispatcher
 *
 * Returns -1, 0, or 1 for ordering.
 * Returns -2 on error (unknown collation rule or invalid data).
 */
LONG
NtfslxCollate(
    _In_ ULONG CollationRule,
    _In_reads_bytes_(Data1Length) const VOID *Data1,
    _In_ ULONG Data1Length,
    _In_reads_bytes_(Data2Length) const VOID *Data2,
    _In_ ULONG Data2Length,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength)
{
    switch (CollationRule)
    {
    case NTFSLX_COLLATION_BINARY:
        return NtfslxCollateBinary(Data1, Data1Length, Data2, Data2Length);

    case NTFSLX_COLLATION_FILE_NAME:
        return NtfslxCollateFileName(Data1, Data1Length, Data2, Data2Length,
                                     UpcaseTable, UpcaseLength);

    case NTFSLX_COLLATION_NTOFS_ULONG:
        return NtfslxCollateNtofsUlong(Data1, Data1Length, Data2, Data2Length);

    case NTFSLX_COLLATION_NTOFS_ULONGS:
        return NtfslxCollateNtofsUlongs(Data1, Data1Length, Data2, Data2Length);

    default:
        DbgPrint("NTFSLX: Unknown collation rule 0x%lx\n", CollationRule);
        return -2;
    }
}
