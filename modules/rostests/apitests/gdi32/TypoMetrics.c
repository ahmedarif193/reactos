/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that TEXTMETRIC uses OS/2 win metrics whether or not USE_TYPO_METRICS is set
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _TYPO_METRICS_CASE
{
    USHORT WinAscent;
    USHORT WinDescent;
    BOOL UseTypoMetrics;
    LONG Height;
    LONG Ascent;
    LONG Descent;
    LONG TotalHeight;
    LONG InternalLeading;
    LONG ExternalLeading;
} TYPO_METRICS_CASE;

static const TYPO_METRICS_CASE g_Cases[] =
{
    { 900, 300, TRUE,  -20, 18, 6, 24,   4, 0 },
    { 900, 300, TRUE,   20, 15, 5, 20,   3, 0 },
    { 900, 300, FALSE, -20, 18, 6, 24,   4, 0 },
    { 900, 300, FALSE,  20, 15, 5, 20,   3, 0 },
    { 500, 100, TRUE,  -20, 10, 2, 12,  -8, 3 },
    { 500, 100, TRUE,   20, 17, 3, 20, -13, 5 },
};

static ULONG ReadBe32(const BYTE *Data)
{
    return ((ULONG)Data[0] << 24) | ((ULONG)Data[1] << 16) | ((ULONG)Data[2] << 8) | Data[3];
}

static void WriteBe32(BYTE *Data, ULONG Value)
{
    Data[0] = (BYTE)(Value >> 24);
    Data[1] = (BYTE)(Value >> 16);
    Data[2] = (BYTE)(Value >> 8);
    Data[3] = (BYTE)Value;
}

static void WriteBe16(BYTE *Data, USHORT Value)
{
    Data[0] = (BYTE)(Value >> 8);
    Data[1] = (BYTE)Value;
}

static ULONG TableChecksum(const BYTE *Data, ULONG Length)
{
    ULONG Sum = 0, i, k;
    BYTE Word[4];

    for (i = 0; i < Length; i += 4)
    {
        for (k = 0; k < 4; k++)
            Word[k] = (i + k < Length) ? Data[i + k] : 0;
        Sum += ReadBe32(Word);
    }
    return Sum;
}

static BYTE *FindTable(BYTE *Font, DWORD Size, ULONG Tag, PULONG Length, BYTE **DirectoryEntry)
{
    USHORT Count = (USHORT)((Font[4] << 8) | Font[5]);
    USHORT i;

    for (i = 0; i < Count && 12 + (i + 1) * 16 <= Size; i++)
    {
        BYTE *Entry = Font + 12 + i * 16;
        if (ReadBe32(Entry) == Tag && ReadBe32(Entry + 8) + ReadBe32(Entry + 12) <= Size)
        {
            *Length = ReadBe32(Entry + 12);
            *DirectoryEntry = Entry;
            return Font + ReadBe32(Entry + 8);
        }
    }
    return NULL;
}

static BOOL PatchWinMetrics(BYTE *Font, DWORD Size, USHORT WinAscent, USHORT WinDescent, BOOL UseTypoMetrics)
{
    BYTE *Os2, *Head, *Os2Entry, *HeadEntry;
    ULONG Os2Length, HeadLength;
    USHORT Selection;

    Os2 = FindTable(Font, Size, 0x4F532F32, &Os2Length, &Os2Entry);
    Head = FindTable(Font, Size, 0x68656164, &HeadLength, &HeadEntry);
    if (!Os2 || !Head || Os2Length < 78 || HeadLength < 12)
        return FALSE;

    WriteBe16(Os2 + 74, WinAscent);
    WriteBe16(Os2 + 76, WinDescent);
    Selection = (USHORT)((Os2[62] << 8) | Os2[63]);
    Selection = UseTypoMetrics ? (Selection | 0x80) : (Selection & ~0x80);
    WriteBe16(Os2 + 62, Selection);
    WriteBe32(Os2Entry + 4, TableChecksum(Os2, Os2Length));
    WriteBe32(Head + 8, 0);
    WriteBe32(HeadEntry + 4, TableChecksum(Head, HeadLength));
    WriteBe32(Head + 8, 0xB1B0AFBA - TableChecksum(Font, Size));
    return TRUE;
}

START_TEST(TypoMetrics)
{
    HRSRC Resource;
    HGLOBAL Template;
    const BYTE *Original;
    BYTE *Font;
    DWORD Size, Count;
    HDC hDC;
    SIZE_T i;

    Resource = FindResourceA(GetModuleHandleA(NULL), "ExampleFont.ttf", MAKEINTRESOURCEA(RT_RCDATA));
    ok(Resource != NULL, "ExampleFont.ttf resource not found\n");
    if (!Resource)
        return;
    Template = LoadResource(GetModuleHandleA(NULL), Resource);
    Original = LockResource(Template);
    Size = SizeofResource(GetModuleHandleA(NULL), Resource);
    Font = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Original || !Font)
    {
        skip("Cannot load ExampleFont.ttf\n");
        return;
    }

    hDC = CreateCompatibleDC(NULL);
    for (i = 0; i < ARRAYSIZE(g_Cases); i++)
    {
        const TYPO_METRICS_CASE *Case = &g_Cases[i];
        HANDLE FontHandle;
        LOGFONTW LogFont;
        HFONT hFont;
        HGDIOBJ OldFont;
        TEXTMETRICW Metrics;

        CopyMemory(Font, Original, Size);
        if (!PatchWinMetrics(Font, Size, Case->WinAscent, Case->WinDescent, Case->UseTypoMetrics))
        {
            skip("Cannot patch ExampleFont.ttf\n");
            break;
        }

        Count = 0;
        FontHandle = AddFontMemResourceEx(Font, Size, NULL, &Count);
        ok(FontHandle != NULL && Count == 1, "case %Iu: AddFontMemResourceEx failed, count %lu\n", i, Count);
        if (!FontHandle)
            continue;

        ZeroMemory(&LogFont, sizeof(LogFont));
        LogFont.lfHeight = Case->Height;
        StringCchCopyW(LogFont.lfFaceName, ARRAYSIZE(LogFont.lfFaceName), L"EnglishDisplayName");
        hFont = CreateFontIndirectW(&LogFont);
        OldFont = SelectObject(hDC, hFont);
        ok(GetTextMetricsW(hDC, &Metrics), "case %Iu: GetTextMetricsW failed\n", i);
        ok(Metrics.tmAscent == Case->Ascent, "case %Iu: tmAscent %ld, expected %ld\n", i, Metrics.tmAscent, Case->Ascent);
        ok(Metrics.tmDescent == Case->Descent, "case %Iu: tmDescent %ld, expected %ld\n", i, Metrics.tmDescent, Case->Descent);
        ok(Metrics.tmHeight == Case->TotalHeight, "case %Iu: tmHeight %ld, expected %ld\n", i, Metrics.tmHeight, Case->TotalHeight);
        ok(Metrics.tmInternalLeading == Case->InternalLeading, "case %Iu: tmInternalLeading %ld, expected %ld\n",
           i, Metrics.tmInternalLeading, Case->InternalLeading);
        ok(Metrics.tmExternalLeading == Case->ExternalLeading, "case %Iu: tmExternalLeading %ld, expected %ld\n",
           i, Metrics.tmExternalLeading, Case->ExternalLeading);
        SelectObject(hDC, OldFont);
        DeleteObject(hFont);
        ok(RemoveFontMemResourceEx(FontHandle), "case %Iu: RemoveFontMemResourceEx failed\n", i);
    }
    DeleteDC(hDC);
    HeapFree(GetProcessHeap(), 0, Font);
}
