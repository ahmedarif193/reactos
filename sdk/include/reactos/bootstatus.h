/*
 * PROJECT:     ReactOS Modern Boot Status
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared boot-status surface interface
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#pragma once

#include <windef.h>

#ifdef __cplusplus
extern "C" {
#endif

HWND
WINAPI
BootStatusFind(VOID);

HWND
WINAPI
BootStatusCreate(
    _In_opt_ PCWSTR StatusText);

BOOL
WINAPI
BootStatusUpdate(
    _In_opt_ HWND Window,
    _In_opt_ PCWSTR PhaseText,
    _In_opt_ PCWSTR DetailText,
    _In_ ULONG Completed,
    _In_ ULONG Total);

BOOL
WINAPI
BootStatusSetText(
    _In_opt_ HWND Window,
    _In_opt_ PCWSTR StatusText);

VOID
WINAPI
BootStatusDestroy(
    _In_opt_ HWND Window);

#ifdef __cplusplus
}
#endif
