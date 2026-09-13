/*
 * Copyright © 2023 Imagination Technologies Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __ZINK_DRM_PUBLIC_H__
#define __ZINK_DRM_PUBLIC_H__

struct pipe_screen_config;
struct pipe_screen;
struct renderonly;

struct pipe_screen *
zink_drm_create_screen_renderonly(int fd,
                                  struct renderonly *ro,
                                  const struct pipe_screen_config *config);

#endif
