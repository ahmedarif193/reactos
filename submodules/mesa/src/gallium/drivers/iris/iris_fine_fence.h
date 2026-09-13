/*
 * Copyright © 2020 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef IRIS_FINE_FENCE_DOT_H
#define IRIS_FINE_FENCE_DOT_H

#include <stdbool.h>
#include <stdint.h>

#include "iris_screen.h"
#include "iris_resource.h"

/**
 * A lightweight sequence number fence.
 *
 * We emit PIPE_CONTROLs inside a batch (possibly in the middle)
 * which update a monotonically increasing, 32-bit counter.  We
 * can then check if that moment has passed by either:
 *
 * 1. Checking on the CPU by snooping on the DWord via a coherent map
 *
 * 2. Blocking on the GPU with MI_SEMAPHORE_WAIT from a second batch
 *    (relying on mid-batch preemption to switch GPU execution to the
 *    batch that writes it).
 */
struct iris_fine_fence {
   struct pipe_reference reference;

   /** Buffer where the seqno lives */
   struct iris_state_ref ref;

   /** Coherent CPU map of the buffer containing the seqno DWord. */
   const uint32_t *map;

   /**
    * A drm_syncobj pointing which will be signaled at the end of the
    * batch which writes this seqno.  This can be used to block until
    * the seqno has definitely passed (but may wait longer than necessary).
    */
   struct iris_syncobj *syncobj;

   /**
    * Sequence number expected to be written by the flush we inserted
    * when creating this fence.  The iris_fine_fence is 'signaled' when *@map
    * (written by the flush on the GPU) is greater-than-or-equal to @seqno.
    */
   uint32_t seqno;
};

void iris_fine_fence_init(struct iris_batch *batch);

struct iris_fine_fence *iris_fine_fence_new(struct iris_batch *batch);

void iris_fine_fence_destroy(struct iris_screen *screen, struct iris_fine_fence *sq);

static inline void
iris_fine_fence_reference(struct iris_screen *screen,
                          struct iris_fine_fence **dst,
                          struct iris_fine_fence *src)
{
   if (pipe_reference(&(*dst)->reference, &src->reference))
      iris_fine_fence_destroy(screen, *dst);

   *dst = src;
}

/**
 * Return true if this seqno has passed.
 *
 * NULL is considered signaled.
 */
static inline bool
iris_fine_fence_signaled(const struct iris_fine_fence *sq)
{
   return !sq || (READ_ONCE(*sq->map) >= sq->seqno);
}

#endif
