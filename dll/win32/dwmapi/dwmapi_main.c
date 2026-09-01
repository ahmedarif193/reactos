/*
 * Dwmapi
 *
 * Copyright 2007 Andras Kovacs
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
 */

#include <stdarg.h>

#ifdef __REACTOS__
#include <ndk/kefuncs.h>
#include <ndk/rtlfuncs.h>
#else
#include "winternl.h"
#endif
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#ifdef __REACTOS__
#include <reactos/user32_vista.h>
#include <reactos/dwmframe.h>
#endif
#include "dwmapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dwmapi);

#ifdef __REACTOS__
/* Chromium intentionally discovers DirectComposition with GetModuleHandleW()
 * after loading the desktop composition stack. Keep dcomp.dll resident as a
 * real dwmapi dependency, matching that system-level availability contract. */
extern HRESULT WINAPI DCompositionCreateDevice3(IUnknown *, REFIID, void **);
static HRESULT (WINAPI * volatile dcomp_create_device3_import)(IUnknown *, REFIID, void **)
        = DCompositionCreateDevice3;
DWORD_PTR NTAPI NtUserCallOneParam(DWORD_PTR Param, DWORD Routine);
#endif

/* Shared with the themed non-client renderer in uxtheme.dll. */
static const WCHAR immersive_dark_mode_propW[] = L"ReactOS.Dwm.ImmersiveDarkMode";
static const WCHAR nc_rendering_policy_propW[] = L"ReactOS.Dwm.NcRenderingPolicy";
static const WCHAR transitions_disabled_propW[] = L"ReactOS.Dwm.TransitionsDisabled";
static const WCHAR allow_ncpaint_propW[] = L"ReactOS.Dwm.AllowNcPaint";
static const WCHAR nc_rtl_layout_propW[] = L"ReactOS.Dwm.NcRtlLayout";
static const WCHAR force_iconic_propW[] = L"ReactOS.Dwm.ForceIconicRepresentation";
static const WCHAR has_iconic_bitmap_propW[] = L"ReactOS.Dwm.HasIconicBitmap";
static const WCHAR disallow_peek_propW[] = L"ReactOS.Dwm.DisallowPeek";
static const WCHAR excluded_from_peek_propW[] = L"ReactOS.Dwm.ExcludedFromPeek";
static const WCHAR cloak_propW[] = L"ReactOS.Dwm.Cloak";
static const WCHAR freeze_representation_propW[] = L"ReactOS.Dwm.FreezeRepresentation";
static const WCHAR passive_update_propW[] = L"ReactOS.Dwm.PassiveUpdateMode";
static const WCHAR host_backdrop_propW[] = L"ReactOS.Dwm.UseHostBackdropBrush";
static const WCHAR corner_preference_propW[] = L"ReactOS.Dwm.WindowCornerPreference";
static const WCHAR border_color_propW[] = L"ReactOS.Dwm.BorderColor";
static const WCHAR caption_color_propW[] = L"ReactOS.Dwm.CaptionColor";
static const WCHAR text_color_propW[] = L"ReactOS.Dwm.TextColor";
static const WCHAR system_backdrop_propW[] = L"ReactOS.Dwm.SystemBackdropType";
static const WCHAR mica_effect_propW[] = L"ReactOS.Dwm.MicaEffect";

BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);

    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

HRESULT WINAPI
DllCanUnloadNow(void)
{
    return S_OK;
}

HRESULT WINAPI
DllGetClassObject(REFCLSID class_id, REFIID interface_id, void **object)
{
    UNREFERENCED_PARAMETER(class_id);
    UNREFERENCED_PARAMETER(interface_id);

    if (object == NULL)
        return E_POINTER;
    *object = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* These compatibility entry points are literal return stubs in native Win11.
 * Their private parameter lists are intentionally not inferred here: the
 * ARM64 ABI permits ignored register arguments and the implementation does not
 * inspect caller state. */
HRESULT WINAPI DwmpRestartComposition(void) { return S_OK; }
HRESULT WINAPI DwmpSetColorizationColor(void) { return E_NOTIMPL; }
HRESULT WINAPI DwmpStartOrStopFlip3D(void) { return S_OK; }
HRESULT WINAPI DwmpEnableRedirection(void) { return E_NOTIMPL; }
HRESULT WINAPI DwmpOpenGraphicsStream(void) { return DWM_E_COMPOSITIONDISABLED; }
HRESULT WINAPI DwmpCloseGraphicsStream(void) { return DWM_E_COMPOSITIONDISABLED; }
HRESULT WINAPI DwmpSetGraphicsStreamTransformHint(void) { return DWM_E_COMPOSITIONDISABLED; }
HRESULT WINAPI DwmpEnableDDASupport(void) { return S_OK; }
HRESULT WINAPI DwmTetherTextContact(void) { return S_OK; }

HRESULT WINAPI
DwmpIsCompositionCapable(void *reserved, BOOL *capable)
{
    UNREFERENCED_PARAMETER(reserved);

    if (capable == NULL)
        return E_INVALIDARG;
    *capable = TRUE;
    return S_OK;
}

HRESULT WINAPI
DwmpGetTitleBarVisual(HWND hwnd, ULONGLONG *visual)
{
    UNREFERENCED_PARAMETER(hwnd);

    *visual = ~(ULONGLONG)0;
    return E_NOTIMPL;
}

static const WCHAR *
dwm_get_attribute_property(DWORD attribute)
{
    switch (attribute)
    {
        case DWMWA_NCRENDERING_POLICY: return nc_rendering_policy_propW;
        case DWMWA_TRANSITIONS_FORCEDISABLED: return transitions_disabled_propW;
        case DWMWA_ALLOW_NCPAINT: return allow_ncpaint_propW;
        case DWMWA_NONCLIENT_RTL_LAYOUT: return nc_rtl_layout_propW;
        case DWMWA_FORCE_ICONIC_REPRESENTATION: return force_iconic_propW;
        case DWMWA_HAS_ICONIC_BITMAP: return has_iconic_bitmap_propW;
        case DWMWA_DISALLOW_PEEK: return disallow_peek_propW;
        case DWMWA_EXCLUDED_FROM_PEEK: return excluded_from_peek_propW;
        case DWMWA_CLOAK: return cloak_propW;
        case DWMWA_FREEZE_REPRESENTATION: return freeze_representation_propW;
        case DWMWA_PASSIVE_UPDATE_MODE: return passive_update_propW;
        case DWMWA_USE_HOSTBACKDROPBRUSH: return host_backdrop_propW;
        case 19: /* Pre-release alias retained by native Win11. */
        case DWMWA_USE_IMMERSIVE_DARK_MODE: return immersive_dark_mode_propW;
        case DWMWA_WINDOW_CORNER_PREFERENCE: return corner_preference_propW;
        case DWMWA_BORDER_COLOR: return border_color_propW;
        case DWMWA_CAPTION_COLOR: return caption_color_propW;
        case DWMWA_TEXT_COLOR: return text_color_propW;
        case DWMWA_SYSTEMBACKDROP_TYPE: return system_backdrop_propW;
        case 39: return mica_effect_propW;
        default: return NULL;
    }
}

static DWORD
dwm_get_dword_attribute(HWND hwnd, DWORD attribute)
{
    const WCHAR *property = dwm_get_attribute_property(attribute);

    if (property == NULL)
        return 0;
    return (DWORD)(ULONG_PTR)GetPropW(hwnd, property);
}

static HRESULT
dwm_set_dword_attribute(HWND hwnd, DWORD attribute, DWORD value)
{
    const WCHAR *property = dwm_get_attribute_property(attribute);

    if (property == NULL)
        return E_INVALIDARG;
    if (value == 0)
    {
        RemovePropW(hwnd, property);
    }
    else if (!SetPropW(hwnd, property, (HANDLE)(ULONG_PTR)value))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    RedrawWindow(hwnd, NULL, NULL,
                 RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    return S_OK;
}


/**********************************************************************
 *           DwmIsCompositionEnabled         (DWMAPI.@)
 */
HRESULT WINAPI DwmIsCompositionEnabled(BOOL *enabled)
{
#ifndef __REACTOS__
    RTL_OSVERSIONINFOEXW version;
#endif

    TRACE("%p\n", enabled);

    if (!enabled)
        return E_INVALIDARG;

#ifdef __REACTOS__
    (void)dcomp_create_device3_import;
#endif
    *enabled = FALSE;
#ifdef __REACTOS__
    *enabled = NtUserCallOneParam(0, DWM_ROUTINE_ISENABLED) != 0;
#else
    version.dwOSVersionInfoSize = sizeof(version);
    if (!RtlGetVersion(&version))
        *enabled = (version.dwMajorVersion > 6 || (version.dwMajorVersion == 6 && version.dwMinorVersion >= 3));
#endif

    return S_OK;
}

/**********************************************************************
 *           DwmEnableComposition         (DWMAPI.102)
 */
HRESULT WINAPI DwmEnableComposition(UINT uCompositionAction)
{
    FIXME("(%d) stub\n", uCompositionAction);

    return S_OK;
}

/**********************************************************************
 *           DwmExtendFrameIntoClientArea    (DWMAPI.@)
 */
HRESULT WINAPI DwmExtendFrameIntoClientArea(HWND hwnd, const MARGINS* margins)
{
#ifdef __REACTOS__
    TRACE("(%p, %p)\n", hwnd, margins);

    if (!IsWindow(hwnd))
        return E_HANDLE;
    if (!margins)
        return E_INVALIDARG;

    /* The frame margins are compositor metadata. Do not rewrite USER window
     * styles here: doing so changes Chromium's output-device selection before
     * DirectComposition has attached its target. */
    return S_OK;
#else
    FIXME("(%p, %p) stub\n", hwnd, margins);

    return S_OK;
#endif
}

/**********************************************************************
 *           DwmGetColorizationColor      (DWMAPI.@)
 */
HRESULT WINAPI DwmGetColorizationColor(DWORD *colorization, BOOL *opaque_blend)
{
    FIXME("(%p, %p) stub\n", colorization, opaque_blend);

    return E_NOTIMPL;
}

/**********************************************************************
 *        DwmInvalidateIconicBitmaps      (DWMAPI.@)
 */
HRESULT WINAPI DwmInvalidateIconicBitmaps(HWND hwnd)
{
    static BOOL once;

    if (!once++) FIXME("(%p) stub\n", hwnd);

    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmSetWindowAttribute         (DWMAPI.@)
 */
HRESULT WINAPI DwmSetWindowAttribute(HWND hwnd, DWORD attributenum, LPCVOID attribute, DWORD size)
{
    static BOOL once;
    DWORD value;

    TRACE("(%p, %lx, %p, %lx)\n", hwnd, attributenum, attribute, size);

    /* Native rejects the buffer before it contacts the window manager. */
    if (!attribute || size < sizeof(value))
        return E_INVALIDARG;
    if (!IsWindow(hwnd))
        return E_HANDLE;

    value = *(const DWORD *)attribute;
    switch (attributenum)
    {
        case DWMWA_NCRENDERING_POLICY:
        case DWMWA_TRANSITIONS_FORCEDISABLED:
        case DWMWA_ALLOW_NCPAINT:
        case DWMWA_NONCLIENT_RTL_LAYOUT:
        case DWMWA_FORCE_ICONIC_REPRESENTATION:
        case DWMWA_HAS_ICONIC_BITMAP:
        case DWMWA_DISALLOW_PEEK:
        case DWMWA_EXCLUDED_FROM_PEEK:
        case DWMWA_CLOAK:
        case DWMWA_FREEZE_REPRESENTATION:
        case DWMWA_PASSIVE_UPDATE_MODE:
        case 19:
        case DWMWA_USE_IMMERSIVE_DARK_MODE:
        case DWMWA_WINDOW_CORNER_PREFERENCE:
        case DWMWA_BORDER_COLOR:
        case DWMWA_CAPTION_COLOR:
        case DWMWA_TEXT_COLOR:
        case DWMWA_SYSTEMBACKDROP_TYPE:
        case 39:
            return dwm_set_dword_attribute(hwnd, attributenum, value);

        case DWMWA_USE_HOSTBACKDROPBRUSH:
            /* Native's special path requires the exact DWORD shape. */
            if (size != sizeof(value))
                return E_INVALIDARG;
            return dwm_set_dword_attribute(hwnd, attributenum, value);

        case DWMWA_NCRENDERING_ENABLED:
        case DWMWA_CAPTION_BUTTON_BOUNDS:
        case DWMWA_EXTENDED_FRAME_BOUNDS:
        case DWMWA_CLOAKED:
        case DWMWA_VISIBLE_FRAME_BORDER_THICKNESS:
            return E_INVALIDARG;

        default:
            break;
    }

    if (!once++) FIXME("attribute %lu is not implemented\n", attributenum);
    return E_INVALIDARG;
}

/**********************************************************************
 *           DwmGetGraphicsStreamClient         (DWMAPI.@)
 */
HRESULT WINAPI DwmGetGraphicsStreamClient(UINT uIndex, UUID *pClientUuid)
{
    FIXME("(%d, %p) stub\n", uIndex, pClientUuid);

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmGetTransportAttributes         (DWMAPI.@)
 */
HRESULT WINAPI DwmGetTransportAttributes(BOOL *pfIsRemoting, BOOL *pfIsConnected, DWORD *pDwGeneration)
{
    FIXME("(%p, %p, %p) stub\n", pfIsRemoting, pfIsConnected, pDwGeneration);

    *pfIsRemoting = FALSE;
    *pfIsConnected = TRUE;
    *pDwGeneration = 1;
    return S_OK;
}

/**********************************************************************
 *           DwmGetUnmetTabRequirements         (DWMAPI.@)
 */
HRESULT WINAPI DwmGetUnmetTabRequirements(
    HWND hwnd, enum DWM_TAB_WINDOW_REQUIREMENTS *requirements)
{
    FIXME("(%p, %p) stub\n", hwnd, requirements);

    *requirements = DWMTWR_IMPLEMENTED_BY_SYSTEM;
    return S_OK;
}

/**********************************************************************
 *           DwmUnregisterThumbnail         (DWMAPI.@)
 */
HRESULT WINAPI DwmUnregisterThumbnail(HTHUMBNAIL thumbnail)
{
    FIXME("(%p) stub\n", thumbnail);

    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmQueryThumbnailSourceSize         (DWMAPI.@)
 */
HRESULT WINAPI DwmQueryThumbnailSourceSize(HTHUMBNAIL thumbnail, SIZE *size)
{
    FIXME("(%p, %p) stub\n", thumbnail, size);

    if (size == NULL)
        return E_INVALIDARG;
    size->cx = 0;
    size->cy = 0;
    return E_INVALIDARG;
}

/**********************************************************************
 *           DwmEnableMMCSS         (DWMAPI.@)
 */
HRESULT WINAPI DwmEnableMMCSS(BOOL enableMMCSS)
{
    FIXME("(%d) stub\n", enableMMCSS);

    return S_OK;
}

/**********************************************************************
 *           DwmGetGraphicsStreamTransformHint         (DWMAPI.@)
 */
HRESULT WINAPI DwmGetGraphicsStreamTransformHint(UINT uIndex, MilMatrix3x2D *pTransform)
{
    FIXME("(%d, %p) stub\n", uIndex, pTransform);

    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmEnableBlurBehindWindow         (DWMAPI.@)
 */
HRESULT WINAPI DwmEnableBlurBehindWindow(HWND hWnd, const DWM_BLURBEHIND *pBlurBuf)
{
#ifdef __REACTOS__
    DWM_BLUR_REQUEST Request;
    NTSTATUS Status;
#endif

    TRACE("%p %p\n", hWnd, pBlurBuf);

    /* Native Win11 validates this shape before allocating or submitting its
     * 0x40000022 composition-channel command. */
    if (!IsWindow(hWnd) || pBlurBuf == NULL || pBlurBuf->dwFlags == 0 ||
        (pBlurBuf->dwFlags & ~(DWM_BB_ENABLE | DWM_BB_BLURREGION |
                              DWM_BB_TRANSITIONONMAXIMIZED)) != 0)
    {
        return E_INVALIDARG;
    }

#ifdef __REACTOS__
    RtlZeroMemory(&Request, sizeof(Request));
    Request.StructSize = sizeof(Request);
    Request.Flags = pBlurBuf->dwFlags;
    Request.Window = (ULONGLONG)(ULONG_PTR)hWnd;
    Request.Region = (ULONGLONG)(ULONG_PTR)pBlurBuf->hRgnBlur;
    Request.Enable = !!pBlurBuf->fEnable;
    Request.TransitionOnMaximized = !!pBlurBuf->fTransitionOnMaximized;

    Status = (NTSTATUS)(LONG)NtUserCallOneParam(
        (DWORD_PTR)&Request, DWM_ROUTINE_SETBLUR);
    if (NT_SUCCESS(Status))
        return S_OK;
    if (Status == STATUS_DEVICE_NOT_READY)
        return DWM_E_COMPOSITIONDISABLED;
    if (Status == STATUS_INVALID_PARAMETER)
        return E_INVALIDARG;
    return HRESULT_FROM_NT(Status);
#else
    return E_NOTIMPL;
#endif
}

/**********************************************************************
 *           DwmDefWindowProc         (DWMAPI.@)
 */
BOOL WINAPI DwmDefWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, LRESULT *plResult)
{
    static int i;

    if (!i++) FIXME("stub\n");

    return FALSE;
}

/**********************************************************************
 *           DwmGetWindowAttribute         (DWMAPI.@)
 */
HRESULT WINAPI DwmGetWindowAttribute(HWND hwnd, DWORD attribute, PVOID pv_attribute, DWORD size)
{
    HRESULT hr;

    TRACE("(%p %ld %p %ld)\n", hwnd, attribute, pv_attribute, size);

    /* Native performs this common shape check before attribute dispatch. */
    if (!pv_attribute || size < sizeof(DWORD))
        return E_INVALIDARG;
    if (!IsWindow(hwnd))
        return E_HANDLE;

    switch (attribute) {
    case DWMWA_NCRENDERING_ENABLED:
    {
        BOOL *enabled = (BOOL *)pv_attribute;

        hr = DwmIsCompositionEnabled(enabled);
        break;
    }
    case DWMWA_NCRENDERING_POLICY:
    case DWMWA_TRANSITIONS_FORCEDISABLED:
    case DWMWA_ALLOW_NCPAINT:
    case DWMWA_NONCLIENT_RTL_LAYOUT:
    case DWMWA_FORCE_ICONIC_REPRESENTATION:
    case DWMWA_HAS_ICONIC_BITMAP:
    case DWMWA_DISALLOW_PEEK:
    case DWMWA_EXCLUDED_FROM_PEEK:
    case DWMWA_CLOAK:
    case DWMWA_FREEZE_REPRESENTATION:
    case DWMWA_PASSIVE_UPDATE_MODE:
    case DWMWA_USE_HOSTBACKDROPBRUSH:
    case 19:
    case DWMWA_USE_IMMERSIVE_DARK_MODE:
    case DWMWA_WINDOW_CORNER_PREFERENCE:
    case DWMWA_BORDER_COLOR:
    case DWMWA_CAPTION_COLOR:
    case DWMWA_TEXT_COLOR:
    case DWMWA_SYSTEMBACKDROP_TYPE:
    case 39:
    {
        DWORD *value = (DWORD *)pv_attribute;

        *value = dwm_get_dword_attribute(hwnd, attribute);
        hr = S_OK;
        break;
    }
    case DWMWA_EXTENDED_FRAME_BOUNDS:
    {
        RECT *rect = (RECT *)pv_attribute;
        DPI_AWARENESS_CONTEXT context;

        if (size < sizeof(*rect))
            return E_NOT_SUFFICIENT_BUFFER;
        if (GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD)
            return E_HANDLE;

        /* DWM frame bounds are always in physical coords */
        context = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        if (GetWindowRect(hwnd, rect))
            hr = S_OK;
        else
            hr = HRESULT_FROM_WIN32(GetLastError());

        SetThreadDpiAwarenessContext(context);
        break;
    }
    case DWMWA_CAPTION_BUTTON_BOUNDS:
    {
        RECT *rect = (RECT *)pv_attribute;
        RECT win;
        DWORD style, ex_style;
        LONG border_x, border_y, button_w, button_h, count;

        if (size < sizeof(*rect))
            return E_NOT_SUFFICIENT_BUFFER;

        style = GetWindowLongW(hwnd, GWL_STYLE);
        ex_style = GetWindowLongW(hwnd, GWL_EXSTYLE);
        if (!GetWindowRect(hwnd, &win))
            return HRESULT_FROM_WIN32(GetLastError());

        if (style & WS_THICKFRAME)
        {
            border_x = GetSystemMetrics(SM_CXSIZEFRAME);
            border_y = GetSystemMetrics(SM_CYSIZEFRAME);
        }
        else if ((style & (WS_DLGFRAME | WS_BORDER)) == WS_DLGFRAME || (ex_style & WS_EX_DLGMODALFRAME))
        {
            border_x = GetSystemMetrics(SM_CXFIXEDFRAME);
            border_y = GetSystemMetrics(SM_CYFIXEDFRAME);
        }
        else
        {
            border_x = GetSystemMetrics(SM_CXBORDER);
            border_y = GetSystemMetrics(SM_CYBORDER);
        }

        if (ex_style & WS_EX_TOOLWINDOW)
        {
            button_w = GetSystemMetrics(SM_CXSMSIZE);
            button_h = GetSystemMetrics(SM_CYSMSIZE) - 2;
            count = 1;
        }
        else
        {
            button_w = GetSystemMetrics(SM_CXSIZE);
            button_h = GetSystemMetrics(SM_CYSIZE) - 2;
            count = (style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) ? 3 : 1;
        }

        rect->top = border_y + 2;
        rect->bottom = rect->top + button_h;
        rect->right = (win.right - win.left) - border_x - 2;
        rect->left = rect->right - count * button_w - (count - 1);
        hr = S_OK;
        break;
    }
    case DWMWA_CLOAKED:
    {
        DWORD *cloaked = (DWORD *)pv_attribute;

        *cloaked = dwm_get_dword_attribute(hwnd, DWMWA_CLOAK) != 0;
        hr = S_OK;
        break;
    }
    case DWMWA_VISIBLE_FRAME_BORDER_THICKNESS:
    {
        DWORD *thickness = (DWORD *)pv_attribute;
        DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
        DWORD ex_style = GetWindowLongW(hwnd, GWL_EXSTYLE);

        if (style & WS_THICKFRAME)
            *thickness = GetSystemMetrics(SM_CXSIZEFRAME) +
                         GetSystemMetrics(SM_CXPADDEDBORDER);
        else if ((style & (WS_DLGFRAME | WS_BORDER)) == WS_DLGFRAME ||
                 (ex_style & WS_EX_DLGMODALFRAME))
            *thickness = GetSystemMetrics(SM_CXFIXEDFRAME);
        else if (style & WS_BORDER)
            *thickness = GetSystemMetrics(SM_CXBORDER);
        else
            *thickness = 0;
        hr = S_OK;
        break;
    }
    default:
        FIXME("attribute %ld not implemented.\n", attribute);
        hr = E_INVALIDARG;
        break;
    }

    return hr;
}

/**********************************************************************
 *           DwmRegisterThumbnail         (DWMAPI.@)
 */
HRESULT WINAPI DwmRegisterThumbnail(HWND dest, HWND src, PHTHUMBNAIL thumbnail_id)
{
    FIXME("(%p %p %p) stub\n", dest, src, thumbnail_id);

    return E_NOTIMPL;
}

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

/**********************************************************************
 *           DwmGetCompositionTimingInfo         (DWMAPI.@)
 */
HRESULT WINAPI DwmGetCompositionTimingInfo(HWND hwnd, DWM_TIMING_INFO *info)
{
    LARGE_INTEGER performance_frequency, qpc;
    static int i, display_frequency;

    if (!info)
        return E_INVALIDARG;

    if (info->cbSize != sizeof(DWM_TIMING_INFO))
        return MILERR_MISMATCHED_SIZE;

    if(!i++) FIXME("(%p %p)\n", hwnd, info);

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
 *                  DwmFlush              (DWMAPI.@)
 */
HRESULT WINAPI DwmFlush(void)
{
    LARGE_INTEGER qpf, qpc, delay;
    LONG64 qpc_refresh_period;
    int display_frequency;
    static BOOL once;

    if (!once++)
        FIXME("stub.\n");
    else
        TRACE("stub.\n");

    display_frequency = get_display_frequency();
    NtQueryPerformanceCounter(&qpc, &qpf);
    qpc_refresh_period = qpf.QuadPart / display_frequency;
    delay.QuadPart = (qpc.QuadPart - ((qpc.QuadPart + qpc_refresh_period - 1) / qpc_refresh_period) * qpc_refresh_period)
            * 10000000 / qpf.QuadPart;
    NtDelayExecution(FALSE, &delay);

    return S_OK;
}

/**********************************************************************
 *           DwmAttachMilContent         (DWMAPI.@)
 */
HRESULT WINAPI DwmAttachMilContent(HWND hwnd)
{
    FIXME("(%p) stub\n", hwnd);
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmDetachMilContent         (DWMAPI.@)
 */
HRESULT WINAPI DwmDetachMilContent(HWND hwnd)
{
    FIXME("(%p) stub\n", hwnd);
    return DWM_E_COMPOSITIONDISABLED;
}

/**********************************************************************
 *           DwmModifyPreviousDxFrameDuration         (DWMAPI.@)
 */
HRESULT WINAPI DwmModifyPreviousDxFrameDuration(HWND hwnd, INT refreshes,
                                                BOOL relative)
{
    FIXME("(%p, %d, %d) stub\n", hwnd, refreshes, relative);
    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmSetDxFrameDuration         (DWMAPI.@)
 */
HRESULT WINAPI DwmSetDxFrameDuration(HWND hwnd, INT refreshes)
{
    FIXME("(%p, %d) stub\n", hwnd, refreshes);
    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmUpdateThumbnailProperties         (DWMAPI.@)
 */
HRESULT WINAPI DwmUpdateThumbnailProperties(HTHUMBNAIL thumbnail, const DWM_THUMBNAIL_PROPERTIES *props)
{
    FIXME("(%p, %p) stub\n", thumbnail, props);
    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmSetPresentParameters         (DWMAPI.@)
 */
HRESULT WINAPI DwmSetPresentParameters(HWND hwnd, DWM_PRESENT_PARAMETERS *params)
{
    FIXME("(%p %p) stub\n", hwnd, params);
    return E_NOTIMPL;
};

/**********************************************************************
 *           DwmSetIconicLivePreviewBitmap         (DWMAPI.@)
 */
HRESULT WINAPI DwmSetIconicLivePreviewBitmap(HWND hwnd, HBITMAP hbmp, POINT *pos, DWORD flags)
{
    FIXME("(%p %p %p %lx) stub\n", hwnd, hbmp, pos, flags);
    return S_OK;
};

/**********************************************************************
 *           DwmSetIconicThumbnail         (DWMAPI.@)
 */
HRESULT WINAPI DwmSetIconicThumbnail(HWND hwnd, HBITMAP hbmp, DWORD flags)
{
    FIXME("(%p %p %lx) stub\n", hwnd, hbmp, flags);
    return S_OK;
};

/**********************************************************************
 *           DwmpGetColorizationParameters         (DWMAPI.@)
 */
HRESULT WINAPI DwmpGetColorizationParameters(void *params)
{
    FIXME("(%p) stub\n", params);
    return E_NOTIMPL;
}

/**********************************************************************
 *           DwmShowContact         (DWMAPI.@)
 */
HRESULT WINAPI DwmShowContact(DWORD pointer_id, enum DWM_SHOWCONTACT showcontact)
{
    FIXME("pointer_id %#lx, showcontact %#x stub\n", pointer_id, showcontact);
    return S_OK;
}

/**********************************************************************
 *           DwmRenderGesture         (DWMAPI.@)
 */
HRESULT WINAPI DwmRenderGesture(enum GESTURE_TYPE gesture, UINT contact_count,
                                const DWORD *pointer_ids,
                                const POINT *points)
{
    if (pointer_ids == NULL || points == NULL ||
        contact_count < 1 || contact_count > 2 ||
        (contact_count == 2 && gesture != GT_TOUCH_PRESSANDTAP) ||
        (contact_count == 1 && gesture == GT_TOUCH_PRESSANDTAP))
    {
        return E_INVALIDARG;
    }

    return S_OK;
}

/**********************************************************************
 *           DwmTetherContact         (DWMAPI.@)
 */
HRESULT WINAPI DwmTetherContact(DWORD pointer_id, BOOL enable,
                                POINT tether_point)
{
    UNREFERENCED_PARAMETER(pointer_id);
    UNREFERENCED_PARAMETER(enable);
    UNREFERENCED_PARAMETER(tether_point);
    return S_OK;
}

/**********************************************************************
 *           DwmTransitionOwnedWindow         (DWMAPI.@)
 */
HRESULT WINAPI DwmTransitionOwnedWindow(
    HWND hwnd, enum DWMTRANSITION_OWNEDWINDOW_TARGET target)
{
    UNREFERENCED_PARAMETER(target);

    if (!IsWindow(hwnd))
        return E_INVALIDARG;
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
    return S_OK;
}
