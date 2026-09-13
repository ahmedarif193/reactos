/*
 * Copyright © 2012 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

struct bblock_t;

#ifdef __cplusplus

#include "brw_inst.h"

/**
 * CFG edge types.
 *
 * A logical edge represents a potential control flow path of the original
 * scalar program, while a physical edge represents a control flow path that
 * may not have existed in the original program but was introduced during
 * vectorization in order to implement divergent control flow of different
 * shader invocations within the same SIMD thread.
 *
 * All logical edges in the CFG are considered to be physical edges but not
 * the other way around -- I.e. the logical CFG is a subset of the physical
 * one.
 */
enum bblock_link_kind {
   bblock_link_logical = 0,
   bblock_link_physical
};

struct bblock_link {
   DECLARE_RALLOC_CXX_OPERATORS(bblock_link)

   bblock_link(bblock_t *block, enum bblock_link_kind kind)
      : block(block), kind(kind)
   {
   }

   struct brw_exec_node link;
   struct bblock_t *block;

   /* Type of this CFG edge.  Because bblock_link_logical also implies
    * bblock_link_physical, the proper way to test for membership of edge 'l'
    * in CFG kind 'k' is 'l.kind <= k'.
    */
   enum bblock_link_kind kind;
};

struct brw_shader;
struct cfg_t;

struct bblock_t {
   DECLARE_RALLOC_CXX_OPERATORS(bblock_t)

   explicit bblock_t(cfg_t *cfg);

   void add_successor(void *mem_ctx, bblock_t *successor,
                      enum bblock_link_kind kind);

   brw_inst *start();
   const brw_inst *start() const;
   brw_inst *end();
   const brw_inst *end() const;

   bblock_t *next();
   const bblock_t *next() const;
   bblock_t *prev();
   const bblock_t *prev() const;

   bool ends_with_control_flow() const;

   brw_inst *last_non_control_flow_inst();

   void insert_before(brw_inst *inst, brw_exec_node *ref);
   void remove(brw_inst *inst);

   struct cfg_t *cfg;

   unsigned num_instructions;

   struct brw_exec_list instructions;
   struct brw_exec_list parents;
   struct brw_exec_list children;
   int num;
};

inline brw_inst *
bblock_t::start()
{
   return (brw_inst *)brw_exec_list_get_head(&instructions);
}

inline const brw_inst *
bblock_t::start() const
{
   return (const brw_inst *)brw_exec_list_get_head_const(&instructions);
}

inline brw_inst *
bblock_t::end()
{
   return (brw_inst *)brw_exec_list_get_tail(&instructions);
}

inline const brw_inst *
bblock_t::end() const
{
   return (const brw_inst *)brw_exec_list_get_tail_const(&instructions);
}

inline bool
bblock_t::ends_with_control_flow() const
{
   enum opcode op = end()->opcode;
   return op == BRW_OPCODE_IF ||
          op == BRW_OPCODE_ELSE ||
          op == BRW_OPCODE_WHILE ||
          op == BRW_OPCODE_BREAK ||
          op == BRW_OPCODE_CONTINUE ||
          op == SHADER_OPCODE_FLOW;
}

inline brw_inst *
bblock_t::last_non_control_flow_inst()
{
   brw_inst *inst = end();
   if (ends_with_control_flow())
      inst = (brw_inst *)inst->prev;
   return inst;
}

struct cfg_t {
   DECLARE_RALLOC_CXX_OPERATORS(cfg_t)

   cfg_t(brw_shader *s, brw_exec_list *instructions);
   ~cfg_t();

   void remove_block(bblock_t *block);

   bblock_t *first_block()             { return blocks[0]; }
   const bblock_t *first_block() const { return blocks[0]; }
   bblock_t *last_block()              { return blocks[num_blocks - 1]; }
   const bblock_t *last_block() const  { return blocks[num_blocks - 1]; }

   bblock_t *new_block();
   void set_next_block(bblock_t **cur, bblock_t *block, int ip);

   void dump_cfg();

#ifdef NDEBUG
   void validate(UNUSED const char *stage_abbrev) { }
#else
   void validate(const char *stage_abbrev);
#endif

   struct brw_shader *s;
   void *mem_ctx;

   /** Ordered list (by ip) of basic blocks */
   bblock_t **blocks;
   int num_blocks;
   int cap_blocks;

   unsigned total_instructions;
};

inline bblock_t *
bblock_t::next()
{
   if (num == cfg->num_blocks - 1)
      return NULL;
   return cfg->blocks[num + 1];
}

inline const bblock_t *
bblock_t::next() const
{
   if (num == cfg->num_blocks - 1)
      return NULL;
   return cfg->blocks[num + 1];
}

inline bblock_t *
bblock_t::prev()
{
   if (num == 0)
      return NULL;
   return cfg->blocks[num - 1];
}

inline const bblock_t *
bblock_t::prev() const
{
   if (num == 0)
      return NULL;
   return cfg->blocks[num - 1];
}


/* Note that this is implemented with a double for loop -- break will
 * break from the inner loop only!
 */
#define foreach_block_and_inst(__block, __type, __inst, __cfg) \
   foreach_block (__block, __cfg)                              \
      foreach_inst_in_block (__type, __inst, __block)

/* Note that this is implemented with a double for loop -- break will
 * break from the inner loop only!
 */
#define foreach_block_and_inst_safe(__block, __type, __inst, __cfg) \
   foreach_block (__block, __cfg)                                   \
      foreach_inst_in_block_safe (__type, __inst, __block)

#define foreach_block(__block, __cfg)                                          \
   for (int __block_idx = 0; __block_idx < (__cfg)->num_blocks; __block_idx++) \
      if (bblock_t *__block = (__cfg)->blocks[__block_idx]; true)

#define foreach_block_reverse(__block, __cfg)                                          \
   for (int __block_idx = (__cfg)->num_blocks - 1; __block_idx >= 0; __block_idx--) \
      if (bblock_t *__block = (__cfg)->blocks[__block_idx]; true)

#define foreach_inst_in_block(__type, __inst, __block)         \
   brw_foreach_in_list(__type, __inst, &(__block)->instructions)

#define foreach_inst_in_block_safe(__type, __inst, __block)    \
   for (__type *__inst = (__type *)__block->instructions.head_sentinel.next, \
               *__next = (__type *)__inst->next;               \
        __next != NULL;                                        \
        __inst = __next,                                       \
        __next = (__type *)__next->next)

#define foreach_inst_in_block_reverse(__type, __inst, __block) \
   brw_foreach_in_list_reverse(__type, __inst, &(__block)->instructions)

#define foreach_inst_in_block_reverse_safe(__type, __inst, __block) \
   brw_foreach_in_list_reverse_safe(__type, __inst, &(__block)->instructions)

#define foreach_inst_in_block_starting_from(__type, __scan_inst, __inst) \
   for (__type *__scan_inst = (__type *)__inst->next;          \
        !__scan_inst->is_tail_sentinel();                      \
        __scan_inst = (__type *)__scan_inst->next)

#define foreach_inst_in_block_reverse_starting_from(__type, __scan_inst, __inst) \
   for (__type *__scan_inst = (__type *)__inst->prev;          \
        !__scan_inst->is_head_sentinel();                      \
        __scan_inst = (__type *)__scan_inst->prev)

#endif
