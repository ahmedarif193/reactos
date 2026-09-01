/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DXGI shared user-mode display driver declarations
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * This is the kernel-safe subset of the public DXGI DDI contract needed by
 * display miniports when exchanging primary allocation data with a UMD.
 */

#pragma once

#include <d3dukmdt.h>
#include <dxgicommon.h>
#include <dxgiformat.h>

typedef struct DXGI_DDI_RATIONAL
{
    UINT Numerator;
    UINT Denominator;
} DXGI_DDI_RATIONAL;

typedef enum DXGI_DDI_MODE_SCANLINE_ORDER
{
    DXGI_DDI_MODE_SCANLINE_ORDER_UNSPECIFIED = 0,
    DXGI_DDI_MODE_SCANLINE_ORDER_PROGRESSIVE = 1,
    DXGI_DDI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST = 2,
    DXGI_DDI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST = 3,
} DXGI_DDI_MODE_SCANLINE_ORDER;

typedef enum DXGI_DDI_MODE_SCALING
{
    DXGI_DDI_MODE_SCALING_UNSPECIFIED = 0,
    DXGI_DDI_MODE_SCALING_STRETCHED = 1,
    DXGI_DDI_MODE_SCALING_CENTERED = 2,
} DXGI_DDI_MODE_SCALING;

typedef enum DXGI_DDI_MODE_ROTATION
{
    DXGI_DDI_MODE_ROTATION_UNSPECIFIED = 0,
    DXGI_DDI_MODE_ROTATION_IDENTITY = 1,
    DXGI_DDI_MODE_ROTATION_ROTATE90 = 2,
    DXGI_DDI_MODE_ROTATION_ROTATE180 = 3,
    DXGI_DDI_MODE_ROTATION_ROTATE270 = 4,
} DXGI_DDI_MODE_ROTATION;

typedef struct DXGI_DDI_MODE_DESC
{
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    DXGI_DDI_RATIONAL RefreshRate;
    DXGI_DDI_MODE_SCANLINE_ORDER ScanlineOrdering;
    DXGI_DDI_MODE_ROTATION Rotation;
    DXGI_DDI_MODE_SCALING Scaling;
} DXGI_DDI_MODE_DESC;

typedef struct DXGI_DDI_PRIMARY_DESC
{
    UINT Flags;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    DXGI_DDI_MODE_DESC ModeDesc;
    UINT DriverFlags;
} DXGI_DDI_PRIMARY_DESC;
