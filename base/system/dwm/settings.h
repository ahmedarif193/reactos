/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private live settings contract with the desktop settings panel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <windows.h>

#define DWM_SETTINGS_KEY L"Software\\ReactOS\\DWM"
#define DWM_SETTINGS_CLASS L"ReactOS.Dwm.Settings"
#define DWM_SETTINGS_QUERY (WM_USER + 1)

#define DWM_EFFECT_ANIMATIONS 0x01u
#define DWM_EFFECT_SHADOWS    0x02u
#define DWM_EFFECT_CORNERS    0x04u
#define DWM_EFFECT_BLUR       0x08u
#define DWM_EFFECT_ACRYLIC    0x10u
#define DWM_EFFECT_ALL        0x1fu

/* Scalar replies from our message-only window, not a Windows DWM ABI.
 * WM_SETTINGCHANGE reloads preferences; QUERY only reports current state. */
#define DWM_SETTINGS_REPLY   0x44570000u
#define DWM_SETTINGS_GPU     0x00000100u
#define DWM_SETTINGS_ERROR   0x00000200u

typedef struct _DWM_SETTINGS
{
    DWORD Effects;
    BOOL Changed;
    BOOL (*GpuActive)(void);
} DWM_SETTINGS;

struct _DWM_WIN;

LONG DwmSettingsRead(DWORD *Effects);
LONG DwmSettingsWrite(DWORD Effect, BOOL Enabled);
HWND DwmSettingsCreateWindow(HINSTANCE Instance, DWM_SETTINGS *Settings);
void DwmSettingsApplyWindow(const DWM_SETTINGS *Settings, struct _DWM_WIN *Window);
