#include "../win32nt.h"

START_TEST(NtGdiPolyPatBlt)
{
    BITMAPINFO Info;
    POLYPATBLT Poly[2];
    PULONG Bits = NULL;
    HBITMAP Bitmap, OldBitmap;
    HBRUSH White, Red;
    HDC hdc;

    hdc = CreateCompatibleDC(NULL);
    ok(hdc != NULL, "CreateCompatibleDC failed\n");
    if (!hdc) return;

    ZeroMemory(&Info, sizeof(Info));
    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = 8;
    Info.bmiHeader.biHeight = -8;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    Bitmap = CreateDIBSection(hdc, &Info, DIB_RGB_COLORS, (PVOID *)&Bits, NULL, 0);
    ok(Bitmap != NULL && Bits != NULL, "CreateDIBSection failed\n");
    if (!Bitmap || !Bits)
    {
        DeleteDC(hdc);
        return;
    }
    OldBitmap = SelectObject(hdc, Bitmap);
    ZeroMemory(Bits, 8 * 8 * sizeof(ULONG));

    White = GetStockObject(WHITE_BRUSH);
    Red = CreateSolidBrush(RGB(255, 0, 0));
    Poly[0].nXLeft = 0;
    Poly[0].nYLeft = 0;
    Poly[0].nWidth = 4;
    Poly[0].nHeight = 8;
    Poly[0].hBrush = White;
    Poly[1].nXLeft = 4;
    Poly[1].nYLeft = 0;
    Poly[1].nWidth = 4;
    Poly[1].nHeight = 8;
    Poly[1].hBrush = Red;

    ok(NtGdiPolyPatBlt(hdc, PATCOPY, Poly, RTL_NUMBER_OF(Poly), 0), "NtGdiPolyPatBlt failed\n");
    GdiFlush();
    ok_hex(Bits[0] & 0xFFFFFF, 0xFFFFFF);
    ok_hex(Bits[7 * 8 + 3] & 0xFFFFFF, 0xFFFFFF);
    ok_hex(Bits[4] & 0xFFFFFF, 0xFF0000);
    ok_hex(Bits[7 * 8 + 7] & 0xFFFFFF, 0xFF0000);

    SelectObject(hdc, OldBitmap);
    DeleteObject(Red);
    DeleteObject(Bitmap);
    DeleteDC(hdc);
}
