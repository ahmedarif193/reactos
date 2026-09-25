/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for NtCreateSection
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(NtCreateSection)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\NtCreateSectionApitest");
    OBJECT_ATTRIBUTES Attributes;
    LARGE_INTEGER Size;
    HANDLE First = NULL;
    HANDLE Second = INVALID_HANDLE_VALUE;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Size.QuadPart = PAGE_SIZE;

    Status = NtCreateSection(&First, SECTION_ALL_ACCESS, &Attributes, &Size,
                             PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = NtCreateSection(&Second, SECTION_ALL_ACCESS, &Attributes, &Size,
                             PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_hex(Status, STATUS_OBJECT_NAME_COLLISION);
    ok(Second == INVALID_HANDLE_VALUE, "Handle written on failure: %p\n", Second);

    Second = NULL;
    Attributes.Attributes |= OBJ_OPENIF;
    Status = NtCreateSection(&Second, SECTION_ALL_ACCESS, &Attributes, &Size,
                             PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_hex(Status, STATUS_OBJECT_NAME_EXISTS);
    ok(Second != NULL, "No handle returned for OBJ_OPENIF\n");
    if (Second)
        NtClose(Second);

    Size.QuadPart = -1;
    Second = NULL;
    Status = NtCreateSection(&Second, SECTION_ALL_ACCESS, NULL, &Size,
                             PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_hex(Status, STATUS_SECTION_TOO_BIG);

    NtClose(First);
}
