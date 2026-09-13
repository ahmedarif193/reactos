/*
 * Copyright © 2026 Imagination Technologies Ltd.
 * SPDX-License-Identifier: MIT
 */
#ifndef PVR_YCBCR_H
#define PVR_YCBCR_H

#include <stdint.h>

void pvr_setup_static_yuv_csc_table(uint8_t *const heap_ptr,
                                    uint64_t yuv_table_offset_in_bytes);

#define PVR_YCBCR_SLOT_RGB_IDENTITY 0
#define PVR_YCBCR_SLOT_YCBCR_IDENTITY_FULL 1
#define PVR_YCBCR_SLOT_YCBCR_IDENTITY 2
#define PVR_YCBCR_SLOT_YCBCR_709_FULL 3
#define PVR_YCBCR_SLOT_YCBCR_709 4
#define PVR_YCBCR_SLOT_YCBCR_601_FULL 5
#define PVR_YCBCR_SLOT_YCBCR_601 6
#define PVR_YCBCR_SLOT_YCBCR_2020_FULL 7
#define PVR_YCBCR_SLOT_YCBCR_2020 8

#endif /* PVR_YCBCR_H */
