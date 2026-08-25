/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     User-mode compositor: pulls frame metadata from win32k, maps
 *              the per-window section surfaces read-only and composes them
 *              into the primary. See sdk/include/reactos/dwmframe.h.
 */

#include <windows.h>
#include <reactos/dwmframe.h>

DWORD_PTR NTAPI NtUserCallOneParam(DWORD_PTR Param, DWORD Routine);

#define DWM_BG_COLOR  0x003A6EA5u

static void DwmLog(const char *s) { OutputDebugStringA(s); }

static LONG g_originX, g_originY;

typedef struct _DWM_SURFACE
{
    ULONG  Id;
    ULONG  Generation;
    HANDLE hSection;
    BYTE  *View;
    ULONG  LastSeenFrame;
    BOOL   Used;
} DWM_SURFACE;
static DWM_SURFACE g_views[DWM_MAX_WINDOWS];
static ULONG g_frameSeq;

static void
DwmDropView(DWM_SURFACE *v)
{
    if (v->View)     UnmapViewOfFile(v->View);
    if (v->hSection) CloseHandle(v->hSection);
    RtlZeroMemory(v, sizeof(*v));
}

static const BYTE *
DwmGetSurfaceView(const DWM_WIN *w)
{
    DWM_SURFACE *slot = NULL;
    DWM_OPEN_SURFACE req;
    ULONG i;

    for (i = 0; i < DWM_MAX_WINDOWS; i++)
    {
        if (g_views[i].Used && g_views[i].Id == w->SurfaceId)
        {
            slot = &g_views[i];
            break;
        }
    }
    if (slot != NULL && slot->Generation == w->Generation)
    {
        slot->LastSeenFrame = g_frameSeq;
        return slot->View;
    }
    if (slot != NULL)
        DwmDropView(slot);

    if (slot == NULL)
    {
        for (i = 0; i < DWM_MAX_WINDOWS; i++)
        {
            if (!g_views[i].Used)
            {
                slot = &g_views[i];
                break;
            }
        }
    }
    if (slot == NULL)
        return NULL;

    req.SurfaceId = w->SurfaceId;
    req.Generation = w->Generation;
    req.hSection = NULL;
    if ((LONG)NtUserCallOneParam((DWORD_PTR)&req, DWM_ROUTINE_OPENSURFACE) < 0 ||
        req.hSection == NULL)
        return NULL;

    slot->View = (BYTE *)MapViewOfFile(req.hSection, FILE_MAP_READ, 0, 0, 0);
    if (slot->View == NULL)
    {
        CloseHandle(req.hSection);
        return NULL;
    }
    slot->hSection = req.hSection;
    slot->Id = w->SurfaceId;
    slot->Generation = w->Generation;
    slot->LastSeenFrame = g_frameSeq;
    slot->Used = TRUE;
    return slot->View;
}

static void
DwmSweepViews(void)
{
    ULONG i;
    for (i = 0; i < DWM_MAX_WINDOWS; i++)
    {
        if (g_views[i].Used && (g_frameSeq - g_views[i].LastSeenFrame) > 256)
            DwmDropView(&g_views[i]);
    }
}

static void
DwmBlitWindow(ULONG *comp, LONG scrW,
              LONG clipL, LONG clipT, LONG clipR, LONG clipB,
              const BYTE *pix, const DWM_WIN *w)
{
    LONG r, r0, r1, x0, x1, srcx0, dy, x, width;
    LONGLONG wx = (LONGLONG)w->x - g_originX;
    LONGLONG wy = (LONGLONG)w->y - g_originY;
    LONGLONG right, bottom;
    BOOL useKey = (w->LayerFlags & DWM_LWA_COLORKEY) != 0;
    BOOL useAlpha = (w->LayerFlags & DWM_LWA_ALPHA) != 0 && w->Alpha < 255;
    ULONG a = w->Alpha, ia = 255 - w->Alpha, key = 0;

    if (w->cx <= 0 || w->cy <= 0 ||
        (ULONG)w->cx > ((ULONG)-1) / sizeof(ULONG) ||
        w->Stride < (ULONG)w->cx * sizeof(ULONG))
        return;

    right = wx + w->cx;
    bottom = wy + w->cy;
    if (right <= clipL || wx >= clipR || bottom <= clipT || wy >= clipB)
        return;
    x0 = (wx < clipL) ? clipL : (LONG)wx;
    x1 = (right > clipR) ? clipR : (LONG)right;
    if (x1 <= x0) return;
    srcx0 = (LONG)(x0 - wx);
    width = x1 - x0;

    r0 = (wy < clipT) ? (LONG)(clipT - wy) : 0;
    r1 = (bottom > clipB) ? (LONG)(clipB - wy) : w->cy;
    if (r1 <= r0) return;

    if (useKey)
    {
        ULONG c = w->ColorKey;
        key = ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);
    }

    for (r = r0; r < r1; r++)
    {
        const ULONG *srcrow;
        ULONG *dstrow;

        dy = (LONG)(wy + r);
        srcrow = (const ULONG *)(pix + (SIZE_T)r * w->Stride) + srcx0;
        dstrow = comp + (SIZE_T)dy * scrW + x0;

        if (!useKey && !useAlpha)
        {
            RtlCopyMemory(dstrow, srcrow, (SIZE_T)width * 4);
            continue;
        }

        for (x = 0; x < width; x++)
        {
            ULONG s = srcrow[x], d;
            if (useKey && (s & 0x00FFFFFFu) == key)
                continue;
            if (useAlpha)
            {
                d = dstrow[x];
                dstrow[x] =
                    ((((s >> 16) & 0xFFu) * a + ((d >> 16) & 0xFFu) * ia) / 255u << 16) |
                    ((((s >> 8)  & 0xFFu) * a + ((d >> 8)  & 0xFFu) * ia) / 255u << 8)  |
                    (((s & 0xFFu) * a + (d & 0xFFu) * ia) / 255u);
            }
            else
            {
                dstrow[x] = s;
            }
        }
    }
}

static HDC     g_hdcComp;
static HBITMAP g_hbmComp;
static void   *g_compBits;
static BYTE   *g_buf;
static ULONG   g_bufSize;
static LONG    g_W, g_H;

static BOOL
DwmCreateSurfaces(HDC hdcScreen, LONG W, LONG H)
{
    BITMAPINFO bmi;
    HDC hdcNew;
    HBITMAP hbmNew;
    void *bitsNew = NULL;

    if (W <= 0 || H <= 0 || (ULONG)W > ((ULONG)-1) / sizeof(ULONG) ||
        (ULONG)H > ((ULONG)-1) / ((ULONG)W * sizeof(ULONG)))
        return FALSE;

    hdcNew = CreateCompatibleDC(hdcScreen);
    if (hdcNew == NULL)
        return FALSE;

    RtlZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    hbmNew = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bitsNew, NULL, 0);
    if (hbmNew == NULL || bitsNew == NULL)
    {
        if (hbmNew != NULL)
            DeleteObject(hbmNew);
        DeleteDC(hdcNew);
        return FALSE;
    }
    if (SelectObject(hdcNew, hbmNew) == NULL)
    {
        DeleteObject(hbmNew);
        DeleteDC(hdcNew);
        return FALSE;
    }

    if (g_buf == NULL)
    {
        g_bufSize = DWM_FRAME_BYTES;
        g_buf = (BYTE *)VirtualAlloc(NULL, g_bufSize, MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
        if (g_buf == NULL)
        {
            DeleteDC(hdcNew);
            DeleteObject(hbmNew);
            return FALSE;
        }
    }

    if (g_hdcComp != NULL)
        DeleteDC(g_hdcComp);
    if (g_hbmComp != NULL)
        DeleteObject(g_hbmComp);
    g_hdcComp = hdcNew;
    g_hbmComp = hbmNew;
    g_compBits = bitsNew;

    g_W = W;
    g_H = H;
    return TRUE;
}

static void
DwmComposeLoop(void)
{
    HDC hdcScreen = GetDC(NULL);
    DWM_ATTACH att;
    HANDLE hWake, hVblank;
    BOOL forceFull = TRUE;
    LONG vw, vh, primW, primH;

    if (hdcScreen == NULL)
    {
        DwmLog("DWM: screen DC unavailable\n");
        return;
    }

    primW = GetSystemMetrics(SM_CXSCREEN);
    primH = GetSystemMetrics(SM_CYSCREEN);
    vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    g_originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0)
    {
        vw = (primW > 0) ? primW : 800;
        vh = (primH > 0) ? primH : 600;
        g_originX = g_originY = 0;
    }

    if (!DwmCreateSurfaces(hdcScreen, vw, vh))
    {
        DwmLog("DWM: surface creation failed\n");
        return;
    }

    RtlZeroMemory(&att, sizeof(att));
    att.Attach = 1;
    if ((LONG)NtUserCallOneParam((DWORD_PTR)&att, DWM_ROUTINE_ATTACH) < 0)
    {
        DwmLog("DWM: attach refused\n");
        return;
    }
    hWake = att.hWake;
    hVblank = att.hVblank;
    if (hWake == NULL)
    {
        DwmLog("DWM: attach refused (no composition on this display stack)\n");
        return;
    }
    DwmLog("DWM: attached\n");

    for (;;)
    {
        PDWM_FRAME_HEADER hdr = (PDWM_FRAME_HEADER)g_buf;
        PDWM_WIN wins;
        LONG st;
        ULONG *p, n, i;

        hdr->Magic = DWM_FRAME_MAGIC;
        hdr->BufBytes = g_bufSize;

        st = (LONG)NtUserCallOneParam((DWORD_PTR)g_buf, DWM_ROUTINE_GETFRAME);
        if (st < 0)
        {
            Sleep(50);
            continue;
        }

        if (hdr->Magic != DWM_FRAME_MAGIC || hdr->WinArrayBase != DWM_WINARRAY_BASE ||
            hdr->Count > DWM_MAX_WINDOWS || hdr->ScreenW > MAXLONG || hdr->ScreenH > MAXLONG)
        {
            DwmLog("DWM: invalid frame metadata\n");
            Sleep(50);
            continue;
        }

        if ((LONG)hdr->ScreenW != primW || (LONG)hdr->ScreenH != primH)
        {
            primW = (LONG)hdr->ScreenW;
            primH = (LONG)hdr->ScreenH;
            vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            g_originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            g_originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
            if (vw <= 0 || vh <= 0) { vw = primW; vh = primH; g_originX = g_originY = 0; }
            if (primW == 0 || primH == 0 || !DwmCreateSurfaces(hdcScreen, vw, vh))
            {
                Sleep(50);
                continue;
            }
            forceFull = TRUE;
            continue;
        }

        if (hdr->Dirty == 0 && !forceFull)
        {
            if (hWake != NULL)
                WaitForSingleObject(hWake, 200);
            else
                Sleep(16);
            continue;
        }

        {
            LONG pl, pt, pr, pb, y;
            BOOL completeFrame = TRUE;

            if (forceFull || hdr->FullDamage ||
                hdr->DmgR <= hdr->DmgL || hdr->DmgB <= hdr->DmgT)
            {
                pl = 0; pt = 0; pr = g_W; pb = g_H;
            }
            else
            {
                LONGLONG l = (LONGLONG)hdr->DmgL - g_originX;
                LONGLONG t = (LONGLONG)hdr->DmgT - g_originY;
                LONGLONG r = (LONGLONG)hdr->DmgR - g_originX;
                LONGLONG b = (LONGLONG)hdr->DmgB - g_originY;
                pl = (l < 0) ? 0 : (l > g_W ? g_W : (LONG)l);
                pt = (t < 0) ? 0 : (t > g_H ? g_H : (LONG)t);
                pr = (r < 0) ? 0 : (r > g_W ? g_W : (LONG)r);
                pb = (b < 0) ? 0 : (b > g_H ? g_H : (LONG)b);
            }
            forceFull = FALSE;

            if (pr > pl && pb > pt)
            {
                for (y = pt; y < pb; y++)
                {
                    p = (ULONG *)g_compBits + (SIZE_T)y * g_W + pl;
                    n = (ULONG)(pr - pl);
                    while (n--) *p++ = DWM_BG_COLOR;
                }

                wins = (PDWM_WIN)(g_buf + hdr->WinArrayBase);
                for (i = 0; i < hdr->Count; i++)
                {
                    const BYTE *pix = DwmGetSurfaceView(&wins[i]);
                    if (pix == NULL)
                    {
                        completeFrame = FALSE;
                        break;
                    }
                    DwmBlitWindow((ULONG *)g_compBits, g_W, pl, pt, pr, pb, pix, &wins[i]);
                }

                /* Never replace the last complete scan-out with a partially
                 * composed buffer. A surface can legitimately be recreated
                 * between GETFRAME and OPENSURFACE; the next idle metadata
                 * pull contains all current FRONTs and retries this frame. */
                if (!completeFrame)
                {
                    forceFull = TRUE;
                }
                else if (NtUserCallOneParam(1, DWM_ROUTINE_PRESENTSYNC))
                {
                    BOOL bltResult = BitBlt(hdcScreen, g_originX + pl, g_originY + pt,
                                            pr - pl, pb - pt, g_hdcComp, pl, pt,
                                            SRCCOPY);
                    BOOL endResult = NtUserCallOneParam(0, DWM_ROUTINE_PRESENTSYNC) != 0;
                    if (!bltResult || !endResult)
                        forceFull = TRUE;
                }
                else
                {
                    forceFull = TRUE;
                }
            }
        }

        g_frameSeq++;
        if ((g_frameSeq & 255) == 0)
            DwmSweepViews();

        if (hVblank != NULL)
            WaitForSingleObject(hVblank, 50);
        else
            Sleep(15);
    }
}

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nShow)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrev);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShow);

    DwmComposeLoop();
    return 0;
}
