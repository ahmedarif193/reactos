/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Public D3D12 user-mode DDI declarations used by kernel drivers
 */

#pragma once

#include <d3d10umddi.h>

typedef enum D3D12DDI_TEXTURE_LAYOUT
{
    D3D12DDI_TL_UNDEFINED = 0,
    D3D12DDI_TL_ROW_MAJOR = 1,
    D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE = 2,
    D3D12DDI_TL_64KB_TILE_STANDARD_SWIZZLE = 3,
    D3D12DDI_TL_DEVICE_DEPENDENT_SWIZZLE_0 = 0x100,
} D3D12DDI_TEXTURE_LAYOUT;
