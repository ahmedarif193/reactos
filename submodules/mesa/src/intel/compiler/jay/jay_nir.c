/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/brw/brw_compiler.h"
#include "compiler/brw/brw_eu.h"
#include "compiler/brw/brw_eu_defines.h"
#include "compiler/brw/brw_nir.h"
#include "compiler/brw/brw_private.h"
#include "compiler/intel_nir.h"
#include "compiler/intel_prim.h"
#include "glsl_types.h"
#include "jay_private.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"
#include "nir_intrinsics.h"
#include "shader_enums.h"

#define JAY_NIR_SNAPSHOT(name)  BRW_NIR_SNAPSHOT(name)
#define JAY_NIR_PASS(pass, ...) BRW_NIR_PASS(pass, ##__VA_ARGS__)

/*
 * Jay-to-NIR relies on a careful indexing of defs: every 32-bit word has
 * its own index. Vectors/64-bit use contiguous indices. We therefore run a
 * modified version of nir_index_ssa_defs right before translating NIR->Jay.
 */
static bool
index_ssa_def_cb(nir_def *def, void *state)
{
   unsigned *index = (unsigned *) state;
   def->index = *index;
   *index += DIV_ROUND_UP(def->num_components * MAX2(def->bit_size, 32), 32);
   return true;
}

static void
nj_index_ssa_defs(nir_shader *nir)
{
   nir_foreach_function_impl(impl, nir) {
      /* The zero index means null in Jay, so start SSA indices at 1 */
      unsigned index = 1;

      nir_foreach_block_unstructured(block, impl) {
         nir_foreach_instr(instr, block)
            nir_foreach_def(instr, index_ssa_def_cb, &index);
      }

      impl->ssa_alloc = index;
   }
}

static bool
lower_frag_coord(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   b->cursor = nir_before_instr(&intr->instr);

   if (intr->intrinsic == nir_intrinsic_load_pixel_coord) {
      nir_def_replace(&intr->def,
                      nir_unpack_32_2x16(b, nir_load_pixel_coord_intel(b)));
      return true;
   } else if (intr->intrinsic == nir_intrinsic_load_frag_coord_w) {
      nir_def_replace(&intr->def, nir_frcp(b, nir_load_frag_coord_w_rcp(b)));
      return true;
   }
   return false;
}

static bool
jay_nir_lower_simd(nir_builder *b, nir_intrinsic_instr *intr, void *simd_)
{
   b->cursor = nir_after_instr(&intr->instr);
   unsigned *simd_width = simd_;

   /* mask & -mask isolates the lowest set bit in the mask. */
   if (intr->intrinsic == nir_intrinsic_elect) {
      nir_def *mask = nir_ballot(b, 1, *simd_width, nir_imm_true(b));
      mask = nir_iand(b, mask, nir_ineg(b, mask));
      nir_def_replace(&intr->def, nir_inverse_ballot(b, mask));
      return true;
   }

   /* Ballots must match the SIMD size */
   if (intr->intrinsic == nir_intrinsic_ballot ||
       intr->intrinsic == nir_intrinsic_ballot_relaxed) {
      unsigned old_bitsize = intr->def.bit_size;
      intr->def.bit_size = *simd_width;
      nir_def *u2uN = nir_u2uN(b, &intr->def, old_bitsize);
      nir_def_rewrite_uses_after(&intr->def, u2uN);
      return true;
   }

   /* Just a constant */
   if (intr->intrinsic == nir_intrinsic_load_simd_width_intel) {
      nir_def_replace(&intr->def, nir_imm_int(b, *simd_width));
      return true;
   }

   /* Note: we don't treat read_invocation specially because there's little
    * benefit but doing so would require expensive uniformizing in some cases.
    */
   if (intr->intrinsic != nir_intrinsic_shuffle &&
       intr->intrinsic != nir_intrinsic_read_invocation)
      return false;

   nir_def *data = intr->src[0].ssa;
   assert(data->num_components == 1 && data->bit_size <= 32 && "scalarized");

   nir_def *offset_B = nir_imul_imm(b, intr->src[1].ssa, 4);
   nir_def_replace(&intr->def, nir_shuffle_intel(b, 1, data, offset_B));
   return true;
}

struct frag_out_ctx {
   nir_scalar colour[FRAG_RESULT_MAX][4];
   nir_def *outputs[FRAG_RESULT_MAX];
   bool dual_blend;
   bool replicate_alpha;
};

static bool
collect_fragment_output(nir_builder *b, nir_intrinsic_instr *intr, void *ctx_)
{
   struct frag_out_ctx *ctx = ctx_;
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   const unsigned wrmask = nir_intrinsic_write_mask(intr);
   const unsigned c = nir_intrinsic_component(intr);

   gl_frag_result loc = nir_intrinsic_io_semantics(intr).location;
   if (loc == FRAG_RESULT_COLOR)
      loc = FRAG_RESULT_DATA0;
   else if (loc == FRAG_RESULT_DUAL_SRC_BLEND)
      ctx->dual_blend = true;

   if (loc < FRAG_RESULT_DATA0) {
      assert(c == 0 && wrmask == 1);
      assert(!ctx->outputs[loc] && "each non-colour output written only once");
      ctx->outputs[loc] = intr->src[0].ssa;

      /* Remove SampleMask writes that don't mask out any samples */
      const unsigned all_samples = BITFIELD_MASK(16);
      if (loc == FRAG_RESULT_SAMPLE_MASK &&
          nir_src_is_const(intr->src[0]) &&
          (nir_src_as_uint(intr->src[0]) & all_samples) == all_samples)
         ctx->outputs[loc] = NULL;
   } else {
      u_foreach_bit(i, wrmask) {
         assert(!ctx->colour[loc][c + i].def &&
                "each colour component written only once");
         ctx->colour[loc][c + i] = nir_get_scalar(intr->src[0].ssa, i);
      }
   }

   nir_instr_remove(&intr->instr);
   return true;
}

/* nir_vec_scalar colour components, filling any unwritten with undef */
static bool
gather_colour_components(nir_builder *b,
                         struct frag_out_ctx *ctx,
                         gl_frag_result loc,
                         nir_def *undef)
{
   bool written = false;

   for (unsigned c = 0; c < 4; c++) {
      if (!ctx->colour[loc][c].def)
         ctx->colour[loc][c] = nir_get_scalar(undef, 0);
      else
         written = true;
   }

   if (written)
      ctx->outputs[loc] = nir_vec_scalars(b, ctx->colour[loc], 4);

   return written;
}

static void
insert_rt_store(nir_builder *b, struct frag_out_ctx *ctx, signed target)
{
   const unsigned src0_alpha_loc =
      FRAG_RESULT_DATA0 + (ctx->replicate_alpha ? 0 : MAX2(target, 0));

   nir_def *colour = ctx->outputs[FRAG_RESULT_DATA0 + MAX2(target, 0)];
   nir_def *dual_colour = ctx->outputs[FRAG_RESULT_DUAL_SRC_BLEND] ?: colour;
   nir_def *src0_alpha = nir_mov_scalar(b, ctx->colour[src0_alpha_loc][3]);

   nir_store_render_target_intel(b, colour, dual_colour, src0_alpha,
                                 ctx->outputs[FRAG_RESULT_SAMPLE_MASK],
                                 ctx->outputs[FRAG_RESULT_DEPTH],
                                 ctx->outputs[FRAG_RESULT_STENCIL],
                                 .target = target);
}

static unsigned int
calc_control_data_bits_per_vertex(struct brw_gs_prog_data *progdata)
{
   unsigned format = progdata->control_data_format;
   assert(format == GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_CUT ||
          format == GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_SID);
   return format == GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_CUT ? 1 : 2;
}

static void
emit_gs_control_data_bits(struct brw_gs_prog_data *progdata,
                          nir_builder *b,
                          nir_variable *control_data_bits,
                          nir_def *vertex_count)
{

   nir_def *curr_control_data_bits = nir_load_var(b, control_data_bits);

   nir_def *dword_urb_offset =
      nir_ushr_imm(b, nir_iadd_imm(b, nir_imax_imm(b, vertex_count, 1), -1),
                   6 - calc_control_data_bits_per_vertex(progdata));

   nir_def *byte_urb_offset = nir_ishl_imm(b, dword_urb_offset, 2u);

   nir_def *output_handle = nir_load_urb_output_handle_intel(b);
   nir_def *urb_addr = nir_iadd(b, output_handle, byte_urb_offset);

   nir_store_urb_lsc_intel(b, curr_control_data_bits, urb_addr,
                           .base =
                              progdata->static_vertex_count == -1 ? 32 : 0);
}

/* This function is responsible for the code that emits control data bits
 * for geometry shaders. Control data bits have two purposes:
 * - For rendering, they determine whether a given vertex represents the
 *   final one in its primitive.
 * - For streamout, they determine what stream a given vertex is
 *   to be placed in.
 *
 * Because we cannot emit control data bits one-at-a-time
 * (that'd be really inefficient anyway), we accumulate
 * control data bits in batches of 32-at-a-time in a dedicated variable.
 * Only when that variable fills up with 32 control data bits do we
 * actually write them to the URB.
 */
static void
emit_gs_vertex(nir_builder *b,
               struct brw_gs_prog_data *progdata,
               nir_variable *control_data_bits,
               nir_intrinsic_instr *intr)
{
   nir_src *vertex_count_src = &intr->src[0];

   uint32_t control_data_header_size_bits =
      progdata->control_data_header_size_hwords * 32 * 8;

   /* Haswell and later hardware ignores the "Render Stream Select" bits
    * from the 3DSTATE_STREAMOUT packet when the SOL stage is disabled,
    * and instead sends all primitives down the pipeline for rasterization.
    * If the SOL stage is enabled, "Render Stream Select" is honored and
    * primitives bound to non-zero streams are discarded after stream output.
    *
    * Since the only purpose of primives sent to non-zero streams is to
    * be recorded by transform feedback, we can simply discard all geometry
    * bound to these streams when transform feedback is disabled.
    */
   if (nir_intrinsic_stream_id(intr) > 0 &&
       !b->shader->info.has_transform_feedback_varyings) {
      return;
   }

   /* If there's less than 32 control data bits, we can just emit them at the
    * end.
    */
   if (control_data_header_size_bits > 32) {
      /* Is the index of the vertex we're emitting a multiple of 32?
       * If so, continue.
       */
      nir_def *should_push_vertex = nir_ieq_imm(
         b,
         nir_iand_imm(b, vertex_count_src->ssa,
                      32u - calc_control_data_bits_per_vertex(progdata) - 1),
         0);
      nir_push_if(b, should_push_vertex);
      {
         /* If the vertex index is 0, don't emit anything. */
         nir_push_if(b, nir_ine_imm(b, vertex_count_src->ssa, 0));
         {
            emit_gs_control_data_bits(progdata, b, control_data_bits,
                                      vertex_count_src->ssa);
         }
         nir_pop_if(b, NULL);

         /* reset control data bits to 0 so we can accum a new batch */
         nir_store_var(b, control_data_bits, nir_imm_int(b, 0u), ~0);
      }
      nir_pop_if(b, NULL);
   }
}

static void
set_vert_and_prim_count(nir_builder *b,
                        struct brw_gs_prog_data *progdata,
                        nir_intrinsic_instr *intr,
                        nir_variable *final_gs_vertex_count)
{
   nir_store_var(b, final_gs_vertex_count, intr->src[0].ssa, ~0);
   nir_instr_remove(&intr->instr);
}

static void
end_primitive(nir_builder *b,
              struct brw_gs_prog_data *progdata,
              nir_intrinsic_instr *intr,
              nir_variable *control_data_bits)
{
   /* Store a control data bit representing the fact that this vertex is the
    * last one in its primitive.
    */
   if (progdata->control_data_header_size_hwords &&
       progdata->control_data_format == GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_CUT) {

      nir_def *prev_count = nir_iadd_imm(b, intr->src[0].ssa, -1);
      nir_def *mask =
         nir_ishl(b, nir_imm_int(b, 1), nir_iand_imm(b, prev_count, 0b11111));
      nir_store_var(b, control_data_bits,
                    nir_ior(b, mask, nir_load_var(b, control_data_bits)), ~0);
   }

   nir_instr_remove(&intr->instr);
}

static bool
remove_gs_outputs_cb(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic == nir_intrinsic_emit_vertex_with_counter) {
      nir_instr_remove(&intr->instr);
      return true;
   }

   return false;
}

struct lower_gs_outputs_cb_data {
   nir_variable *control_data_bits;
   nir_variable *final_gs_vertex_count;
   struct brw_gs_prog_data *progdata;
};

static bool
lower_gs_outputs_cb(nir_builder *b, nir_intrinsic_instr *intr, void *_data)
{
   struct lower_gs_outputs_cb_data *data = _data;
   b->cursor = nir_before_instr(&intr->instr);

   if (intr->intrinsic == nir_intrinsic_emit_vertex_with_counter) {
      emit_gs_vertex(b, data->progdata, data->control_data_bits, intr);
   } else if (intr->intrinsic == nir_intrinsic_end_primitive_with_counter) {
      end_primitive(b, data->progdata, intr, data->control_data_bits);
   } else if (intr->intrinsic == nir_intrinsic_set_vertex_and_primitive_count) {
      set_vert_and_prim_count(b, data->progdata, intr,
                              data->final_gs_vertex_count);
   } else {
      return false;
   }

   return true;
}

static void
lower_fragment_outputs(nir_function_impl *impl,
                       const struct intel_device_info *devinfo,
                       unsigned nr_colour_regions,
                       bool replicate_alpha)
{
   nir_builder b_ = nir_builder_at(nir_after_impl(impl));
   nir_builder *b = &b_;

   struct frag_out_ctx ctx = { .replicate_alpha = replicate_alpha };
   nir_function_intrinsics_pass(impl, collect_fragment_output,
                                nir_metadata_control_flow, &ctx);

   nir_def *undef = nir_undef(b, 1, 32);
   if (!ctx.outputs[FRAG_RESULT_DEPTH])
      ctx.outputs[FRAG_RESULT_DEPTH] = undef;
   if (!ctx.outputs[FRAG_RESULT_STENCIL])
      ctx.outputs[FRAG_RESULT_STENCIL] = undef;
   if (!ctx.outputs[FRAG_RESULT_SAMPLE_MASK])
      ctx.outputs[FRAG_RESULT_SAMPLE_MASK] = undef;

   if (ctx.dual_blend) {
      gather_colour_components(b, &ctx, FRAG_RESULT_DATA0, undef);
      gather_colour_components(b, &ctx, FRAG_RESULT_DUAL_SRC_BLEND, undef);
      insert_rt_store(b, &ctx, 0);
      return;
   }
   ctx.outputs[FRAG_RESULT_DUAL_SRC_BLEND] = nir_undef(b, 4, 32);

   bool written = false;
   for (unsigned i = 0; i < nr_colour_regions; i++) {
      if (gather_colour_components(b, &ctx, FRAG_RESULT_DATA0 + i, undef)) {
         insert_rt_store(b, &ctx, i);
         written = true;
      }
   }

   if (!written) {
      /* Even if we don't write a RT, we still need to write alpha for
       * alpha-to-coverage and alpha testing. Optimize the other channels out.
       */
      for (unsigned c = 0; c < 3; c++)
         ctx.colour[FRAG_RESULT_DATA0][c] = nir_get_scalar(undef, 0);
      gather_colour_components(b, &ctx, FRAG_RESULT_DATA0, undef);

      insert_rt_store(b, &ctx, -1);
   }
}

static inline bool
nir_phi_merges_divergent_control_flow(nir_phi_instr *phi)
{
   nir_cf_node *prev = nir_cf_node_prev(&phi->instr.block->cf_node);

   if (!prev) {
      /* Continues are already lowered, so loop headers always return false */
      assert(phi->instr.block->cf_node.parent->type == nir_cf_node_loop);
      return false;
   } else if (prev->type == nir_cf_node_if) {
      return nir_src_is_divergent(&nir_cf_node_as_if(prev)->condition);
   } else {
      return nir_loop_is_divergent(nir_cf_node_as_loop(prev));
   }
}

static bool
lower_1bit_phi(nir_builder *b, nir_phi_instr *phi, void *_)
{
   if (phi->def.bit_size == 1 && nir_phi_merges_divergent_control_flow(phi)) {
      nir_foreach_phi_src(src, phi) {
         b->cursor = nir_after_block_before_jump(src->pred);
         nir_src_rewrite(&src->src, nir_b2b32(b, src->src.ssa));
      }

      b->cursor = nir_before_block_after_phis(phi->instr.block);
      phi->def.bit_size = 32;
      nir_def *repl = nir_b2b1(b, &phi->def);
      nir_def_rewrite_uses_after(&phi->def, repl);
      return true;
   }

   return false;
}

unsigned
jay_process_nir(const struct intel_device_info *devinfo,
                nir_shader *nir,
                union brw_any_prog_data *prog_data,
                union brw_any_prog_key *key,
                debug_archiver *archiver,
                bool *track_helpers)
{
   enum mesa_shader_stage stage = nir->info.stage;
   struct brw_compiler compiler = { .devinfo = devinfo };
   unsigned nr_packed_regs = 0;

   prog_data->base.ray_queries = nir->info.ray_queries;
   prog_data->base.stage = stage;
   // TODO: Make the driver do this?
   // prog_data->base.source_hash = params->source_hash;
   prog_data->base.total_shared = nir->info.shared_size;

   /* TODO: Real heuristic */
   bool do_simd32 = stage == MESA_SHADER_FRAGMENT ? INTEL_SIMD(FS, 32) :
                    stage == MESA_SHADER_COMPUTE  ? INTEL_SIMD(CS, 32) :
                                                    false;

   /* The 'Render Target Write message' section of the docs says:
    *
    *    "Output Stencil is not supported with SIMD16 Render Target
    *     Write Messages."
    *
    * Likewise for Xe2 at SIMD32.
    */
   if (stage == MESA_SHADER_FRAGMENT &&
       (nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL)))
      do_simd32 = false;

   if (stage == MESA_SHADER_FRAGMENT && nir->info.fs.color_is_dual_source)
      do_simd32 = false;

   /* SIMD splitting of ray queries is inefficient, avoid it when possible */
   if (prog_data->base.ray_queries && nir->info.min_subgroup_size < 32)
      do_simd32 = false;

   unsigned simd_width = do_simd32 ? (nir->info.api_subgroup_size ?: 32) : 16;

   brw_pass_tracker pt_ = {
      .nir = nir,
      .key = &key->base,
      .dispatch_width = simd_width,
      .compiler = &compiler,
      .archiver = archiver,
   }, *pt = &pt_;

   JAY_NIR_SNAPSHOT("first");

   brw_nir_apply_key(pt, &key->base, simd_width);

   if (stage == MESA_SHADER_VERTEX) {
      /* We only expect slot compaction to be disabled when using device
       * generated commands, to provide an independent 3DSTATE_VERTEX_ELEMENTS
       * programming. This should always be enabled together with VF component
       * packing to minimize the size of the payload.
       */
      assert(!key->vs.no_vf_slot_compaction || key->vs.vf_component_packing);

      /* When using Primitive Replication for multiview, each view gets its own
       * position slot.
       */
      const uint32_t pos_slots =
         (nir->info.per_view_outputs & VARYING_BIT_POS) ?
            MAX2(1, util_bitcount(key->base.view_mask)) :
            1;

      /* Only position is allowed to be per-view */
      assert(!(nir->info.per_view_outputs & ~VARYING_BIT_POS));

      brw_compute_vue_map(devinfo, &prog_data->vue.vue_map,
                          nir->info.outputs_written, key->base.vue_layout,
                          pos_slots);

      prog_data->vs.inputs_read = nir->info.inputs_read;
      prog_data->vs.double_inputs_read = nir->info.vs.double_inputs;
      prog_data->vs.no_vf_slot_compaction = key->vs.no_vf_slot_compaction;

      brw_nir_lower_vs_inputs(nir);
      brw_nir_lower_vue_outputs(nir);
      JAY_NIR_SNAPSHOT("after_lower_io");

      memset(prog_data->vs.vf_component_packing, 0,
             sizeof(prog_data->vs.vf_component_packing));
      if (key->vs.vf_component_packing) {
         nr_packed_regs = brw_nir_pack_vs_input(nir, &prog_data->vs);
      }

      /* Get constant offsets out of the way for proper clip/cull handling */
      JAY_NIR_PASS(nir_lower_io_to_scalar, nir_var_shader_out, NULL, NULL);
      /* Unroll multiview loops */
      JAY_NIR_PASS(nir_opt_loop_unroll);
      JAY_NIR_PASS(nir_opt_constant_folding);
      JAY_NIR_PASS(intel_nir_lower_shading_rate_output);
      JAY_NIR_PASS(brw_nir_lower_deferred_urb_writes, devinfo,
                   &prog_data->vue.vue_map, 0, 0);
   } else if (stage == MESA_SHADER_TESS_CTRL) {
      nir->info.outputs_written = key->tcs.outputs_written;
      nir->info.patch_outputs_written = key->tcs.patch_outputs_written;

      struct intel_vue_map input_vue_map;
      brw_compute_vue_map(devinfo, &input_vue_map, nir->info.inputs_read,
                          key->base.vue_layout, 1);
      brw_compute_tess_vue_map(&prog_data->vue.vue_map,
                               nir->info.outputs_written,
                               nir->info.patch_outputs_written,
                               key->tcs.separate_tess_vue_layout);

      brw_nir_lower_tcs_inputs(nir, devinfo, &input_vue_map);
      brw_nir_lower_tcs_outputs(nir, devinfo, &prog_data->vue.vue_map,
                                key->tcs._tes_primitive_mode);
      JAY_NIR_SNAPSHOT("after_lower_io");

      brw_nir_opt_vectorize_urb(pt);
      JAY_NIR_PASS(intel_nir_lower_patch_vertices_in, key->tcs.input_vertices);

      /* Compute URB entry size.  The maximum allowed URB entry size is 32k.
       * That divides up as follows:
       *
       *     32 bytes for the patch header (tessellation factors)
       *    480 bytes for per-patch varyings (a varying component is 4 bytes and
       *              gl_MaxTessPatchComponents = 120)
       *  16384 bytes for per-vertex varyings (a varying component is 4 bytes,
       *              gl_MaxPatchVertices = 32 and
       *              gl_MaxTessControlOutputComponents = 128)
       *
       *  15808 bytes left for varying packing overhead
       */
      const int num_per_patch_slots =
         prog_data->vue.vue_map.num_per_patch_slots;
      const int num_per_vertex_slots =
         prog_data->vue.vue_map.num_per_vertex_slots;
      unsigned output_size_bytes = 0;
      /* Note that the patch header is counted in num_per_patch_slots. */
      output_size_bytes += num_per_patch_slots * 16;
      output_size_bytes +=
         nir->info.tess.tcs_vertices_out * num_per_vertex_slots * 16;

      assert(output_size_bytes >= 1);
      assert(output_size_bytes < GFX7_MAX_HS_URB_ENTRY_SIZE_BYTES);

      /* URB entry sizes are stored as a multiple of 64 bytes. */
      prog_data->vue.urb_entry_size = align(output_size_bytes, 64) / 64;
   } else if (stage == MESA_SHADER_GEOMETRY) {
      nir_variable *control_data_bits =
         nir_variable_create(nir, nir_var_shader_temp, glsl_uint_type(),
                             "control_data_bits");
      nir_variable *final_gs_vertex_count =
         nir_variable_create(nir, nir_var_shader_temp, glsl_uint_type(),
                             "final_gs_vertex_count");

      struct intel_vue_map input_vue_map = { 0 };
      brw_compute_vue_map(devinfo, &input_vue_map, nir->info.inputs_read,
                          key->base.vue_layout, 1);

      const uint32_t pos_slots =
         (nir->info.per_view_outputs & VARYING_BIT_POS) ?
            MAX2(1, util_bitcount(key->base.view_mask)) :
            1;

      brw_compute_vue_map(devinfo, &prog_data->vue.vue_map,
                          nir->info.outputs_written, key->base.vue_layout,
                          pos_slots);

      brw_nir_apply_key(pt, &key->base, simd_width);

      brw_nir_lower_gs_inputs(nir, compiler.devinfo, &input_vue_map,
                              &prog_data->vue.urb_read_length);

      brw_nir_lower_vue_outputs(nir);
      JAY_NIR_SNAPSHOT("after_lower_io");

      brw_nir_opt_vectorize_urb(pt);
      nir_lower_gs_intrinsics(nir, 0);

      jay_populate_prog_data(devinfo, nir, prog_data, key, 0);

      /* Get constant offsets out of the way for proper clip/cull handling */
      JAY_NIR_PASS(nir_lower_io_to_scalar, nir_var_shader_out, NULL, NULL);
      /* Unroll multiview loops */
      JAY_NIR_PASS(nir_opt_loop_unroll);
      JAY_NIR_PASS(nir_opt_constant_folding);
      JAY_NIR_PASS(intel_nir_lower_shading_rate_output);

      nir_gs_count_vertices_and_primitives(nir,
                                           &prog_data->gs.static_vertex_count,
                                           NULL, NULL, 1u);

      struct lower_gs_outputs_cb_data data = {
         .control_data_bits = control_data_bits,
         .final_gs_vertex_count = final_gs_vertex_count,
         .progdata = &prog_data->gs,
      };
      JAY_NIR_PASS(nir_shader_intrinsics_pass, lower_gs_outputs_cb,
                   nir_metadata_none, &data);

      nir_builder at_end =
         nir_builder_at(nir_after_impl(nir_shader_get_entrypoint(nir)));

      nir_def *out_vert_count = nir_load_var(&at_end, final_gs_vertex_count);
      if (prog_data->gs.control_data_header_size_hwords > 0) {
         emit_gs_control_data_bits(&prog_data->gs, &at_end, control_data_bits,
                                   out_vert_count);
      }

      if (prog_data->gs.static_vertex_count <= 0) {
         nir_def *output_handle = nir_load_urb_output_handle_intel(&at_end);
         nir_store_urb_lsc_intel(&at_end, out_vert_count, output_handle,
                                 .base = 0);
      }

      uint32_t starting_urb_offset =
         2 * prog_data->gs.control_data_header_size_hwords +
         ((prog_data->gs.static_vertex_count == -1) ? 2 : 0);

      JAY_NIR_PASS(brw_nir_lower_deferred_urb_writes, devinfo,
                   &prog_data->vue.vue_map, starting_urb_offset,
                   2 * prog_data->gs.output_vertex_size_hwords);

      unsigned output_size_bytes = prog_data->gs.output_vertex_size_hwords *
                                   32 *
                                   nir->info.gs.vertices_out;
      output_size_bytes += 32 * prog_data->gs.control_data_header_size_hwords;

      /* Broadwell stores "Vertex Count" as a full 8 DWord (32 byte) URB output,
       * which comes before the control header.
       */
      output_size_bytes += 32;

      /* Shaders can technically set max_vertices = 0, at which point we
       * may have a URB size of 0 bytes.  Nothing good can come from that,
       * so enforce a minimum size.
       */
      if (output_size_bytes == 0)
         output_size_bytes = 1;

      /* URB entry sizes are stored as a multiple of 64 bytes in gfx7+. */
      prog_data->vue.urb_entry_size = align(output_size_bytes, 64) / 64;

      JAY_NIR_PASS(nir_lower_global_vars_to_local);
      JAY_NIR_PASS(nir_lower_vars_to_ssa);

      JAY_NIR_PASS(nir_shader_intrinsics_pass, remove_gs_outputs_cb,
                   nir_metadata_control_flow, NULL);
   } else if (stage == MESA_SHADER_TESS_EVAL) {
      const uint32_t pos_slots =
         (nir->info.per_view_outputs & VARYING_BIT_POS) ?
            MAX2(1, util_bitcount(key->base.view_mask)) :
            1;

      brw_compute_vue_map(devinfo, &prog_data->vue.vue_map,
                          nir->info.outputs_written, key->base.vue_layout,
                          pos_slots);

      struct intel_vue_map input_vue_map;

      brw_compute_tess_vue_map(&input_vue_map, nir->info.inputs_read,
                               nir->info.patch_inputs_read,
                               key->tes.separate_tess_vue_layout);

      brw_nir_lower_tes_inputs(nir, devinfo, &input_vue_map,
                               &prog_data->vue.urb_read_length);
      brw_nir_lower_vue_outputs(nir);
      BRW_NIR_SNAPSHOT("after_lower_io");

      brw_nir_opt_vectorize_urb(pt);
      BRW_NIR_PASS(intel_nir_lower_patch_vertices_tes);

      BRW_NIR_PASS(brw_nir_lower_deferred_urb_writes, devinfo,
                   &prog_data->vue.vue_map, 0, 0);

      unsigned output_size_bytes = prog_data->vue.vue_map.num_slots * 4 * 4;

      assert(output_size_bytes >= 1);
      assert(output_size_bytes <= GFX7_MAX_DS_URB_ENTRY_SIZE_BYTES);

      prog_data->tes.include_primitive_id =
         BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);

      /* URB entry sizes are stored as a multiple of 64 bytes. */
      prog_data->vue.urb_entry_size = align(output_size_bytes, 64) / 64;

      brw_fill_tess_info_from_shader_info(&prog_data->tes.tess_info,
                                          &nir->info);
   } else if (stage == MESA_SHADER_FRAGMENT) {
      assert(key->fs.mesh_input == INTEL_NEVER && "todo");
      brw_nir_lower_fs_inputs(nir, devinfo, &key->fs);
      brw_nir_lower_fs_outputs(nir);
      JAY_NIR_SNAPSHOT("after_lower_io");
      JAY_NIR_PASS(nir_lower_io_to_scalar, nir_var_shader_in, NULL, NULL);

      if (!brw_can_coherent_fb_fetch(devinfo))
         JAY_NIR_PASS(brw_nir_lower_fs_load_output, &key->fs);

      /* Do this lowering before jay_populate_prog_data(). */
      JAY_NIR_PASS(nir_opt_frag_coord_to_pixel_coord);
      JAY_NIR_PASS(nir_lower_frag_coord_to_pixel_coord);
      JAY_NIR_PASS(nir_opt_barycentric, true);
      JAY_NIR_PASS(nir_opt_constant_folding);

      if (key->fs.alpha_to_coverage != INTEL_NEVER)
         JAY_NIR_PASS(brw_nir_lower_alpha_to_coverage);

      lower_fragment_outputs(nir_shader_get_entrypoint(nir), devinfo,
                             key->fs.nr_color_regions,
                             key->fs.alpha_test_replicate_alpha);

      /* nir_lower_terminate_to_demote will hamper our ability to schedule
       * terminates (since it turns them into real control flow), so run
       * nir_opt_move_discards_to_top first as a prepass. That should help
       * scheduling demotes too (which is more important).
       */
      JAY_NIR_PASS(nir_opt_move_discards_to_top);
      JAY_NIR_PASS(nir_lower_terminate_to_demote);

      brw_nir_cleanup_pre_fs_prog_data(pt);

      // TODO
      // JAY_NIR_PASS(brw_nir_move_interpolation_to_top);

      /* Do this before lower_fs_config_intel so that the pass has the right
       * information.
       */
      jay_populate_prog_data(devinfo, nir, prog_data, key, 0);

      if (prog_data->fs.coarse_pixel_dispatch)
         JAY_NIR_PASS(brw_nir_lower_frag_coord_z, devinfo);

      JAY_NIR_PASS(nir_shader_intrinsics_pass, lower_frag_coord,
                   nir_metadata_control_flow, NULL);

      JAY_NIR_PASS(brw_nir_lower_fs_config_intel, &key->fs, &prog_data->fs);
   }

   JAY_NIR_PASS(nir_opt_phi_to_bool);
   brw_postprocess_nir_opts(pt);

   JAY_NIR_PASS(nir_shader_intrinsics_pass, jay_nir_lower_simd,
                nir_metadata_control_flow, &simd_width);
   JAY_NIR_PASS(nir_opt_algebraic_late);
   JAY_NIR_PASS(intel_nir_opt_peephole_imul32x16);

   nir_divergence_analysis(nir);
   JAY_NIR_PASS(nir_shader_phi_pass, lower_1bit_phi, nir_metadata_control_flow,
                NULL);

   /* Late postprocess while remaining in SSA */
   /* Run fsign lowering again after the last time brw_nir_optimize is called.
    * As is the case with conversion lowering (below), brw_nir_optimize can
    * create additional fsign instructions.
    */
   JAY_NIR_PASS(jay_nir_lower_bfloat_math);
   JAY_NIR_PASS(jay_nir_lower_fsign);
   JAY_NIR_PASS(jay_nir_lower_bool);
   JAY_NIR_PASS(nir_opt_cse);
   JAY_NIR_PASS(nir_opt_dce);

   /* Run nir_split_conversions only after the last tiem
    * brw_nir_optimize is called. Various optimizations invoked there can
    * rematerialize the conversions that the lowering pass eliminates.
    */
   const nir_split_conversions_options split_conv_opts = {
      .callback = intel_nir_split_conversions_cb,
   };
   JAY_NIR_PASS(nir_split_conversions, &split_conv_opts);

   /* Do this only after the last opt_gcm. GCM will undo this lowering. */
   if (stage == MESA_SHADER_FRAGMENT) {
      JAY_NIR_PASS(intel_nir_lower_non_uniform_barycentric_at_sample);
   }

   JAY_NIR_PASS(nir_opt_constant_folding);
   JAY_NIR_PASS(nir_lower_load_const_to_scalar);
   JAY_NIR_PASS(nir_lower_all_phis_to_scalar);
   JAY_NIR_PASS(nir_opt_copy_prop);
   JAY_NIR_PASS(nir_opt_dce);

   /* Jay requires LCSSA for correctness reading convergent loop-dependent
    * values outside of a divergent loop. Converting to LCSSA inserts the
    * required divergent 1-source phi after the loop.
    */
   JAY_NIR_PASS(nir_convert_to_lcssa, true, true);
   nir_divergence_analysis(nir);
   JAY_NIR_PASS(nir_shader_phi_pass, lower_1bit_phi, nir_metadata_control_flow,
                NULL);
   JAY_NIR_PASS(jay_nir_lower_bool);

   /* Run divergence analysis at the end */
   nir_sweep(nir);
   nir_divergence_analysis(nir);

   if (stage == MESA_SHADER_FRAGMENT) {
      /* Certain features require tracking helpers for correctness */
      nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
      *track_helpers |= nir->info.fs.uses_discard || nir->info.writes_memory;
      *track_helpers |= BITSET_TEST(nir->info.system_values_read,
                                    SYSTEM_VALUE_HELPER_INVOCATION);

      /* ...but this is more subtle. nir_opt_load_skip_helpers flags texturing
       * operations that we can skip for bandwidth savings.  We need divergence
       * info for this, so we run late.
       *
       * We may or may not want to force track_helpers on if this makes
       * progress. Possibly driconf'ing on furmark makes sense.
       */
      struct nir_opt_load_skip_helpers_options skip_helpers = {
         .no_add_divergence = true
      };
      JAY_NIR_PASS(nir_opt_load_skip_helpers, &skip_helpers);
   } else {
      jay_populate_prog_data(devinfo, nir, prog_data, key, nr_packed_regs);
   }

   /* This must be the very last pass since nir_print itself will reindex! */
   nj_index_ssa_defs(nir);
   return simd_width;
}
