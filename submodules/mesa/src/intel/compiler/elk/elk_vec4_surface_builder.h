/* -*- c++ -*- */
/*
 * Copyright © 2013-2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "elk_vec4_builder.h"

namespace elk {
   namespace surface_access {
      src_reg
      emit_untyped_read(const vec4_builder &bld,
                        const src_reg &surface, const src_reg &addr,
                        unsigned dims, unsigned size,
                        elk_predicate pred = ELK_PREDICATE_NONE);

      void
      emit_untyped_write(const vec4_builder &bld, const src_reg &surface,
                         const src_reg &addr, const src_reg &src,
                         unsigned dims, unsigned size,
                         elk_predicate pred = ELK_PREDICATE_NONE);

      src_reg
      emit_untyped_atomic(const vec4_builder &bld,
                          const src_reg &surface, const src_reg &addr,
                          const src_reg &src0, const src_reg &src1,
                          unsigned dims, unsigned rsize, unsigned op,
                          elk_predicate pred = ELK_PREDICATE_NONE);
   }
}
