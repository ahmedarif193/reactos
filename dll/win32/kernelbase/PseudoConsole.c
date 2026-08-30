/*
 * PROJECT:     ReactOS KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pseudo-console compatibility entry points
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <wincon.h>

HRESULT
WINAPI
ResizePseudoConsole(
    _In_ HPCON PseudoConsole,
    _In_ COORD Size)
{
    if (!PseudoConsole || Size.X < 0 || Size.Y < 0)
        return E_INVALIDARG;

    /* ReactOS does not yet expose a compatible pseudo-console creator. */
    return E_NOTIMPL;
}
