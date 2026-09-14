/*
 * Copyright 2026 Ahmed Arif <arif193@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef VC4_D3DKMT_PUBLIC_H
#define VC4_D3DKMT_PUBLIC_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>
#include "dwmpresenttracecore.h"

extern DPT_BANK vc4_present_trace;
BOOL vc4_d3dkmt_trace_control(const void *request, void *output, ULONG bytes);

struct pipe_screen;
struct pipe_screen_config;
struct pipe_context;
struct pipe_blit_info;
struct pipe_resource;
struct pipe_box;

bool vc4_render_blit_for_present(struct pipe_context *context,
                                const struct pipe_blit_info *info);
bool vc4_d3dkmt_present(struct pipe_screen *screen,
                        struct pipe_context *context,
                        struct pipe_resource *resource,
                        unsigned level, unsigned layer, void *hdc,
                        unsigned nboxes, struct pipe_box *subbox);

struct pipe_screen *
vc4_d3dkmt_screen_create(const struct pipe_screen_config *config);

int vc4_d3dkmt_ioctl(int fd, unsigned long request, void *arg);
void *vc4_d3dkmt_bo_map(int fd, uint32_t handle);
void vc4_d3dkmt_bo_mark_cpu_dirty(int fd, uint32_t handle);
bool vc4_d3dkmt_primary_info(int fd, uintptr_t *global_share,
                             uint32_t *width, uint32_t *height,
                             uint32_t *pitch);
bool vc4_d3dkmt_present_primary(int fd, uint32_t primary_handle,
                                HWND window, const RECT *dirty_rect);
void vc4_d3dkmt_close(int fd);

#endif /* VC4_D3DKMT_PUBLIC_H */
