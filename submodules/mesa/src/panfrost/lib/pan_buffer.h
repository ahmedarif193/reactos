/*
 * Copyright (C) 2026 Arm Ltd.
 *
 * Derived from pan_texture.h which is:
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * Copyright (C) 2025 Arm Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_BUFFER_H
#define __PAN_BUFFER_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "genxml/gen_macros.h"

struct pan_buffer_view;
struct mali_buffer_packed;
struct mali_attribute_buffer_packed;
struct mali_attribute_packed;
struct mali_texture_packed;
struct pan_ptr;

#if PAN_ARCH >= 9
void GENX(pan_buffer_texture_emit)(const struct pan_buffer_view *bview,
                                   struct mali_buffer_packed *out);
#elif PAN_ARCH >= 6
void GENX(pan_buffer_texture_emit)(const struct pan_buffer_view *bview,
                                   struct mali_attribute_buffer_packed *out_buf,
                                   struct mali_attribute_packed *out_attrib);
#else
void GENX(pan_buffer_texture_emit)(const struct pan_buffer_view *bview,
                                   struct mali_texture_packed *out,
                                   const struct pan_ptr *payload);
#endif

#endif
