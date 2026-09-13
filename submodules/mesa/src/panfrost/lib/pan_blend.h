/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019-2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_BLEND_H__
#define __PAN_BLEND_H__

#include "genxml/gen_macros.h"

#include "compiler/nir/nir.h"
#include "util/blend.h"
#include "util/format/u_format.h"

struct MALI_BLEND_EQUATION;

struct pan_blend_equation {
   unsigned blend_enable                  : 1;
   unsigned is_float                      : 1;
   enum pipe_blend_func rgb_func          : 3;
   enum pipe_blendfactor rgb_src_factor   : 5;
   enum pipe_blendfactor rgb_dst_factor   : 5;
   enum pipe_blend_func alpha_func        : 3;
   enum pipe_blendfactor alpha_src_factor : 5;
   enum pipe_blendfactor alpha_dst_factor : 5;
   unsigned color_mask                    : 4;
};

struct pan_blend_rt_state {
   /* RT format */
   enum pipe_format format;

   /* Number of samples */
   unsigned nr_samples;

   struct pan_blend_equation equation;
};

struct pan_blend_state {
   bool alpha_to_one;
   bool logicop_enable;
   enum pipe_logicop logicop_func;
   float constants[4];
   unsigned rt_count;
   struct pan_blend_rt_state rts[8];
};

struct pan_blend_shader_key {
   enum pipe_format format;
   nir_alu_type src0_type, src1_type;
   uint32_t rt             : 3;
   uint32_t logicop_enable : 1;
   uint32_t logicop_func   : 4;
   uint32_t nr_samples     : 5;
   uint32_t alpha_to_one   : 1;
   uint32_t padding        : 17;
   struct pan_blend_equation equation;
};

bool pan_blend_reads_dest(const struct pan_blend_equation eq);

bool pan_blend_can_fixed_function(unsigned arch,
                                  const struct pan_blend_equation equation,
                                  bool supports_2src);

bool pan_blend_is_opaque(const struct pan_blend_equation eq);

bool pan_blend_alpha_zero_nop(const struct pan_blend_equation eq);

bool pan_blend_alpha_one_store(const struct pan_blend_equation eq);

unsigned pan_blend_constant_mask(const struct pan_blend_equation eq);

void pan_blend_optimize_equation(struct pan_blend_equation *eq,
                                 enum pipe_format format,
                                 const float *constants);

/* Fixed-function blending only supports a single constant, so if multiple bits
 * are set in constant_mask, the constants must match. Therefore we may pick
 * just the first constant. */

static inline float
pan_blend_get_constant(unsigned mask, const float *constants)
{
   return mask ? constants[ffs(mask) - 1] : 0.0;
}

/* v6 doesn't support blend constants in FF blend equations whatsoever, and v7
 * only uses the constant from RT 0 (TODO: what if it's the same constant? or a
 * constant is shared?) */

static inline bool
pan_blend_supports_constant(unsigned arch, unsigned rt)
{
   return !((arch == 6) || (arch == 7 && rt > 0));
}

/* The SOURCE_2 value is new in Bifrost */

static inline bool
pan_blend_supports_2src(unsigned arch)
{
   return (arch >= 6);
}

bool pan_blend_is_homogenous_constant(unsigned mask, const float *constants);

uint16_t pan_pack_blend_constant(enum pipe_format format, float cons);

void pan_blend_to_fixed_function_equation(const struct pan_blend_equation eq,
                                          struct MALI_BLEND_EQUATION *equation);

enum mali_register_file_format pan_blend_type_from_nir(nir_alu_type nir_type);
uint32_t pan_pack_blend(const struct pan_blend_equation equation);

#ifdef PAN_ARCH

enum pipe_format GENX(pan_blend_shader_fmt)(enum pipe_format format);

nir_shader *GENX(pan_blend_create_shader)(const struct pan_blend_state *state,
                                          nir_alu_type src0_type,
                                          nir_alu_type src1_type, unsigned rt);

#if PAN_ARCH >= 6
uint64_t GENX(pan_blend_get_internal_desc)(enum pipe_format fmt, unsigned rt,
                                           unsigned force_size, bool dithered);

#if PAN_ARCH < 9
enum mali_register_file_format
   GENX(pan_fixup_blend_type)(nir_alu_type T_size, enum pipe_format format);
#endif
#endif

#endif

#endif
