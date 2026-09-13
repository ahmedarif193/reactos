/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_PUBLIC_H
#define PAN_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

struct pipe_screen;
struct pipe_screen_config;
struct renderonly;

struct pipe_screen *
panfrost_create_screen(int fd, const struct pipe_screen_config *config,
                       struct renderonly *ro);

#ifdef __cplusplus
}
#endif

#endif
