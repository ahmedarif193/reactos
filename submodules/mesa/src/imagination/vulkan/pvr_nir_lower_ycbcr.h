/*
 * Copyright © 2026 Imagination Technologies Ltd.
 * SPDX-License-Identifier: MIT
 */
#ifndef PVR_NIR_LOWER_YCBCR_H
#define PVR_NIR_LOWER_YCBCR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct nir_shader nir_shader;

typedef const struct vk_ycbcr_conversion_state *(
   *nir_pvr_ycbcr_conversion_lookup_cb)(const void *data,
                                        uint32_t set,
                                        uint32_t binding,
                                        uint32_t array_index);
bool nir_pvr_lower_ycbcr_tex(nir_shader *nir,
                             nir_pvr_ycbcr_conversion_lookup_cb cb,
                             const void *cb_data);

#endif /* PVR_NIR_LOWER_YCBCR_H */
