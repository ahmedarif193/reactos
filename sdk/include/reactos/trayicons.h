/*
 * PROJECT:     ReactOS shell
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Shared notification area icon metric
 */

#pragma once

#include <wingdi.h>

#define SHELL_TRAY_ICON_BASE 22

static inline INT
ShellTrayIconSize(VOID)
{
    INT nDpi = 0;
    HDC hdc = GetDC(NULL);

    if (hdc)
    {
        nDpi = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(NULL, hdc);
    }

    if (nDpi <= 0)
        nDpi = 96;

    return MulDiv(SHELL_TRAY_ICON_BASE, nDpi, 96);
}
