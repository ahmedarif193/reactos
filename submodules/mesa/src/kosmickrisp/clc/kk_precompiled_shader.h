/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_PRECOMPILED_SHADER_H
#define KK_PRECOMPILED_SHADER_H 1

#include "kosmickrisp/bridge/mtl_types.h"

#include <stdint.h>

struct kk_precompiled_info {
   uint16_t workgroup_size[3];
};

struct kk_precompiled_shader {
   struct kk_precompiled_info info;
   mtl_compute_pipeline_state *pipeline;
};

#endif /* KK_PRECOMPILED_SHADER_H_ */
