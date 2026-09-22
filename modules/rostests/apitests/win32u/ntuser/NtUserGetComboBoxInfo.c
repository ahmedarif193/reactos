#include "../win32nt.h"

START_TEST(NtUserGetComboBoxInfo)
{
    COMBOBOXINFO Info;
    HWND hWnd;

    hWnd = CreateWindowExW(0, L"COMBOBOX", L"", WS_POPUP | CBS_DROPDOWN, 0, 0, 100, 100,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd) return;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    ok(NtUserGetComboBoxInfo(hWnd, &Info), "NtUserGetComboBoxInfo failed: %lu\n", GetLastError());
    ok(Info.hwndCombo == hWnd, "hwndCombo is %p, expected %p\n", Info.hwndCombo, hWnd);
    ok(IsWindow(Info.hwndItem), "hwndItem %p is not a window\n", Info.hwndItem);
    ok(IsWindow(Info.hwndList), "hwndList %p is not a window\n", Info.hwndList);
    ok(Info.rcButton.right > Info.rcButton.left, "Empty button rectangle\n");

    Info.cbSize = sizeof(Info) - 1;
    ok(!NtUserGetComboBoxInfo(hWnd, &Info), "NtUserGetComboBoxInfo accepted a short structure\n");

    DestroyWindow(hWnd);
}
