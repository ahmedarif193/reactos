#include "precomp.h"

#ifdef __REACTOS__

int WINAPI COMCTL32_ReactOSDrawShadowText(HDC hdc, const WCHAR *text, UINT length, RECT *rect,
                                         DWORD flags, COLORREF text_color, COLORREF shadow_color,
                                         int offset_x, int offset_y)
{
    COLORREF old_text;
    RECT text_rect;
    INT ret, x, y, x2, y2;
    BYTE *bits;
    HBITMAP bitmap, old_bitmap;
    BITMAPINFO info;
    HDC memory_dc;
    HFONT old_font;
    BLENDFUNCTION blend;

    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = rect->right - rect->left + 4;
    info.bmiHeader.biHeight = rect->bottom - rect->top + 5;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(hdc, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!bitmap)
        return 0;

    memory_dc = CreateCompatibleDC(hdc);
    if (!memory_dc)
    {
        DeleteObject(bitmap);
        return 0;
    }

    old_bitmap = SelectObject(memory_dc, bitmap);
    old_font = SelectObject(memory_dc, GetCurrentObject(hdc, OBJ_FONT));
    SetTextColor(memory_dc, RGB(16, 16, 16));
    SetBkColor(memory_dc, RGB(0, 0, 0));
    SetBkMode(memory_dc, TRANSPARENT);
    SetRect(&text_rect, 0, 0, rect->right - rect->left, rect->bottom - rect->top);
    DrawTextW(memory_dc, text, length, &text_rect, flags);
    SelectObject(memory_dc, old_font);
    GdiFlush();

    for (x = 0; x < info.bmiHeader.biWidth; x += 1)
    {
        for (y = 0; y < info.bmiHeader.biHeight; y += 1)
        {
            BYTE *dest = &bits[(y * info.bmiHeader.biWidth + x) * 4];
            UINT alpha = 0;

            for (x2 = x - 3; x2 <= x; x2 += 1)
            {
                for (y2 = y; y2 < y + 5; y2 += 1)
                {
                    if (x2 >= 0 && x2 < info.bmiHeader.biWidth && y2 >= 0 && y2 < info.bmiHeader.biHeight)
                        alpha += bits[(y2 * info.bmiHeader.biWidth + x2) * 4];
                }
            }

            if (alpha > 255)
                alpha = 255;
            dest[3] = alpha;
        }
    }

    for (x = 0; x < info.bmiHeader.biWidth; x += 1)
    {
        for (y = 0; y < info.bmiHeader.biHeight; y += 1)
        {
            BYTE *dest = &bits[(y * info.bmiHeader.biWidth + x) * 4];
            dest[0] = GetBValue(shadow_color) * dest[3] / 255;
            dest[1] = GetGValue(shadow_color) * dest[3] / 255;
            dest[2] = GetRValue(shadow_color) * dest[3] / 255;
        }
    }

    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    GdiAlphaBlend(hdc, rect->left + offset_x - 3, rect->top + offset_y - 3,
                  info.bmiHeader.biWidth, info.bmiHeader.biHeight, memory_dc, 0, 0,
                  info.bmiHeader.biWidth, info.bmiHeader.biHeight, blend);

    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);

    old_text = SetTextColor(hdc, text_color);
    SetBkMode(hdc, TRANSPARENT);
    ret = DrawTextW(hdc, text, length, rect, flags);
    SetTextColor(hdc, old_text);
    return ret;
}

#endif /* __REACTOS__ */
