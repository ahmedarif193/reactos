/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared glass material tuning
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

/* The software material uses three box passes at half resolution, whereas
 * the GPU material uses a full-resolution Gaussian. These radii are chosen
 * as a perceptual pair rather than as numerically identical kernels. */
#define DWM_MATERIAL_BLUR_RADIUS_96       6
#define DWM_GPU_MATERIAL_BLUR_RADIUS      20

/* Keep the dither below the point where display scaling turns it into grit. */
#define DWM_MATERIAL_SATURATION           20
#define DWM_MATERIAL_NOISE                1

#define DWM_MATERIAL_REFLECT_STRENGTH     48u
#define DWM_MATERIAL_EDGE_LIGHT           32u
