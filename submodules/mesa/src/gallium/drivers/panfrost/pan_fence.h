/*
 * Copyright (c) 2022 Amazon.com, Inc. or its affiliates.
 * Copyright 2018-2019 Alyssa Rosenzweig
 * Copyright 2018-2019 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pipe/p_state.h"

struct panfrost_context;

struct pipe_fence_handle {
   struct pipe_reference reference;
   uint32_t syncobj;
   bool signaled;
};

void panfrost_fence_reference(struct pipe_screen *pscreen,
                              struct pipe_fence_handle **ptr,
                              struct pipe_fence_handle *fence);

bool panfrost_fence_finish(struct pipe_screen *pscreen,
                           struct pipe_context *ctx,
                           struct pipe_fence_handle *fence, uint64_t timeout);

int panfrost_fence_get_fd(struct pipe_screen *screen,
                          struct pipe_fence_handle *f);

struct pipe_fence_handle *panfrost_fence_from_fd(struct panfrost_context *ctx,
                                                 int fd,
                                                 enum pipe_fd_type type);

struct pipe_fence_handle *panfrost_fence_create(struct panfrost_context *ctx);
