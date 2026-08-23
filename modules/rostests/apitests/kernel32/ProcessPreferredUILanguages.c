/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process preferred user-interface language coverage
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL
(WINAPI *PGET_PROCESS_PREFERRED_UI_LANGUAGES)(
    _In_ DWORD Flags,
    _Out_ PULONG Count,
    _Out_writes_opt_(*Size) PZZWSTR Buffer,
    _Inout_ PULONG Size);

typedef BOOL
(WINAPI *PSET_PROCESS_PREFERRED_UI_LANGUAGES)(
    _In_ DWORD Flags,
    _In_opt_ PCZZWSTR Buffer,
    _Out_opt_ PULONG Count);

static VOID
TraceLanguages(
    _In_ PCSTR Phase,
    _In_ ULONG Count,
    _In_ ULONG Size,
    _In_opt_ PCZZWSTR Buffer)
{
    ULONG Index = 0;

    trace("%s: count %lu size %lu\n", Phase, Count, Size);
    while (Buffer && *Buffer)
    {
        trace("%s[%lu] = %S\n", Phase, Index++, Buffer);
        Buffer += wcslen(Buffer) + 1;
    }
}

static VOID
CheckGet(
    _In_ PGET_PROCESS_PREFERRED_UI_LANGUAGES GetLanguages,
    _In_ DWORD Flags,
    _In_ PCSTR Phase,
    _In_ ULONG ExpectedCount,
    _In_reads_(ExpectedSize) PCZZWSTR Expected,
    _In_ ULONG ExpectedSize,
    _In_ BOOL CountIsWritten)
{
    WCHAR Buffer[256];
    ULONG Count = 0xcccccccc;
    ULONG Size = 0;
    BOOL Result;
    DWORD Error;

    SetLastError(0xdeadbeef);
    Result = GetLanguages(Flags, &Count, NULL, &Size);
    Error = GetLastError();
    ok(Result,
       "%s size query failed: %lu\n",
       Phase,
       Error);
    ok(Error == 0xdeadbeef,
       "%s size query changed last error to %lu\n",
       Phase,
       Error);
    if (CountIsWritten)
        ok(Count == ExpectedCount,
           "%s size query returned count %lu, expected %lu\n",
           Phase,
           Count,
           ExpectedCount);
    else
        ok(Count == 0xcccccccc,
           "%s size query changed an empty-list count to %lu\n",
           Phase,
           Count);
    ok(Size == ExpectedSize,
       "%s size query returned %lu, expected %lu\n",
       Phase,
       Size,
       ExpectedSize);
    if (!Result || Size != ExpectedSize || Size > _countof(Buffer))
        return;

    RtlFillMemory(Buffer, sizeof(Buffer), 0xcc);
    Count = 0xcccccccc;
    SetLastError(0xdeadbeef);
    Result = GetLanguages(Flags, &Count, Buffer, &Size);
    Error = GetLastError();
    ok(Result,
       "%s buffer query failed: %lu\n",
       Phase,
       Error);
    ok(Error == 0xdeadbeef,
       "%s buffer query changed last error to %lu\n",
       Phase,
       Error);
    if (CountIsWritten)
        ok(Count == ExpectedCount,
           "%s buffer query returned count %lu, expected %lu\n",
           Phase,
           Count,
           ExpectedCount);
    else
        ok(Count == 0xcccccccc,
           "%s buffer query changed an empty-list count to %lu\n",
           Phase,
           Count);
    ok(Size == ExpectedSize,
       "%s buffer query returned size %lu, expected %lu\n",
       Phase,
       Size,
       ExpectedSize);
    ok(!memcmp(Buffer, Expected, ExpectedSize * sizeof(WCHAR)),
       "%s returned an unexpected multistring\n",
       Phase);
    TraceLanguages(Phase, Count, Size, Buffer);
}

START_TEST(ProcessPreferredUILanguages)
{
    static const WCHAR Names[] =
        L"fr-FR\0en-US\0fr-FR\0invalid-language\0de-DE\0es-ES\0it-IT\0pt-PT\0";
    static const WCHAR Ids[] =
        L"040C\0" L"0409\0" L"040C\0" L"FFFF\0"
        L"0407\0" L"0C0A\0" L"0410\0" L"0816\0";
    static const WCHAR EmptyExpected[] = L"\0";
    static const WCHAR NamesExpected[] =
        L"fr-FR\0en-US\0de-DE\0es-ES\0";
    static const WCHAR IdsExpected[] =
        L"040C\0" L"0409\0" L"0407\0" L"0C0A\0";
    HMODULE Kernel32;
    PGET_PROCESS_PREFERRED_UI_LANGUAGES GetLanguages;
    PSET_PROCESS_PREFERRED_UI_LANGUAGES SetLanguages;
    ULONG Count;
    BOOL Result;
    DWORD Error;

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    GetLanguages = (PGET_PROCESS_PREFERRED_UI_LANGUAGES)GetProcAddress(
        Kernel32,
        "GetProcessPreferredUILanguages");
    SetLanguages = (PSET_PROCESS_PREFERRED_UI_LANGUAGES)GetProcAddress(
        Kernel32,
        "SetProcessPreferredUILanguages");
    ok(GetLanguages != NULL,
       "GetProcessPreferredUILanguages is not exported\n");
    ok(SetLanguages != NULL,
       "SetProcessPreferredUILanguages is not exported\n");
    if (!GetLanguages || !SetLanguages)
        return;

    CheckGet(GetLanguages,
             0,
             "initial-default",
             0,
             EmptyExpected,
             _countof(EmptyExpected),
             FALSE);
    CheckGet(GetLanguages,
             MUI_LANGUAGE_NAME,
             "initial-name",
             0,
             EmptyExpected,
             _countof(EmptyExpected),
             FALSE);
    CheckGet(GetLanguages,
             MUI_LANGUAGE_ID,
             "initial-id",
             0,
             EmptyExpected,
             _countof(EmptyExpected),
             FALSE);

    Count = 0xcccccccc;
    SetLastError(0xdeadbeef);
    Result = SetLanguages(MUI_LANGUAGE_NAME, Names, &Count);
    Error = GetLastError();
    ok(Result, "name list failed: %lu\n", Error);
    ok(Error == 0xdeadbeef,
       "name list changed last error to %lu\n",
       Error);
    ok(Count == 4,
       "name list set %lu languages, expected 4\n",
       Count);
    CheckGet(GetLanguages,
             0,
             "after-names-default",
             4,
             NamesExpected,
             _countof(NamesExpected),
             TRUE);
    CheckGet(GetLanguages,
             MUI_LANGUAGE_NAME,
             "after-names-name",
             4,
             NamesExpected,
             _countof(NamesExpected),
             TRUE);
    CheckGet(GetLanguages,
             MUI_LANGUAGE_ID,
             "after-names-id",
             4,
             IdsExpected,
             _countof(IdsExpected),
             TRUE);

    Count = 0xcccccccc;
    SetLastError(0xdeadbeef);
    Result = SetLanguages(MUI_LANGUAGE_ID, Ids, &Count);
    Error = GetLastError();
    ok(Result, "identifier list failed: %lu\n", Error);
    ok(Error == 0xdeadbeef,
       "identifier list changed last error to %lu\n",
       Error);
    ok(Count == 4,
       "identifier list set %lu languages, expected 4\n",
       Count);
    CheckGet(GetLanguages,
             MUI_LANGUAGE_NAME,
             "after-ids-name",
             4,
             NamesExpected,
             _countof(NamesExpected),
             TRUE);
    CheckGet(GetLanguages,
             MUI_LANGUAGE_ID,
             "after-ids-id",
             4,
             IdsExpected,
             _countof(IdsExpected),
             TRUE);

    Count = 0xcccccccc;
    SetLastError(0xdeadbeef);
    Result = SetLanguages(MUI_LANGUAGE_ID | MUI_LANGUAGE_NAME, Names, &Count);
    Error = GetLastError();
    ok(!Result, "mutually exclusive flags unexpectedly succeeded\n");
    ok(Error == ERROR_INVALID_PARAMETER,
       "mutually exclusive flags returned error %lu\n",
       Error);
    ok(Count == 0xcccccccc,
       "mutually exclusive flags changed count to %lu\n",
       Count);
    CheckGet(GetLanguages,
             MUI_LANGUAGE_NAME,
             "after-invalid-flags",
             4,
             NamesExpected,
             _countof(NamesExpected),
             TRUE);

    Count = 0xcccccccc;
    SetLastError(0xdeadbeef);
    Result = SetLanguages(0, NULL, &Count);
    Error = GetLastError();
    ok(Result, "clearing the process list failed: %lu\n", Error);
    ok(Error == 0xdeadbeef,
       "clearing the process list changed last error to %lu\n",
       Error);
    ok(Count == 0xcccccccc,
       "clearing the process list changed count to %lu\n",
       Count);
    CheckGet(GetLanguages,
             MUI_LANGUAGE_NAME,
             "after-clear",
             0,
             EmptyExpected,
             _countof(EmptyExpected),
             FALSE);
}
