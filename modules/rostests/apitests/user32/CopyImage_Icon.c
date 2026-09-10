/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Generated icon/cursor resizing, masks, hotspots and alpha
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>

START_TEST(CopyImage_Icon)
{
    static const struct { int width, height; UINT flags; } cases[] =
    {
        { 16, 16, 0 }, { 48, 48, 0 }, { 17, 13, 0 },
        { 0, 17, 0 }, { 17, 0, 0 }, { 0, 0, 0 },
        { 0, 0, LR_DEFAULTSIZE }, { 16, 16, LR_COPYFROMRESOURCE },
        { 32, 32, LR_COPYRETURNORG | LR_COPYDELETEORG }
    };
    BITMAPINFO info = { 0 };
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP color;
    DWORD *bits, masks[64] = { 0 }, copied[16 * 16];
    UINT x, y, i, mono, icon;

    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 32;
    info.bmiHeader.biHeight = -32;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    color = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    ok(dc && color, "Could not create source DIB\n");
    if (!dc || !color)
        goto done;
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
        {
            DWORD alpha = (x % 4 == 0 || x == y) ? 255 : x % 4 == 1 ? 96 : 0;
            bits[y * 32 + x] = (alpha << 24) | 0xffffff;
        }

    for (mono = 0; mono < 2; ++mono)
    for (icon = 0; icon < 2; ++icon)
    {
        ICONINFO source = { 0 };
        HICON original;

        source.fIcon = icon;
        source.xHotspot = 5;
        source.yHotspot = 11;
        source.hbmMask = CreateBitmap(32, mono ? 64 : 32, 1, 1, masks);
        source.hbmColor = mono ? NULL : color;
        original = CreateIconIndirect(&source);
        ok(!!original, "Could not create source icon, error %lu\n", GetLastError());
        if (!original)
        {
            DeleteObject(source.hbmMask);
            continue;
        }
        for (i = 0; i < ARRAY_SIZE(cases); ++i)
        {
            ICONINFO output = { 0 };
            BITMAP bm = { 0 };
            int width = cases[i].width, height = cases[i].height;
            HICON copy;
            BOOL ret;

            if (!width)
                width = cases[i].flags & LR_DEFAULTSIZE ? GetSystemMetrics(icon ? SM_CXICON : SM_CXCURSOR) : 32;
            if (!height)
                height = cases[i].flags & LR_DEFAULTSIZE ? GetSystemMetrics(icon ? SM_CYICON : SM_CYCURSOR) : 32;
            copy = CopyImage(original, icon ? IMAGE_ICON : IMAGE_CURSOR, cases[i].width, cases[i].height, cases[i].flags);
            ok(!!copy, "mono=%u icon=%u case=%u: CopyImage failed, error %lu\n", mono, icon, i, GetLastError());
            if (!copy)
                continue;
            ret = GetIconInfo(copy, &output);
            ok(ret, "GetIconInfo failed\n");
            if (ret)
            {
                ok(output.fIcon == (BOOL)icon, "Wrong image type\n");
                ok(output.xHotspot == (DWORD)MulDiv(icon ? 16 : 5, width, 32), "Wrong x hotspot %lu\n", output.xHotspot);
                ok(output.yHotspot == (DWORD)MulDiv(icon ? 16 : 11, height, 32), "Wrong y hotspot %lu\n", output.yHotspot);
                ok(!!output.hbmColor == !mono, "Wrong color plane presence\n");
                GetObjectW(output.hbmMask, sizeof(bm), &bm);
                ok(bm.bmWidth == width && bm.bmHeight == height * (mono ? 2 : 1) && bm.bmBitsPixel == 1, "Incorrect mask %ldx%ld/%u\n", bm.bmWidth, bm.bmHeight, bm.bmBitsPixel);
                if (!mono)
                {
                    GetObjectW(output.hbmColor, sizeof(bm), &bm);
                    ok(bm.bmWidth == width && bm.bmHeight == height, "Incorrect color size %ldx%ld, expected %dx%d\n", bm.bmWidth, bm.bmHeight, width, height);
                    if (width == 16 && height == 16 && bm.bmWidth == 16 && bm.bmHeight == 16)
                    {
                        BITMAPINFO copyInfo = info;
                        copyInfo.bmiHeader.biWidth = 16;
                        copyInfo.bmiHeader.biHeight = -16;
                        ret = GetDIBits(dc, output.hbmColor, 0, 16, copied, &copyInfo, DIB_RGB_COLORS) == 16;
                        ok(ret, "Could not read copied pixels\n");
                        if (ret)
                            for (y = 0; y < 16; ++y)
                                for (x = 0; x < 16; ++x)
                                {
                                    UINT src = y * 64 + x * 2;
                                    UINT alpha = ((bits[src] >> 24) + (bits[src + 1] >> 24) + (bits[src + 32] >> 24) + (bits[src + 33] >> 24)) / 4;
                                    DWORD expected = (alpha << 24) | 0xffffff;
                                    ok(copied[y * 16 + x] == expected, "(%u,%u): got %#lx, expected %#lx\n", x, y, copied[y * 16 + x], expected);
                                }
                    }
                }
                DeleteObject(output.hbmColor);
                DeleteObject(output.hbmMask);
            }
            ok((copy == original) == !!(cases[i].flags & LR_COPYRETURNORG), "Incorrect handle reuse\n");
            if (copy != original)
                DestroyIcon(copy);
        }
        SetLastError(0xdeadbeef);
        ok(!CopyImage(original, icon ? IMAGE_ICON : IMAGE_CURSOR, -1, 16, 0), "Negative dimensions accepted\n");
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "Wrong error %lu\n", GetLastError());
        ok(DestroyIcon(original), "The original was unexpectedly destroyed\n");
        DeleteObject(source.hbmMask);
    }
done:
    DeleteObject(color);
    DeleteDC(dc);
}
