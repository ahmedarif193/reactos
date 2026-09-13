/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class algebraic_test : public brw_shader_pass_test {};

TEST_F(algebraic_test, imax_a_a)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_D);

   bld.emit_minmax(dst0, src0, src0, BRW_CONDITIONAL_GE);

   EXPECT_PROGRESS(brw_opt_algebraic, bld);

   exp.MOV(dst0, src0);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(algebraic_test, sel_a_a)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_D);

   bld.SEL(dst0, src0, src0)
      ->predicate = BRW_PREDICATE_NORMAL;

   EXPECT_PROGRESS(brw_opt_algebraic, bld);

   exp.MOV(dst0, src0);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(algebraic_test, fmax_a_a)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, BRW_TYPE_F);

   bld.emit_minmax(dst0, src0, src0, BRW_CONDITIONAL_GE);

   /* SEL.GE may flush denorms to zero. We don't have enough information at
    * this point in compilation to know whether or not it is safe to remove
    * that.
    */
   EXPECT_NO_PROGRESS(brw_opt_algebraic, bld);
}

TEST_F(algebraic_test, fmin_sat_a_1)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, BRW_TYPE_F);

   /* This is NaN. The definition of NAN in math.h has a scary comment about
    * possibly raising an exception outside static initializers.
    */
   bld.MOV(retype(src0, BRW_TYPE_UD), brw_imm_ud(0x7fc00000));
   bld.emit_minmax(dst0, src0, brw_imm_f(1.0), BRW_CONDITIONAL_L)
      ->saturate = true;

   /* SEL.L of NaN should produce 1.0, and the .SAT should occur
    * afterwards. This means the SEL.L.SAT cannot be optimized to a simple
    * MOV.SAT since that would saturate NaN to 0.0 instead.
    */
   EXPECT_NO_PROGRESS(brw_opt_algebraic, bld);
}

TEST_F(algebraic_test, mad_with_all_immediates)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dst = vgrf(bld, exp, BRW_TYPE_UD);

   bld.MAD(dst, brw_imm_ud(2), brw_imm_ud(3), brw_imm_ud(4));

   EXPECT_PROGRESS(brw_opt_algebraic, bld);

   exp.MOV(dst, brw_imm_ud(14));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(algebraic_test, mov_sat_neg_constant_type_d_conversion)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);

   bld.MOV(dst0, brw_imm_d(-5))
      ->saturate = true;

   EXPECT_PROGRESS(brw_opt_algebraic, bld);

   exp.MOV(dst0, brw_imm_f(0.0));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(algebraic_test, mov_sat_neg_constant_type_ud_conversion)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);

   bld.MOV(dst0, brw_imm_ud(-5))
      ->saturate = true;

   EXPECT_PROGRESS(brw_opt_algebraic, bld);

   exp.MOV(dst0, brw_imm_f(1.0));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(algebraic_test, mov_sat_pos_constant_type_conversion)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);

   bld.MOV(dst0, brw_imm_ud(37))
      ->saturate = true;

   EXPECT_PROGRESS(brw_opt_algebraic, bld);

   exp.MOV(dst0, brw_imm_f(1.0));

   EXPECT_SHADERS_MATCH(bld, exp);
}
