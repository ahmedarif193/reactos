/*
 *    file system folder
 *
 *    Copyright 1997                Marcus Meissner
 *    Copyright 1998, 1999, 2002    Juergen Schmied
 *    Copyright 2009              Andrew Hill
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL (shell);

/***********************************************************************
*   IDropTargetHelper implementation
*/

#define DRAG_IMAGE_MAX_SIZE 1024
#define DRAG_IMAGE_KEY RGB(255, 0, 255)
#define DRAG_IMAGE_KEY_PIXEL 0x00FF00FFu

static const WCHAR s_DragImageFormat[] = L"ReactOS Shell Drag Image";
static const WCHAR s_DragImageClass[] = L"SysDragImage";

typedef struct _DRAG_IMAGE_HEADER
{
    SIZE Size;
    POINT Offset;
} DRAG_IMAGE_HEADER;

static CLIPFORMAT GetDragImageFormat()
{
    return (CLIPFORMAT)RegisterClipboardFormatW(s_DragImageFormat);
}

CDropTargetHelper::CDropTargetHelper() :
    m_hwndImage(NULL),
    m_hdcImage(NULL),
    m_hbmImage(NULL),
    m_hbmOld(NULL),
    m_bVisible(FALSE)
{
    m_Size.cx = m_Size.cy = 0;
    m_Offset.x = m_Offset.y = 0;
    m_Position.x = m_Position.y = 0;
}

CDropTargetHelper::~CDropTargetHelper()
{
    DestroyImage();
}

HRESULT WINAPI CDropTargetHelper::InitializeFromBitmap(LPSHDRAGIMAGE pshdi, IDataObject *pDataObject)
{
    if (!pshdi || !pDataObject || !pshdi->hbmpDragImage)
        return E_INVALIDARG;

    LONG cx = pshdi->sizeDragImage.cx, cy = pshdi->sizeDragImage.cy;
    if (cx <= 0 || cy <= 0 || cx > DRAG_IMAGE_MAX_SIZE || cy > DRAG_IMAGE_MAX_SIZE)
        return E_INVALIDARG;

    SIZE_T Count = (SIZE_T)cx * cy;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(DRAG_IMAGE_HEADER) + Count * sizeof(DWORD));
    if (!hGlobal)
        return E_OUTOFMEMORY;

    DRAG_IMAGE_HEADER *pHeader = (DRAG_IMAGE_HEADER *)GlobalLock(hGlobal);
    if (!pHeader)
    {
        GlobalFree(hGlobal);
        return E_OUTOFMEMORY;
    }
    DWORD *pBits = (DWORD *)(pHeader + 1);
    pHeader->Size = pshdi->sizeDragImage;
    pHeader->Offset = pshdi->ptOffset;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC hdc = GetDC(NULL);
    INT Lines = hdc ? GetDIBits(hdc, pshdi->hbmpDragImage, 0, cy, pBits, &bmi, DIB_RGB_COLORS) : 0;
    if (hdc)
        ReleaseDC(NULL, hdc);
    if (Lines != cy)
    {
        GlobalUnlock(hGlobal);
        GlobalFree(hGlobal);
        return E_FAIL;
    }

    BOOL bAlpha = FALSE;
    if (pshdi->crColorKey == CLR_NONE)
    {
        for (SIZE_T i = 0; i < Count && !bAlpha; ++i)
            bAlpha = (pBits[i] >> 24) != 0;
    }
    DWORD Key = (GetRValue(pshdi->crColorKey) << 16) | (GetGValue(pshdi->crColorKey) << 8) |
                GetBValue(pshdi->crColorKey);
    for (SIZE_T i = 0; i < Count; ++i)
    {
        if (pshdi->crColorKey != CLR_NONE)
            pBits[i] = (pBits[i] & 0x00FFFFFF) == Key ? 0 : (pBits[i] | 0xFF000000);
        else if (!bAlpha)
            pBits[i] |= 0xFF000000;
    }
    GlobalUnlock(hGlobal);

    FORMATETC Format = { GetDragImageFormat(), NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM Medium = {};
    Medium.tymed = TYMED_HGLOBAL;
    Medium.hGlobal = hGlobal;
    HRESULT hr = pDataObject->SetData(&Format, &Medium, TRUE);
    if (FAILED(hr))
    {
        GlobalFree(hGlobal);
        return hr;
    }

    DeleteObject(pshdi->hbmpDragImage);
    return S_OK;
}

HRESULT WINAPI CDropTargetHelper::InitializeFromWindow(HWND hwnd, POINT *ppt, IDataObject *pDataObject)
{
    FIXME ("(%p)->()\n", this);
    return E_NOTIMPL;
}

LRESULT CALLBACK CDropTargetHelper::ImageWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_NCCREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((LPCREATESTRUCTW)lParam)->lpCreateParams);
            break;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return TRUE;
        case WM_PAINT:
        {
            CDropTargetHelper *pThis = (CDropTargetHelper *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hdc && pThis && pThis->m_hdcImage)
                BitBlt(hdc, 0, 0, pThis->m_Size.cx, pThis->m_Size.cy, pThis->m_hdcImage, 0, 0, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

BOOL CDropTargetHelper::CreateImage(IDataObject *pDataObject)
{
    FORMATETC Format = { GetDragImageFormat(), NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM Medium = {};
    if (FAILED(pDataObject->GetData(&Format, &Medium)))
        return FALSE;

    BOOL bResult = FALSE;
    SIZE_T cbData = Medium.tymed == TYMED_HGLOBAL && Medium.hGlobal ? GlobalSize(Medium.hGlobal) : 0;
    const DRAG_IMAGE_HEADER *pHeader = cbData >= sizeof(DRAG_IMAGE_HEADER) ?
        (const DRAG_IMAGE_HEADER *)GlobalLock(Medium.hGlobal) : NULL;
    if (pHeader)
    {
        LONG cx = pHeader->Size.cx, cy = pHeader->Size.cy;
        if (cx > 0 && cy > 0 && cx <= DRAG_IMAGE_MAX_SIZE && cy <= DRAG_IMAGE_MAX_SIZE &&
            cbData >= sizeof(DRAG_IMAGE_HEADER) + (SIZE_T)cx * cy * sizeof(DWORD))
        {
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
            bmi.bmiHeader.biWidth = cx;
            bmi.bmiHeader.biHeight = -cy;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            DWORD *pDst = NULL;
            m_hdcImage = CreateCompatibleDC(NULL);
            m_hbmImage = m_hdcImage ? CreateDIBSection(m_hdcImage, &bmi, DIB_RGB_COLORS, (void **)&pDst, NULL, 0) : NULL;
            if (m_hbmImage && pDst)
            {
                const DWORD *pSrc = (const DWORD *)(pHeader + 1);
                for (SIZE_T i = 0; i < (SIZE_T)cx * cy; ++i)
                {
                    DWORD Pixel = pSrc[i] & 0x00FFFFFF;
                    if ((pSrc[i] >> 24) < 0x80)
                        Pixel = DRAG_IMAGE_KEY_PIXEL;
                    else if (Pixel == DRAG_IMAGE_KEY_PIXEL)
                        Pixel ^= 0x00000100;
                    pDst[i] = Pixel;
                }
                m_hbmOld = SelectObject(m_hdcImage, m_hbmImage);
                m_Size = pHeader->Size;
                m_Offset = pHeader->Offset;
                bResult = TRUE;
            }
        }
        GlobalUnlock(Medium.hGlobal);
    }
    ReleaseStgMedium(&Medium);
    if (!bResult)
    {
        DestroyImage();
        return FALSE;
    }

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = ImageWndProc;
    wc.hInstance = shell32_hInstance;
    wc.lpszClassName = s_DragImageClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        DestroyImage();
        return FALSE;
    }

    m_hwndImage = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                  s_DragImageClass, NULL, WS_POPUP | WS_DISABLED,
                                  0, 0, m_Size.cx, m_Size.cy, NULL, NULL, shell32_hInstance, this);
    if (!m_hwndImage || !SetLayeredWindowAttributes(m_hwndImage, DRAG_IMAGE_KEY, 0, LWA_COLORKEY))
    {
        DestroyImage();
        return FALSE;
    }
    return TRUE;
}

void CDropTargetHelper::DestroyImage()
{
    if (m_hwndImage)
        DestroyWindow(m_hwndImage);
    if (m_hdcImage)
    {
        if (m_hbmOld)
            SelectObject(m_hdcImage, m_hbmOld);
        DeleteDC(m_hdcImage);
    }
    if (m_hbmImage)
        DeleteObject(m_hbmImage);
    m_hwndImage = NULL;
    m_hdcImage = NULL;
    m_hbmImage = NULL;
    m_hbmOld = NULL;
    m_bVisible = FALSE;
}

void CDropTargetHelper::MoveImage(const POINT *ppt)
{
    POINT Position = { ppt->x - m_Offset.x, ppt->y - m_Offset.y };

    if (!m_hwndImage || (m_bVisible && Position.x == m_Position.x && Position.y == m_Position.y))
        return;
    m_Position = Position;
    if (m_bVisible)
    {
        SetWindowPos(m_hwndImage, NULL, Position.x, Position.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        return;
    }
    SetWindowPos(m_hwndImage, HWND_TOPMOST, Position.x, Position.y, m_Size.cx, m_Size.cy,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    UpdateWindow(m_hwndImage);
    m_bVisible = TRUE;
}

HRESULT WINAPI CDropTargetHelper::DragEnter (HWND hwndTarget, IDataObject* pDataObject, POINT* ppt, DWORD dwEffect)
{
    DestroyImage();
    if (!pDataObject || !ppt)
        return E_INVALIDARG;
    if (CreateImage(pDataObject))
        MoveImage(ppt);
    return S_OK;
}

HRESULT WINAPI CDropTargetHelper::DragLeave()
{
    DestroyImage();
    return S_OK;
}

HRESULT WINAPI CDropTargetHelper::DragOver(POINT *ppt, DWORD dwEffect)
{
    if (!ppt)
        return E_INVALIDARG;
    MoveImage(ppt);
    return S_OK;
}

HRESULT WINAPI CDropTargetHelper::Drop(IDataObject* pDataObject, POINT* ppt, DWORD dwEffect)
{
    DestroyImage();
    return S_OK;
}

HRESULT WINAPI CDropTargetHelper::Show(BOOL fShow)
{
    if (!m_hwndImage)
        return S_OK;
    if (!fShow)
    {
        ShowWindow(m_hwndImage, SW_HIDE);
        m_bVisible = FALSE;
    }
    else if (!m_bVisible)
    {
        POINT pt = { m_Position.x + m_Offset.x, m_Position.y + m_Offset.y };
        MoveImage(&pt);
    }
    return S_OK;
}

/*************************************************************************
 *      SH32_SimulateDropWithSite [SHELL32.INTERNAL]
 */
HRESULT SH32_SimulateDropWithSite(IDropTarget *pDT, IDataObject *pDO, DWORD grfKeyState, PPOINTL pPtl, LPDWORD pdwEffect, IUnknown *pSite)
{
    CScopedSetObjectWithSite site(pDT, pSite);
    return SHSimulateDrop(pDT, pDO, grfKeyState, pPtl, pdwEffect);
}

/*************************************************************************
 *      SHSimulateDropOnClsid [SHELL32.751]
 */
EXTERN_C HRESULT WINAPI SHSimulateDropOnClsid(_In_ REFCLSID clsid, _In_opt_ IUnknown* pSite, _In_ IDataObject* pDO)
{
    CComPtr<IDropTarget> pDT;
    HRESULT hr = SH32_ExtCoCreateInstance(NULL, &clsid, NULL, CLSCTX_ALL, IID_PPV_ARG(IDropTarget, &pDT));
    return SUCCEEDED(hr) ? SH32_SimulateDropWithSite(pDT, pDO, 0, NULL, NULL, pSite) : hr;
}
