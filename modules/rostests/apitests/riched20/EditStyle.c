/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests rich edit edit styles and bidirectional options
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <richedit.h>

#define CASE_STYLES (SES_UPPERCASE | SES_LOWERCASE)

static void
TypeText(HWND hWnd, PCWSTR Text)
{
    while (*Text)
        SendMessageW(hWnd, WM_CHAR, *Text++, 1);
}

static void
TestCaseStyles(HWND hWnd)
{
    WCHAR Text[32];
    LRESULT Style;

    Style = SendMessageW(hWnd, EM_GETEDITSTYLE, 0, 0);
    ok(!(Style & CASE_STYLES), "Unexpected initial style 0x%lx\n", (ULONG)Style);

    Style = SendMessageW(hWnd, EM_SETEDITSTYLE, SES_UPPERCASE, CASE_STYLES);
    ok((Style & CASE_STYLES) == SES_UPPERCASE, "EM_SETEDITSTYLE returned 0x%lx\n", (ULONG)Style);
    ok(SendMessageW(hWnd, EM_GETEDITSTYLE, 0, 0) == Style, "EM_GETEDITSTYLE does not match\n");

    SetWindowTextW(hWnd, L"");
    TypeText(hWnd, L"abC");
    GetWindowTextW(hWnd, Text, ARRAYSIZE(Text));
    ok(!wcscmp(Text, L"ABC"), "Typed text is %s\n", wine_dbgstr_w(Text));

    Style = SendMessageW(hWnd, EM_SETEDITSTYLE, SES_LOWERCASE, CASE_STYLES);
    ok((Style & CASE_STYLES) == SES_LOWERCASE, "EM_SETEDITSTYLE returned 0x%lx\n", (ULONG)Style);

    SetWindowTextW(hWnd, L"");
    TypeText(hWnd, L"DeF");
    GetWindowTextW(hWnd, Text, ARRAYSIZE(Text));
    ok(!wcscmp(Text, L"def"), "Typed text is %s\n", wine_dbgstr_w(Text));

    Style = SendMessageW(hWnd, EM_SETEDITSTYLE, 0, CASE_STYLES);
    ok(!(Style & CASE_STYLES), "EM_SETEDITSTYLE returned 0x%lx\n", (ULONG)Style);

    SetWindowTextW(hWnd, L"");
    TypeText(hWnd, L"gH");
    GetWindowTextW(hWnd, Text, ARRAYSIZE(Text));
    ok(!wcscmp(Text, L"gH"), "Typed text is %s\n", wine_dbgstr_w(Text));
}

static void
TestPastedCase(HWND hWnd)
{
    static const WCHAR Source[] = L"pasteMe";
    WCHAR Text[32];
    HGLOBAL Global;
    PVOID Pointer;

    Global = GlobalAlloc(GMEM_MOVEABLE, sizeof(Source));
    if (!Global)
    {
        skip("GlobalAlloc failed\n");
        return;
    }
    Pointer = GlobalLock(Global);
    CopyMemory(Pointer, Source, sizeof(Source));
    GlobalUnlock(Global);

    if (!OpenClipboard(hWnd))
    {
        GlobalFree(Global);
        skip("OpenClipboard failed: %lu\n", GetLastError());
        return;
    }
    EmptyClipboard();
    ok(SetClipboardData(CF_UNICODETEXT, Global) != NULL, "SetClipboardData failed: %lu\n", GetLastError());
    CloseClipboard();

    SendMessageW(hWnd, EM_SETEDITSTYLE, SES_UPPERCASE, CASE_STYLES);
    SetWindowTextW(hWnd, L"");
    SendMessageW(hWnd, WM_PASTE, 0, 0);
    GetWindowTextW(hWnd, Text, ARRAYSIZE(Text));
    ok(!wcscmp(Text, L"PASTEME"), "Pasted text is %s\n", wine_dbgstr_w(Text));
    SendMessageW(hWnd, EM_SETEDITSTYLE, 0, CASE_STYLES);

    if (OpenClipboard(hWnd))
    {
        EmptyClipboard();
        CloseClipboard();
    }
}

static void
TestBidiOptions(HWND hWnd)
{
    BIDIOPTIONS Options;

    ZeroMemory(&Options, sizeof(Options));
    Options.cbSize = sizeof(Options);
    Options.wMask = BOM_CONTEXTREADING;
    Options.wEffects = BOE_CONTEXTREADING;
    SendMessageW(hWnd, EM_SETBIDIOPTIONS, 0, (LPARAM)&Options);

    ZeroMemory(&Options, sizeof(Options));
    Options.cbSize = sizeof(Options);
    Options.wMask = BOM_CONTEXTREADING | BOM_CONTEXTALIGNMENT;
    SendMessageW(hWnd, EM_GETBIDIOPTIONS, 0, (LPARAM)&Options);
    ok(Options.wEffects & BOE_CONTEXTREADING, "Context reading is not set, effects 0x%x\n", Options.wEffects);
    ok(!(Options.wEffects & BOE_CONTEXTALIGNMENT), "Context alignment is set, effects 0x%x\n", Options.wEffects);

    ZeroMemory(&Options, sizeof(Options));
    Options.cbSize = sizeof(Options);
    Options.wMask = BOM_CONTEXTREADING;
    Options.wEffects = 0;
    SendMessageW(hWnd, EM_SETBIDIOPTIONS, 0, (LPARAM)&Options);

    ZeroMemory(&Options, sizeof(Options));
    Options.cbSize = sizeof(Options);
    Options.wMask = BOM_CONTEXTREADING | BOM_CONTEXTALIGNMENT;
    SendMessageW(hWnd, EM_GETBIDIOPTIONS, 0, (LPARAM)&Options);
    ok(!(Options.wEffects & BOE_CONTEXTREADING), "Context reading is still set, effects 0x%x\n", Options.wEffects);
}

START_TEST(EditStyle)
{
    HMODULE Module;
    HWND hWnd;

    Module = LoadLibraryW(L"riched20.dll");
    ok(Module != NULL, "LoadLibraryW failed: %lu\n", GetLastError());
    if (!Module)
        return;

    hWnd = CreateWindowExW(0, RICHEDIT_CLASS20W, NULL, WS_POPUP | ES_MULTILINE,
                           0, 0, 200, 100, NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (hWnd)
    {
        TestCaseStyles(hWnd);
        TestPastedCase(hWnd);
        TestBidiOptions(hWnd);
        DestroyWindow(hWnd);
    }

    FreeLibrary(Module);
}
