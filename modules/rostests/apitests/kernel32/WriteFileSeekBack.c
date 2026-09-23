/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that small appends after a seek-back overwrite of a cached file keep their data
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static BYTE g_Expected[1024];

static void WriteAt(HANDLE File, DWORD Offset, DWORD Length, BYTE Value)
{
    BYTE Buffer[512];
    DWORD Written = 0;
    BOOL Ret;

    FillMemory(Buffer, Length, Value);
    FillMemory(g_Expected + Offset, Length, Value);
    ok(SetFilePointer(File, Offset, NULL, FILE_BEGIN) == Offset, "SetFilePointer(%lu) failed, error %lu\n", Offset, GetLastError());
    Ret = WriteFile(File, Buffer, Length, &Written, NULL);
    ok(Ret && Written == Length, "WriteFile(%lu, %lu) failed, written %lu, error %lu\n", Offset, Length, Written, GetLastError());
}

static void CheckContents(HANDLE File, DWORD Size, PCSTR Step)
{
    BYTE Buffer[1024];
    DWORD Read = 0, i, Bad = 0, First = 0;

    FillMemory(Buffer, sizeof(Buffer), 0xEE);
    SetFilePointer(File, 0, NULL, FILE_BEGIN);
    ok(ReadFile(File, Buffer, Size, &Read, NULL), "%s: ReadFile failed, error %lu\n", Step, GetLastError());
    ok(Read == Size, "%s: read %lu bytes, expected %lu\n", Step, Read, Size);
    for (i = 0; i < Read; i++)
    {
        if (Buffer[i] != g_Expected[i])
        {
            if (!Bad)
                First = i;
            Bad++;
        }
    }
    ok(Bad == 0, "%s: %lu bytes differ, first at %#lx (0x%02x, expected 0x%02x)\n", Step, Bad, First,
       Bad ? Buffer[First] : 0, Bad ? g_Expected[First] : 0);
}

START_TEST(WriteFileSeekBack)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;

    ok(GetTempPathW(ARRAYSIZE(Path), Path) != 0, "GetTempPathW failed, error %lu\n", GetLastError());
    StringCchCatW(Path, ARRAYSIZE(Path), L"WriteFileSeekBack.bin");

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        skip("CreateFileW failed, error %lu\n", GetLastError());
        return;
    }

    WriteAt(File, 0, 50, 'A');
    WriteAt(File, 50, 180, 'B');
    CheckContents(File, 230, "append");
    WriteAt(File, 14, 12, 'C');
    CheckContents(File, 230, "patch");
    WriteAt(File, 230, 50, 'D');
    CheckContents(File, 280, "append after patch");
    WriteAt(File, 280, 277, 'E');
    CheckContents(File, 557, "append across 512");
    WriteAt(File, 244, 12, 'F');
    CheckContents(File, 557, "second patch");
    ok(FlushFileBuffers(File), "FlushFileBuffers failed, error %lu\n", GetLastError());
    CheckContents(File, 557, "flushed");
    CloseHandle(File);

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "reopen failed, error %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        ok(GetFileSize(File, NULL) == 557, "file size %lu\n", GetFileSize(File, NULL));
        CheckContents(File, 557, "reopened");
        CloseHandle(File);
    }
    ok(DeleteFileW(Path), "DeleteFileW failed, error %lu\n", GetLastError());
}
