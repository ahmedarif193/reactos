/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef VK_BLEND_H
#define VK_BLEND_H

#include <stdbool.h>
#include "util/blend.h"
#include "vulkan/vulkan_core.h"

#ifdef __cplusplus
extern "C" {
#endif

enum pipe_logicop vk_logic_op_to_pipe(VkLogicOp in);
enum pipe_blend_func vk_blend_op_to_pipe(VkBlendOp in);
enum pipe_blendfactor vk_blend_factor_to_pipe(VkBlendFactor in);
enum pipe_advanced_blend_mode vk_advanced_blend_op_to_pipe(VkBlendOp in);
enum pipe_blend_overlap_mode vk_blend_overlap_to_pipe(VkBlendOverlapEXT in);

#ifdef __cplusplus
}
#endif

#endif
