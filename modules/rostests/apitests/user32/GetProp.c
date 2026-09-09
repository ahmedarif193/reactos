/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Window property lookup regression tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <wine/test.h>

START_TEST(GetProp)
{
    WNDCLASSW Class = {0};
    HWND Window, Other;
    ATOM Atom;
    HANDLE Values[] = {&Class, NULL, (HANDLE)(ULONG_PTR)0x89abcdef, (HANDLE)~(ULONG_PTR)0};
    HANDLE Value;
    unsigned i;

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"GetPropTest";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed: %lu\n", GetLastError());
    Window = CreateWindowW(Class.lpszClassName, L"Property owner", WS_POPUP, 0, 0, 10, 10, NULL, NULL, Class.hInstance, NULL);
    Other = CreateWindowW(Class.lpszClassName, L"Other window", WS_POPUP, 0, 0, 10, 10, NULL, NULL, Class.hInstance, NULL);
    ok(Window && Other, "CreateWindowW failed: %lu\n", GetLastError());
    if (!Window || !Other)
        goto Cleanup;

    Atom = GlobalAddAtomW(L"GetPropTest.Value");
    ok(Atom != 0, "GlobalAddAtomW failed: %lu\n", GetLastError());
    if (!Atom)
        goto Cleanup;

    for (i = 0; i < sizeof(Values) / sizeof(Values[0]); ++i)
    {
        ok(SetPropA(Window, "GetPropTest.Value", Values[i]), "SetPropA failed: %lu\n", GetLastError());
        Value = GetPropW(Window, L"GetPropTest.Value");
        ok(Value == Values[i], "GetPropW(name) returned %p, expected %p\n", Value, Values[i]);
        Value = GetPropA(Window, "GetPropTest.Value");
        ok(Value == Values[i], "GetPropA(name) returned %p, expected %p\n", Value, Values[i]);
        Value = GetPropW(Window, (LPCWSTR)MAKEINTATOM(Atom));
        ok(Value == Values[i], "GetPropW(atom) returned %p, expected %p\n", Value, Values[i]);
        Value = GetPropA(Window, (LPCSTR)MAKEINTATOM(Atom));
        ok(Value == Values[i], "GetPropA(atom) returned %p, expected %p\n", Value, Values[i]);
        ok(GetPropW(Other, (LPCWSTR)MAKEINTATOM(Atom)) == NULL, "Property was returned for another window\n");
        Value = RemovePropW(Window, (LPCWSTR)MAKEINTATOM(Atom));
        ok(Value == Values[i], "RemovePropW returned %p, expected %p\n", Value, Values[i]);
        ok(GetPropW(Window, L"GetPropTest.Value") == NULL, "Removed property was returned\n");
    }

    ok(SetPropW(Window, (LPCWSTR)MAKEINTATOM(Atom), &Atom), "SetPropW failed: %lu\n", GetLastError());
    Value = GetPropA(Window, "GetPropTest.Value");
    ok(Value == &Atom, "GetPropA after SetPropW returned %p, expected %p\n", Value, &Atom);
    Value = RemovePropA(Window, "GetPropTest.Value");
    ok(Value == &Atom, "RemovePropA returned %p, expected %p\n", Value, &Atom);
    GlobalDeleteAtom(Atom);

Cleanup:
    if (Other)
        DestroyWindow(Other);
    if (Window)
        DestroyWindow(Window);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
