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
CreatePseudoConsole(
    _In_ COORD Size,
    _In_ HANDLE Input,
    _In_ HANDLE Output,
    _In_ DWORD Flags,
    _Out_ HPCON *PseudoConsole)
{
    if (!PseudoConsole)
        return E_INVALIDARG;

    *PseudoConsole = NULL;

    if (Size.X <= 0 || Size.Y <= 0 ||
        !Input || Input == INVALID_HANDLE_VALUE ||
        !Output || Output == INVALID_HANDLE_VALUE ||
        (Flags & ~PSEUDOCONSOLE_INHERIT_CURSOR))
    {
        return E_INVALIDARG;
    }

    /* ReactOS does not yet provide a compatible pseudo-console host. */
    return E_NOTIMPL;
}

VOID
WINAPI
ClosePseudoConsole(
    _In_ HPCON PseudoConsole)
{
    /* CreatePseudoConsole cannot currently return an owned object. */
    UNREFERENCED_PARAMETER(PseudoConsole);
}

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
