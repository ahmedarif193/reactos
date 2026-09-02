
#include "precomp.h"

static void test_stretch_sampling(void)
{
    static const DWORD source[4] = { 0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff };
    static const int sizes[] = { 4, 3, 6 };
    static const WORD maskBits[2] = { 0, 0 };
    BITMAPINFO bmi;
    ICONINFO ii;
    HICON icon;
    HBITMAP color, mask;
    DWORD *bits;
    UINT i;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 2;
    bmi.bmiHeader.biHeight = -2;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    color = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    ok(color != NULL, "CreateDIBSection failed\n");
    if (!color)
        return;
    CopyMemory(bits, source, sizeof(source));
    mask = CreateBitmap(2, 2, 1, 1, maskBits);

    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    icon = CreateIconIndirect(&ii);
    ok(icon != NULL, "CreateIconIndirect failed\n");
    DeleteObject(color);
    DeleteObject(mask);
    if (!icon)
        return;

    for (i = 0; i < ARRAYSIZE(sizes); i++)
    {
        int size = sizes[i], x, y;
        HBITMAP target, old;
        HDC hdc;
        DWORD *pixels;
        BOOL ret;

        bmi.bmiHeader.biWidth = size;
        bmi.bmiHeader.biHeight = -size;
        target = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
        hdc = CreateCompatibleDC(NULL);
        ok(target != NULL && hdc != NULL, "cannot create a %dx%d target\n", size, size);
        if (!target || !hdc)
            continue;
        for (y = 0; y < size * size; y++)
            pixels[y] = 0xff7f7f7f;
        old = SelectObject(hdc, target);

        ret = DrawIconEx(hdc, 0, 0, icon, size, size, 0, NULL, DI_NORMAL);
        ok(ret, "DrawIconEx %dx%d failed\n", size, size);
        GdiFlush();

        for (y = 0; y < size; y++)
        {
            for (x = 0; x < size; x++)
            {
                DWORD expected = source[(y * 2 / size) * 2 + (x * 2 / size)] & 0xffffff;
                DWORD got = pixels[y * size + x] & 0xffffff;
                ok(got == expected, "%dx%d (%d,%d): got %06lx, point sampling expects %06lx\n",
                   size, size, x, y, got, expected);
            }
            if (size == 3)
                trace("3x3 row %d: %06lx %06lx %06lx\n", y, pixels[y * 3] & 0xffffff,
                      pixels[y * 3 + 1] & 0xffffff, pixels[y * 3 + 2] & 0xffffff);
        }

        SelectObject(hdc, old);
        DeleteObject(target);
        DeleteDC(hdc);
    }

    DestroyIcon(icon);
}

START_TEST(DrawIconEx)
{
    HCURSOR hcursor;
    HBITMAP hbmp;
    ICONINFO ii;
    HDC hdcScreen, hdc;
    BOOL ret;
    HBRUSH hbrush;

    ZeroMemory(&ii, sizeof(ii));

    ii.hbmMask = CreateBitmap(8, 16, 1, 1, NULL);
    ok(ii.hbmMask != NULL, "\n");
    hcursor = CreateIconIndirect(&ii);
    ok(hcursor != NULL, "\n");
    DeleteObject(ii.hbmMask);

    hdcScreen = GetDC(0);
    hbmp = CreateCompatibleBitmap(hdcScreen, 8, 8);
    ok(hbmp != NULL, "\n");
    hdc = CreateCompatibleDC(hdcScreen);
    ok(hdc != NULL, "\n");
    ReleaseDC(0, hdcScreen);

    hbmp = SelectObject(hdc, hbmp);
    ok(hbmp != NULL, "\n");

    hbrush = GetStockObject(DKGRAY_BRUSH);
    ok(hbrush != NULL, "\n");

    ret = DrawIconEx(hdc, 0, 0, hcursor, 8, 8, 0, hbrush, DI_NORMAL);
    ok(ret, "\n");
    DestroyCursor(hcursor);

    /* Try with color */
    ii.hbmMask = CreateBitmap(8, 8, 1, 1, NULL);
    ok(ii.hbmMask != NULL, "\n");
    ii.hbmColor = CreateBitmap(8, 8, 16, 1, NULL);
    ok(ii.hbmColor != NULL, "\n");
    hcursor = CreateIconIndirect(&ii);
    ok(hcursor != NULL, "\n");
    DeleteObject(ii.hbmMask);
    DeleteObject(ii.hbmColor);

    ret = DrawIconEx(hdc, 0, 0, hcursor, 8, 8, 0, hbrush, DI_NORMAL);
    ok(ret, "\n");
    DestroyCursor(hcursor);

    hbmp = SelectObject(hdc, hbmp);
    DeleteObject(hbmp);
    DeleteDC(hdc);

    test_stretch_sampling();
}
