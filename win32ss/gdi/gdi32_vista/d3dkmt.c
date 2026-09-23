/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3DKMT dxgkrnl syscalls
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

#include <gdi32_vista.h>
/* User-mode D3DKMT thunk: use the user-mode runtime header (d3dkmthk.h), not
 * the kernel miniport DDI (d3dkmddi.h), which pulls MDL and other WDM-only
 * types that are undefined in this user-mode component. */
#include <d3dkmthk.h>

/*
 * D3DKMTOpenAdapterFromGdiDisplayName
 *
 * Open a WDDM adapter instance from a GDI display device name such as
 * L"\\\\.\\DISPLAY1". win32k resolves the name to its registered PDEV and
 * preserves that device's identity. Going through the legacy HDC thunk can
 * instead select a paired renderer, which need not own this display.
 */
NTSTATUS
WINAPI
D3DKMTOpenAdapterFromGdiDisplayName(_Inout_ D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME* unnamedParam1)
{
    if (unnamedParam1 == NULL)
        return STATUS_INVALID_PARAMETER;
    return NtGdiDdDDIOpenAdapterFromGdiDisplayName(unnamedParam1);
}
