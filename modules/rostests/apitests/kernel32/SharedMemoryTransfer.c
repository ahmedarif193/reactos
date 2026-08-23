/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests cross process transfer of pagefile backed file mappings
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <strsafe.h>

#define ROUNDS 6

static const SIZE_T Sizes[ROUNDS] = { 4096, 345600, 65536, 921600, 12345, 2073600 };

static void
FillPattern(
    _Out_writes_bytes_(Size) PBYTE Buffer,
    _In_ SIZE_T Size,
    _In_ BYTE Seed)
{
    SIZE_T Index;

    for (Index = 0; Index < Size; ++Index)
        Buffer[Index] = (BYTE)(Seed + (Index * 7));
}

static BOOL
CheckPattern(
    _In_reads_bytes_(Size) const BYTE *Buffer,
    _In_ SIZE_T Size,
    _In_ BYTE Seed)
{
    SIZE_T Index;

    for (Index = 0; Index < Size; ++Index)
    {
        if (Buffer[Index] != (BYTE)(Seed + (Index * 7)))
            return FALSE;
    }

    return TRUE;
}

static int
RunChild(void)
{
    HANDLE Input = GetStdHandle(STD_INPUT_HANDLE);
    CHAR Line[128];
    DWORD Read, Total = 0;
    ULONG Round;

    for (Round = 0; Round < ROUNDS; ++Round)
    {
        unsigned long long Handle, ReadOnlyHandle, Size;
        PBYTE View, ReadOnlyView;
        BOOL Ok;

        Total = 0;
        for (;;)
        {
            if (!ReadFile(Input, Line + Total, 1, &Read, NULL) || Read != 1)
                return 10;
            if (Line[Total] == '\n')
                break;
            if (++Total >= sizeof(Line) - 1)
                return 11;
        }
        Line[Total] = '\0';

        if (sscanf(Line, "%llu %llu %llu", &Handle, &ReadOnlyHandle, &Size) != 3)
            return 12;

        View = MapViewOfFileEx((HANDLE)(ULONG_PTR)Handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, (SIZE_T)Size, NULL);
        if (!View)
            return 20;

        Ok = CheckPattern(View, (SIZE_T)Size, (BYTE)Round);
        if (!Ok)
            return 21;

        FillPattern(View, (SIZE_T)Size, (BYTE)(Round + 100));

        ReadOnlyView = MapViewOfFileEx((HANDLE)(ULONG_PTR)ReadOnlyHandle, FILE_MAP_READ, 0, 0, (SIZE_T)Size, NULL);
        if (!ReadOnlyView)
            return 22;
        if (!CheckPattern(ReadOnlyView, (SIZE_T)Size, (BYTE)(Round + 100)))
            return 23;
        if (MapViewOfFileEx((HANDLE)(ULONG_PTR)ReadOnlyHandle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, (SIZE_T)Size, NULL))
            return 24;
        if (GetLastError() != ERROR_ACCESS_DENIED)
            return 25;

        UnmapViewOfFile(ReadOnlyView);
        UnmapViewOfFile(View);
        CloseHandle((HANDLE)(ULONG_PTR)ReadOnlyHandle);
        CloseHandle((HANDLE)(ULONG_PTR)Handle);
    }

    return 0;
}

START_TEST(SharedMemoryTransfer)
{
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH + 64];
    SECURITY_ATTRIBUTES Attributes;
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    HANDLE PipeRead = NULL, PipeWrite = NULL, Target = NULL;
    DWORD ExitCode = 0xFFFFFFFF;
    char **Arguments;
    int ArgumentCount;
    ULONG Round;
    BOOL Result;

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 3 && !strcmp(Arguments[2], "child"))
        ExitProcess(RunChild());

    Result = GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)) != 0;
    ok(Result, "GetModuleFileNameW failed with %lu\n", GetLastError());
    if (!Result) return;

    Attributes.nLength = sizeof(Attributes);
    Attributes.lpSecurityDescriptor = NULL;
    Attributes.bInheritHandle = TRUE;
    Result = CreatePipe(&PipeRead, &PipeWrite, &Attributes, 0);
    ok(Result, "CreatePipe failed with %lu\n", GetLastError());
    if (!Result) return;
    SetHandleInformation(PipeWrite, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = PipeRead;
    StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" SharedMemoryTransfer child", Application);
    Result = CreateProcessW(Application, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &StartupInfo, &ProcessInfo);
    ok(Result, "CreateProcessW failed with %lu\n", GetLastError());
    CloseHandle(PipeRead);
    PipeRead = NULL;
    if (!Result) goto Cleanup;
    CloseHandle(ProcessInfo.hThread);

    Target = OpenProcess(PROCESS_DUP_HANDLE, FALSE, ProcessInfo.dwProcessId);
    ok(Target != NULL, "OpenProcess(PROCESS_DUP_HANDLE) failed with %lu\n", GetLastError());
    if (!Target) goto Cleanup;

    for (Round = 0; Round < ROUNDS; ++Round)
    {
        SIZE_T Size = Sizes[Round];
        HANDLE Mapping, ChildHandle = NULL, ChildReadOnly = NULL, Frozen = NULL;
        PBYTE View;
        CHAR Line[128];
        DWORD Written;

        Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)Size, NULL);
        ok(Mapping != NULL, "round %lu: CreateFileMappingW(%Iu) failed with %lu\n", Round, Size, GetLastError());
        if (!Mapping) break;

        View = MapViewOfFileEx(Mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, Size, NULL);
        ok(View != NULL, "round %lu: MapViewOfFileEx failed with %lu\n", Round, GetLastError());
        if (!View) { CloseHandle(Mapping); break; }
        FillPattern(View, Size, (BYTE)Round);

        Result = DuplicateHandle(GetCurrentProcess(), Mapping, GetCurrentProcess(), &Frozen, GENERIC_READ | FILE_MAP_READ, FALSE, 0);
        ok(Result, "round %lu: read-only DuplicateHandle failed with %lu\n", Round, GetLastError());

        Result = DuplicateHandle(GetCurrentProcess(), Mapping, Target, &ChildHandle, 0, FALSE, DUPLICATE_SAME_ACCESS);
        ok(Result, "round %lu: DuplicateHandle into child failed with %lu\n", Round, GetLastError());
        if (Frozen)
        {
            Result = DuplicateHandle(GetCurrentProcess(), Frozen, Target, &ChildReadOnly, 0, FALSE, DUPLICATE_SAME_ACCESS);
            ok(Result, "round %lu: read-only DuplicateHandle into child failed with %lu\n", Round, GetLastError());
        }

        StringCchPrintfA(Line, sizeof(Line), "%llu %llu %llu\n",
                         (unsigned long long)(ULONG_PTR)ChildHandle,
                         (unsigned long long)(ULONG_PTR)ChildReadOnly,
                         (unsigned long long)Size);
        Result = WriteFile(PipeWrite, Line, (DWORD)strlen(Line), &Written, NULL);
        ok(Result, "round %lu: WriteFile to child failed with %lu\n", Round, GetLastError());

        {
            ULONG Spins;
            for (Spins = 0; Spins < 200; ++Spins)
            {
                if (CheckPattern(View, Size, (BYTE)(Round + 100)))
                    break;
                if (WaitForSingleObject(ProcessInfo.hProcess, 10) == WAIT_OBJECT_0)
                {
                    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
                    ok(CheckPattern(View, Size, (BYTE)(Round + 100)), "round %lu: child exited with %lu before replying\n", Round, ExitCode);
                    break;
                }
            }
            ok(CheckPattern(View, Size, (BYTE)(Round + 100)), "round %lu: child did not write the reply pattern\n", Round);
        }

        UnmapViewOfFile(View);
        if (Frozen) CloseHandle(Frozen);
        CloseHandle(Mapping);
    }

    CloseHandle(PipeWrite);
    PipeWrite = NULL;
    WaitForSingleObject(ProcessInfo.hProcess, 10000);
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    ok(ExitCode == 0, "child exited with %lu\n", ExitCode);

Cleanup:
    if (Target) CloseHandle(Target);
    if (ProcessInfo.hProcess) CloseHandle(ProcessInfo.hProcess);
    if (PipeRead) CloseHandle(PipeRead);
    if (PipeWrite) CloseHandle(PipeWrite);
}
