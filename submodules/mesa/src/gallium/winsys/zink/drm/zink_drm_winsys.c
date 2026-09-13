/*
 * Copyright © 2023 Imagination Technologies Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/os_file.h"
#include "util/u_screen.h"

#include "zink_drm_public.h"
#include "zink/zink_public.h"

struct pipe_screen *
zink_drm_create_screen_renderonly(int fd,
                                  struct renderonly *ro,
                                  const struct pipe_screen_config *config)
{
   /* The fd is dup'ed in zink_drm_create_screen() */
   return u_pipe_screen_lookup_or_create(fd, config, ro, zink_drm_create_screen);
}
