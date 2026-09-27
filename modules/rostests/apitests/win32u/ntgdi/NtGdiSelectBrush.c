/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test for NtGdiSelectBrush
 * PROGRAMMERS:
 */

#include "../win32nt.h"

START_TEST(NtGdiSelectBrush)
{
    HDC hDC;
    HBRUSH hBrush, hOldBrush;

    hDC = CreateDCW(L"DISPLAY", NULL, NULL, NULL);

    hBrush = GetStockObject(GRAY_BRUSH);

    /* Test NULL DC */
    SetLastError(ERROR_SUCCESS);
    hOldBrush = NtGdiSelectBrush(NULL, hBrush);
    ok_ptr(hOldBrush, NULL);
    ok_long(GetLastError(), ERROR_SUCCESS);

    /* Test invalid DC */
    SetLastError(ERROR_SUCCESS);
    hOldBrush = NtGdiSelectBrush((HDC)((ULONG_PTR)hDC & 0x0000ffff), hBrush);
    ok_ptr(hOldBrush, NULL);
    ok_long(GetLastError(), ERROR_SUCCESS);

    /* Test NULL brush */
    SetLastError(ERROR_SUCCESS);
    hOldBrush = NtGdiSelectBrush(hDC, NULL);
    ok_ptr(hOldBrush, NULL);
    ok_long(GetLastError(), ERROR_SUCCESS);

    /* Test invalid brush */
    SetLastError(ERROR_SUCCESS);
    hOldBrush = NtGdiSelectBrush(hDC, (HBRUSH)((ULONG_PTR)hBrush & 0x0000ffff));
    ok_ptr(hOldBrush, NULL);
    ok_long(GetLastError(), ERROR_SUCCESS);

    SetLastError(ERROR_SUCCESS);
    hOldBrush = NtGdiSelectBrush(hDC, hBrush);
    ok(hOldBrush != NULL, "hOldBrush was NULL\n");
    hOldBrush = NtGdiSelectBrush(hDC, hOldBrush);
    ok_ptr(hOldBrush, hBrush);
    ok_long(GetLastError(), ERROR_SUCCESS);

    /* Begin with a white brush */
    NtGdiSelectBrush(hDC, GetStockObject(WHITE_BRUSH));
    /* Select a brush in user mode */
    SelectObject(hDC, GetStockObject(BLACK_BRUSH));
    /* See what we get returned */
    hOldBrush = NtGdiSelectBrush(hDC, GetStockObject(WHITE_BRUSH));
    ok_ptr(hOldBrush, GetStockObject(BLACK_BRUSH));

    DeleteDC(hDC);
}

