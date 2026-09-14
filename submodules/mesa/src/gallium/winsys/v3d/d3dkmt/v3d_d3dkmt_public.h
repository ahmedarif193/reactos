/*
 * Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef V3D_D3DKMT_PUBLIC_H
#define V3D_D3DKMT_PUBLIC_H

#include <stdbool.h>
#include <stdint.h>

struct pipe_screen;
struct pipe_context;
struct pipe_resource;
struct pipe_screen_config;
struct pipe_fence_handle;

struct pipe_screen *
v3d_d3dkmt_screen_create(const struct pipe_screen_config *config);

bool
v3d_d3dkmt_present_frontbuffer(struct pipe_screen *screen,
                                struct pipe_context *ctx,
                                struct pipe_resource *resource,
                                unsigned level, unsigned layer, void *hdc);

bool
v3d_d3dkmt_fence_signal_event(struct pipe_screen *screen,
                              struct pipe_fence_handle *fence,
                              void *event);

bool
v3d_d3dkmt_shared_surface_info(struct pipe_screen *screen,
                               uintptr_t shared_handle,
                               uint32_t *width,
                               uint32_t *height,
                               uint32_t *pitch);

#endif /* V3D_D3DKMT_PUBLIC_H */
