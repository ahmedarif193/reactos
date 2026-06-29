/*
 * Dwmapi
 *
 * Copyright 2007 Andras Kovacs
 * Copyright 2026 ReactOS Team
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
 *
 * PURPOSE: Desktop Window Manager API client library.
 *
 * dwmapi.dll is a thin LPC proxy that normally forwards requests to
 * the DWM service via \UxSmsApiPort.  Until the full DWM compositor
 * is operational, composition-dependent functions report composition
 * disabled instead of claiming that rendering or redirection happened.
 */

#include <stdarg.h>
#include <string.h>

#ifdef __REACTOS__
#include <rtlfuncs.h>
#else
#include "winternl.h"
#endif

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "dwmapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dwmapi);

/* DWM error codes */
#ifndef DWM_E_COMPOSITIONDISABLED
#define DWM_E_COMPOSITIONDISABLED _HRESULT_TYPEDEF_(0x80263001)
#endif

/* Undocumented USER32 API -- returns TRUE if the desktop is composited.
 * Resolved at runtime to avoid hard import dependency on Vista+ export. */
typedef BOOL (WINAPI *PFN_IsThreadDesktopComposited)(void);

static BOOL DwmpIsDesktopComposited(void)
{
    static PFN_IsThreadDesktopComposited pfn = NULL;
    static BOOL resolved = FALSE;

    if (!resolved)
    {
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32)
            pfn = (PFN_IsThreadDesktopComposited)GetProcAddress(hUser32,
                                                                "IsThreadDesktopComposited");
        resolved = TRUE;
    }

    if (pfn)
        return pfn();

    return FALSE;
}

static HRESULT DwmpValidateWindow(HWND hwnd)
{
    if (!hwnd)
        return E_INVALIDARG;

    if (!IsWindow(hwnd))
        return E_HANDLE;

    return S_OK;
}

static HRESULT DwmpValidateDisabledWindow(HWND hwnd)
{
    HRESULT hr = DwmpValidateWindow(hwnd);

    if (FAILED(hr))
        return hr;

    return DWM_E_COMPOSITIONDISABLED;
}

static BOOL DwmpValidRect(const RECT *rect)
{
    return rect->left <= rect->right && rect->top <= rect->bottom;
}

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

static int get_display_frequency(void)
{
    DEVMODEW mode;
    BOOL ret;

    memset(&mode, 0, sizeof(mode));
    mode.dmSize = sizeof(mode);
    ret = EnumDisplaySettingsExW(NULL, ENUM_CURRENT_SETTINGS, &mode, 0);
    if (ret && mode.dmFields & DM_DISPLAYFREQUENCY && mode.dmDisplayFrequency)
    {
        return mode.dmDisplayFrequency;
    }
    else
    {
        WARN("Failed to query display frequency, returning a fallback value.\n");
        return 60;
    }
}

/* ========================================================================
 * NAMED EXPORTS (24) - Public DWM API
 * ======================================================================== */

/**********************************************************************
 *           DwmIsCompositionEnabled         (DWMAPI.144)
 */
HRESULT WINAPI DwmIsCompositionEnabled(BOOL *enabled)
{
    TRACE("%p\n", enabled);

    if (!enabled)
        return E_INVALIDARG;

    *enabled = DwmpIsDesktopComposited();

    return S_OK;
}

/**********************************************************************
 *           DwmEnableComposition         (DWMAPI.102)
 *
 * Enables or disables DWM composition.
 * Report: validates parameter <= 2, returns E_INVALIDARG otherwise.
 */
HRESULT WINAPI DwmEnableComposition(UINT uCompositionAction)
{
    TRACE("(%d)\n", uCompositionAction);

    if (uCompositionAction > DWM_EC_ENABLECOMPOSITION)
        return E_INVALIDARG;

    if (uCompositionAction == DWM_EC_DISABLECOMPOSITION)
        return S_OK;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmExtendFrameIntoClientArea    (DWMAPI.135)
 */
HRESULT WINAPI DwmExtendFrameIntoClientArea(HWND hwnd, const MARGINS* margins)
{
    HRESULT hr;

    TRACE("(%p, %p)\n", hwnd, margins);

    hr = DwmpValidateWindow(hwnd);
    if (FAILED(hr))
        return hr;

    if (!margins)
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmGetColorizationColor      (DWMAPI.137)
 *
 * Reads colorization from registry (no LPC needed).
 */
HRESULT WINAPI DwmGetColorizationColor(DWORD *colorization, BOOL *opaque_blend)
{
    HKEY hKey;
    DWORD dwType, dwData, cbData;
    LONG lResult;

    TRACE("(%p, %p)\n", colorization, opaque_blend);

    if (!colorization || !opaque_blend)
        return E_INVALIDARG;

    /* Default Aero blue */
    *colorization = 0x6B74B8FC;
    *opaque_blend = FALSE;

    /* Try to read from registry like the real DWM */
    lResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                            L"Software\\Microsoft\\Windows\\DWM",
                            0, KEY_READ, &hKey);
    if (lResult == ERROR_SUCCESS)
    {
        cbData = sizeof(dwData);
        if (RegQueryValueExW(hKey, L"ColorizationColor", NULL, &dwType,
                             (LPBYTE)&dwData, &cbData) == ERROR_SUCCESS
            && dwType == REG_DWORD)
        {
            *colorization = dwData;
        }

        cbData = sizeof(dwData);
        if (RegQueryValueExW(hKey, L"ColorizationOpaqueBlend", NULL, &dwType,
                             (LPBYTE)&dwData, &cbData) == ERROR_SUCCESS
            && dwType == REG_DWORD)
        {
            *opaque_blend = (dwData != 0);
        }

        RegCloseKey(hKey);
    }

    return S_OK;
}

/**********************************************************************
 *                  DwmFlush              (DWMAPI.136)
 */
HRESULT WINAPI DwmFlush(void)
{
    TRACE("()\n");
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *        DwmInvalidateIconicBitmaps      (DWMAPI.143)
 *
 * Report: sends invalidation to DWM. When composition is disabled,
 * returns S_OK (no work to do).
 */
HRESULT WINAPI DwmInvalidateIconicBitmaps(HWND hwnd)
{
    HRESULT hr;

    TRACE("(%p)\n", hwnd);

    hr = DwmpValidateWindow(hwnd);
    if (FAILED(hr))
        return hr;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmSetWindowAttribute         (DWMAPI.152)
 *
 * Report: validates hwnd, pvAttribute non-NULL, cbAttribute >= 4.
 * Dispatches via USER32 SetWindowCompositionAttribute.
 */
HRESULT WINAPI DwmSetWindowAttribute(HWND hwnd, DWORD attributenum, LPCVOID attribute, DWORD size)
{
    HRESULT hr;

    TRACE("(%p, %lx, %p, %lx)\n", hwnd, attributenum, attribute, size);

    hr = DwmpValidateWindow(hwnd);
    if (FAILED(hr))
        return hr;

    if (size < sizeof(DWORD))
        return E_INVALIDARG;

    if (!attribute)
        return E_INVALIDARG;

    switch (attributenum)
    {
    case DWMWA_NCRENDERING_POLICY:
    case DWMWA_TRANSITIONS_FORCEDISABLED:
    case DWMWA_ALLOW_NCPAINT:
    case DWMWA_NONCLIENT_RTL_LAYOUT:
    case DWMWA_FORCE_ICONIC_REPRESENTATION:
    case DWMWA_FLIP3D_POLICY:
    case DWMWA_HAS_ICONIC_BITMAP:
    case DWMWA_DISALLOW_PEEK:
    case DWMWA_EXCLUDED_FROM_PEEK:
    case DWMWA_CLOAK:
    case DWMWA_FREEZE_REPRESENTATION:
        return DWM_E_COMPOSITIONDISABLED;

    default:
        return E_INVALIDARG;
    }
}

/**********************************************************************
 *           DwmGetGraphicsStreamClient         (DWMAPI.139)
 *
 * Report: MIL/graphics-stream functions return DWM_E_COMPOSITIONDISABLED.
 */
HRESULT WINAPI DwmGetGraphicsStreamClient(UINT uIndex, UUID *pClientUuid)
{
    TRACE("(%d, %p)\n", uIndex, pClientUuid);

    if (!pClientUuid)
        return E_INVALIDARG;

    memset(pClientUuid, 0, sizeof(*pClientUuid));
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmGetTransportAttributes         (DWMAPI.141)
 */
HRESULT WINAPI DwmGetTransportAttributes(BOOL *pfIsRemoting, BOOL *pfIsConnected, DWORD *pDwGeneration)
{
    TRACE("(%p, %p, %p)\n", pfIsRemoting, pfIsConnected, pDwGeneration);

    if (!pfIsRemoting || !pfIsConnected || !pDwGeneration)
        return E_INVALIDARG;

    *pfIsRemoting = FALSE;
    *pfIsConnected = FALSE;
    *pDwGeneration = 0;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmUnregisterThumbnail         (DWMAPI.153)
 */
HRESULT WINAPI DwmUnregisterThumbnail(HTHUMBNAIL thumbnail)
{
    TRACE("(%p)\n", thumbnail);

    if (!thumbnail)
        return E_INVALIDARG;

    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmEnableMMCSS         (DWMAPI.123)
 */
HRESULT WINAPI DwmEnableMMCSS(BOOL enableMMCSS)
{
    TRACE("(%d)\n", enableMMCSS);

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmGetGraphicsStreamTransformHint         (DWMAPI.140)
 */
HRESULT WINAPI DwmGetGraphicsStreamTransformHint(UINT uIndex, MilMatrix3x2D *pTransform)
{
    TRACE("(%d, %p)\n", uIndex, pTransform);

    if (!pTransform)
        return E_INVALIDARG;

    memset(pTransform, 0, sizeof(*pTransform));
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmEnableBlurBehindWindow         (DWMAPI.122)
 *
 * Report: validates hwnd, checks composition. When disabled, S_OK.
 */
HRESULT WINAPI DwmEnableBlurBehindWindow(HWND hWnd, const DWM_BLURBEHIND *pBlurBuf)
{
    HRESULT hr;

    TRACE("(%p, %p)\n", hWnd, pBlurBuf);

    hr = DwmpValidateWindow(hWnd);
    if (FAILED(hr))
        return hr;

    if (!pBlurBuf)
        return E_INVALIDARG;

    if (pBlurBuf->dwFlags & ~(DWM_BB_ENABLE | DWM_BB_BLURREGION |
                              DWM_BB_TRANSITIONONMAXIMIZED))
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmDefWindowProc         (DWMAPI.116)
 */
BOOL WINAPI DwmDefWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, LRESULT *plResult)
{
    TRACE("(%p, %u, %Ix, %Ix, %p)\n", hWnd, Msg, wParam, lParam, plResult);

    return FALSE;
}

/**********************************************************************
 *           DwmGetWindowAttribute         (DWMAPI.142)
 *
 * Report: dispatches on attribute. Some resolved locally:
 *   DWMWA_NCRENDERING_ENABLED (1) -> BOOL
 *   DWMWA_CAPTION_BUTTON_BOUNDS (5) -> RECT
 *   DWMWA_EXTENDED_FRAME_BOUNDS (9) -> RECT via GetWindowRect
 *   DWMWA_CLOAKED (14) -> DWORD, 0
 */
HRESULT WINAPI DwmGetWindowAttribute(HWND hwnd, DWORD attribute, PVOID pv_attribute, DWORD size)
{
    TRACE("(%p %ld %p %ld)\n", hwnd, attribute, pv_attribute, size);

    if (!hwnd || !pv_attribute)
        return E_INVALIDARG;

    if (!IsWindow(hwnd))
        return E_HANDLE;

    switch (attribute)
    {
    case DWMWA_NCRENDERING_ENABLED:
        if (size < sizeof(BOOL))
            return E_INVALIDARG;
        *(BOOL *)pv_attribute = FALSE;
        return S_OK;

    case DWMWA_CAPTION_BUTTON_BOUNDS:
        if (size < sizeof(RECT))
            return E_INVALIDARG;
        SetRectEmpty((RECT *)pv_attribute);
        return S_OK;

    case DWMWA_EXTENDED_FRAME_BOUNDS:
        if (size < sizeof(RECT))
            return E_INVALIDARG;
        if (!GetWindowRect(hwnd, (RECT *)pv_attribute))
            return HRESULT_FROM_WIN32(GetLastError());
        return S_OK;

    case DWMWA_CLOAKED:
        if (size < sizeof(DWORD))
            return E_INVALIDARG;
        *(DWORD *)pv_attribute = 0;
        return S_OK;

    default:
        FIXME("(%p %ld %p %ld) unhandled attribute\n", hwnd, attribute, pv_attribute, size);
        return E_INVALIDARG;
    }
}

/**********************************************************************
 *           DwmRegisterThumbnail         (DWMAPI.147)
 */
HRESULT WINAPI DwmRegisterThumbnail(HWND dest, HWND src, PHTHUMBNAIL thumbnail_id)
{
    HRESULT hr;

    TRACE("(%p %p %p)\n", dest, src, thumbnail_id);

    if (!thumbnail_id)
        return E_INVALIDARG;

    *thumbnail_id = NULL;

    hr = DwmpValidateWindow(dest);
    if (FAILED(hr))
        return hr;

    hr = DwmpValidateWindow(src);
    if (FAILED(hr))
        return hr;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmGetCompositionTimingInfo         (DWMAPI.138)
 */
HRESULT WINAPI DwmGetCompositionTimingInfo(HWND hwnd, DWM_TIMING_INFO *info)
{
    LARGE_INTEGER performance_frequency, qpc;
    static int i, display_frequency;

    if (!info)
        return E_INVALIDARG;

    if (info->cbSize != sizeof(DWM_TIMING_INFO))
        return MILERR_MISMATCHED_SIZE;

    if(!i++) TRACE("(%p %p)\n", hwnd, info);

    memset(info, 0, info->cbSize);
    info->cbSize = sizeof(DWM_TIMING_INFO);

    display_frequency = get_display_frequency();
    info->rateRefresh.uiNumerator = display_frequency;
    info->rateRefresh.uiDenominator = 1;
    info->rateCompose.uiNumerator = display_frequency;
    info->rateCompose.uiDenominator = 1;

    QueryPerformanceFrequency(&performance_frequency);
    info->qpcRefreshPeriod = performance_frequency.QuadPart / display_frequency;

    QueryPerformanceCounter(&qpc);
    info->qpcVBlank = (qpc.QuadPart / info->qpcRefreshPeriod) * info->qpcRefreshPeriod;

    return S_OK;
}

/**********************************************************************
 *           DwmAttachMilContent         (DWMAPI.111)
 *
 * Report: returns DWM_E_COMPOSITIONDISABLED when composition off.
 */
HRESULT WINAPI DwmAttachMilContent(HWND hwnd)
{
    TRACE("(%p)\n", hwnd);
    return DwmpValidateDisabledWindow(hwnd);
}

/**********************************************************************
 *           DwmDetachMilContent         (DWMAPI.117)
 *
 * Report: returns DWM_E_COMPOSITIONDISABLED when composition off.
 */
HRESULT WINAPI DwmDetachMilContent(HWND hwnd)
{
    TRACE("(%p)\n", hwnd);
    return DwmpValidateDisabledWindow(hwnd);
}

/**********************************************************************
 *           DwmUpdateThumbnailProperties         (DWMAPI.154)
 */
HRESULT WINAPI DwmUpdateThumbnailProperties(HTHUMBNAIL thumbnail, const DWM_THUMBNAIL_PROPERTIES *props)
{
    TRACE("(%p, %p)\n", thumbnail, props);

    if (!thumbnail || !props)
        return E_INVALIDARG;

    if (props->dwFlags & ~(DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE |
                           DWM_TNP_OPACITY | DWM_TNP_VISIBLE |
                           DWM_TNP_SOURCECLIENTAREAONLY))
        return E_INVALIDARG;

    if ((props->dwFlags & DWM_TNP_RECTDESTINATION) &&
        !DwmpValidRect(&props->rcDestination))
        return E_INVALIDARG;

    if ((props->dwFlags & DWM_TNP_RECTSOURCE) &&
        !DwmpValidRect(&props->rcSource))
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmSetPresentParameters         (DWMAPI.151)
 */
HRESULT WINAPI DwmSetPresentParameters(HWND hwnd, DWM_PRESENT_PARAMETERS *params)
{
    HRESULT hr;

    TRACE("(%p %p)\n", hwnd, params);

    hr = DwmpValidateWindow(hwnd);
    if (FAILED(hr))
        return hr;

    if (!params || params->cbSize != sizeof(*params))
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmSetIconicLivePreviewBitmap         (DWMAPI.149)
 */
HRESULT WINAPI DwmSetIconicLivePreviewBitmap(HWND hwnd, HBITMAP hbmp, POINT *pos, DWORD flags)
{
    HRESULT hr;

    TRACE("(%p %p %p %lx)\n", hwnd, hbmp, pos, flags);

    hr = DwmpValidateWindow(hwnd);
    if (FAILED(hr))
        return hr;

    if (!hbmp || (flags & ~DWM_SIT_DISPLAYFRAME))
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmSetIconicThumbnail         (DWMAPI.150)
 */
HRESULT WINAPI DwmSetIconicThumbnail(HWND hwnd, HBITMAP hbmp, DWORD flags)
{
    HRESULT hr;

    TRACE("(%p %p %lx)\n", hwnd, hbmp, flags);

    hr = DwmpValidateWindow(hwnd);
    if (FAILED(hr))
        return hr;

    if (!hbmp || (flags & ~DWM_SIT_DISPLAYFRAME))
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmModifyPreviousDxFrameDuration         (DWMAPI.145)
 *
 * Modifies the duration of the most recently queued frame.
 */
HRESULT WINAPI DwmModifyPreviousDxFrameDuration(HWND hwnd, INT cRefreshes, BOOL fRelative)
{
    HRESULT hr;

    TRACE("(%p %d %d)\n", hwnd, cRefreshes, fRelative);

    hr = DwmpValidateWindow(hwnd);
    if (FAILED(hr))
        return hr;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmQueryThumbnailSourceSize         (DWMAPI.146)
 *
 * Queries the native size of the thumbnail source window.
 */
HRESULT WINAPI DwmQueryThumbnailSourceSize(HTHUMBNAIL hThumbnail, PSIZE pSize)
{
    TRACE("(%p %p)\n", hThumbnail, pSize);

    if (!hThumbnail || !pSize)
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmSetDxFrameDuration         (DWMAPI.148)
 *
 * Tells DWM how many refresh cycles each frame should be held.
 */
HRESULT WINAPI DwmSetDxFrameDuration(HWND hwnd, INT cRefreshes)
{
    HRESULT hr;

    TRACE("(%p %d)\n", hwnd, cRefreshes);

    hr = DwmpValidateWindow(hwnd);
    if (FAILED(hr))
        return hr;

    return DWM_E_COMPOSITIONDISABLED;
}

/* ========================================================================
 * ORDINAL-ONLY EXPORTS (31) - Private/internal APIs
 *
 * Used by explorer.exe, shell32, and other system components.
 * Names derived from ETW trace strings in the Windows 7 binary.
 * ======================================================================== */

/**********************************************************************
 *           DwmpDxGetWindowSharedSurface         (DWMAPI.100)
 *
 * Gets the DirectX shared surface for a window's redirection surface.
 */
HRESULT WINAPI DwmpDxGetWindowSharedSurface(HWND hwnd, void *pAdapterLuid,
    void *pFmtWindow, void *pSharedHandle, DWORD *pPresentFlags, void *pSurface)
{
    TRACE("(%p %p %p %p %p %p)\n", hwnd, pAdapterLuid, pFmtWindow,
          pSharedHandle, pPresentFlags, pSurface);

    if (!hwnd || !pAdapterLuid || !pFmtWindow || !pSharedHandle ||
        !pPresentFlags || !pSurface)
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpDxUpdateWindowSharedSurface         (DWMAPI.101)
 *
 * Notifies DWM that a window's shared surface has been updated.
 */
HRESULT WINAPI DwmpDxUpdateWindowSharedSurface(HWND hwnd, void *pPresent,
    DWORD dwFlags, void *pDirtyRegion)
{
    TRACE("(%p %p %ld %p)\n", hwnd, pPresent, dwFlags, pDirtyRegion);

    if (!hwnd || !pPresent)
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpRestartComposition         (DWMAPI.103)
 */
HRESULT WINAPI DwmpRestartComposition(void)
{
    TRACE("()\n");
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpSetColorizationColor         (DWMAPI.104)
 *
 * Report: returns E_NOTIMPL (write-side, not for clients).
 */
HRESULT WINAPI DwmpSetColorizationColor(DWORD color)
{
    FIXME("(%lx) stub\n", color);
    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmpStartOrStopFlip3D         (DWMAPI.105)
 */
HRESULT WINAPI DwmpStartOrStopFlip3D(void)
{
    TRACE("()\n");
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpIsCompositionCapable         (DWMAPI.106)
 */
HRESULT WINAPI DwmpIsCompositionCapable(void)
{
    TRACE("()\n");
    return S_FALSE;
}

/**********************************************************************
 *           DwmpGetGlobalState         (DWMAPI.107)
 *
 * Queries composition policy/global state.
 */
HRESULT WINAPI DwmpGetGlobalState(void *pState)
{
    FIXME("(%p) stub\n", pState);

    if (!pState)
        return E_INVALIDARG;

    *(DWORD *)pState = 0;
    return S_OK;
}

/**********************************************************************
 *           DwmpEnableRedirection         (DWMAPI.108)
 *
 * Report: returns E_NOTIMPL (same RVA as ordinal 104).
 */
HRESULT WINAPI DwmpEnableRedirection(DWORD enable)
{
    FIXME("(%ld) stub\n", enable);
    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmpOpenGraphicsStream         (DWMAPI.109)
 *
 * Report: stub returning DWM_E_COMPOSITIONDISABLED.
 */
HRESULT WINAPI DwmpOpenGraphicsStream(void *pStream, void *pUnknown)
{
    TRACE("(%p %p)\n", pStream, pUnknown);
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpCloseGraphicsStream         (DWMAPI.110)
 *
 * Report: stub returning DWM_E_COMPOSITIONDISABLED.
 */
HRESULT WINAPI DwmpCloseGraphicsStream(void *pStream)
{
    TRACE("(%p)\n", pStream);
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpSetGraphicsStreamTransformHint         (DWMAPI.112)
 *
 * Report: stub returning DWM_E_COMPOSITIONDISABLED.
 */
HRESULT WINAPI DwmpSetGraphicsStreamTransformHint(DWORD uIndex, MilMatrix3x2D *pTransform)
{
    TRACE("(%ld %p)\n", uIndex, pTransform);

    if (!pTransform)
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpActivateLivePreview         (DWMAPI.113)
 *
 * Activates Aero Peek live preview.
 */
HRESULT WINAPI DwmpActivateLivePreview(DWORD fActivate, HWND hwnd, DWORD dwUnk)
{
    TRACE("(%ld %p %ld)\n", fActivate, hwnd, dwUnk);
    return DwmpValidateDisabledWindow(hwnd);
}

/**********************************************************************
 *           DwmpQueryThumbnailType         (DWMAPI.114)
 */
HRESULT WINAPI DwmpQueryThumbnailType(void *pType)
{
    FIXME("(%p) stub\n", pType);

    if (!pType)
        return E_INVALIDARG;

    *(DWORD *)pType = 0;
    return S_OK;
}

/**********************************************************************
 *           DwmpStartupViaUserInit         (DWMAPI.115)
 *
 * Report: session initialization entry point called by winlogon/csrss.
 */
HRESULT WINAPI DwmpStartupViaUserInit(void)
{
    TRACE("()\n");
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpGetAssessment         (DWMAPI.118)
 *
 * WEI (Windows Experience Index) score related.
 */
HRESULT WINAPI DwmpGetAssessment(void *pAssessment)
{
    FIXME("(%p) stub\n", pAssessment);

    if (!pAssessment)
        return E_INVALIDARG;

    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmpGetAssessmentUsage         (DWMAPI.119)
 */
HRESULT WINAPI DwmpGetAssessmentUsage(void *pUsage)
{
    FIXME("(%p) stub\n", pUsage);

    if (!pUsage)
        return E_INVALIDARG;

    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmpSetAssessmentUsage         (DWMAPI.120)
 *
 * Report: validates parameter (returns E_INVALIDARG if nonzero).
 */
HRESULT WINAPI DwmpSetAssessmentUsage(DWORD usage)
{
    TRACE("(%ld)\n", usage);

    if (usage != 0)
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpIsSessionDWM         (DWMAPI.121)
 */
HRESULT WINAPI DwmpIsSessionDWM(BOOL *pIsDWM)
{
    FIXME("(%p) stub\n", pIsDWM);

    if (!pIsDWM)
        return E_INVALIDARG;

    *pIsDWM = FALSE;
    return S_OK;
}

/**********************************************************************
 *           DwmpRegisterThumbnail         (DWMAPI.124)
 *
 * Private version of DwmRegisterThumbnail.
 */
HRESULT WINAPI DwmpRegisterThumbnail(HWND dest, HWND src, PHTHUMBNAIL thumbnail_id)
{
    TRACE("(%p %p %p)\n", dest, src, thumbnail_id);
    return DwmRegisterThumbnail(dest, src, thumbnail_id);
}

/**********************************************************************
 *           DwmpDxBindSwapChain         (DWMAPI.125)
 */
HRESULT WINAPI DwmpDxBindSwapChain(void *pSwapChain, DWORD dwFlags)
{
    FIXME("(%p %ld) stub\n", pSwapChain, dwFlags);
    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmpDxUnbindSwapChain         (DWMAPI.126)
 */
HRESULT WINAPI DwmpDxUnbindSwapChain(void *pSwapChain)
{
    FIXME("(%p) stub\n", pSwapChain);
    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmpGetColorizationParameters         (DWMAPI.127)
 *
 * Returns the full set of colorization parameters.
 */
HRESULT WINAPI DwmpGetColorizationParameters(void *params)
{
    TRACE("(%p)\n", params);

    if (!params)
        return E_INVALIDARG;

    /*
     * This private ordinal takes an opaque buffer and no size.  Writing the
     * seven DWORD Windows payload would be an ABI guess and can overrun the
     * caller, so keep the unsupported status explicit.
     */
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpDxgiIsThreadDesktopComposited         (DWMAPI.128)
 */
HRESULT WINAPI DwmpDxgiIsThreadDesktopComposited(void)
{
    TRACE("()\n");
    return S_FALSE;
}

/**********************************************************************
 *           DwmpDxgiDisableRedirection         (DWMAPI.129)
 */
HRESULT WINAPI DwmpDxgiDisableRedirection(DWORD dwFlags)
{
    TRACE("(%ld)\n", dwFlags);
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpDxgiEnableRedirection         (DWMAPI.130)
 */
HRESULT WINAPI DwmpDxgiEnableRedirection(DWORD dwFlags)
{
    TRACE("(%ld)\n", dwFlags);
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpSetColorizationParameters         (DWMAPI.131)
 */
HRESULT WINAPI DwmpSetColorizationParameters(void *params)
{
    TRACE("(%p)\n", params);

    if (!params)
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpGetCompositionTimingInfoEx         (DWMAPI.132)
 */
HRESULT WINAPI DwmpGetCompositionTimingInfoEx(HWND hwnd, DWM_TIMING_INFO *info, DWORD dwFlags)
{
    TRACE("(%p %p %ld)\n", hwnd, info, dwFlags);

    if (!info)
        return E_INVALIDARG;

    if (dwFlags)
        return E_INVALIDARG;

    return DwmGetCompositionTimingInfo(hwnd, info);
}

/**********************************************************************
 *           DwmpDxUpdateWindowRedirectionBltSurface         (DWMAPI.133)
 */
HRESULT WINAPI DwmpDxUpdateWindowRedirectionBltSurface(void *pSurface, void *pDirtyRegion)
{
    TRACE("(%p %p)\n", pSurface, pDirtyRegion);

    if (!pSurface)
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmpDxSetContentHostingInformation         (DWMAPI.134)
 */
HRESULT WINAPI DwmpDxSetContentHostingInformation(void *pInfo, DWORD dwFlags)
{
    TRACE("(%p %ld)\n", pInfo, dwFlags);

    if (!pInfo)
        return E_INVALIDARG;

    return DWM_E_COMPOSITIONDISABLED;
}
