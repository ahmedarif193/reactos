/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests QueryWorkingSetEx for private, section and image pages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <psapi.h>

static BOOL QueryOne(PVOID Address, PSAPI_WORKING_SET_EX_BLOCK *Block)
{
    PSAPI_WORKING_SET_EX_INFORMATION Info;
    BOOL Result;

    Info.VirtualAddress = Address;
    Info.VirtualAttributes.Flags = ~(ULONG_PTR)0;
    Result = QueryWorkingSetEx(GetCurrentProcess(), &Info, sizeof(Info));
    *Block = Info.VirtualAttributes;
    return Result;
}

START_TEST(QueryWorkingSetEx)
{
    PSAPI_WORKING_SET_EX_INFORMATION Pages[2];
    PSAPI_WORKING_SET_EX_BLOCK Block;
    SYSTEM_INFO System;
    HANDLE Mapping;
    PUCHAR Private, View1, View2;
    BOOL Result;

    GetSystemInfo(&System);

    Private = VirtualAlloc(NULL, 4 * System.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok(Private != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (Private)
    {
        Private[0] = 1;
        Pages[0].VirtualAddress = Private;
        Pages[1].VirtualAddress = Private + System.dwPageSize;
        Result = QueryWorkingSetEx(GetCurrentProcess(), Pages, sizeof(Pages));
        ok(Result, "QueryWorkingSetEx failed with %lu\n", GetLastError());
        if (Result)
        {
            ok(Pages[0].VirtualAttributes.Valid == 1, "Touched private page is not valid\n");
            ok(Pages[0].VirtualAttributes.ShareCount >= 1, "ShareCount is %u\n",
               (UINT)Pages[0].VirtualAttributes.ShareCount);
            ok(Pages[0].VirtualAttributes.Win32Protection == PAGE_READWRITE, "Win32Protection is %#x\n",
               (UINT)Pages[0].VirtualAttributes.Win32Protection);
            ok(Pages[0].VirtualAttributes.Shared == 0, "Private page is shared\n");
            ok(Pages[1].VirtualAttributes.Valid == 0, "Untouched private page is valid\n");
        }
        VirtualFree(Private, 0, MEM_RELEASE);

        Result = QueryOne(Private, &Block);
        ok(Result, "QueryWorkingSetEx on a free page failed with %lu\n", GetLastError());
        ok(!Result || Block.Valid == 0, "Free page is valid\n");
    }

    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, System.dwPageSize, NULL);
    ok(Mapping != NULL, "CreateFileMapping failed with %lu\n", GetLastError());
    if (Mapping)
    {
        View1 = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, 0);
        View2 = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, 0);
        ok(View1 != NULL && View2 != NULL, "MapViewOfFile failed with %lu\n", GetLastError());
        if (View1 && View2)
        {
            View1[0] = 7;
            ok(View2[0] == 7, "Views do not share the page\n");
            Result = QueryOne(View1, &Block);
            ok(Result, "QueryWorkingSetEx on a view failed with %lu\n", GetLastError());
            if (Result)
            {
                ok(Block.Valid == 1, "Touched view page is not valid\n");
                ok(Block.Shared == 1, "View page is not shared\n");
                ok(Block.Win32Protection == PAGE_READWRITE, "View Win32Protection is %#x\n",
                   (UINT)Block.Win32Protection);
            }
        }
        if (View1) UnmapViewOfFile(View1);
        if (View2) UnmapViewOfFile(View2);
        CloseHandle(Mapping);
    }

    Result = QueryOne(GetModuleHandleW(NULL), &Block);
    ok(Result, "QueryWorkingSetEx on the image failed with %lu\n", GetLastError());
    if (Result)
    {
        ok(Block.Valid == 1, "Image header page is not valid\n");
        ok(Block.Shared == 1, "Image header page is not shared\n");
    }
}
