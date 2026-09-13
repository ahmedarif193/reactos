/*
 * Copyright © 2022 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "dev/intel_device_info.h"
#include "elk_eu_opcodes.h"

#ifdef __cplusplus
extern "C" {
#endif

struct elk_opcode_desc;

struct elk_isa_info {
   const struct intel_device_info *devinfo;

   /* A mapping from enum elk_opcode to the corresponding opcode_desc */
   const struct elk_opcode_desc *ir_to_descs[NUM_ELK_OPCODES];

   /** A mapping from a HW opcode encoding to the corresponding opcode_desc */
   const struct elk_opcode_desc *hw_to_descs[128];
};

void elk_init_isa_info(struct elk_isa_info *isa,
                       const struct intel_device_info *devinfo);

struct elk_opcode_desc {
   unsigned ir;
   unsigned hw;
   const char *name;
   int nsrc;
   int ndst;
   int gfx_vers;
};

const struct elk_opcode_desc *
elk_opcode_desc(const struct elk_isa_info *isa, enum elk_opcode opcode);

const struct elk_opcode_desc *
elk_opcode_desc_from_hw(const struct elk_isa_info *isa, unsigned hw);

static inline unsigned
elk_opcode_encode(const struct elk_isa_info *isa, enum elk_opcode opcode)
{
   return elk_opcode_desc(isa, opcode)->hw;
}

static inline enum elk_opcode
elk_opcode_decode(const struct elk_isa_info *isa, unsigned hw)
{
   const struct elk_opcode_desc *desc = elk_opcode_desc_from_hw(isa, hw);
   return desc ? (enum elk_opcode)desc->ir : ELK_OPCODE_ILLEGAL;
}

static inline bool
elk_is_3src(const struct elk_isa_info *isa, enum elk_opcode opcode)
{
   const struct elk_opcode_desc *desc = elk_opcode_desc(isa, opcode);
   return desc && desc->nsrc == 3;
}

#ifdef __cplusplus
}
#endif
