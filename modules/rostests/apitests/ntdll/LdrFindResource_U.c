/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Test for LdrFindResource_U
 * COPYRIGHT:   Copyright 2025 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include "precomp.h"

static void Test_CORE_20401(void)
{
    HMODULE hmod = GetModuleHandleW(NULL);
    LDR_RESOURCE_INFO info;
    IMAGE_RESOURCE_DATA_ENTRY *entry = NULL;
    IMAGE_RESOURCE_DIRECTORY *directory = NULL;
    HRSRC resource;
    PVOID data = NULL, expected;
    ULONG size = 0;
    DWORD expected_size;
    NTSTATUS Status;

    // Use LdrFindResource_U to find a bitmap resource called "NORMAL_FRAMECAPTION_BMP"
    // CORE-20401 resulted in wrong comparison of strings containing underscores.
    // If the test fails, the resource name comparison is probably broken (again).
    info.Type = 2; // RT_BITMAP;
    info.Name = (ULONG_PTR)L"NORMAL_FRAMECAPTION_BMP";
    info.Language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    Status = LdrFindResource_U(hmod, &info, 3, &entry);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(entry != NULL, "Resource entry is NULL\n");
    if (!NT_SUCCESS(Status) || !entry)
        return;

    Status = LdrFindResourceDirectory_U(hmod, &info, 2, &directory);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(directory != NULL, "Resource directory is NULL\n");
    if (NT_SUCCESS(Status) && directory)
    {
        ok(directory->NumberOfNamedEntries == 0, "Unexpected named language entries\n");
        ok(directory->NumberOfIdEntries == 1, "Unexpected language count %u\n", directory->NumberOfIdEntries);
    }

    Status = LdrAccessResource(hmod, entry, &data, &size);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(data != NULL, "Resource data is NULL\n");
    if (!NT_SUCCESS(Status) || !data)
        return;

    resource = FindResourceW(hmod, (LPCWSTR)info.Name, (LPCWSTR)info.Type);
    ok(resource != NULL, "FindResourceW failed: %lu\n", GetLastError());
    if (!resource)
        return;
    expected_size = SizeofResource(hmod, resource);
    expected = LockResource(LoadResource(hmod, resource));
    ok(size == expected_size, "Resource size %lu, expected %lu\n", size, expected_size);
    ok(expected != NULL, "LoadResource failed: %lu\n", GetLastError());
    if (expected && size == expected_size)
        ok(!memcmp(data, expected, size), "Resource contents differ\n");

    size = 0;
    Status = LdrAccessResource(hmod, entry, NULL, &size);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(size == expected_size, "Size-only query returned %lu\n", size);
    data = NULL;
    Status = LdrAccessResource(hmod, entry, &data, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(data == expected, "Data-only query returned %p, expected %p\n", data, expected);

    info.Name = (ULONG_PTR)L"MISSING_FRAMECAPTION_BMP";
    Status = LdrFindResource_U(hmod, &info, 3, &entry);
    /* Windows 11 may report TYPE_NOT_FOUND on the first missing-name
     * lookup, then NAME_NOT_FOUND for an identical repeated lookup. */
    ok(Status == STATUS_RESOURCE_NAME_NOT_FOUND || Status == STATUS_RESOURCE_TYPE_NOT_FOUND,
       "Unexpected missing-resource status %lx\n", Status);
    Status = LdrFindResource_U(hmod, &info, 3, &entry);
    ok_ntstatus(Status, STATUS_RESOURCE_NAME_NOT_FOUND);
    Status = LdrFindResourceDirectory_U(hmod, &info, 2, &directory);
    ok_ntstatus(Status, STATUS_RESOURCE_NAME_NOT_FOUND);
}

START_TEST(LdrFindResource_U)
{
    Test_CORE_20401();
}
