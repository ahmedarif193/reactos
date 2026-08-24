/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private DXGI and DirectComposition integration contract
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#pragma once

#include <guiddef.h>
#include <windef.h>

struct reactos_dxgi_composition_target
{
    HWND window;
    LONG offset_x;
    LONG offset_y;
    RECT clip;
    BOOL has_clip;
};

static const GUID GUID_ReactOSDXGICompositionWindow =
{
    0x35aa6a08, 0x6f14, 0x4d67, {0x90, 0xb0, 0x39, 0xf0, 0x13, 0x14, 0xba, 0x7c}
};
