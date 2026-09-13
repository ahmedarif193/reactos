/*
 * Copyright © 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef IRIS_FENCE_H
#define IRIS_FENCE_H

#include "util/u_inlines.h"

struct pipe_screen;
struct iris_batch;
struct iris_bufmgr;

/**
 * A refcounted DRM Sync Object (drm_syncobj).
 */
struct iris_syncobj {
   struct pipe_reference ref;
   uint32_t handle;
};

struct iris_syncobj *iris_create_syncobj(struct iris_bufmgr *bufmgr);
void iris_syncobj_destroy(struct iris_bufmgr *, struct iris_syncobj *);
void iris_syncobj_signal(struct iris_bufmgr *, struct iris_syncobj *);

void iris_batch_add_syncobj(struct iris_batch *batch,
                            struct iris_syncobj *syncobj,
                            uint32_t flags);
bool iris_wait_syncobj(struct iris_bufmgr *bufmgr,
                       struct iris_syncobj *syncobj,
                       int64_t timeout_nsec);

static inline void
iris_syncobj_reference(struct iris_bufmgr *bufmgr,
                       struct iris_syncobj **dst,
                       struct iris_syncobj *src)
{
   if (pipe_reference(*dst ? &(*dst)->ref : NULL,
                      src ? &src->ref : NULL))
      iris_syncobj_destroy(bufmgr, *dst);

   *dst = src;
}

/* ------------------------------------------------------------------- */

void iris_init_context_fence_functions(struct pipe_context *ctx);
void iris_init_screen_fence_functions(struct pipe_screen *screen);

#endif
