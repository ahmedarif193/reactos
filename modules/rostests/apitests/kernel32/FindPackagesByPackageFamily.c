/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for FindPackagesByPackageFamily
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef LONG (WINAPI *PFIND_PACKAGES_BY_PACKAGE_FAMILY)(PCWSTR, UINT32, UINT32 *, PWSTR *, UINT32 *, WCHAR *, UINT32 *);

static VOID
CheckFunction(
    _In_ PFIND_PACKAGES_BY_PACKAGE_FAMILY Function,
    _In_ PCSTR Provider)
{
    UINT32 Count;
    UINT32 BufferLength;
    LONG Result;

    Count = 0;
    BufferLength = 0;
    Result = Function(L"OpenAI.Codex_8wekyb3d8bbwe", 0x10, &Count, NULL, &BufferLength, NULL, NULL);
    ok_long(Result, ERROR_SUCCESS);
    ok_long(Count, 0);
    ok_long(BufferLength, 0);

    Count = 0;
    BufferLength = 0;
    Result = Function(NULL, 0x10, &Count, NULL, &BufferLength, NULL, NULL);
    ok_long(Result, ERROR_INVALID_PARAMETER);

    Result = Function(L"not-a-family", 0x10, &Count, NULL, &BufferLength, NULL, NULL);
    ok_long(Result, ERROR_INVALID_PARAMETER);

    Result = Function(L"OpenAI.Codex_8wekyb3d8bbwe", 0x10, NULL, NULL, &BufferLength, NULL, NULL);
    ok_long(Result, ERROR_INVALID_PARAMETER);

    Result = Function(L"OpenAI.Codex_8wekyb3d8bbwe", 0x10, &Count, NULL, NULL, NULL, NULL);
    ok_long(Result, ERROR_INVALID_PARAMETER);

    Count = 0x11111111;
    BufferLength = 0x22222222;
    Result = Function(L"OpenAI.Codex_8wekyb3d8bbwe", 0x10, &Count, NULL, &BufferLength, NULL, NULL);
    ok_long(Result, ERROR_INVALID_PARAMETER);
    ok_long(Count, 0x11111111);
    ok_long(BufferLength, 0x22222222);

    trace("%s package-family contract complete\n", Provider);
}

START_TEST(FindPackagesByPackageFamily)
{
    PFIND_PACKAGES_BY_PACKAGE_FAMILY Kernel32Function;
    PFIND_PACKAGES_BY_PACKAGE_FAMILY KernelBaseFunction;
    DWORD Error;
    HMODULE Kernel32;
    HMODULE KernelBase;

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    Kernel32Function = (PFIND_PACKAGES_BY_PACKAGE_FAMILY)(PVOID)GetProcAddress(Kernel32, "FindPackagesByPackageFamily");
    ok(Kernel32Function != NULL, "FindPackagesByPackageFamily is not exported by kernel32\n");
    if (Kernel32Function)
        CheckFunction(Kernel32Function, "kernel32");

    KernelBase = LoadLibraryW(L"kernelbase.dll");
    Error = GetLastError();
    ok(KernelBase != NULL, "Failed to load kernelbase.dll: %lu\n", Error);
    KernelBaseFunction = KernelBase ? (PFIND_PACKAGES_BY_PACKAGE_FAMILY)(PVOID)GetProcAddress(KernelBase, "FindPackagesByPackageFamily") : NULL;
    ok(KernelBaseFunction != NULL, "FindPackagesByPackageFamily is not exported by kernelbase\n");
    if (KernelBaseFunction)
        CheckFunction(KernelBaseFunction, "kernelbase");

    if (KernelBase)
        FreeLibrary(KernelBase);
}
