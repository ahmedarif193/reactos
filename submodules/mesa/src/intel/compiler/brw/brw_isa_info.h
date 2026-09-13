/*
 * Copyright © 2022 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "dev/intel_device_info.h"
#include "brw_eu_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

struct opcode_desc;

struct brw_isa_info {
   const struct intel_device_info *devinfo;

   /* A mapping from enum opcode to the corresponding opcode_desc */
   const struct opcode_desc *ir_to_descs[NUM_BRW_OPCODES];

   /** A mapping from a HW opcode encoding to the corresponding opcode_desc */
   const struct opcode_desc *hw_to_descs[128];
};

void brw_init_isa_info(struct brw_isa_info *isa,
                       const struct intel_device_info *devinfo);

struct opcode_desc {
   unsigned ir;
   unsigned hw;
   const char *name;
   int nsrc;
   int ndst;
   int gfx_vers;
};

const struct opcode_desc *
brw_opcode_desc(const struct brw_isa_info *isa, enum opcode opcode);

const struct opcode_desc *
brw_opcode_desc_from_hw(const struct brw_isa_info *isa, unsigned hw);

static inline unsigned
brw_opcode_encode(const struct brw_isa_info *isa, enum opcode opcode)
{
   return brw_opcode_desc(isa, opcode)->hw;
}

static inline enum opcode
brw_opcode_decode(const struct brw_isa_info *isa, unsigned hw)
{
   const struct opcode_desc *desc = brw_opcode_desc_from_hw(isa, hw);
   return desc ? (enum opcode)desc->ir : BRW_OPCODE_ILLEGAL;
}

static inline bool
is_3src(const struct brw_isa_info *isa, enum opcode opcode)
{
   const struct opcode_desc *desc = brw_opcode_desc(isa, opcode);
   return desc && desc->nsrc == 3;
}

#ifdef __cplusplus
}
#endif
