/*
 * Copyright © 2021 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef IRIS_UTRACE_DOT_H
#define IRIS_UTRACE_DOT_H

#include <stdint.h>

enum intel_ds_stall_flag;

struct iris_context;
struct iris_batch;

void iris_utrace_init(struct iris_context *ice);
void iris_utrace_fini(struct iris_context *ice);

void iris_utrace_flush(struct iris_batch *batch,
                       uint64_t submission_id);

enum intel_ds_stall_flag
iris_utrace_pipe_flush_bit_to_ds_stall_flag(uint32_t flags);

#endif
