/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Private compositor output presentation with overlay planes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <d3d11.h>

#define DWM_MAX_OVERLAY_PLANES 2

/* A client surface the display scans out above the compositor output,
 * unscaled: Source and Destination have the same size. */
typedef struct _DWM_OVERLAY_PLANE
{
    ID3D11Texture2D *Texture;
    RECT Source;
    RECT Destination;
} DWM_OVERLAY_PLANE;

/*
 * Queried from the compositor's own output swap chain. PresentWithOverlays
 * is Present1 with the planes shown above the presented buffer, bottom-up,
 * until the next present. It fails without presenting when the display
 * cannot show that configuration.
 */
struct IDwmOverlaySwapChain : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE PresentWithOverlays(
        UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS *Parameters,
        UINT PlaneCount, const DWM_OVERLAY_PLANE *Planes) = 0;
};

static const IID IID_IDwmOverlaySwapChain =
    {0x6f1c3a52, 0x9d4e, 0x4b7a, {0x8e, 0x2f, 0x5a, 0x0d, 0x9c, 0x3b, 0x7e, 0x14}};
