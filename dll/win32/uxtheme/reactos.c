/* ReactOS native integration retained around the Wine uxtheme engine. */

#include "uxthemep.h"

#include <uxundoc.h>

HINSTANCE hDllInst;
ATOM atWndContext;
DWORD gdwErrorInfoTlsIndex = TLS_OUT_OF_INDEXES;

static BOOL CALLBACK send_theme_changed(HWND hwnd, LPARAM enable)
{
    SendMessageW(hwnd, WM_THEMECHANGED, enable, 0);
    return TRUE;
}

BOOL CALLBACK UXTHEME_broadcast_theme_changed(HWND hwnd, LPARAM enable)
{
    if (!hwnd)
        EnumWindows(UXTHEME_broadcast_theme_changed, enable);
    else
    {
        send_theme_changed(hwnd, enable);
        EnumChildWindows(hwnd, send_theme_changed, enable);
    }
    return TRUE;
}

HTHEME WINAPI OpenThemeDataFromFile(HTHEMEFILE theme_file, HWND hwnd,
                                    const WCHAR *class_list, DWORD flags)
{
    PTHEME_CLASS theme;
    UINT dpi = GetDpiForWindow(hwnd);

    if (!theme_file || !class_list)
    {
        SetLastError(E_POINTER);
        return NULL;
    }
    if (!dpi)
        dpi = GetDpiForSystem();
    if (flags)
        FIXME("unhandled flags: %#lx\n", flags);

    theme = MSSTYLES_OpenThemeClassFromFile(theme_file, NULL, class_list, dpi);
    SetLastError(theme ? ERROR_SUCCESS : E_PROP_ID_UNSUPPORTED);
    return theme;
}

HRESULT WINAPI SetSystemVisualStyle(const WCHAR *style_file, const WCHAR *color,
                                    const WCHAR *size, UINT flags)
{
    PTHEME_FILE theme_file = NULL;
    HRESULT hr;

    if (style_file)
    {
        if (!g_bThemeHooksActive)
            return E_FAIL;
        if (color && !*color)
            color = NULL;
        if (size && !*size)
            size = NULL;
        if (FAILED(hr = MSSTYLES_OpenThemeFile(style_file, color, size, &theme_file)))
            return hr;
    }

    hr = ApplyTheme(theme_file, flags, NULL);
    if (theme_file)
        MSSTYLES_CloseThemeFile(theme_file);
    return hr;
}
