/*
 * PROJECT:     ReactOS Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Modern Explorer icon rendering
 */

#include "frame.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer11);

#define E11_SS 3

typedef struct _E11AA
{
    HDC hdc;
    HBITMAP hbm;
    HGDIOBJ hbmOld;
    RECT rc;
} E11AA;

static HDC
E11AABegin(E11AA *pAA, HDC hdcRef, const RECT *prc, COLORREF crBg)
{
    int w = (prc->right - prc->left) * E11_SS;
    int h = (prc->bottom - prc->top) * E11_SS;

    pAA->rc = *prc;
    pAA->hdc = CreateCompatibleDC(hdcRef);
    pAA->hbm = CreateCompatibleBitmap(hdcRef, w, h);
    pAA->hbmOld = SelectObject(pAA->hdc, pAA->hbm);

    RECT rcFill = { 0, 0, w, h };
    HBRUSH hbr = CreateSolidBrush(crBg);
    FillRect(pAA->hdc, &rcFill, hbr);
    DeleteObject(hbr);
    return pAA->hdc;
}

static VOID
E11AAEnd(E11AA *pAA, HDC hdcTarget)
{
    int w = pAA->rc.right - pAA->rc.left;
    int h = pAA->rc.bottom - pAA->rc.top;
    POINT ptOrg;
    int mode = SetStretchBltMode(hdcTarget, HALFTONE);
    SetBrushOrgEx(hdcTarget, 0, 0, &ptOrg);
    StretchBlt(hdcTarget, pAA->rc.left, pAA->rc.top, w, h,
               pAA->hdc, 0, 0, w * E11_SS, h * E11_SS, SRCCOPY);
    SetStretchBltMode(hdcTarget, mode);
    SetBrushOrgEx(hdcTarget, ptOrg.x, ptOrg.y, NULL);
    SelectObject(pAA->hdc, pAA->hbmOld);
    DeleteObject(pAA->hbm);
    DeleteDC(pAA->hdc);
}

static COLORREF
E11Mix(COLORREF a, COLORREF b, int nB255)
{
    int nA = 255 - nB255;
    return RGB((GetRValue(a) * nA + GetRValue(b) * nB255) / 255,
               (GetGValue(a) * nA + GetGValue(b) * nB255) / 255,
               (GetBValue(a) * nA + GetBValue(b) * nB255) / 255);
}

static VOID
E11FillRoundRect(HDC hdc, int l, int t, int r, int b, int rad, COLORREF cr)
{
    HBRUSH hbr = CreateSolidBrush(cr);
    HPEN hpen = CreatePen(PS_SOLID, 1, cr);
    HGDIOBJ hbrOld = SelectObject(hdc, hbr);
    HGDIOBJ hpenOld = SelectObject(hdc, hpen);
    RoundRect(hdc, l, t, r, b, rad, rad);
    SelectObject(hdc, hpenOld);
    SelectObject(hdc, hbrOld);
    DeleteObject(hbr);
    DeleteObject(hpen);
}

static VOID
E11FillPoly(HDC hdc, const POINT *pts, int n, COLORREF cr)
{
    HBRUSH hbr = CreateSolidBrush(cr);
    HGDIOBJ hbrOld = SelectObject(hdc, hbr);
    HGDIOBJ hpenOld = SelectObject(hdc, GetStockObject(NULL_PEN));
    Polygon(hdc, (POINT *)pts, n);
    SelectObject(hdc, hpenOld);
    SelectObject(hdc, hbrOld);
    DeleteObject(hbr);
}

static VOID
E11Stroke(HDC hdc, int nWidth, COLORREF cr, const POINT *pts, int n, BOOL bClose)
{
    HPEN hpen = CreatePen(PS_SOLID, nWidth, cr);
    HGDIOBJ hpenOld = SelectObject(hdc, hpen);
    MoveToEx(hdc, pts[0].x, pts[0].y, NULL);
    for (int i = 1; i < n; i++)
        LineTo(hdc, pts[i].x, pts[i].y);
    if (bClose)
        LineTo(hdc, pts[0].x, pts[0].y);
    SelectObject(hdc, hpenOld);
    DeleteObject(hpen);
}

static VOID
E11StrokeRect(HDC hdc, int nWidth, COLORREF cr, int l, int t, int r, int b)
{
    POINT pts[4] = { { l, t }, { r, t }, { r, b }, { l, b } };
    E11Stroke(hdc, nWidth, cr, pts, 4, TRUE);
}

static VOID
E11Circle(HDC hdc, int cx, int cy, int r, int nWidth, COLORREF cr, BOOL bFill)
{
    HPEN hpen = CreatePen(PS_SOLID, nWidth, cr);
    HBRUSH hbr = bFill ? CreateSolidBrush(cr) : (HBRUSH)GetStockObject(NULL_BRUSH);
    HGDIOBJ hpenOld = SelectObject(hdc, hpen);
    HGDIOBJ hbrOld = SelectObject(hdc, hbr);
    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(hdc, hbrOld);
    SelectObject(hdc, hpenOld);
    DeleteObject(hpen);
    if (bFill)
        DeleteObject(hbr);
}

static VOID
E11DrawFolderShape(HDC hdc, int w, int h, COLORREF crBody)
{
    COLORREF crBack = E11Mix(crBody, RGB(0, 0, 0), 60);
    int tabW = w * 42 / 100;
    int topY = h * 18 / 100;
    int tabY = h * 8 / 100;
    int rad = w / 9;

    E11FillRoundRect(hdc, w / 24, tabY, tabW, topY + rad, rad, crBack);
    E11FillRoundRect(hdc, w / 24, topY, w - w / 24, h - h / 12, rad, crBack);
    E11FillRoundRect(hdc, w / 24, topY + h / 12, w - w / 24, h - h / 12, rad, crBody);
}

static VOID
E11FolderGlyphColor(int nIcon, COLORREF *pcrBody, int *pnGlyph)
{
    switch (nIcon)
    {
        case EI_FOLDER_DESKTOP:   *pcrBody = RGB(60, 140, 231);  *pnGlyph = 1; break;
        case EI_FOLDER_DOCS:      *pcrBody = RGB(255, 183, 80);  *pnGlyph = 2; break;
        case EI_FOLDER_DOWNLOADS: *pcrBody = RGB(76, 175, 80);   *pnGlyph = 3; break;
        case EI_FOLDER_MUSIC:     *pcrBody = RGB(240, 98, 100);  *pnGlyph = 4; break;
        case EI_FOLDER_PICTURES:  *pcrBody = RGB(66, 180, 175);  *pnGlyph = 5; break;
        case EI_FOLDER_VIDEOS:    *pcrBody = RGB(158, 114, 235); *pnGlyph = 6; break;
        default:                  *pcrBody = RGB(255, 200, 90);  *pnGlyph = 0; break;
    }
}

static VOID
E11DrawFolderIcon(HDC hdc, int w, int h, int nIcon)
{
    COLORREF crBody;
    int nGlyph;
    COLORREF crGlyph = RGB(255, 255, 255);
    int cx = w / 2, cy = h * 60 / 100;
    int g = w / 4;

    E11FolderGlyphColor(nIcon, &crBody, &nGlyph);
    E11DrawFolderShape(hdc, w, h, crBody);

    switch (nGlyph)
    {
        case 1:
        {
            E11StrokeRect(hdc, w / 14, crGlyph, cx - g, cy - g * 3 / 4, cx + g, cy + g / 3);
            POINT stand[2] = { { cx, cy + g / 3 }, { cx, cy + g * 2 / 3 } };
            E11Stroke(hdc, w / 14, crGlyph, stand, 2, FALSE);
            POINT base[2] = { { cx - g / 2, cy + g * 2 / 3 }, { cx + g / 2, cy + g * 2 / 3 } };
            E11Stroke(hdc, w / 14, crGlyph, base, 2, FALSE);
            break;
        }
        case 2:
        {
            for (int i = -1; i <= 1; i++)
            {
                POINT line[2] = { { cx - g, cy + i * g / 2 }, { cx + g, cy + i * g / 2 } };
                E11Stroke(hdc, w / 14, crGlyph, line, 2, FALSE);
            }
            break;
        }
        case 3:
        {
            POINT shaft[2] = { { cx, cy - g }, { cx, cy + g / 2 } };
            E11Stroke(hdc, w / 10, crGlyph, shaft, 2, FALSE);
            POINT head[3] = { { cx - g * 2 / 3, cy - g / 6 }, { cx, cy + g * 2 / 3 }, { cx + g * 2 / 3, cy - g / 6 } };
            E11FillPoly(hdc, head, 3, crGlyph);
            break;
        }
        case 4:
        {
            POINT bar[2] = { { cx - g / 3, cy + g / 2 }, { cx - g / 3, cy - g * 2 / 3 } };
            E11Stroke(hdc, w / 16, crGlyph, bar, 2, FALSE);
            POINT flag[2] = { { cx - g / 3, cy - g * 2 / 3 }, { cx + g * 2 / 3, cy - g / 3 } };
            E11Stroke(hdc, w / 16, crGlyph, flag, 2, FALSE);
            E11Circle(hdc, cx - g / 3 - w / 24, cy + g / 2, w / 12, 1, crGlyph, TRUE);
            break;
        }
        case 5:
        {
            E11Circle(hdc, cx + g / 2, cy - g / 2, w / 12, 1, crGlyph, TRUE);
            POINT mtn[4] = { { cx - g, cy + g / 2 }, { cx - g / 4, cy - g / 3 },
                             { cx + g / 4, cy + g / 8 }, { cx + g, cy + g / 2 } };
            E11FillPoly(hdc, mtn, 4, crGlyph);
            break;
        }
        case 6:
        {
            POINT tri[3] = { { cx - g / 2, cy - g * 2 / 3 }, { cx - g / 2, cy + g * 2 / 3 },
                             { cx + g * 3 / 4, cy } };
            E11FillPoly(hdc, tri, 3, crGlyph);
            break;
        }
    }
}

static VOID
E11DrawStrokeIcon(HDC hdc, int w, int h, int nIcon, const E11_PALETTE *pPal, BOOL bDim)
{
    COLORREF cr = bDim ? pPal->DimText : pPal->Text;
    COLORREF crAcc = bDim ? pPal->DimText : pPal->Accent;
    int pw = max(w / 12, 2);
    int cx = w / 2, cy = h / 2;
    int g = w * 30 / 100;

    switch (nIcon)
    {
        case EI_COPY:
        {
            E11StrokeRect(hdc, pw, cr, cx - g, cy - g + g / 2, cx + g - g / 2, cy + g);
            E11StrokeRect(hdc, pw, cr, cx - g + g / 2, cy - g, cx + g, cy + g - g / 2);
            break;
        }
        case EI_CUT:
        {
            POINT l1[2] = { { cx - g, cy - g }, { cx + g / 2, cy + g / 2 } };
            POINT l2[2] = { { cx + g, cy - g }, { cx - g / 2, cy + g / 2 } };
            E11Stroke(hdc, pw, cr, l1, 2, FALSE);
            E11Stroke(hdc, pw, cr, l2, 2, FALSE);
            E11Circle(hdc, cx - g * 3 / 4, cy + g * 3 / 4, g / 3, pw, cr, FALSE);
            E11Circle(hdc, cx + g * 3 / 4, cy + g * 3 / 4, g / 3, pw, cr, FALSE);
            break;
        }
        case EI_PASTE:
        {
            E11StrokeRect(hdc, pw, cr, cx - g, cy - g, cx + g / 2, cy + g);
            POINT clip[4] = { { cx - g / 2, cy - g }, { cx - g / 2, cy - g - g / 3 },
                              { cx, cy - g - g / 3 }, { cx, cy - g } };
            E11Stroke(hdc, pw, cr, clip, 4, FALSE);
            E11StrokeRect(hdc, pw, crAcc, cx - g / 4, cy - g / 4, cx + g, cy + g + g / 4);
            break;
        }
        case EI_DELETE:
        {
            E11StrokeRect(hdc, pw, cr, cx - g * 3 / 4, cy - g / 2, cx + g * 3 / 4, cy + g);
            POINT lid[2] = { { cx - g, cy - g / 2 }, { cx + g, cy - g / 2 } };
            E11Stroke(hdc, pw, cr, lid, 2, FALSE);
            POINT hndl[4] = { { cx - g / 3, cy - g / 2 }, { cx - g / 4, cy - g },
                              { cx + g / 4, cy - g }, { cx + g / 3, cy - g / 2 } };
            E11Stroke(hdc, pw, cr, hndl, 4, FALSE);
            POINT s1[2] = { { cx - g / 3, cy - g / 6 }, { cx - g / 3, cy + g * 2 / 3 } };
            POINT s2[2] = { { cx + g / 3, cy - g / 6 }, { cx + g / 3, cy + g * 2 / 3 } };
            E11Stroke(hdc, pw, cr, s1, 2, FALSE);
            E11Stroke(hdc, pw, cr, s2, 2, FALSE);
            break;
        }
        case EI_RENAME:
        {
            POINT base[2] = { { cx - g, cy + g }, { cx + g, cy + g } };
            E11Stroke(hdc, pw, cr, base, 2, FALSE);
            POINT pen[4] = { { cx - g / 2, cy + g / 2 }, { cx + g / 2, cy - g / 2 },
                             { cx + g * 3 / 4, cy - g / 4 }, { cx - g / 4, cy + g * 3 / 4 } };
            E11Stroke(hdc, pw, crAcc, pen, 4, TRUE);
            break;
        }
        case EI_NEWFOLDER:
        {
            E11DrawFolderShape(hdc, w, h, RGB(255, 200, 90));
            E11Circle(hdc, w * 3 / 4, h * 3 / 4, w / 5, 1, crAcc, TRUE);
            POINT ph[2] = { { w * 3 / 4 - w / 9, h * 3 / 4 }, { w * 3 / 4 + w / 9, h * 3 / 4 } };
            POINT pv[2] = { { w * 3 / 4, h * 3 / 4 - w / 9 }, { w * 3 / 4, h * 3 / 4 + w / 9 } };
            E11Stroke(hdc, pw, RGB(255, 255, 255), ph, 2, FALSE);
            E11Stroke(hdc, pw, RGB(255, 255, 255), pv, 2, FALSE);
            break;
        }
        case EI_PROPERTIES:
        {
            for (int i = -1; i <= 1; i++)
            {
                POINT line[2] = { { cx - g, cy + i * g * 2 / 3 }, { cx + g, cy + i * g * 2 / 3 } };
                E11Stroke(hdc, pw, cr, line, 2, FALSE);
                E11Circle(hdc, cx - g / 2 + i * g / 2, cy + i * g * 2 / 3, g / 4, pw, crAcc, TRUE);
            }
            break;
        }
        case EI_COPYPATH:
        {
            E11StrokeRect(hdc, pw, cr, cx - g, cy - g, cx + g / 3, cy + g);
            POINT a1[3] = { { cx, cy - g / 4 }, { cx + g / 2, cy }, { cx, cy + g / 4 } };
            E11Stroke(hdc, pw, crAcc, a1, 3, FALSE);
            POINT a2[3] = { { cx + g / 2, cy - g / 4 }, { cx + g, cy }, { cx + g / 2, cy + g / 4 } };
            E11Stroke(hdc, pw, crAcc, a2, 3, FALSE);
            break;
        }
        case EI_VIEW_LARGE:
        {
            E11StrokeRect(hdc, pw, cr, cx - g, cy - g, cx + g, cy + g);
            break;
        }
        case EI_VIEW_MEDIUM:
        {
            int q = g;
            E11StrokeRect(hdc, pw, cr, cx - q, cy - q, cx - q / 6, cy - q / 6);
            E11StrokeRect(hdc, pw, cr, cx + q / 6, cy - q, cx + q, cy - q / 6);
            E11StrokeRect(hdc, pw, cr, cx - q, cy + q / 6, cx - q / 6, cy + q);
            E11StrokeRect(hdc, pw, cr, cx + q / 6, cy + q / 6, cx + q, cy + q);
            break;
        }
        case EI_VIEW_SMALL:
        {
            int q = g;
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++)
                {
                    int x0 = cx - q + c * q * 2 / 3 + 1;
                    int y0 = cy - q + r * q * 2 / 3 + 1;
                    E11StrokeRect(hdc, pw, cr, x0, y0, x0 + q / 2, y0 + q / 2);
                }
            break;
        }
        case EI_VIEW_LIST:
        {
            for (int i = -1; i <= 1; i++)
            {
                RECT rcSq = { cx - g, cy + i * g * 2 / 3 - g / 6, cx - g + g / 3, cy + i * g * 2 / 3 + g / 6 };
                HBRUSH hbrSq = CreateSolidBrush(cr);
                FillRect(hdc, &rcSq, hbrSq);
                DeleteObject(hbrSq);
                POINT line[2] = { { cx - g / 3, cy + i * g * 2 / 3 }, { cx + g, cy + i * g * 2 / 3 } };
                E11Stroke(hdc, pw, cr, line, 2, FALSE);
            }
            break;
        }
        case EI_VIEW_DETAILS:
        {
            for (int i = -1; i <= 1; i++)
            {
                POINT l1[2] = { { cx - g, cy + i * g * 2 / 3 }, { cx - g / 4, cy + i * g * 2 / 3 } };
                POINT l2[2] = { { cx, cy + i * g * 2 / 3 }, { cx + g, cy + i * g * 2 / 3 } };
                E11Stroke(hdc, pw, cr, l1, 2, FALSE);
                E11Stroke(hdc, pw, cr, l2, 2, FALSE);
            }
            break;
        }
        case EI_VIEW_TILES:
        {
            E11StrokeRect(hdc, pw, cr, cx - g, cy - g, cx - g / 8, cy - g / 8);
            E11StrokeRect(hdc, pw, cr, cx - g, cy + g / 8, cx - g / 8, cy + g);
            POINT t1[2] = { { cx + g / 8, cy - g * 3 / 4 }, { cx + g, cy - g * 3 / 4 } };
            POINT t2[2] = { { cx + g / 8, cy - g / 4 }, { cx + g, cy - g / 4 } };
            POINT t3[2] = { { cx + g / 8, cy + g / 4 }, { cx + g, cy + g / 4 } };
            POINT t4[2] = { { cx + g / 8, cy + g * 3 / 4 }, { cx + g, cy + g * 3 / 4 } };
            E11Stroke(hdc, pw, cr, t1, 2, FALSE);
            E11Stroke(hdc, pw, cr, t2, 2, FALSE);
            E11Stroke(hdc, pw, cr, t3, 2, FALSE);
            E11Stroke(hdc, pw, cr, t4, 2, FALSE);
            break;
        }
        case EI_REFRESH:
        {
            HPEN hpen = CreatePen(PS_SOLID, pw, cr);
            HGDIOBJ hpenOld = SelectObject(hdc, hpen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Arc(hdc, cx - g, cy - g, cx + g, cy + g,
                cx + g, cy - g / 2, cx + g / 3, cy - g);
            SelectObject(hdc, hpenOld);
            DeleteObject(hpen);
            POINT head[3] = { { cx + g / 4, cy - g - g / 4 }, { cx + g, cy - g / 2 },
                              { cx + g / 6, cy - g / 4 } };
            E11FillPoly(hdc, head, 3, cr);
            break;
        }
        case EI_SETTINGS:
        {
            E11Circle(hdc, cx, cy, g, pw, cr, FALSE);
            E11Circle(hdc, cx, cy, g / 2, pw, cr, FALSE);
            static const int sp[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
            for (int i = 0; i < 8; i++)
            {
                int nx = sp[i][0], ny = sp[i][1];
                int len = (nx && ny) ? g * 707 / 1000 : g;
                POINT t[2] = { { cx + nx * len, cy + ny * len },
                               { cx + nx * (len + g / 3), cy + ny * (len + g / 3) } };
                E11Stroke(hdc, pw, cr, t, 2, FALSE);
            }
            break;
        }
        case EI_UNINSTALL:
        {
            E11StrokeRect(hdc, pw, cr, cx - g, cy - g / 2, cx + g, cy + g);
            POINT lid[4] = { { cx - g, cy - g / 2 }, { cx - g * 3 / 4, cy - g },
                             { cx + g * 3 / 4, cy - g }, { cx + g, cy - g / 2 } };
            E11Stroke(hdc, pw, cr, lid, 4, FALSE);
            POINT x1[2] = { { cx - g / 3, cy - g / 6 }, { cx + g / 3, cy + g / 2 } };
            POINT x2[2] = { { cx + g / 3, cy - g / 6 }, { cx - g / 3, cy + g / 2 } };
            E11Stroke(hdc, pw, crAcc, x1, 2, FALSE);
            E11Stroke(hdc, pw, crAcc, x2, 2, FALSE);
            break;
        }
        case EI_SYSPROPS:
        {
            E11StrokeRect(hdc, pw, cr, cx - g, cy - g, cx + g, cy + g / 3);
            POINT stand[2] = { { cx, cy + g / 3 }, { cx, cy + g * 2 / 3 } };
            E11Stroke(hdc, pw, cr, stand, 2, FALSE);
            POINT base[2] = { { cx - g / 2, cy + g * 2 / 3 }, { cx + g / 2, cy + g * 2 / 3 } };
            E11Stroke(hdc, pw, cr, base, 2, FALSE);
            POINT chk[3] = { { cx - g / 2, cy - g / 3 }, { cx - g / 6, cy },
                             { cx + g / 2, cy - g * 2 / 3 } };
            E11Stroke(hdc, pw, crAcc, chk, 3, FALSE);
            break;
        }
        case EI_MANAGE:
        {
            E11Circle(hdc, cx - g / 2, cy - g / 2, g / 2, pw, cr, FALSE);
            POINT hndl[2] = { { cx - g / 6, cy - g / 6 }, { cx + g, cy + g } };
            E11Stroke(hdc, pw * 2, cr, hndl, 2, FALSE);
            break;
        }
        case EI_MAPDRIVE:
        {
            E11StrokeRect(hdc, pw, cr, cx - g, cy, cx + g, cy + g);
            E11Circle(hdc, cx + g / 2, cy + g / 2, g / 8, 1, cr, TRUE);
            POINT a1[3] = { { cx - g / 3, cy - g / 3 }, { cx, cy - g * 2 / 3 }, { cx + g / 3, cy - g / 3 } };
            E11Stroke(hdc, pw, crAcc, a1, 3, FALSE);
            break;
        }
        case EI_NETLOC:
        {
            E11Circle(hdc, cx, cy, g, pw, cr, FALSE);
            HPEN hpen = CreatePen(PS_SOLID, pw, cr);
            HGDIOBJ hpenOld = SelectObject(hdc, hpen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Ellipse(hdc, cx - g / 3, cy - g, cx + g / 3, cy + g);
            SelectObject(hdc, hpenOld);
            DeleteObject(hpen);
            POINT eq[2] = { { cx - g, cy }, { cx + g, cy } };
            E11Stroke(hdc, pw, cr, eq, 2, FALSE);
            break;
        }
        case EI_MEDIA:
        {
            E11Circle(hdc, cx, cy, g, pw, cr, FALSE);
            E11Circle(hdc, cx, cy, g / 4, pw, cr, FALSE);
            break;
        }
        case EI_OPEN:
        {
            E11DrawFolderShape(hdc, w, h, RGB(255, 200, 90));
            POINT arr[3] = { { cx, cy }, { cx + g, cy }, { cx + g / 2, cy - g / 2 } };
            E11Stroke(hdc, pw, RGB(255, 255, 255), arr, 2, FALSE);
            break;
        }
        case EI_SEARCH:
        {
            E11Circle(hdc, cx - g / 4, cy - g / 4, g * 3 / 4, pw, cr, FALSE);
            POINT hndl[2] = { { cx + g / 4, cy + g / 4 }, { cx + g, cy + g } };
            E11Stroke(hdc, pw, cr, hndl, 2, FALSE);
            break;
        }
        case EI_PIN:
        {
            POINT needle[2] = { { cx - g / 6, cy + g / 4 }, { cx - g * 3 / 4, cy + g } };
            E11Stroke(hdc, pw, cr, needle, 2, FALSE);
            POINT body[4] = { { cx - g / 3, cy - g / 4 }, { cx + g / 4, cy - g * 3 / 4 },
                              { cx + g * 3 / 4, cy - g / 6 }, { cx + g / 6, cy + g / 3 } };
            E11FillPoly(hdc, body, 4, cr);
            break;
        }
        case EI_FILE:
        {
            E11StrokeRect(hdc, pw, cr, cx - g * 2 / 3, cy - g, cx + g * 2 / 3, cy + g);
            for (int i = 0; i < 3; i++)
            {
                POINT line[2] = { { cx - g / 3, cy - g / 2 + i * g / 2 },
                                  { cx + g / 3, cy - g / 2 + i * g / 2 } };
                E11Stroke(hdc, pw, cr, line, 2, FALSE);
            }
            break;
        }
    }
}

static VOID
E11DrawPcIcon(HDC hdc, int w, int h, const E11_PALETTE *pPal)
{
    COLORREF crFrame = RGB(96, 100, 106);
    int l = w / 8, t = h / 6, r = w - w / 8, b = h * 62 / 100;

    E11FillRoundRect(hdc, l, t, r, b, w / 10, crFrame);
    E11FillRoundRect(hdc, l + w / 24, t + w / 24, r - w / 24, b - w / 24, w / 12, RGB(60, 150, 235));
    RECT rcStand = { w / 2 - w / 16, b, w / 2 + w / 16, b + h / 8 };
    HBRUSH hbr = CreateSolidBrush(crFrame);
    FillRect(hdc, &rcStand, hbr);
    RECT rcBase = { w / 3, b + h / 8, w - w / 3, b + h / 8 + h / 16 };
    FillRect(hdc, &rcBase, hbr);
    DeleteObject(hbr);
}

static VOID
E11DrawDriveIcon(HDC hdc, int w, int h, BOOL bCd)
{
    COLORREF crBody = RGB(150, 155, 162);
    COLORREF crFace = RGB(108, 112, 118);

    E11FillRoundRect(hdc, w / 10, h / 4, w - w / 10, h * 3 / 4, w / 8, crBody);
    E11FillRoundRect(hdc, w / 10, h / 2, w - w / 10, h * 3 / 4, w / 8, crFace);

    if (bCd)
    {
        E11Circle(hdc, w / 2, h * 42 / 100, w / 5, 1, RGB(220, 222, 228), TRUE);
        E11Circle(hdc, w / 2, h * 42 / 100, w / 16, 1, crBody, TRUE);
    }
    else
    {
        E11Circle(hdc, w - w / 4, h * 5 / 8, w / 20 + 1, 1, RGB(96, 220, 120), TRUE);
    }
}

static VOID
E11DrawNetworkIcon(HDC hdc, int w, int h, const E11_PALETTE *pPal)
{
    COLORREF cr = pPal->Text;
    int pw = max(w / 12, 2);

    E11StrokeRect(hdc, pw, cr, w / 8, h / 6, w / 2 - w / 16, h / 2 - h / 12);
    E11StrokeRect(hdc, pw, cr, w / 2 + w / 16, h / 2 + h / 12, w - w / 8, h - h / 6);
    POINT link[3] = { { w / 4 + w / 24, h / 2 - h / 12 }, { w / 4 + w / 24, h * 3 / 4 },
                      { w / 2 + w / 16, h * 3 / 4 } };
    E11Stroke(hdc, pw, pPal->Accent, link, 3, FALSE);
}


typedef struct _E11_FLUCACHE
{
    UINT nId;
    int n;
    COLORREF cr;
    HBITMAP hbm;
} E11_FLUCACHE;

VOID
E11DrawFluentRes(HDC hdc, const RECT *prc, UINT nResId, COLORREF crTint)
{
    static E11_FLUCACHE s_Cache[96];
    int cx = prc->right - prc->left;
    int cy = prc->bottom - prc->top;
    int n = min(cx, cy);
    HBITMAP hbmTint = NULL;
    HDC hdcMem;
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    if (n <= 0)
        return;

    for (UINT i = 0; i < _countof(s_Cache); i++)
    {
        if (s_Cache[i].hbm && s_Cache[i].nId == nResId &&
            s_Cache[i].n == n && s_Cache[i].cr == crTint)
        {
            hbmTint = s_Cache[i].hbm;
            break;
        }
    }

    if (!hbmTint)
    {
        HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL),
                                        MAKEINTRESOURCEW(nResId),
                                        IMAGE_ICON, n, n, 0);
        ICONINFO ii;
        BITMAPINFO bmi;
        ULONG *pBits = NULL;

        if (!hIcon)
            return;
        if (!GetIconInfo(hIcon, &ii))
        {
            DestroyIcon(hIcon);
            return;
        }

        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = n;
        bmi.bmiHeader.biHeight = -n;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC hdcSrc = CreateCompatibleDC(hdc);
        HBITMAP hbmNew = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS,
                                          (void **)&pBits, NULL, 0);
        if (hbmNew && pBits &&
            GetDIBits(hdcSrc, ii.hbmColor, 0, n, pBits, &bmi, DIB_RGB_COLORS))
        {
            for (int px = 0; px < n * n; px++)
            {
                ULONG a = pBits[px] >> 24;
                pBits[px] = (a << 24) |
                            (((GetRValue(crTint) * a) / 255u) << 16) |
                            (((GetGValue(crTint) * a) / 255u) << 8) |
                            ((GetBValue(crTint) * a) / 255u);
            }
            hbmTint = hbmNew;
        }
        else if (hbmNew)
        {
            DeleteObject(hbmNew);
        }
        DeleteDC(hdcSrc);
        DeleteObject(ii.hbmColor);
        DeleteObject(ii.hbmMask);
        DestroyIcon(hIcon);

        if (!hbmTint)
            return;

        UINT iSlot = 0;
        for (UINT i = 0; i < _countof(s_Cache); i++)
        {
            if (!s_Cache[i].hbm)
            {
                iSlot = i;
                break;
            }
            if (i == _countof(s_Cache) - 1)
            {
                DeleteObject(s_Cache[0].hbm);
                MoveMemory(&s_Cache[0], &s_Cache[1],
                           (_countof(s_Cache) - 1) * sizeof(s_Cache[0]));
                iSlot = _countof(s_Cache) - 1;
            }
        }
        s_Cache[iSlot].nId = nResId;
        s_Cache[iSlot].n = n;
        s_Cache[iSlot].cr = crTint;
        s_Cache[iSlot].hbm = hbmTint;
    }

    hdcMem = CreateCompatibleDC(hdc);
    HGDIOBJ hOld = SelectObject(hdcMem, hbmTint);
    AlphaBlend(hdc, prc->left + (cx - n) / 2, prc->top + (cy - n) / 2, n, n,
               hdcMem, 0, 0, n, n, bf);
    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
}

static UINT
E11FluentResForIcon(int nIcon)
{
    switch (nIcon)
    {
        case EI_COPY:          return IDI_FLU_COPYICO;
        case EI_CUT:           return IDI_FLU_CUTICO;
        case EI_PASTE:         return IDI_FLU_PASTEICO;
        case EI_DELETE:        return IDI_FLU_DELETEICO;
        case EI_RENAME:        return IDI_FLU_RENAME;
        case EI_NEWFOLDER:     return IDI_FLU_FOLDERADD;
        case EI_PROPERTIES:    return IDI_FLU_OPTIONS;
        case EI_COPYPATH:      return IDI_FLU_LINK;
        case EI_VIEW_XLARGE:   return IDI_FLU_SQUARE;
        case EI_VIEW_LARGE:    return IDI_FLU_SQUAREMULTI;
        case EI_VIEW_MEDIUM:   return IDI_FLU_GRID;
        case EI_VIEW_SMALL:    return IDI_FLU_GRIDREG;
        case EI_VIEW_LIST:     return IDI_FLU_LIST;
        case EI_VIEW_DETAILS:  return IDI_FLU_NAVIGATION;
        case EI_VIEW_TILES:    return IDI_FLU_TILES;
        case EI_REFRESH:       return IDI_FLU_REFRESH;
        case EI_SETTINGS:      return IDI_FLU_SETTINGS;
        case EI_UNINSTALL:     return IDI_FLU_UNINSTALL;
        case EI_SYSPROPS:      return IDI_FLU_DESKCHECK;
        case EI_MANAGE:        return IDI_FLU_WRENCH;
        case EI_MAPDRIVE:      return IDI_FLU_HARDDRIVE;
        case EI_NETLOC:        return IDI_FLU_GLOBE;
        case EI_MEDIA:         return IDI_FLU_PLAYCIRCLE;
        case EI_OPEN:          return IDI_FLU_FOLDEROPEN;
        case EI_SEARCH:        return IDI_FLU_SEARCH;
        case EI_PIN:           return IDI_FLU_PIN;
        case EI_FILE:          return IDI_FLU_DOCUMENT;
        case EI_PC:            return IDI_FLU_DESKTOP;
        case EI_DRIVE:         return IDI_FLU_HARDDRIVE;
        case EI_NETWORK:       return IDI_FLU_GLOBE;
        default:               return 0;
    }
}

VOID
E11DrawIconDim(HDC hdc, const RECT *prc, int nIcon, const E11_PALETTE *pPal, BOOL bDim)
{
    E11AA aa;
    COLORREF crBg;
    HDC hdcAA;
    int w, h;

    if (nIcon == EI_NONE)
        return;

    {
        UINT nRes = E11FluentResForIcon(nIcon);
        if (nRes != 0)
        {
            E11DrawFluentRes(hdc, prc, nRes, bDim ? pPal->DimText : pPal->Text);
            return;
        }
    }

    crBg = GetPixel(hdc, (prc->left + prc->right) / 2, (prc->top + prc->bottom) / 2);
    if (crBg == CLR_INVALID)
        crBg = pPal->FrameBg;

    hdcAA = E11AABegin(&aa, hdc, prc, crBg);
    w = (prc->right - prc->left) * E11_SS;
    h = (prc->bottom - prc->top) * E11_SS;

    switch (nIcon)
    {
        case EI_FOLDER:
        case EI_FOLDER_DESKTOP:
        case EI_FOLDER_DOCS:
        case EI_FOLDER_DOWNLOADS:
        case EI_FOLDER_MUSIC:
        case EI_FOLDER_PICTURES:
        case EI_FOLDER_VIDEOS:
            E11DrawFolderIcon(hdcAA, w, h, nIcon);
            break;
        case EI_PC:
            E11DrawPcIcon(hdcAA, w, h, pPal);
            break;
        case EI_DRIVE:
            E11DrawDriveIcon(hdcAA, w, h, FALSE);
            break;
        case EI_DRIVE_CD:
            E11DrawDriveIcon(hdcAA, w, h, TRUE);
            break;
        case EI_NETWORK:
            E11DrawNetworkIcon(hdcAA, w, h, pPal);
            break;
        default:
            E11DrawStrokeIcon(hdcAA, w, h, nIcon, pPal, bDim);
            break;
    }

    E11AAEnd(&aa, hdc);
}

VOID
E11DrawIcon(HDC hdc, const RECT *prc, int nIcon, const E11_PALETTE *pPal)
{
    E11DrawIconDim(hdc, prc, nIcon, pPal, FALSE);
}

HICON
E11CreateAppIcon(int cxIcon)
{
    HDC hdcScreen = GetDC(NULL);
    HDC hdcColor = CreateCompatibleDC(hdcScreen);
    HDC hdcMask = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, cxIcon, cxIcon);
    HBITMAP hbmMask = CreateBitmap(cxIcon, cxIcon, 1, 1, NULL);
    HGDIOBJ hOldColor = SelectObject(hdcColor, hbmColor);
    HGDIOBJ hOldMask = SelectObject(hdcMask, hbmMask);
    E11_PALETTE pal;
    RECT rc = { 0, 0, cxIcon, cxIcon };
    ICONINFO ii;
    HICON hIcon;

    E11GetPalette(&pal);

    COLORREF crKey = RGB(1, 2, 3);
    HBRUSH hbrKey = CreateSolidBrush(crKey);
    FillRect(hdcColor, &rc, hbrKey);
    DeleteObject(hbrKey);

    E11AA aa;
    HDC hdcAA = E11AABegin(&aa, hdcColor, &rc, crKey);
    E11DrawFolderIcon(hdcAA, cxIcon * E11_SS, cxIcon * E11_SS, EI_FOLDER);
    E11AAEnd(&aa, hdcColor);

    for (int y = 0; y < cxIcon; y++)
        for (int x = 0; x < cxIcon; x++)
        {
            BOOL bKey = (GetPixel(hdcColor, x, y) == crKey);
            SetPixel(hdcMask, x, y, bKey ? RGB(255, 255, 255) : RGB(0, 0, 0));
            if (bKey)
                SetPixel(hdcColor, x, y, RGB(0, 0, 0));
        }

    SelectObject(hdcColor, hOldColor);
    SelectObject(hdcMask, hOldMask);

    ii.fIcon = TRUE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;
    hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    DeleteDC(hdcColor);
    DeleteDC(hdcMask);
    ReleaseDC(NULL, hdcScreen);
    return hIcon;
}
