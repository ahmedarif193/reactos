/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Image mappings must honor the caller's address limit
 */

#include "precomp.h"

START_TEST(NtMapViewOfSection_ImageZeroBits)
{
    static const ULONG_PTR Masks[] = { 0x7fffffff, 0x0fffffff };
    WCHAR Path[MAX_PATH];
    HANDLE File, Section;
    PVOID Views[2];
    SIZE_T Size;
    NTSTATUS Status;
    ULONG Length, i, j;

    Length = GetWindowsDirectoryW(Path, ARRAYSIZE(Path));
    ok(Length && Length + ARRAYSIZE(L"\\system32\\apphelp.dll") <= ARRAYSIZE(Path),
       "Invalid Windows directory length %lu\n", Length);
    if (!Length || Length + ARRAYSIZE(L"\\system32\\apphelp.dll") > ARRAYSIZE(Path)) return;
    wcscpy(Path + Length, L"\\system32\\apphelp.dll");
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Open image failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE) return;
    Status = NtCreateSection(&Section, SECTION_ALL_ACCESS, NULL, NULL, PAGE_READONLY, SEC_IMAGE, File);
    CloseHandle(File);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    for (i = 0; i < ARRAYSIZE(Masks); i++)
    {
        /* Keep the first view mapped to exercise preferred-base collisions. */
        for (j = 0; j < ARRAYSIZE(Views); j++)
        {
            Views[j] = NULL;
            Size = 0;
            Status = NtMapViewOfSection(Section, NtCurrentProcess(), &Views[j], Masks[i], 0, NULL, &Size, ViewShare, 0, PAGE_READONLY);
            ok(NT_SUCCESS(Status), "Map mask %Ix view %lu failed: %lx\n", Masks[i], j, Status);
            if (!NT_SUCCESS(Status))
            {
                Views[j] = NULL;
                continue;
            }
            ok(Size && (ULONG_PTR)Views[j] <= Masks[i] && Size - 1 <= Masks[i] - (ULONG_PTR)Views[j],
               "View %p size %Ix exceeds mask %Ix\n", Views[j], Size, Masks[i]);
            ok(!((ULONG_PTR)Views[j] & 0xffff), "Unaligned view %p\n", Views[j]);
        }
        if (Views[0] && Views[1]) ok(Views[0] != Views[1], "Views overlap at %p\n", Views[0]);
        for (j = 0; j < ARRAYSIZE(Views); j++)
        {
            if (!Views[j]) continue;
            Status = NtUnmapViewOfSection(NtCurrentProcess(), Views[j]);
            ok_ntstatus(Status, STATUS_SUCCESS);
        }
    }
    Status = NtClose(Section);
    ok_ntstatus(Status, STATUS_SUCCESS);
}
