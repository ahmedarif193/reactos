/*
 * Copyright © 2025 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nir_test.h"

class nir_large_constants_test : public nir_test {
protected:
   nir_large_constants_test();

   void run_test();

   nir_variable *array;
};

nir_large_constants_test::nir_large_constants_test()
   : nir_test::nir_test("nir_large_constants_test", MESA_SHADER_COMPUTE)
{
}

void
nir_large_constants_test::run_test()
{
   nir_def *index = nir_load_workgroup_index(b);
   nir_def *value = nir_load_array_var(b, array, index);
   nir_use(b, value);

   NIR_PASS(_, b->shader, nir_opt_large_constants, NULL, 0);
   nir_opt_dce(b->shader);
}

TEST_F(nir_large_constants_test, small_int_array)
{
   uint32_t length = 8;
   array = nir_local_variable_create(b->impl, glsl_array_type(glsl_uint_type(), length, 0), "array");
   for (uint32_t i = 0; i < length; i++)
      nir_store_array_var_imm(b, array, i, nir_imm_int(b, i), 0x1);

   run_test();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_large_constants_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32    %0 = @load_workgroup_index
          32    %1 = load_const (0x76543210 = 1985229328)
          32    %2 = load_const (0x00000002)
          32    %3 = ishl %0, %2 (0x2)
          32    %4 = ushr %1 (0x76543210), %3
          32    %5 = load_const (0x0000000f = 15)
          32    %6 = iand %4, %5 (0xf)
                     @use (%6)
                     // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_large_constants_test, small_uint8_t_array)
{
   uint32_t length = 8;
   array = nir_local_variable_create(b->impl, glsl_array_type(glsl_uint8_t_type(), length, 0), "array");
   for (uint32_t i = 0; i < length; i++)
      nir_store_array_var_imm(b, array, i, nir_imm_intN_t(b, i, 8), 0x1);

   run_test();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_large_constants_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32    %0 = @load_workgroup_index
          32    %1 = load_const (0x76543210 = 1985229328)
          32    %2 = load_const (0x00000002)
          32    %3 = ishl %0, %2 (0x2)
          32    %4 = ushr %1 (0x76543210), %3
          32    %5 = load_const (0x0000000f = 15)
          32    %6 = iand %4, %5 (0xf)
          8     %7 = u2u8 %6
                     @use (%7)
                     // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_large_constants_test, small_bool_array)
{
   uint32_t length = 8;
   array = nir_local_variable_create(b->impl, glsl_array_type(glsl_bool_type(), length, 0), "array");
   for (uint32_t i = 0; i < length; i++)
      nir_store_array_var_imm(b, array, i, nir_imm_bool(b, i & 1), 0x1);

   run_test();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_large_constants_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = @load_workgroup_index
          32     %1 = load_const (0x000000aa = 170)
          32     %2 = ushr %1 (0xaa), %0
          32     %3 = load_const (0x00000001)
          32     %4 = iand %2, %3 (0x1)
          32     %5 = load_const (0x00000000)
          1      %6 = ine %4, %5 (0x0)
          1      %7 = load_const (true)
          1      %8 = load_const (false)
          1      %9 = bcsel %6, %7 (true), %8 (false)
                      @use (%9)
                      // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_large_constants_test, small_uint64_t_array)
{
   uint32_t length = 8;
   array = nir_local_variable_create(b->impl, glsl_array_type(glsl_uint64_t_type(), length, 0), "array");
   for (uint32_t i = 0; i < length; i++)
      nir_store_array_var_imm(b, array, i, nir_imm_int64(b, i), 0x1);

   run_test();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_large_constants_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32    %0 = @load_workgroup_index
          32    %1 = load_const (0x76543210 = 1985229328)
          32    %2 = load_const (0x00000002)
          32    %3 = ishl %0, %2 (0x2)
          32    %4 = ushr %1 (0x76543210), %3
          32    %5 = load_const (0x0000000f = 15)
          32    %6 = iand %4, %5 (0xf)
          64    %7 = u2u64 %6
                     @use (%7)
                     // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_large_constants_test, small_float_natural_numbers_including_zero_array)
{
   uint32_t length = 8;
   array = nir_local_variable_create(b->impl, glsl_array_type(glsl_float_type(), length, 0), "array");
   for (uint32_t i = 0; i < length; i++)
      nir_store_array_var_imm(b, array, i, nir_imm_float(b, i), 0x1);

   run_test();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_large_constants_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32    %0 = @load_workgroup_index
          32    %1 = load_const (0x76543210 = 1985229328)
          32    %2 = load_const (0x00000002)
          32    %3 = ishl %0, %2 (0x2)
          32    %4 = ushr %1 (0x76543210), %3
          32    %5 = load_const (0x0000000f = 15)
          32    %6 = iand %4, %5 (0xf)
          32    %7 = u2f32 %6 // exact, preserve:sz
                     @use (%7)
                     // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_large_constants_test, small_float_natural_numbers_including_zero_vec_array)
{
   uint32_t length = 8;
   array = nir_local_variable_create(b->impl, glsl_array_type(glsl_vec2_type(), length, 0), "array");
   for (uint32_t i = 0; i < length; i++)
      nir_store_array_var_imm(b, array, i, nir_imm_vec2(b, i, length - 1 - i), 0x3);

   run_test();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_large_constants_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = @load_workgroup_index
          32     %1 = load_const (0x76543210 = 1985229328)
          32     %2 = load_const (0x00000002)
          32     %3 = ishl %0, %2 (0x2)
          32     %4 = ushr %1 (0x76543210), %3
          32     %5 = load_const (0x0000000f = 15)
          32     %6 = iand %4, %5 (0xf)
          32     %7 = u2f32 %6 // exact, preserve:sz
          32     %8 = load_const (0x01234567 = 19088743)
          32     %9 = load_const (0x00000002)
          32    %10 = ishl %0, %9 (0x2)
          32    %11 = ushr %8 (0x1234567), %10
          32    %12 = load_const (0x0000000f = 15)
          32    %13 = iand %11, %12 (0xf)
          32    %14 = u2f32 %13 // exact, preserve:sz
          32x2  %15 = vec2 %7, %14
                      @use (%15)
                      // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_large_constants_test, small_float_whole_numbers_array)
{
   int32_t length = 8;
   array = nir_local_variable_create(b->impl, glsl_array_type(glsl_float_type(), length, 0), "array");
   for (int32_t i = 0; i < length; i++)
      nir_store_array_var_imm(b, array, i, nir_imm_float(b, i - 4), 0x1);

   run_test();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_large_constants_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = @load_workgroup_index
          32     %1 = load_const (0x76543210 = 1985229328)
          32     %2 = load_const (0x00000002)
          32     %3 = ishl %0, %2 (0x2)
          32     %4 = ushr %1 (0x76543210), %3
          32     %5 = load_const (0x0000000f = 15)
          32     %6 = iand %4, %5 (0xf)
          32     %7 = load_const (0xfffffffc = -4 = 4294967292)
          32     %8 = iadd %6, %7 (0xfffffffc)
          32     %9 = i2f32 %8 // exact, preserve:sz
                      @use (%9)
                      // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_large_constants_test, small_fraction_array)
{
   int32_t length = 8;
   array = nir_local_variable_create(b->impl, glsl_array_type(glsl_float_type(), length, 0), "array");
   for (int32_t i = 0; i < length / 2; i++)
      nir_store_array_var_imm(b, array, i, nir_imm_float(b, i + 2.25), 0x1);
   for (int32_t i = length / 2; i < length; i++)
      nir_store_array_var_imm(b, array, i, nir_imm_float(b, (i - length / 2) + 0.5), 0x1);

   run_test();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_large_constants_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = @load_workgroup_index
          64     %1 = load_const (0x0c080400130f0b07 = 866947326635084551)
          32     %2 = load_const (0x00000003)
          32     %3 = ishl %0, %2 (0x3)
          64     %4 = ushr %1 (0xc080400130f0b07), %3
          64     %5 = load_const (0x00000000000000ff = 255)
          64     %6 = iand %4, %5 (0xff)
          32     %7 = unpack_64_2x32_split_x %6
          32     %8 = load_const (0x00000002)
          32     %9 = iadd %7, %8 (0x2)
          32    %10 = u2f32 %9 // exact, preserve:sz
          32    %11 = load_const (0x3e800000 = 0.250000)
          32    %12 = fmul %10, %11 (0.250000) // exact, preserve:sz
                      @use (%12)
                      // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_large_constants_test, bcsel_vec)
{
   uint32_t length = 4;
   array = nir_local_variable_create(b->impl, glsl_array_type(glsl_vec4_type(), length, 0), "array");
   for (uint32_t i = 0; i < length; i++)
      nir_store_array_var_imm(b, array, i, nir_imm_vec4(b, i == 0, i == 1, i == 2, i == 3), 0xf);

   run_test();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_large_constants_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = @load_workgroup_index
          32     %1 = load_const (0x00000000)
          1      %2 = ieq %0, %1 (0x0)
          32     %3 = load_const (0x3f800000 = 1.000000 = 1065353216)
          32     %4 = load_const (0x00000000 = 0.000000)
          32     %5 = bcsel %2, %3 (0x3f800000), %4 (0x0)
          32     %6 = load_const (0x00000001)
          1      %7 = ieq %0, %6 (0x1)
          32     %8 = load_const (0x3f800000 = 1.000000 = 1065353216)
          32     %9 = load_const (0x00000000 = 0.000000)
          32    %10 = bcsel %7, %8 (0x3f800000), %9 (0x0)
          32    %11 = load_const (0x00000002)
          1     %12 = ieq %0, %11 (0x2)
          32    %13 = load_const (0x3f800000 = 1.000000 = 1065353216)
          32    %14 = load_const (0x00000000 = 0.000000)
          32    %15 = bcsel %12, %13 (0x3f800000), %14 (0x0)
          32    %16 = load_const (0x00000003)
          1     %17 = ieq %0, %16 (0x3)
          32    %18 = load_const (0x3f800000 = 1.000000 = 1065353216)
          32    %19 = load_const (0x00000000 = 0.000000)
          32    %20 = bcsel %17, %18 (0x3f800000), %19 (0x0)
          32x4  %21 = vec4 %5, %10, %15, %20
                      @use (%21)
                      // succs: b1
          block b1:
      }
   )"));
}
