/*
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/macros.h"
#include "dev/intel_device_info.h"

enum gfx_ver {
   GFX4    = (1 << 0),
   GFX45   = (1 << 1),
   GFX5    = (1 << 2),
   GFX6    = (1 << 3),
   GFX7    = (1 << 4),
   GFX75   = (1 << 5),
   GFX8    = (1 << 6),
   GFX9    = (1 << 7),
   GFX10   = (1 << 8),
   GFX11   = (1 << 9),
   GFX12   = (1 << 10),
   GFX125  = (1 << 11),
   XE2     = (1 << 12),
   XE3     = (1 << 13),
   GFX_ALL = ~0
};

#define GFX_LT(ver) ((ver) - 1)
#define GFX_GE(ver) (~GFX_LT(ver))
#define GFX_LE(ver) (GFX_LT(ver) | (ver))

static inline enum gfx_ver
gfx_ver_from_devinfo(const struct intel_device_info *devinfo)
{
   switch (devinfo->verx10) {
   case 40: return GFX4;
   case 45: return GFX45;
   case 50: return GFX5;
   case 60: return GFX6;
   case 70: return GFX7;
   case 75: return GFX75;
   case 80: return GFX8;
   case 90: return GFX9;
   case 110: return GFX11;
   case 120: return GFX12;
   case 125: return GFX125;
   case 200: return XE2;
   case 300: return XE3;
   default:
      UNREACHABLE("not reached");
   }
}
