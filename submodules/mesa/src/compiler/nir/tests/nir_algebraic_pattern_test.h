/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef NIR_ALGEBRAIC_PATTERN_TEST_H
#define NIR_ALGEBRAIC_PATTERN_TEST_H

#include "gtest/gtest-spi.h"
#include "nir.h"
#include "nir_search.h"
#include "nir_test.h"

class nir_algebraic_pattern_test_variable_cond {
 public:
   nir_algebraic_pattern_test_variable_cond(nir_alu_instr *alu, unsigned src_index, const nir_search_variable_cond cond)
       : alu(alu), src_index(src_index), cond(cond)
   {
   }

   nir_alu_instr *alu;
   unsigned src_index;
   const nir_search_variable_cond cond;
};

enum input_type {
   BOOL,
   INT,
   UINT,
   FLOAT,
};

class nir_algebraic_pattern_test_input {
 public:
   nir_algebraic_pattern_test_input(nir_load_const_instr *instr, input_type ty, uint32_t fuzzing_start_bit)
       : instr(instr), ty(ty), fuzzing_start_bit(fuzzing_start_bit)
   {
   }

   nir_load_const_instr *instr;
   input_type ty;
   uint32_t fuzzing_start_bit;
};

enum result {
   PASS,
   FAIL,
   UNSUPPORTED,
};

// Pretty prints for gtest output.
static inline std::ostream &
operator<<(std::ostream &out, enum result r)
{
   switch (r) {
   case PASS:
      return out << "PASS";
   case FAIL:
      return out << "FAIL";
   case UNSUPPORTED:
      return out << "UNSUPPORTED";
   default:
      UNREACHABLE("bad result");
   }
}

class nir_algebraic_pattern_test : public nir_test {
 protected:
   nir_algebraic_pattern_test(const char *name);
   virtual ~nir_algebraic_pattern_test();

   void set_inputs(uint32_t seed);
   bool check_variable_conds();
   void validate_pattern();
   bool evaluate_expression(nir_instr *instr);
   bool skip_test(nir_alu_instr *alu, uint32_t bit_size,
                  nir_const_value tmp, int32_t src_index);
   void handle_signed_zero(nir_const_value *val, uint32_t bit_size);

 public:
   nir_const_value *tmp_value(nir_def *def);

   std::vector<nir_algebraic_pattern_test_input> inputs;
   uint32_t fuzzing_bits;

   /* Iteration count for signed 0 non-preservation search -- we set up
    * signed_zero_count during the first iteration, and during other iterations
    * we'll flip signs to try to see if the pattern ever matches.
    */
   uint32_t signed_zero_iter;

   /* Number of 0.0s encountered in the current signed_zero_iter. */
   uint32_t signed_zero_count;

   bool exact = true;
   enum result expected_result = PASS;
   const char *expression_cond_failed = NULL;
   nir_fp_math_control fp_math_ctrl = (nir_fp_math_control)(nir_fp_preserve_signed_zero |
                                                            nir_fp_preserve_inf |
                                                            nir_fp_preserve_nan);
   std::vector<nir_algebraic_pattern_test_variable_cond> variable_conds;
   std::vector<nir_const_value> tmp_values;
};

/* Builders that aren't auto-generated for nir_builder.h, due to not being
 * having a defined dest size (3 or 4 components, independent of src args).
 * Just pick 4 and get some coverage.
 */
static inline nir_def *
nir_fdot_replicated(nir_builder *b, nir_op op, nir_def *x, nir_def *y)
{
   nir_alu_instr *alu = nir_alu_instr_create(b->shader, op);
   alu->src[0].src = nir_src_for_ssa(x);
   alu->src[1].src = nir_src_for_ssa(y);
   alu->fp_math_ctrl = b->fp_math_ctrl;
   nir_def_init(&alu->instr, &alu->def, 4, x->bit_size);
   nir_builder_instr_insert(b, &alu->instr);

   return &alu->def;
}

static inline nir_def *
nir_fdot2_replicated(nir_builder *b, nir_def *x, nir_def *y)
{
   return nir_fdot_replicated(b, nir_op_fdot2_replicated, x, y);
}

static inline nir_def *
nir_fdot3_replicated(nir_builder *b, nir_def *x, nir_def *y)
{
   return nir_fdot_replicated(b, nir_op_fdot3_replicated, x, y);
}

static inline nir_def *
nir_fdot4_replicated(nir_builder *b, nir_def *x, nir_def *y)
{
   return nir_fdot_replicated(b, nir_op_fdot4_replicated, x, y);
}

static inline nir_def *
nir_fdph_replicated(nir_builder *b, nir_def *x, nir_def *y)
{
   return nir_fdot_replicated(b, nir_op_fdph_replicated, x, y);
}

#endif
