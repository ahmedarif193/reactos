/*
 * Copyright © 2014 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/brw_list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct elk_cfg_t;
struct elk_backend_instruction;
struct intel_device_info;

struct inst_group {
   struct brw_exec_node link;

   int offset;

   size_t error_length;
   char *error;

   /* Pointers to the basic block in the CFG if the instruction group starts
    * or ends a basic block.
    */
   struct elk_bblock_t *block_start;
   struct elk_bblock_t *block_end;

   /* Annotation for the generated IR.  One of the two can be set. */
   const void *ir;
   const char *annotation;
};

struct elk_disasm_info {
   struct brw_exec_list group_list;

   const struct elk_isa_info *isa;
   const struct elk_cfg_t *cfg;

   /** Block index in the cfg. */
   int cur_block;
   bool use_tail;
};

void
elk_dump_assembly(void *assembly, int start_offset, int end_offset,
              struct elk_disasm_info *disasm, const unsigned *block_latency);

struct elk_disasm_info *
elk_disasm_initialize(const struct elk_isa_info *isa,
                  const struct elk_cfg_t *cfg);

struct inst_group *
elk_disasm_new_inst_group(struct elk_disasm_info *disasm, unsigned offset);

void
elk_disasm_annotate(struct elk_disasm_info *disasm,
                struct elk_backend_instruction *inst, unsigned offset);

void
elk_disasm_insert_error(struct elk_disasm_info *disasm, unsigned offset,
                    unsigned inst_size, const char *error);

#ifdef __cplusplus
} /* extern "C" */
#endif
