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
 * L"\\\\.\\DISPLAY1".  Unlike most D3DKMT entry points this is NOT a thin
 * syscall: the kernel has no notion of a GDI display name, so the runtime
 * resolves the name to a per-monitor device context (HDC) and then defers to
 * D3DKMTOpenAdapterFromHdc, which the kernel maps to the started dxgkrnl
 * adapter.  This mirrors the Windows gdi32 implementation.
 *
 * On success the adapter handle, LUID and VidPN source id are copied out.
 * The HDC is always released before returning.
 */
NTSTATUS
WINAPI
D3DKMTOpenAdapterFromGdiDisplayName(_Inout_ D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME* unnamedParam1)
{
    D3DKMT_OPENADAPTERFROMHDC OpenFromHdc;
    HDC hDc;
    NTSTATUS Status;

    if (unnamedParam1 == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * Build a DC that maps to the requested GDI display.  The documented form
     * is CreateDC(NULL, "\\\\.\\DISPLAYn", ...).  win32k resolves the name
     * against the registered graphics devices (EngpGetPDEV) and returns NULL
     * for a name that does not exist, so a bogus name such as "\\.\DISPLAY999"
     * fails here — exactly the refusal D3DKMTOpenAdapterFromGdiDisplayName must
     * give.  Do NOT fall back to the primary display: that would make every
     * non-existent display name succeed.
     */
    hDc = CreateDCW(NULL, unnamedParam1->DeviceName, NULL, NULL);
    if (hDc == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&OpenFromHdc, sizeof(OpenFromHdc));
    OpenFromHdc.hDc = hDc;

    /*
     * Call the already-wired NtGdiDdDDIOpenAdapterFromHdc syscall directly
     * (this is the kernel entry point behind the gdi32 D3DKMTOpenAdapterFromHdc
     * export).  dxgkrnl returns the started adapter for any valid display DC.
     */
    Status = NtGdiDdDDIOpenAdapterFromHdc(&OpenFromHdc);

    DeleteDC(hDc);

    if (NT_SUCCESS(Status))
    {
        unnamedParam1->hAdapter = OpenFromHdc.hAdapter;
        unnamedParam1->AdapterLuid = OpenFromHdc.AdapterLuid;
        unnamedParam1->VidPnSourceId = OpenFromHdc.VidPnSourceId;
    }

    return Status;
}
