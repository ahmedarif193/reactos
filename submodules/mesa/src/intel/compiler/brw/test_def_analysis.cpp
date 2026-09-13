/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class defs_test : public brw_shader_pass_test {};

TEST_F(defs_test, dst_is_acc0)
{
   set_gfx_verx10(125);

   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_reg acc0 = retype(brw_acc_reg(16), BRW_TYPE_F);
   brw_reg dst0;

   for (unsigned i = 0; i < 1024; i++) {
      dst0 = vgrf(bld, BRW_TYPE_F);

      if (dst0.nr == acc0.nr)
         break;
   }

   ASSERT_EQ(dst0.nr, acc0.nr);

   brw_reg src0 = vgrf(bld, BRW_TYPE_F);

   brw_inst *inst = bld.MOV(dst0, brw_imm_f(1.0));
   bld.MOV(src0, brw_imm_f(2.0));
   bld.MAC(acc0, src0, brw_imm_f(3.0));

   brw_calculate_cfg(*bld.shader);
   brw_validate(*bld.shader);

   const brw_def_analysis &defs = bld.shader->def_analysis.require();

   EXPECT_EQ(inst, defs.get(dst0));
}

TEST_F(defs_test, dst_and_src_are_acc0)
{
   set_gfx_verx10(125);

   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_reg acc0 = retype(brw_acc_reg(16), BRW_TYPE_F);
   brw_reg dst0;

   for (unsigned i = 0; i < 1024; i++) {
      dst0 = vgrf(bld, BRW_TYPE_F);

      if (dst0.nr == acc0.nr)
         break;
   }

   ASSERT_EQ(dst0.nr, acc0.nr);

   brw_inst *inst = bld.MOV(dst0, brw_imm_f(1.0));
   bld.MOV(acc0, brw_imm_f(2.0));
   bld.MUL(acc0, acc0, brw_imm_f(3.0));

   brw_calculate_cfg(*bld.shader);
   brw_validate(*bld.shader);

   const brw_def_analysis &defs = bld.shader->def_analysis.require();

   EXPECT_EQ(inst, defs.get(dst0));
}

TEST_F(defs_test, src_is_acc2)
{
   set_gfx_verx10(125);

   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, BRW_TYPE_F);
   brw_reg acc2 = retype(brw_acc_reg(16), BRW_TYPE_F);

   acc2.nr = BRW_ARF_ACCUMULATOR + 2;

   bld.MOV(src0, brw_imm_f(1.0));
   bld.MOV(acc2, brw_imm_f(2.0));
   bld.MUL(dst0, src0, acc2);

   brw_calculate_cfg(*bld.shader);
   brw_validate(*bld.shader);

   const brw_def_analysis &defs = bld.shader->def_analysis.require();

   EXPECT_EQ(NULL, defs.get(dst0));
}

TEST_F(defs_test, src_is_address)
{
   set_gfx_verx10(125);

   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, BRW_TYPE_UW);
   brw_reg src0 = vgrf(bld, BRW_TYPE_UW);
   brw_reg addr = brw_address_reg(0);

   addr.nr = 1;

   bld.MOV(src0, brw_imm_uw(1));
   bld.uniform().MOV(addr, brw_imm_uw(2));
   bld.ADD(dst0, src0, addr);

   brw_calculate_cfg(*bld.shader);
   brw_validate(*bld.shader);

   const brw_def_analysis &defs = bld.shader->def_analysis.require();

   EXPECT_EQ(NULL, defs.get(dst0));
}
