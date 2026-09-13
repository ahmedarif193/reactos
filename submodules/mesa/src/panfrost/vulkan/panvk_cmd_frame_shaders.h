/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_FB_PRELOAD_H
#define PANVK_FB_PRELOAD_H

#include "panvk_cmd_buffer.h"
#include "pan_desc.h"
#include "pan_fb.h"

VkResult
panvk_per_arch(cmd_get_frame_shaders)(struct panvk_cmd_buffer *cmdbuf,
                                      const struct pan_fb_layout *fb,
                                      const struct pan_fb_load *load,
                                      const struct pan_fb_resolve *resolve,
                                      struct pan_fb_frame_shaders *fs_out);

#endif
