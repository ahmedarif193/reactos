/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests access reduction for secured unnamed file mappings
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(SharedMemorySecurity)
{
    SECURITY_DESCRIPTOR Descriptor;
    SECURITY_ATTRIBUTES Attributes;
    ACL Dacl;
    HANDLE Mapping = NULL;
    HANDLE ReadOnly = NULL;
    HANDLE Duplicate = NULL;
    PVOID View = NULL;
    BOOL Result;

    Result = InitializeAcl(&Dacl, sizeof(Dacl), ACL_REVISION);
    ok(Result, "InitializeAcl failed with %lu\n", GetLastError());
    if (!Result)
        return;

    Result = InitializeSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    ok(Result, "InitializeSecurityDescriptor failed with %lu\n", GetLastError());
    if (!Result)
        return;

    Result = SetSecurityDescriptorDacl(&Descriptor, TRUE, &Dacl, FALSE);
    ok(Result, "SetSecurityDescriptorDacl failed with %lu\n", GetLastError());
    if (!Result)
        return;

    Attributes.nLength = sizeof(Attributes);
    Attributes.lpSecurityDescriptor = &Descriptor;
    Attributes.bInheritHandle = FALSE;

    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &Attributes, PAGE_READWRITE, 0, 0x10000, NULL);
    ok(Mapping != NULL, "CreateFileMappingW failed with %lu\n", GetLastError());
    if (!Mapping)
        return;

    Result = DuplicateHandle(GetCurrentProcess(), Mapping, GetCurrentProcess(), &ReadOnly, FILE_MAP_READ | SECTION_QUERY, FALSE, 0);
    ok(Result, "reducing the mapping handle failed with %lu\n", GetLastError());
    if (!Result)
        goto Cleanup;

    View = MapViewOfFile(ReadOnly, FILE_MAP_READ, 0, 0, 0x10000);
    ok(View != NULL, "mapping the read-only handle failed with %lu\n", GetLastError());
    if (View)
    {
        UnmapViewOfFile(View);
        View = NULL;
    }

    SetLastError(0xdeadbeef);
    Result = DuplicateHandle(GetCurrentProcess(), ReadOnly, GetCurrentProcess(), &Duplicate, FILE_MAP_WRITE, FALSE, 0);
    ok(!Result, "write access was regained from a secured read-only handle\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "expected ERROR_ACCESS_DENIED, got %lu\n", GetLastError());
    ok(Duplicate == NULL, "failed duplication returned handle %p\n", Duplicate);
    if (Duplicate)
    {
        CloseHandle(Duplicate);
        Duplicate = NULL;
    }

    Result = DuplicateHandle(GetCurrentProcess(), ReadOnly, GetCurrentProcess(), &Duplicate, FILE_MAP_READ | SECTION_QUERY, FALSE, 0);
    ok(Result, "duplicating retained read/query access failed with %lu\n", GetLastError());

Cleanup:
    if (Duplicate)
        CloseHandle(Duplicate);
    if (ReadOnly)
        CloseHandle(ReadOnly);
    CloseHandle(Mapping);
}
