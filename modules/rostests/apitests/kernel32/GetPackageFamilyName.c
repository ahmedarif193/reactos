/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for GetPackageFamilyName
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef LONG (WINAPI *PGET_PACKAGE_FAMILY_NAME)(HANDLE, UINT32 *, PWSTR);

static VOID
CheckFunction(
    _In_ PGET_PACKAGE_FAMILY_NAME Function,
    _In_ PCSTR Provider)
{
    WCHAR Buffer[32];
    UINT32 Length;
    DWORD Error;
    HANDLE Process;
    LONG Result;

    Process = GetCurrentProcess();

    Length = 0;
    SetLastError(0x33333333);
    Result = Function(Process, &Length, NULL);
    Error = GetLastError();
    ok_long(Result, APPMODEL_ERROR_NO_PACKAGE);
    ok_long(Length, 0);
    ok_long(Error, 0x33333333);

    wcscpy(Buffer, L"unchanged");
    Length = _countof(Buffer);
    SetLastError(0x33333333);
    Result = Function(Process, &Length, Buffer);
    Error = GetLastError();
    ok_long(Result, APPMODEL_ERROR_NO_PACKAGE);
    ok_long(Length, _countof(Buffer));
    ok_wstr(Buffer, L"unchanged");
    ok_long(Error, 0x33333333);

    Length = _countof(Buffer);
    SetLastError(0x33333333);
    Result = Function(NULL, &Length, Buffer);
    Error = GetLastError();
    ok_long(Result, ERROR_INVALID_PARAMETER);
    ok_long(Length, _countof(Buffer));
    ok_long(Error, 0x33333333);

    SetLastError(0x33333333);
    Result = Function(Process, NULL, NULL);
    Error = GetLastError();
    ok_long(Result, ERROR_INVALID_PARAMETER);
    ok_long(Error, 0x33333333);

    Length = _countof(Buffer);
    SetLastError(0x33333333);
    Result = Function(INVALID_HANDLE_VALUE, &Length, Buffer);
    Error = GetLastError();
    ok_long(Result, APPMODEL_ERROR_NO_PACKAGE);
    ok_long(Length, _countof(Buffer));
    ok_long(Error, 0x33333333);

    trace("%s package-family query contract complete\n", Provider);
}

START_TEST(GetPackageFamilyName)
{
    PGET_PACKAGE_FAMILY_NAME Kernel32Function;
    PGET_PACKAGE_FAMILY_NAME KernelBaseFunction;
    DWORD Error;
    HMODULE Kernel32;
    HMODULE KernelBase;

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    Kernel32Function = (PGET_PACKAGE_FAMILY_NAME)(PVOID)GetProcAddress(Kernel32, "GetPackageFamilyName");
    ok(Kernel32Function != NULL, "GetPackageFamilyName is not exported by kernel32\n");
    if (Kernel32Function)
        CheckFunction(Kernel32Function, "kernel32");

    KernelBase = LoadLibraryW(L"kernelbase.dll");
    Error = GetLastError();
    ok(KernelBase != NULL, "Failed to load kernelbase.dll: %lu\n", Error);
    KernelBaseFunction = NULL;
    if (KernelBase)
        KernelBaseFunction = (PGET_PACKAGE_FAMILY_NAME)(PVOID)GetProcAddress(KernelBase, "GetPackageFamilyName");
    ok(KernelBaseFunction != NULL, "GetPackageFamilyName is not exported by kernelbase\n");
    if (KernelBaseFunction)
        CheckFunction(KernelBaseFunction, "kernelbase");

    if (KernelBase)
        FreeLibrary(KernelBase);
}
