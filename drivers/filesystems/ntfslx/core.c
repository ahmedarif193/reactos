/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Imported NTFS core helpers adapted from Linux NTFS
 *
 * The algorithms in this file are staged NT/ReactOS adaptations of the
 * Linux NTFS helpers from mst.c, upcase.c, and unistr.c.
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

static const UCHAR NtfslxLegalAnsiCharArray[0x40] =
{
    0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,

    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,

    0x17, 0x07, 0x18, 0x17, 0x17, 0x17, 0x17, 0x17,
    0x17, 0x17, 0x18, 0x16, 0x16, 0x17, 0x07, 0x00,

    0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17,
    0x17, 0x17, 0x04, 0x16, 0x18, 0x16, 0x18, 0x18
};

ULONG
NtfslxRecordSizeFromClusters(
    _In_ CHAR ClustersPerRecord,
    _In_ ULONG ClusterSize)
{
    LONG SignedValue;
    ULONG Shift;

    SignedValue = (LONG)(signed char)ClustersPerRecord;
    if (SignedValue > 0)
    {
        return (ULONG)SignedValue * ClusterSize;
    }

    if (SignedValue < 0)
    {
        Shift = (ULONG)(-SignedValue);
        if (Shift >= (sizeof(ULONG) * 8))
        {
            return 0;
        }

        return 1UL << Shift;
    }

    return 0;
}

PUSHORT
NtfslxGenerateDefaultUpcase(
    VOID)
{
    static const INT UpcaseRunTable[][3] =
    {
        {0x0061, 0x007B,  -32}, {0x0451, 0x045D, -80}, {0x1F70, 0x1F72,  74},
        {0x00E0, 0x00F7,  -32}, {0x045E, 0x0460, -80}, {0x1F72, 0x1F76,  86},
        {0x00F8, 0x00FF,  -32}, {0x0561, 0x0587, -48}, {0x1F76, 0x1F78, 100},
        {0x0256, 0x0258, -205}, {0x1F00, 0x1F08,   8}, {0x1F78, 0x1F7A, 128},
        {0x028A, 0x028C, -217}, {0x1F10, 0x1F16,   8}, {0x1F7A, 0x1F7C, 112},
        {0x03AC, 0x03AD,  -38}, {0x1F20, 0x1F28,   8}, {0x1F7C, 0x1F7E, 126},
        {0x03AD, 0x03B0,  -37}, {0x1F30, 0x1F38,   8}, {0x1FB0, 0x1FB2,   8},
        {0x03B1, 0x03C2,  -32}, {0x1F40, 0x1F46,   8}, {0x1FD0, 0x1FD2,   8},
        {0x03C2, 0x03C3,  -31}, {0x1F51, 0x1F52,   8}, {0x1FE0, 0x1FE2,   8},
        {0x03C3, 0x03CC,  -32}, {0x1F53, 0x1F54,   8}, {0x1FE5, 0x1FE6,   7},
        {0x03CC, 0x03CD,  -64}, {0x1F55, 0x1F56,   8}, {0x2170, 0x2180, -16},
        {0x03CD, 0x03CF,  -63}, {0x1F57, 0x1F58,   8}, {0x24D0, 0x24EA, -26},
        {0x0430, 0x0450,  -32}, {0x1F60, 0x1F68,   8}, {0xFF41, 0xFF5B, -32},
        {0}
    };

    static const INT UpcaseDupTable[][2] =
    {
        {0x0100, 0x012F}, {0x01A0, 0x01A6}, {0x03E2, 0x03EF}, {0x04CB, 0x04CC},
        {0x0132, 0x0137}, {0x01B3, 0x01B7}, {0x0460, 0x0481}, {0x04D0, 0x04EB},
        {0x0139, 0x0149}, {0x01CD, 0x01DD}, {0x0490, 0x04BF}, {0x04EE, 0x04F5},
        {0x014A, 0x0178}, {0x01DE, 0x01EF}, {0x04BF, 0x04BF}, {0x04F8, 0x04F9},
        {0x0179, 0x017E}, {0x01F4, 0x01F5}, {0x04C1, 0x04C4}, {0x1E00, 0x1E95},
        {0x018B, 0x018B}, {0x01FA, 0x0218}, {0x04C7, 0x04C8}, {0x1EA0, 0x1EF9},
        {0}
    };

    static const INT UpcaseWordTable[][2] =
    {
        {0x00FF, 0x0178}, {0x01AD, 0x01AC}, {0x01F3, 0x01F1}, {0x0269, 0x0196},
        {0x0183, 0x0182}, {0x01B0, 0x01AF}, {0x0253, 0x0181}, {0x026F, 0x019C},
        {0x0185, 0x0184}, {0x01B9, 0x01B8}, {0x0254, 0x0186}, {0x0272, 0x019D},
        {0x0188, 0x0187}, {0x01BD, 0x01BC}, {0x0259, 0x018F}, {0x0275, 0x019F},
        {0x018C, 0x018B}, {0x01C6, 0x01C4}, {0x025B, 0x0190}, {0x0283, 0x01A9},
        {0x0192, 0x0191}, {0x01C9, 0x01C7}, {0x0260, 0x0193}, {0x0288, 0x01AE},
        {0x0199, 0x0198}, {0x01CC, 0x01CA}, {0x0263, 0x0194}, {0x0292, 0x01B7},
        {0x01A8, 0x01A7}, {0x01DD, 0x018E}, {0x0268, 0x0197},
        {0}
    };

    PUSHORT UpcaseTable;
    ULONG Index;
    ULONG RangeIndex;

    UpcaseTable = ExAllocatePoolWithTag(NonPagedPool,
                                        NTFSLX_DEFAULT_UPCASE_LENGTH * sizeof(USHORT),
                                        NTFSLX_TAG);
    if (UpcaseTable == NULL)
    {
        return NULL;
    }

    for (Index = 0; Index < NTFSLX_DEFAULT_UPCASE_LENGTH; ++Index)
    {
        UpcaseTable[Index] = (USHORT)Index;
    }

    for (RangeIndex = 0; UpcaseRunTable[RangeIndex][0] != 0; ++RangeIndex)
    {
        for (Index = (ULONG)UpcaseRunTable[RangeIndex][0];
             Index < (ULONG)UpcaseRunTable[RangeIndex][1];
             ++Index)
        {
            UpcaseTable[Index] = (USHORT)(UpcaseTable[Index] + UpcaseRunTable[RangeIndex][2]);
        }
    }

    for (RangeIndex = 0; UpcaseDupTable[RangeIndex][0] != 0; ++RangeIndex)
    {
        for (Index = (ULONG)UpcaseDupTable[RangeIndex][0];
             Index < (ULONG)UpcaseDupTable[RangeIndex][1];
             Index += 2)
        {
            UpcaseTable[Index + 1] = (USHORT)(UpcaseTable[Index + 1] - 1);
        }
    }

    for (RangeIndex = 0; UpcaseWordTable[RangeIndex][0] != 0; ++RangeIndex)
    {
        UpcaseTable[UpcaseWordTable[RangeIndex][0]] = (USHORT)UpcaseWordTable[RangeIndex][1];
    }

    return UpcaseTable;
}

NTSTATUS
NtfslxPostReadMstFixup(
    _Inout_ PNTFSLX_RECORD_HEADER Record,
    _In_ ULONG Size)
{
    USHORT UsaOffset;
    USHORT UsaCount;
    PUSHORT UsaEntry;
    PUSHORT DataEntry;
    USHORT UpdateSequenceNumber;
    ULONG Index;

    UsaOffset = Record->UsaOffset;
    UsaCount = Record->UsaCount;
    if (UsaCount == 0)
    {
        return STATUS_SUCCESS;
    }

    UsaCount--;
    if ((Size & 0x1FF) != 0 ||
        (UsaOffset & 1) != 0 ||
        UsaOffset + (UsaCount * sizeof(USHORT)) > Size ||
        (Size >> 9) != UsaCount)
    {
        return STATUS_SUCCESS;
    }

    UsaEntry = (PUSHORT)((PUCHAR)Record + UsaOffset);
    UpdateSequenceNumber = *UsaEntry;
    DataEntry = (PUSHORT)((PUCHAR)Record + 512 - sizeof(USHORT));

    for (Index = 0; Index < UsaCount; ++Index)
    {
        if (*DataEntry != UpdateSequenceNumber)
        {
            Record->Magic = NTFSLX_RECORD_MAGIC_BAAD;
            return STATUS_FILE_CORRUPT_ERROR;
        }

        DataEntry = (PUSHORT)((PUCHAR)DataEntry + 512);
    }

    DataEntry = (PUSHORT)((PUCHAR)Record + 512 - sizeof(USHORT));
    for (Index = 0; Index < UsaCount; ++Index)
    {
        *DataEntry = *(++UsaEntry);
        DataEntry = (PUSHORT)((PUCHAR)DataEntry + 512);
    }

    return STATUS_SUCCESS;
}

int
NtfslxUcsNCompare(
    _In_reads_(Length) const USHORT *String1,
    _In_reads_(Length) const USHORT *String2,
    _In_ SIZE_T Length)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; ++Index)
    {
        if (String1[Index] < String2[Index])
        {
            return -1;
        }

        if (String1[Index] > String2[Index])
        {
            return 1;
        }

        if (String1[Index] == UNICODE_NULL)
        {
            break;
        }
    }

    return 0;
}

int
NtfslxUcsNCaseCompare(
    _In_reads_(Length) const USHORT *String1,
    _In_reads_(Length) const USHORT *String2,
    _In_ SIZE_T Length,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength)
{
    SIZE_T Index;
    USHORT Char1;
    USHORT Char2;

    for (Index = 0; Index < Length; ++Index)
    {
        Char1 = String1[Index];
        if (Char1 < UpcaseLength)
        {
            Char1 = UpcaseTable[Char1];
        }

        Char2 = String2[Index];
        if (Char2 < UpcaseLength)
        {
            Char2 = UpcaseTable[Char2];
        }

        if (Char1 < Char2)
        {
            return -1;
        }

        if (Char1 > Char2)
        {
            return 1;
        }

        if (Char1 == UNICODE_NULL)
        {
            break;
        }
    }

    return 0;
}

BOOLEAN
NtfslxAreNamesEqual(
    _In_reads_(String1Length) const USHORT *String1,
    _In_ SIZE_T String1Length,
    _In_reads_(String2Length) const USHORT *String2,
    _In_ SIZE_T String2Length,
    _In_ BOOLEAN IgnoreCase,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength)
{
    if (String1Length != String2Length)
    {
        return FALSE;
    }

    if (!IgnoreCase)
    {
        return NtfslxUcsNCompare(String1, String2, String1Length) == 0;
    }

    return NtfslxUcsNCaseCompare(String1,
                                 String2,
                                 String1Length,
                                 UpcaseTable,
                                 UpcaseLength) == 0;
}

LONG
NtfslxCollateNames(
    _In_reads_(Name1Length) const USHORT *Name1,
    _In_ ULONG Name1Length,
    _In_reads_(Name2Length) const USHORT *Name2,
    _In_ ULONG Name2Length,
    _In_ LONG InvalidCharReturn,
    _In_ BOOLEAN IgnoreCase,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength)
{
    ULONG Count;
    ULONG Limit;
    USHORT Char1;
    USHORT Char2;

    Limit = (Name1Length < Name2Length) ? Name1Length : Name2Length;
    for (Count = 0; Count < Limit; ++Count)
    {
        Char1 = Name1[Count];
        Char2 = Name2[Count];

        if (IgnoreCase)
        {
            if (Char1 < UpcaseLength)
            {
                Char1 = UpcaseTable[Char1];
            }

            if (Char2 < UpcaseLength)
            {
                Char2 = UpcaseTable[Char2];
            }
        }

        if (Char1 < RTL_NUMBER_OF(NtfslxLegalAnsiCharArray) &&
            (NtfslxLegalAnsiCharArray[Char1] & 0x08) != 0)
        {
            return InvalidCharReturn;
        }

        if (Char1 < Char2)
        {
            return -1;
        }

        if (Char1 > Char2)
        {
            return 1;
        }
    }

    if (Name1Length < Name2Length)
    {
        return -1;
    }

    if (Name1Length == Name2Length)
    {
        return 0;
    }

    Char1 = Name1[Limit];
    if (Char1 < RTL_NUMBER_OF(NtfslxLegalAnsiCharArray) &&
        (NtfslxLegalAnsiCharArray[Char1] & 0x08) != 0)
    {
        return InvalidCharReturn;
    }

    return 1;
}
