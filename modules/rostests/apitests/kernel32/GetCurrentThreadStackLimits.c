/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for GetCurrentThreadStackLimits
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef VOID (WINAPI *PGET_CURRENT_THREAD_STACK_LIMITS)(PULONG_PTR, PULONG_PTR);

static VOID
CheckFunction(
    _In_ PGET_CURRENT_THREAD_STACK_LIMITS Function,
    _In_ PCSTR Provider)
{
    ULONG_PTR Low = 0;
    ULONG_PTR High = 0;
    ULONG_PTR Marker = (ULONG_PTR)&Low;

    Function(&Low, &High);
    ok(Low == (ULONG_PTR)NtCurrentTeb()->DeallocationStack, "%s returned low limit %p, expected %p\n", Provider, (PVOID)Low, NtCurrentTeb()->DeallocationStack);
    ok(High == (ULONG_PTR)NtCurrentTeb()->NtTib.StackBase, "%s returned high limit %p, expected %p\n", Provider, (PVOID)High, NtCurrentTeb()->NtTib.StackBase);
    ok(Low < Marker && Marker < High, "%s returned limits %p..%p outside marker %p\n", Provider, (PVOID)Low, (PVOID)High, (PVOID)Marker);
}

START_TEST(GetCurrentThreadStackLimits)
{
    PGET_CURRENT_THREAD_STACK_LIMITS Kernel32Function;
    PGET_CURRENT_THREAD_STACK_LIMITS KernelBaseFunction;
    HMODULE Kernel32;
    HMODULE KernelBase;
    DWORD Error;

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    Kernel32Function = (PGET_CURRENT_THREAD_STACK_LIMITS)(PVOID)GetProcAddress(Kernel32, "GetCurrentThreadStackLimits");
    ok(Kernel32Function != NULL, "GetCurrentThreadStackLimits is not exported by kernel32\n");
    if (Kernel32Function)
        CheckFunction(Kernel32Function, "kernel32");

    KernelBase = LoadLibraryW(L"kernelbase.dll");
    Error = GetLastError();
    ok(KernelBase != NULL, "Failed to load kernelbase.dll: %lu\n", Error);
    KernelBaseFunction = NULL;
    if (KernelBase)
        KernelBaseFunction = (PGET_CURRENT_THREAD_STACK_LIMITS)(PVOID)GetProcAddress(KernelBase, "GetCurrentThreadStackLimits");
    ok(KernelBaseFunction != NULL, "GetCurrentThreadStackLimits is not exported by kernelbase\n");
    if (KernelBaseFunction)
        CheckFunction(KernelBaseFunction, "kernelbase");

    if (KernelBase)
        FreeLibrary(KernelBase);
}
