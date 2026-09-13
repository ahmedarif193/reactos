/*
 * Copyright © 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef IRIS_BINDER_DOT_H
#define IRIS_BINDER_DOT_H

#include <stdint.h>
#include <stdbool.h>
#include "compiler/shader_enums.h"

struct iris_bo;
struct iris_batch;
struct iris_bufmgr;
struct iris_compiled_shader;
struct iris_context;

struct iris_binder
{
   struct iris_bo *bo;
   void *map;

   /** Required alignment for each binding table in bytes */
   uint32_t alignment;

   /** Binding table size in bytes */
   uint32_t size;

   /** Insert new entries at this offset (in bytes) */
   uint32_t insert_point;

   /**
    * Last assigned offset for each shader stage's binding table.
    * Zero is considered invalid and means there's no binding table.
    */
   uint32_t bt_offset[MESA_SHADER_STAGES];
};

void iris_init_binder(struct iris_context *ice);
void iris_destroy_binder(struct iris_binder *binder);
uint32_t iris_binder_reserve(struct iris_context *ice, unsigned size);
void iris_binder_reserve_3d(struct iris_context *ice);
void iris_binder_reserve_gen(struct iris_context *ice);
void iris_binder_reserve_compute(struct iris_context *ice);

#endif
