/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests GetClipboardData results and register preservation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifdef __i386__
BOOL __cdecl CallPreservingRegisters(PVOID Function, UINT_PTR Argument);
__asm__(
    ".text\n"
    ".globl _CallPreservingRegisters\n"
    "_CallPreservingRegisters:\n\t"
    "pushl %ebx\n\t"
    "pushl %esi\n\t"
    "pushl %edi\n\t"
    "movl $0x11111111, %ebx\n\t"
    "movl $0x22222222, %esi\n\t"
    "movl $0x33333333, %edi\n\t"
    "pushl 20(%esp)\n\t"
    "call *20(%esp)\n\t"
    "xorl %eax, %eax\n\t"
    "cmpl $0x11111111, %ebx\n\t"
    "jne 1f\n\t"
    "cmpl $0x22222222, %esi\n\t"
    "jne 1f\n\t"
    "cmpl $0x33333333, %edi\n\t"
    "jne 1f\n\t"
    "incl %eax\n"
    "1:\n\t"
    "popl %edi\n\t"
    "popl %esi\n\t"
    "popl %ebx\n\t"
    "ret\n");
#endif

START_TEST(ClipboardData)
{
    static const WCHAR Text[] = L"ReactOS parity";
    HWND Owner;
    HGLOBAL Global;
    HANDLE Data;
    UINT Missing;
    PVOID Pointer;

    Missing = RegisterClipboardFormatW(L"ReactOS ClipboardData Missing");
    ok(Missing != 0, "RegisterClipboardFormatW failed: %lu\n", GetLastError());

    Owner = CreateWindowExW(0, L"static", NULL, WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    ok(Owner != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!Owner)
        return;

    Global = GlobalAlloc(GMEM_MOVEABLE, sizeof(Text));
    ok(Global != NULL, "GlobalAlloc failed\n");
    if (!Global)
    {
        DestroyWindow(Owner);
        return;
    }
    Pointer = GlobalLock(Global);
    CopyMemory(Pointer, Text, sizeof(Text));
    GlobalUnlock(Global);

    ok(OpenClipboard(Owner), "OpenClipboard failed: %lu\n", GetLastError());
    ok(EmptyClipboard(), "EmptyClipboard failed: %lu\n", GetLastError());
    ok(SetClipboardData(CF_UNICODETEXT, Global) == Global, "SetClipboardData failed: %lu\n", GetLastError());
    ok(CloseClipboard(), "CloseClipboard failed: %lu\n", GetLastError());

    ok(OpenClipboard(Owner), "OpenClipboard failed: %lu\n", GetLastError());

    ok(GetClipboardData(Missing) == NULL, "GetClipboardData returned data for a missing format\n");

    Data = GetClipboardData(CF_UNICODETEXT);
    ok(Data != NULL, "GetClipboardData(CF_UNICODETEXT) failed: %lu\n", GetLastError());
    if (Data)
    {
        Pointer = GlobalLock(Data);
        ok(Pointer && !wcscmp(Pointer, Text), "Unexpected text %s\n", wine_dbgstr_w(Pointer));
        GlobalUnlock(Data);
    }

    Data = GetClipboardData(CF_TEXT);
    ok(Data != NULL, "GetClipboardData(CF_TEXT) failed: %lu\n", GetLastError());
    if (Data)
    {
        Pointer = GlobalLock(Data);
        ok(Pointer && !strcmp(Pointer, "ReactOS parity"), "Unexpected text %s\n", wine_dbgstr_a(Pointer));
        GlobalUnlock(Data);
    }

#ifdef __i386__
    ok(CallPreservingRegisters(GetClipboardData, Missing), "GetClipboardData changed a callee-saved register\n");
    ok(CallPreservingRegisters(GetClipboardData, CF_UNICODETEXT), "GetClipboardData changed a callee-saved register\n");
    ok(CallPreservingRegisters(GetClipboardData, CF_TEXT), "GetClipboardData changed a callee-saved register\n");
#endif

    ok(EmptyClipboard(), "EmptyClipboard failed: %lu\n", GetLastError());
    ok(CloseClipboard(), "CloseClipboard failed: %lu\n", GetLastError());
    DestroyWindow(Owner);
}
