/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Persistent GPU effect preferences and compositor-thread reload
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "settings.h"
#include <reactos/dwmframe.h>

static const struct
{
    DWORD Effect;
    const WCHAR *Name;
} DwmPreferences[] =
{
    {DWM_EFFECT_SHADOWS, L"EnableShadows"},
    {DWM_EFFECT_BLUR, L"EnableBlur"},
    {DWM_EFFECT_ACRYLIC, L"EnableAcrylic"},
    {DWM_EFFECT_CONTENT_BLUR, L"EnableContentBlur"}
};

static LONG
DwmSettingsWriteValue(const WCHAR *Name, DWORD Value)
{
    HKEY Key;
    LONG Error;

    Error = RegCreateKeyExW(HKEY_CURRENT_USER, DWM_SETTINGS_KEY, 0, NULL, 0,
                            KEY_SET_VALUE, NULL, &Key, NULL);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = RegSetValueExW(Key, Name, 0, REG_DWORD,
                           (const BYTE *)&Value, sizeof(Value));
    RegCloseKey(Key);
    return Error;
}

LONG
DwmSettingsRead(DWORD *Effects)
{
    ANIMATIONINFO Animation = {sizeof(Animation)};
    HKEY Key;
    LONG Error;
    UINT Index;

    *Effects = DWM_EFFECT_ALL;
    if (!SystemParametersInfoW(SPI_GETANIMATION, sizeof(Animation), &Animation, 0))
        return ERROR_READ_FAULT;
    if (!Animation.iMinAnimate)
        *Effects &= ~DWM_EFFECT_ANIMATIONS;

    Error = RegOpenKeyExW(HKEY_CURRENT_USER, DWM_SETTINGS_KEY, 0, KEY_QUERY_VALUE, &Key);
    if (Error == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;
    if (Error != ERROR_SUCCESS)
        return Error;

    for (Index = 0; Index < ARRAYSIZE(DwmPreferences); ++Index)
    {
        DWORD Value, Type, Size = sizeof(Value);
        Error = RegQueryValueExW(Key, DwmPreferences[Index].Name, NULL, &Type, (BYTE *)&Value, &Size);
        if (Error == ERROR_FILE_NOT_FOUND)
            continue;
        if (Error != ERROR_SUCCESS)
            break;
        if (Type != REG_DWORD || Size != sizeof(Value))
        {
            Error = ERROR_DATATYPE_MISMATCH;
            break;
        }
        if (Value == 0)
            *Effects &= ~DwmPreferences[Index].Effect;
    }
    RegCloseKey(Key);
    return Error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : Error;
}

LONG
DwmSettingsWrite(DWORD Effect, BOOL Enabled)
{
    LONG Error;
    UINT Index;

    if (Effect == DWM_EFFECT_ANIMATIONS)
    {
        ANIMATIONINFO Animation = {sizeof(Animation), !!Enabled};
        if (!SystemParametersInfoW(SPI_SETANIMATION, sizeof(Animation), &Animation, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        {
            Error = GetLastError();
            return Error != ERROR_SUCCESS ? Error : ERROR_WRITE_FAULT;
        }
        return ERROR_SUCCESS;
    }

    for (Index = 0; Index < ARRAYSIZE(DwmPreferences); ++Index)
    {
        if (DwmPreferences[Index].Effect == Effect)
            break;
    }
    if (Index == ARRAYSIZE(DwmPreferences))
        return ERROR_INVALID_PARAMETER;

    return DwmSettingsWriteValue(DwmPreferences[Index].Name, !!Enabled);
}

LONG
DwmSettingsWriteColorScheme(DWORD Scheme)
{
    if (Scheme > DWM_COLOR_SCHEME_DARK)
        return ERROR_INVALID_PARAMETER;
    return DwmSettingsWriteValue(REACTOS_DWM_COLOR_SCHEME, Scheme);
}

static LRESULT CALLBACK
DwmSettingsWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    DWM_SETTINGS *Settings = (DWM_SETTINGS *)GetWindowLongPtrW(Window, GWLP_USERDATA);
    DWORD Effects;
    LONG Error = ERROR_SUCCESS;

    if (Message == WM_NCCREATE)
    {
        Settings = (DWM_SETTINGS *)((CREATESTRUCTW *)LParam)->lpCreateParams;
        SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)Settings);
        return TRUE;
    }
    if (Settings != NULL && (Message == WM_SETTINGCHANGE || Message == DWM_SETTINGS_QUERY))
    {
        if (Message == WM_SETTINGCHANGE)
        {
            Error = DwmSettingsRead(&Effects);
            if (Error == ERROR_SUCCESS)
            {
                Settings->Effects = Effects;
                Settings->Changed = TRUE;
            }
        }
        return DWM_SETTINGS_REPLY | Settings->Effects |
               (Settings->GpuActive() ? DWM_SETTINGS_GPU : 0) |
               (Error != ERROR_SUCCESS ? DWM_SETTINGS_ERROR : 0);
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

HWND
DwmSettingsCreateWindow(HINSTANCE Instance, DWM_SETTINGS *Settings)
{
    WNDCLASSW Class = {0};

    Class.lpfnWndProc = DwmSettingsWindowProc;
    Class.hInstance = Instance;
    Class.lpszClassName = DWM_SETTINGS_CLASS;
    if (!RegisterClassW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;
    return CreateWindowExW(0, DWM_SETTINGS_CLASS, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, Instance, Settings);
}

void
DwmSettingsApplyWindow(const DWM_SETTINGS *Settings, DWM_WIN *Window)
{
    DWORD Blur = DWM_EFFECT_BLUR;

    if (!(Settings->Effects & DWM_EFFECT_SHADOWS))
        Window->LayerFlags &= ~DWM_WINDOW_NC_SHADOW;
    if (Window->ContentBackdrop &&
        Window->BackdropRegion == DWM_BACKDROP_REGION_WINDOW)
    {
        if (Settings->Effects & DWM_EFFECT_CONTENT_BLUR)
            Blur = DWM_EFFECT_CONTENT_BLUR;
        else
            Window->BackdropRegion = DWM_BACKDROP_REGION_NONCLIENT;
    }
    Window->BlurFlags &= ~DWM_BLUR_DISABLE_FILTER;
    if (!(Settings->Effects & Blur) ||
        !(Settings->Effects & DWM_EFFECT_ACRYLIC))
    {
        /* DWM_BLUR_ENABLE also marks textures with meaningful pixel alpha.
         * Suppress filtering regions without changing that blending rule. */
        Window->BlurFlags &= ~DWM_BLUR_REGION_ENTIRE_WINDOW;
        Window->BlurFlags |= DWM_BLUR_DISABLE_FILTER;
        Window->BlurRectCount = 0;
    }
    if (!(Settings->Effects & DWM_EFFECT_ACRYLIC) && Window->BackdropType == DWM_BACKDROP_TRANSIENT)
    {
        Window->BackdropType = DWM_BACKDROP_NONE;
        Window->BackdropRegion = 0;
        Window->BackdropOpacity = 255;
    }
}
