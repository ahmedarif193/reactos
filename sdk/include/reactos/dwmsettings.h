/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared names for ReactOS DWM user preferences
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <uxtheme.h>
#include <vssym32.h>
#include <reactos/dwmframe.h>

#define REACTOS_DWM_SETTINGS_KEY L"Software\\ReactOS\\DWM"

#define REACTOS_DWM_COLOR_SCHEME L"ColorScheme"

#define DWM_COLOR_SCHEME_LIGHT 0u
#define DWM_COLOR_SCHEME_DARK  1u

#define DWM_COLOR_SCHEME_MATERIAL(Scheme) \
    ((Scheme) == DWM_COLOR_SCHEME_LIGHT ? L"ContentLight::Liquid" : L"ContentDark::Liquid")

static __inline DWORD
DwmSettingsReadColorScheme(void)
{
    HKEY Key;
    DWORD Scheme, Type, Size = sizeof(Scheme);
    LONG Error;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REACTOS_DWM_SETTINGS_KEY, 0,
                      KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return DWM_COLOR_SCHEME_DARK;
    Error = RegQueryValueExW(Key, REACTOS_DWM_COLOR_SCHEME, NULL, &Type,
                             (BYTE *)&Scheme, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS || Type != REG_DWORD ||
        Size != sizeof(Scheme) || Scheme > DWM_COLOR_SCHEME_DARK)
        return DWM_COLOR_SCHEME_DARK;
    return Scheme;
}

static __inline BOOL
DwmSettingsLoadSchemeMaterial(DWORD Scheme, COLORREF *Fill, INT *Opacity)
{
    HTHEME Theme = OpenThemeData(NULL, DWM_COLOR_SCHEME_MATERIAL(Scheme));
    BOOL Composited = FALSE;
    BOOL Loaded;

    if (Theme == NULL)
        return FALSE;
    Loaded = SUCCEEDED(GetThemeBool(Theme, 0, 0, TMT_COMPOSITED, &Composited)) &&
             Composited &&
             SUCCEEDED(GetThemeInt(Theme, 0, 0, TMT_OPACITY, Opacity)) &&
             *Opacity >= 0 && *Opacity <= 255 &&
             SUCCEEDED(GetThemeColor(Theme, 0, 0, TMT_FILLCOLOR, Fill));
    CloseThemeData(Theme);
    return Loaded;
}

static __inline BOOL
DwmSettingsSetContentBackdrop(HWND Window, COLORREF Fill, INT Opacity)
{
    if (SetPropW(Window, DWM_PROP_SYSTEM_BACKDROP_TYPE,
                 (HANDLE)(ULONG_PTR)DWM_BACKDROP_TRANSIENT) &&
        SetPropW(Window, DWM_PROP_BACKDROP_OPACITY, (HANDLE)(ULONG_PTR)(Opacity + 1)) &&
        SetPropW(Window, DWM_PROP_BACKDROP_COLOR, (HANDLE)(ULONG_PTR)((ULONG)Fill + 1)) &&
        SetPropW(Window, DWM_PROP_BACKDROP_COLORIZATION,
                 (HANDLE)(ULONG_PTR)((ULONG)Fill + 1)) &&
        SetPropW(Window, DWM_PROP_BACKDROP_REGION,
                 (HANDLE)(ULONG_PTR)DWM_BACKDROP_REGION_WINDOW) &&
        SetPropW(Window, DWM_PROP_CONTENT_BACKDROP, (HANDLE)1))
    {
        return TRUE;
    }
    RemovePropW(Window, DWM_PROP_CONTENT_BACKDROP);
    SetPropW(Window, DWM_PROP_BACKDROP_REGION,
             (HANDLE)(ULONG_PTR)DWM_BACKDROP_REGION_NONCLIENT);
    return FALSE;
}

static __inline void
DwmSettingsClearContentBackdrop(HWND Window)
{
    RemovePropW(Window, DWM_PROP_CONTENT_BACKDROP);
    RemovePropW(Window, DWM_PROP_BACKDROP_REGION);
    RemovePropW(Window, DWM_PROP_BACKDROP_COLORIZATION);
    RemovePropW(Window, DWM_PROP_BACKDROP_COLOR);
    RemovePropW(Window, DWM_PROP_BACKDROP_OPACITY);
    RemovePropW(Window, DWM_PROP_SYSTEM_BACKDROP_TYPE);
}
