/*
 * Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef V3D_D3DKMT_PUBLIC_H
#define V3D_D3DKMT_PUBLIC_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

struct _DPT_BANK;
extern struct _DPT_BANK v3d_present_trace;
BOOL v3d_d3dkmt_trace_control(const void *request, void *output, ULONG bytes);

struct pipe_screen;
struct pipe_context;
struct pipe_resource;
struct pipe_screen_config;
struct pipe_fence_handle;

struct pipe_screen *
v3d_d3dkmt_screen_create(const struct pipe_screen_config *config);

struct pipe_screen *
v3d_d3dkmt_screen_create_umd(const struct pipe_screen_config *config,
                             void *adapter, void *device,
                             const void *callbacks);

bool
v3d_d3dkmt_runtime_resource_begin(struct pipe_screen *screen,
                                  void *runtime_resource,
                                  const void *resource_private_data,
                                  uint32_t resource_private_data_size);

void
v3d_d3dkmt_runtime_resource_end(struct pipe_screen *screen);

uint32_t
v3d_d3dkmt_open_runtime_resource(struct pipe_screen *screen,
                                 void *runtime_resource,
                                 uint32_t allocation,
                                 uint32_t size);

void
v3d_d3dkmt_discard_runtime_resource(struct pipe_screen *screen,
                                    uint32_t handle);

uint32_t
v3d_d3dkmt_resource_allocation(struct pipe_screen *screen,
                               struct pipe_resource *resource);

bool
v3d_d3dkmt_rebind_runtime_resources(
   struct pipe_screen *screen,
   struct pipe_resource *const *resources,
   void *const *runtime_resources,
   unsigned count);

void *
v3d_d3dkmt_present_context(struct pipe_screen *screen);

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
