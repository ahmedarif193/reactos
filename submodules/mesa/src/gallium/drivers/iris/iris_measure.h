/*
 * Copyright © 2019 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef IRIS_MEASURE_H
#define IRIS_MEASURE_H

#include "common/intel_measure.h"
#include "pipe/p_state.h"
struct iris_screen;

struct iris_measure_batch {
   struct iris_bo *bo;
   struct intel_measure_batch base;
};

void iris_init_screen_measure(struct iris_screen *screen);
void iris_init_batch_measure(struct iris_context *ice,
                             struct iris_batch *batch);
void iris_destroy_batch_measure(struct iris_measure_batch *batch);
void iris_destroy_ctx_measure(struct iris_context *ice);
void iris_destroy_screen_measure(struct iris_screen *screen);
void iris_measure_frame_end(struct iris_context *ice);
void iris_measure_batch_end(struct iris_context *ice, struct iris_batch *batch);
void _iris_measure_snapshot(struct iris_context *ice,
                            struct iris_batch *batch,
                            enum intel_measure_snapshot_type type,
                            const struct pipe_draw_info *draw,
                            const struct pipe_draw_indirect_info *indirect,
                            const struct pipe_draw_start_count_bias *sc);

#define iris_measure_snapshot(ice, batch, type, draw, indirect, start_count)        \
   if (unlikely(((struct iris_screen *) ice->ctx.screen)->measure.config)) \
      _iris_measure_snapshot(ice, batch, type, draw, indirect, start_count)

#endif  /* IRIS_MEASURE_H */
