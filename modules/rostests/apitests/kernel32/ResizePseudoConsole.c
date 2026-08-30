/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ResizePseudoConsole
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef HRESULT (WINAPI *PCREATE_PSEUDO_CONSOLE)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
typedef VOID (WINAPI *PCLOSE_PSEUDO_CONSOLE)(HPCON);
typedef HRESULT (WINAPI *PRESIZE_PSEUDO_CONSOLE)(HPCON, COORD);

static VOID
CheckFunction(
    _In_ PRESIZE_PSEUDO_CONSOLE Function,
    _In_ PCSTR Provider)
{
    COORD Size;
    HRESULT Result;

    Size.X = 80;
    Size.Y = 25;
    Result = Function(NULL, Size);
    ok_hex(Result, E_INVALIDARG);

    Size.X = -1;
    Result = Function(NULL, Size);
    ok_hex(Result, E_INVALIDARG);

    Size.X = 80;
    Size.Y = -1;
    Result = Function(NULL, Size);
    ok_hex(Result, E_INVALIDARG);

    trace("%s pseudo-console resize validation complete\n", Provider);
}

START_TEST(ResizePseudoConsole)
{
    PCREATE_PSEUDO_CONSOLE Kernel32Create;
    PCLOSE_PSEUDO_CONSOLE Kernel32Close;
    PRESIZE_PSEUDO_CONSOLE Kernel32Function;
    PCREATE_PSEUDO_CONSOLE KernelBaseCreate;
    PCLOSE_PSEUDO_CONSOLE KernelBaseClose;
    PRESIZE_PSEUDO_CONSOLE KernelBaseFunction;
    HMODULE Kernel32;
    HMODULE KernelBase;
    DWORD Error;

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    Kernel32Create = (PCREATE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(Kernel32, "CreatePseudoConsole");
    ok(Kernel32Create != NULL, "CreatePseudoConsole is not exported by kernel32\n");
    Kernel32Close = (PCLOSE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(Kernel32, "ClosePseudoConsole");
    ok(Kernel32Close != NULL, "ClosePseudoConsole is not exported by kernel32\n");
    Kernel32Function = (PRESIZE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(Kernel32, "ResizePseudoConsole");
    ok(Kernel32Function != NULL, "ResizePseudoConsole is not exported by kernel32\n");
    if (Kernel32Function)
        CheckFunction(Kernel32Function, "kernel32");

    KernelBase = LoadLibraryW(L"kernelbase.dll");
    Error = GetLastError();
    ok(KernelBase != NULL, "Failed to load kernelbase.dll: %lu\n", Error);
    KernelBaseFunction = NULL;
    if (KernelBase)
    {
        KernelBaseCreate = (PCREATE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(KernelBase, "CreatePseudoConsole");
        ok(KernelBaseCreate != NULL, "CreatePseudoConsole is not exported by kernelbase\n");
        KernelBaseClose = (PCLOSE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(KernelBase, "ClosePseudoConsole");
        ok(KernelBaseClose != NULL, "ClosePseudoConsole is not exported by kernelbase\n");
        KernelBaseFunction = (PRESIZE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(KernelBase, "ResizePseudoConsole");
    }
    ok(KernelBaseFunction != NULL, "ResizePseudoConsole is not exported by kernelbase\n");
    if (KernelBaseFunction)
        CheckFunction(KernelBaseFunction, "kernelbase");

    if (KernelBase)
        FreeLibrary(KernelBase);
}
