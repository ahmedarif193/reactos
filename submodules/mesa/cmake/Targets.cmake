# SPDX-License-Identifier: MIT
# Explicit source lists for the vendored ReactOS Mesa configuration.

# mesa_util_c11
add_library(mesa_util_c11 STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(mesa_util_c11)
target_sources(mesa_util_c11 PRIVATE
    "${PROJECT_SOURCE_DIR}/src/c11/impl/time.c"
    "${PROJECT_SOURCE_DIR}/src/c11/impl/threads_win32.c"
    "${PROJECT_SOURCE_DIR}/src/c11/impl/threads_win32_tls_callback.cpp"
)
target_include_directories(mesa_util_c11 PRIVATE
    "${PROJECT_BINARY_DIR}/src/c11/impl"
    "${PROJECT_SOURCE_DIR}/src/c11/impl"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
)
target_compile_options(mesa_util_c11 PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
)

# blake3
add_library(blake3 STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(blake3)
target_sources(blake3 PRIVATE
    "${PROJECT_SOURCE_DIR}/src/util/blake3/blake3.c"
    "${PROJECT_SOURCE_DIR}/src/util/blake3/blake3_dispatch.c"
    "${PROJECT_SOURCE_DIR}/src/util/blake3/blake3_portable.c"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_sources(blake3 PRIVATE
        "${PROJECT_SOURCE_DIR}/src/util/blake3/blake3_neon.c"
    )
endif()
if(MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
    target_sources(blake3 PRIVATE
        "${PROJECT_SOURCE_DIR}/src/util/blake3/blake3_sse2_x86-64_windows_gnu.S"
        "${PROJECT_SOURCE_DIR}/src/util/blake3/blake3_sse41_x86-64_windows_gnu.S"
        "${PROJECT_SOURCE_DIR}/src/util/blake3/blake3_avx2_x86-64_windows_gnu.S"
        "${PROJECT_SOURCE_DIR}/src/util/blake3/blake3_avx512_x86-64_windows_gnu.S"
    )
endif()
target_include_directories(blake3 PRIVATE
    "${PROJECT_BINARY_DIR}/src/util/blake3"
    "${PROJECT_SOURCE_DIR}/src/util/blake3"
)
target_compile_options(blake3 PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-error>"
)
if(MESA_PROFILE STREQUAL "i386")
    target_compile_options(blake3 PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-DBLAKE3_NO_SSE2>"
        "$<$<COMPILE_LANGUAGE:C>:-DBLAKE3_NO_SSE41>"
        "$<$<COMPILE_LANGUAGE:C>:-DBLAKE3_NO_AVX2>"
        "$<$<COMPILE_LANGUAGE:C>:-DBLAKE3_NO_AVX512>"
    )
endif()

# mesa_util_simd
add_library(mesa_util_simd STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(mesa_util_simd)
target_sources(mesa_util_simd PRIVATE
    "${PROJECT_SOURCE_DIR}/src/util/streaming-load-memcpy.c"
)
target_include_directories(mesa_util_simd PRIVATE
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_SOURCE_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
)
target_compile_options(mesa_util_simd PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=date-time>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-alignof-expression>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=ignored-qualifiers>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-fallthrough>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack-suspicious-include>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=sizeof-array-div>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=string-plus-int>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=unreachable-code-loop-increment>"
)
if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
    target_compile_options(mesa_util_simd PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-msse4.1>"
    )
endif()

# mesa_util
add_library(mesa_util STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(mesa_util)
target_sources(mesa_util PRIVATE
    "${PROJECT_SOURCE_DIR}/src/util/anon_file.c"
    "${PROJECT_SOURCE_DIR}/src/util/bitscan.c"
    "${PROJECT_SOURCE_DIR}/src/util/blob.c"
    "${PROJECT_SOURCE_DIR}/src/util/build_id.c"
    "${PROJECT_SOURCE_DIR}/src/util/cnd_monotonic.c"
    "${PROJECT_SOURCE_DIR}/src/util/compress.c"
    "${PROJECT_SOURCE_DIR}/src/util/thread_sched.c"
    "${PROJECT_SOURCE_DIR}/src/util/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/util/dag.c"
    "${PROJECT_SOURCE_DIR}/src/util/disk_cache.c"
    "${PROJECT_SOURCE_DIR}/src/util/disk_cache_os.c"
    "${PROJECT_SOURCE_DIR}/src/util/double.c"
    "${PROJECT_SOURCE_DIR}/src/util/fast_idiv_by_const.c"
    "${PROJECT_SOURCE_DIR}/src/util/float8.c"
    "${PROJECT_SOURCE_DIR}/src/util/fossilize_db.c"
    "${PROJECT_SOURCE_DIR}/src/util/futex.c"
    "${PROJECT_SOURCE_DIR}/src/util/half_float.c"
    "${PROJECT_SOURCE_DIR}/src/util/hash_table.c"
    "${PROJECT_SOURCE_DIR}/src/util/helpers.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_idalloc.c"
    "${PROJECT_SOURCE_DIR}/src/util/log.c"
    "${PROJECT_SOURCE_DIR}/src/util/lut.c"
    "${PROJECT_SOURCE_DIR}/src/util/memstream.c"
    "${PROJECT_SOURCE_DIR}/src/util/mesa-blake3.c"
    "${PROJECT_SOURCE_DIR}/src/util/os_time.c"
    "${PROJECT_SOURCE_DIR}/src/util/os_file.c"
    "${PROJECT_SOURCE_DIR}/src/util/os_file_notify.c"
    "${PROJECT_SOURCE_DIR}/src/util/os_memory_fd.c"
    "${PROJECT_SOURCE_DIR}/src/util/os_misc.c"
    "${PROJECT_SOURCE_DIR}/src/util/os_socket.c"
    "${PROJECT_SOURCE_DIR}/src/util/pb_slab.c"
    "${PROJECT_SOURCE_DIR}/src/util/perf/u_trace.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_process.c"
    "${PROJECT_SOURCE_DIR}/src/util/rwlock.c"
    "${PROJECT_SOURCE_DIR}/src/util/ralloc.c"
    "${PROJECT_SOURCE_DIR}/src/util/rand_xor.c"
    "${PROJECT_SOURCE_DIR}/src/util/range_minimum_query.c"
    "${PROJECT_SOURCE_DIR}/src/util/rb_tree.c"
    "${PROJECT_SOURCE_DIR}/src/util/register_allocate.c"
    "${PROJECT_SOURCE_DIR}/src/util/rgtc.c"
    "${PROJECT_SOURCE_DIR}/src/util/set.c"
    "${PROJECT_SOURCE_DIR}/src/util/simple_mtx.c"
    "${PROJECT_SOURCE_DIR}/src/util/slab.c"
    "${PROJECT_SOURCE_DIR}/src/util/softfloat.c"
    "${PROJECT_SOURCE_DIR}/src/util/sparse_array.c"
    "${PROJECT_SOURCE_DIR}/src/util/string_buffer.c"
    "${PROJECT_SOURCE_DIR}/src/util/strndup.c"
    "${PROJECT_SOURCE_DIR}/src/util/strtod.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_atomic.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_call_once.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_dl.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_dynarray.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_hash_table.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_queue.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_range_remap.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_shader_variant_cache.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_string.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_thread.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_vector.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_math.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_mm.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_debug.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_debug_memory.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_cpu_detect.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_printf.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_worklist.c"
    "${PROJECT_SOURCE_DIR}/src/util/vl_zscan_data.c"
    "${PROJECT_SOURCE_DIR}/src/util/vma.c"
    "${PROJECT_SOURCE_DIR}/src/util/mesa_cache_db.c"
    "${PROJECT_SOURCE_DIR}/src/util/mesa_cache_db_multipart.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_bptc.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_etc.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_fxt1.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_latc.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_other.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_rgtc.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_s3tc.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_tests.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_unpack_neon.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_yuv.c"
    "${PROJECT_SOURCE_DIR}/src/util/format/u_format_zs.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_debug_stack.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_debug_symbol.c"
    "${PROJECT_BINARY_DIR}/src/util/format/u_format_table.c"
    "${PROJECT_BINARY_DIR}/src/util/format_srgb.c"
    "${PROJECT_SOURCE_DIR}/src/util/u_qsort.cpp"
    "${PROJECT_SOURCE_DIR}/src/util/texcompress_astc_luts.cpp"
    "${PROJECT_SOURCE_DIR}/src/util/texcompress_astc_luts_wrap.cpp"
    "${PROJECT_SOURCE_DIR}/src/util/texcompress_astc.cpp"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_sources(mesa_util PRIVATE
        "${PROJECT_SOURCE_DIR}/src/util/cache_ops_aarch64.c"
    )
endif()
target_include_directories(mesa_util PRIVATE
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_SOURCE_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/src/util/format"
    "${PROJECT_SOURCE_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(mesa_util PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(mesa_util PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=date-time>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-alignof-expression>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=ignored-qualifiers>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-fallthrough>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack-suspicious-include>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=sizeof-array-div>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=string-plus-int>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=unreachable-code-loop-increment>"
)

# xmlconfig
add_library(xmlconfig STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(xmlconfig)
target_sources(xmlconfig PRIVATE
    "${PROJECT_SOURCE_DIR}/src/util/xmlconfig.c"
)
target_include_directories(xmlconfig PRIVATE
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_SOURCE_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(xmlconfig PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(xmlconfig PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-DNO_REGEX>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=date-time>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-alignof-expression>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=ignored-qualifiers>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-fallthrough>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack-suspicious-include>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=sizeof-array-div>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=string-plus-int>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=unreachable-code-loop-increment>"
    "$<$<COMPILE_LANGUAGE:C>:-DWITH_XMLCONFIG=0>"
)
target_compile_definitions(xmlconfig PRIVATE
    "SYSCONFDIR=\"${CMAKE_INSTALL_FULL_SYSCONFDIR}\""
    "DATADIR=\"${CMAKE_INSTALL_FULL_DATADIR}\"")

# compiler
add_library(compiler STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(compiler)
target_sources(compiler PRIVATE
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl_types.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/shader_enums.c"
    "${PROJECT_BINARY_DIR}/src/compiler/builtin_types.c"
)
target_include_directories(compiler PRIVATE
    "${PROJECT_BINARY_DIR}/src/compiler"
    "${PROJECT_SOURCE_DIR}/src/compiler"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
)
target_compile_options(compiler PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=date-time>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-alignof-expression>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=ignored-qualifiers>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-fallthrough>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack-suspicious-include>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=sizeof-array-div>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=string-plus-int>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=unreachable-code-loop-increment>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
)

# nir
add_library(nir STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(nir)
target_sources(nir PRIVATE
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_builder.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_builtin_builder.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_convert_address_format.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_clip_cull_distance_io_utils.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_clone.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_control_flow.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_deref.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_divergence_analysis.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_dominance.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_dominance_lca.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_downgrade_pls_vars.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_fixup_is_exported.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_format_convert.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_from_ssa.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_functions.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_gather_info.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_gather_output_deps.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_gather_tcs_info.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_gather_types.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_gather_xfb_info.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_group_loads.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_gs_count_vertices.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_inline_sysval.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_inline_uniforms.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_instr_set.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_io_add_xfb_info.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_legacy.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_linking_helpers.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_liveness.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_loop_analyze.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_abort.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_alu.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_alu_width.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_alpha.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_amul.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_array_deref_of_vec.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_atomics_to_ssbo.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_bitmap.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_blend.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_bool_to_float.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_bool_to_int32.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_calls_to_builtins.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_cl_images.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_clamp_color_outputs.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_clip.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_clip_disable.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_clip_halfz.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_const_arrays_to_uniforms.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_continue_constructs.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_convert_alu_types.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_cooperative_matrix.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_variable_initializers.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_discard_if.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_double_ops.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_explicit_io.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_fb_read.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_flatshade.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_floats.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_flrp.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_fp16_conv.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_fragcoord_wtrans.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_frag_coord_to_pixel_coord.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_fragcolor.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_frexp.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_global_vars_to_local.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_goto_ifs.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_gs_intrinsics.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_halt_to_return.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_helper_writes.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_load_const_to_scalar.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_locals_to_regs.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_idiv.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_image.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_image_atomics_to_global.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_indirect_derefs_to_if_else_trees.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_input_attachments.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_int64.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_interpolation.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_int_to_float.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_io.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_io_array_vars_to_elements.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_io_indirect_loads.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_io_vars_to_temporaries.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_io_to_scalar.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_io_vars_to_scalar.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_is_helper_invocation.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_multiview.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_mediump.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_mem_access_bit_sizes.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_memcpy.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_memory_model.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_non_uniform_access.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_packing.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_passthrough_edgeflags.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_patch_vertices.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_phis_to_scalar.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_pntc_ytransform.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_point_size.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_point_smooth.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_poly_line_smooth.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_printf.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_reg_intrinsics_to_ssa.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_readonly_images_to_tex.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_returns.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_robust_access.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_samplers.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_sample_shading.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_scratch.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_scratch_to_var.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_shader_calls.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_single_sampled.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_ssbo.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_subgroups.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_system_values.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_task_shader.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_terminate_to_demote.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_tess_coord_z.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_tex_shadow.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_tex.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_texcoord_replace.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_texcoord_replace_late.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_two_sided_color.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_undef_to_zero.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_vars_to_ssa.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_var_copies.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_vec_to_regs.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_vec3_to_vec4.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_view_index_to_device_index.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_viewport_transform.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_wpos_center.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_wpos_ytransform.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_wrmasks.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_bit_size.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_ubo_vec4.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_uniforms_to_ubo.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_workgroup_size.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_sysvals_to_varyings.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_metadata.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_mod_analysis.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_move_output_stores_to_end.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_move_vec_src_uses_to_dest.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_normalize_cubemap_coords.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_normalize_sin_cos.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_access.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_barriers.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_barycentric.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_call.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_clip_cull_const.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_combine_stores.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_comparison_pre.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_constant_folding.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_copy_prop_vars.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_copy_propagate.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_cse.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_dce.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_dead_cf.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_dead_write_vars.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_find_array_copies.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_fp_math_ctrl.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_frag_coord_to_pixel_coord.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_fragdepth.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_gcm.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_generate_bfi.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_idiv_const.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_if.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_intrinsics.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_large_constants.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_licm.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_load_skip_helpers.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_load_store_vectorize.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_loop.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_loop_unroll.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_memcpy.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_move.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_move_discards_to_top.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_move_to_top.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_mqsad.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_non_uniform_access.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_offsets.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_peephole_select.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_phi_precision.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_phi_to_bool.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_preamble.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_ray_queries.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_reassociate.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_reassociate_bfi.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_rematerialize_compares.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_remove_phis.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_shared_vars_to_subgroup.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_shrink_stores.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_shrink_vectors.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_sink.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_undef.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_uniform_atomics.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_uniform_subgroup.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_uub.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_varyings.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_vectorize.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_vectorize_io.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_opt_vectorize_io_vars.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_passthrough_gs.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_passthrough_tcs.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_phi_builder.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_print.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_propagate_invariant.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_range_analysis.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_recompute_io_bases.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_remove_dead_variables.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_remove_outputs.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_remove_tex_shadow.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_repair_ssa.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_scale_fdiv.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_schedule.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_search.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_separate_merged_clip_cull_io.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_serialize.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_shader_bisect.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_split_64bit_vec3_and_vec4.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_split_conversions.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_split_per_member_structs.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_split_var_copies.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_split_vars.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_sweep.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_to_lcssa.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_trivialize_registers.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_unlower_io_to_vars.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_use_dominance.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_validate.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_worklist.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir/nir_lower_atomics.c"
    "${PROJECT_BINARY_DIR}/src/compiler/nir/nir_opt_algebraic.c"
    "${PROJECT_BINARY_DIR}/src/compiler/nir/nir_opcodes.c"
    "${PROJECT_BINARY_DIR}/src/compiler/nir/nir_constant_expressions.c"
    "${PROJECT_BINARY_DIR}/src/compiler/nir/nir_intrinsics.c"
)
target_include_directories(nir PRIVATE
    "${PROJECT_BINARY_DIR}/src/compiler/nir"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/src/compiler"
    "${PROJECT_SOURCE_DIR}/src/compiler"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(nir PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(nir PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=date-time>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-alignof-expression>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=ignored-qualifiers>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-fallthrough>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack-suspicious-include>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=sizeof-array-div>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=string-plus-int>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=unreachable-code-loop-increment>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
)

# vtn
add_library(vtn STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(vtn)
target_sources(vtn PRIVATE
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/gl_spirv.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/spirv_to_nir.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/vtn_alu.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/vtn_amd.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/vtn_cfg.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/vtn_cmat.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/vtn_debug.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/vtn_glsl450.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/vtn_opencl.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/vtn_structured_cfg.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/vtn_subgroup.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/vtn_variables.c"
    "${PROJECT_BINARY_DIR}/src/compiler/spirv/spirv_info.c"
    "${PROJECT_BINARY_DIR}/src/compiler/spirv/vtn_gather_types.c"
)
target_include_directories(vtn PRIVATE
    "${PROJECT_BINARY_DIR}/src/compiler/spirv"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/src/compiler/nir"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir"
    "${PROJECT_BINARY_DIR}/src/compiler"
    "${PROJECT_SOURCE_DIR}/src/compiler"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(vtn PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(vtn PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=date-time>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-alignof-expression>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=ignored-qualifiers>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-fallthrough>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pragma-pack-suspicious-include>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=sizeof-array-div>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=string-plus-int>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=unreachable-code-loop-increment>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
)

# glcpp
add_library(glcpp STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(glcpp)
target_sources(glcpp PRIVATE
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/glcpp/pp.c"
    "${PROJECT_BINARY_DIR}/src/compiler/glsl/glcpp/glcpp-lex.c"
    "${PROJECT_BINARY_DIR}/src/compiler/glsl/glcpp/glcpp-parse.c"
)
target_include_directories(glcpp PRIVATE
    "${PROJECT_BINARY_DIR}/src/compiler/glsl/glcpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/glcpp"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/src/mesa"
    "${PROJECT_SOURCE_DIR}/src/mesa"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(glcpp PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(glcpp PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
)

# glsl
add_library(glsl STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(glsl)
target_sources(glsl PRIVATE
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ast_array_index.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ast_expr.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ast_function.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ast_to_hir.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ast_type.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/builtin_functions.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/builtin_types.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/builtin_variables.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/glsl_parser_extras.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/glsl_symbol_table.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/glsl_to_nir.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/hir_field_selection.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_basic_block.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_builder.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_clone.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_constant_expression.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_expression_flattening.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_function_detect_recursion.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_function.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_hierarchical_visitor.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_hv_accept.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_print_visitor.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_rvalue_visitor.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_validate.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/ir_variable_refcount.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/linker_util.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/lower_builtins.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/lower_instructions.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/lower_jumps.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/lower_mat_op_to_vec.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/lower_packing_builtins.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/lower_precision.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/lower_subroutine.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/lower_vec_index_to_cond_assign.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/lower_vector_derefs.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/opt_algebraic.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/opt_dead_builtin_variables.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/opt_dead_code.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/opt_flatten_nested_if_blocks.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/opt_function_inlining.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/opt_if_simplification.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/opt_minmax.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/opt_rebalance_tree.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/opt_tree_grafting.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/propagate_invariance.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/string_to_uint_map.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/serialize.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/shader_cache.cpp"
    "${PROJECT_BINARY_DIR}/src/compiler/glsl/glsl_parser.cpp"
    "${PROJECT_BINARY_DIR}/src/compiler/glsl/glsl_lexer.cpp"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_detect_function_recursion.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_lower_atomics.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_lower_images.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_lower_blend_equation_advanced.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_lower_buffers.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_lower_discard_flow.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_lower_named_interface_blocks.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_lower_packed_varyings.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_lower_samplers.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_lower_samplers_as_deref.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_lower_xfb_varying.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_link_atomics.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_link_functions.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_link_interface_blocks.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_link_uniform_blocks.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_link_uniform_initializers.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_link_uniforms.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_link_varyings.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_link_xfb.c"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl/gl_nir_linker.c"
)
target_include_directories(glsl PRIVATE
    "${PROJECT_BINARY_DIR}/src/compiler/glsl"
    "${PROJECT_SOURCE_DIR}/src/compiler/glsl"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/src/mesa"
    "${PROJECT_SOURCE_DIR}/src/mesa"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
    "${PROJECT_BINARY_DIR}/src/compiler/nir"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir"
    "${PROJECT_BINARY_DIR}/src/compiler"
    "${PROJECT_SOURCE_DIR}/src/compiler"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(glsl PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(glsl PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
)
target_compile_options(glsl PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:CXX>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wgnu-pointer-arith>"
)

# broadcom_cle
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    add_library(broadcom_cle STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(broadcom_cle)
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_sources(broadcom_cle PRIVATE
            "${PROJECT_SOURCE_DIR}/src/broadcom/cle/v3d_decoder.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_include_directories(broadcom_cle PRIVATE
            "${PROJECT_BINARY_DIR}/src/broadcom/cle"
            "${PROJECT_SOURCE_DIR}/src/broadcom/cle"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_BINARY_DIR}/src/broadcom"
            "${PROJECT_SOURCE_DIR}/src/broadcom"
            "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_compile_options(broadcom_cle PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
        )
    endif()
endif()

# broadcom_compiler
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    add_library(broadcom_compiler STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(broadcom_compiler)
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_sources(broadcom_compiler PRIVATE
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/nir_to_vir.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_dump.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_live_variables.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_opt_alu.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_opt_constant_alu.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_opt_copy_propagate.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_opt_dead_code.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_opt_redundant_flags.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_opt_redundant_setnnmode.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_opt_small_immediates.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_register_allocate.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/vir_to_qpu.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/qpu_schedule.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/qpu_validate.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_tex.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_nir_lower_blend.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_nir_lower_io.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_nir_lower_image_load_store.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_nir_lower_line_smooth.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_nir_lower_load_store_bitsize.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_nir_lower_logic_ops.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_nir_lower_null_descriptors.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_nir_lower_scratch.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_nir_lower_txf_ms.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_nir_lower_load_output.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler/v3d_packing.c"
            "${PROJECT_BINARY_DIR}/src/broadcom/compiler/v3d_nir_lower_algebraic.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_include_directories(broadcom_compiler PRIVATE
            "${PROJECT_BINARY_DIR}/src/broadcom/compiler"
            "${PROJECT_SOURCE_DIR}/src/broadcom/compiler"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src/gallium/include"
            "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
            "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
            "${PROJECT_BINARY_DIR}/src/broadcom"
            "${PROJECT_SOURCE_DIR}/src/broadcom"
            "${PROJECT_BINARY_DIR}/src/broadcom/cle"
            "${PROJECT_SOURCE_DIR}/src/broadcom/cle"
            "${PROJECT_BINARY_DIR}/src/compiler/nir"
            "${PROJECT_SOURCE_DIR}/src/compiler/nir"
            "${PROJECT_BINARY_DIR}/src/compiler"
            "${PROJECT_SOURCE_DIR}/src/compiler"
            "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
            "${PROJECT_BINARY_DIR}/src/util"
            "${PROJECT_BINARY_DIR}/src/util/format"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_compile_options(broadcom_compiler PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
        )
    endif()
endif()

# broadcom_qpu
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    add_library(broadcom_qpu STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(broadcom_qpu)
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_sources(broadcom_qpu PRIVATE
            "${PROJECT_SOURCE_DIR}/src/broadcom/qpu/qpu_disasm.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/qpu/qpu_instr.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/qpu/qpu_pack.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_include_directories(broadcom_qpu PRIVATE
            "${PROJECT_BINARY_DIR}/src/broadcom/qpu"
            "${PROJECT_SOURCE_DIR}/src/broadcom/qpu"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_BINARY_DIR}/src/broadcom"
            "${PROJECT_SOURCE_DIR}/src/broadcom"
            "${PROJECT_BINARY_DIR}/src/broadcom/cle"
            "${PROJECT_SOURCE_DIR}/src/broadcom/cle"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_compile_options(broadcom_qpu PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
        )
    endif()
endif()

# broadcom-v42
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    add_library(broadcom-v42 STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(broadcom-v42)
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_sources(broadcom-v42 PRIVATE
            "${PROJECT_SOURCE_DIR}/src/broadcom/clif/v3dx_dump.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_include_directories(broadcom-v42 PRIVATE
            "${PROJECT_BINARY_DIR}/src/broadcom"
            "${PROJECT_SOURCE_DIR}/src/broadcom"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_BINARY_DIR}/src/broadcom/cle"
            "${PROJECT_SOURCE_DIR}/src/broadcom/cle"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_compile_options(broadcom-v42 PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
            "$<$<COMPILE_LANGUAGE:C>:-DV3D_VERSION=42>"
        )
    endif()
endif()

# broadcom-v71
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    add_library(broadcom-v71 STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(broadcom-v71)
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_sources(broadcom-v71 PRIVATE
            "${PROJECT_SOURCE_DIR}/src/broadcom/clif/v3dx_dump.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_include_directories(broadcom-v71 PRIVATE
            "${PROJECT_BINARY_DIR}/src/broadcom"
            "${PROJECT_SOURCE_DIR}/src/broadcom"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_BINARY_DIR}/src/broadcom/cle"
            "${PROJECT_SOURCE_DIR}/src/broadcom/cle"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_compile_options(broadcom-v71 PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
            "$<$<COMPILE_LANGUAGE:C>:-DV3D_VERSION=71>"
        )
    endif()
endif()

# v3d_neon
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    add_library(v3d_neon STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(v3d_neon)
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_sources(v3d_neon PRIVATE
            "${PROJECT_SOURCE_DIR}/src/broadcom/common/v3d_tiling.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_include_directories(v3d_neon PRIVATE
            "${PROJECT_BINARY_DIR}/src/broadcom"
            "${PROJECT_SOURCE_DIR}/src/broadcom"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src/broadcom/cle"
            "${PROJECT_SOURCE_DIR}/src/broadcom/cle"
            "${PROJECT_BINARY_DIR}/src/compiler/nir"
            "${PROJECT_SOURCE_DIR}/src/compiler/nir"
            "${PROJECT_BINARY_DIR}/src/compiler"
            "${PROJECT_SOURCE_DIR}/src/compiler"
            "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
            "${PROJECT_BINARY_DIR}/src/util"
            "${PROJECT_BINARY_DIR}/src/util/format"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_compile_options(v3d_neon PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
            "$<$<COMPILE_LANGUAGE:C>:-DV3D_BUILD_NEON>"
        )
    endif()
endif()

# broadcom_v3d
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    add_library(broadcom_v3d STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(broadcom_v3d)
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_sources(broadcom_v3d PRIVATE
            "${PROJECT_SOURCE_DIR}/src/broadcom/common/v3d_debug.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/common/v3d_device_info.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/common/v3d_submit_util.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/clif/clif_dump.c"
            "${PROJECT_SOURCE_DIR}/src/broadcom/common/v3d_util.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_include_directories(broadcom_v3d PRIVATE
            "${PROJECT_BINARY_DIR}/src/broadcom"
            "${PROJECT_SOURCE_DIR}/src/broadcom"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_BINARY_DIR}/src/broadcom/cle"
            "${PROJECT_SOURCE_DIR}/src/broadcom/cle"
            "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
            "${PROJECT_BINARY_DIR}/src/util"
            "${PROJECT_BINARY_DIR}/src/util/format"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_compile_options(broadcom_v3d PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_sources(broadcom_v3d PRIVATE
            "$<TARGET_OBJECTS:broadcom_compiler>"
            "$<TARGET_OBJECTS:broadcom_qpu>"
            "$<TARGET_OBJECTS:broadcom-v42>"
            "$<TARGET_OBJECTS:broadcom-v71>"
        )
    endif()
endif()

# glapi_bridge
add_library(glapi_bridge STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(glapi_bridge)
target_sources(glapi_bridge PRIVATE
    "${PROJECT_SOURCE_DIR}/src/mesa/glapi/glapi/libgl_public.c"
)
target_include_directories(glapi_bridge PRIVATE
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi"
    "${PROJECT_SOURCE_DIR}/src/mesa/glapi/glapi"
    "${PROJECT_BINARY_DIR}/src/mesa"
    "${PROJECT_SOURCE_DIR}/src/mesa"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(glapi_bridge PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(glapi_bridge PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-D_GDI32_>"
)

# glapi
add_library(glapi STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(glapi)
target_sources(glapi PRIVATE
    "${PROJECT_SOURCE_DIR}/src/mesa/glapi/shared-glapi/core.c"
)
target_include_directories(glapi PRIVATE
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/shared-glapi"
    "${PROJECT_SOURCE_DIR}/src/mesa/glapi/shared-glapi"
    "${PROJECT_BINARY_DIR}/src/mesa"
    "${PROJECT_SOURCE_DIR}/src/mesa"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(glapi PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(glapi PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-DMAPI_MODE_SHARED_GLAPI>"
)

# mesa
add_library(mesa STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(mesa)
target_sources(mesa PRIVATE
    "${PROJECT_SOURCE_DIR}/src/mesa/main/accum.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/api_arrayelt.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/api_trace_helpers.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/arbprogram.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/arrayobj.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/atifragshader.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/attrib.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/barrier.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/bbox.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/blend.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/blit.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/bufferobj.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/buffers.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/clear.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/clip.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/compute.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/condrender.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/conservativeraster.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/context.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/copyimage.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/debug.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/debug_output.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/depth.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/dlist.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/draw.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/draw_validate.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/drawpix.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/drawtex.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/enable.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/errors.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/es1_conversion.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/eval.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/extensions.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/extensions_table.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/externalobjects.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/fbobject.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/feedback.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/ff_fragment_shader.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/ffvertex_prog.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/fog.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/format_utils.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/formatquery.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/formats.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/framebuffer.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/genmipmap.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/get.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/getstring.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glformats.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glspirv.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glthread.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glthread_bufferobj.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glthread_draw.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glthread_draw_unroll.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glthread_get.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glthread_list.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glthread_pixels.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glthread_shaderobj.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/glthread_varray.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/hash.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/hint.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/image.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/light.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/lines.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/matrix.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/mesh_shader.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/mipmap.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/multisample.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/objectlabel.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/pack.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/pbo.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/performance_monitor.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/performance_query.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/pipelineobj.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/pixel.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/pixelstore.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/pixeltransfer.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/points.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/polygon.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/program_binary.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/program_resource.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/querymatrix.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/queryobj.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/rastpos.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/readpix.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/renderbuffer.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/robustness.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/samplerobj.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/scissor.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/shaderapi.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/shaderimage.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/shaderobj.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/shared.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/spirv_capabilities.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/spirv_extensions.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/state.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/stencil.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/syncobj.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texcompress.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texcompress_bptc.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texcompress_cpal.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texcompress_etc.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texcompress_fxt1.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texcompress_rgtc.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texcompress_s3tc.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texenv.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texgen.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texgetimage.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/teximage.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texobj.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texparam.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texstate.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texstorage.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texstore.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/texturebindless.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/textureview.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/transformfeedback.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/uniforms.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/varray.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/version.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/viewport.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/math/m_eval.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/math/m_matrix.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/arbprogparse.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/prog_cache.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/prog_instruction.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/prog_parameter.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/prog_parameter_layout.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/prog_print.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/prog_statevars.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/prog_to_nir.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/program.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/program_parse_extra.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/program/symbol_table.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atifs_to_nir.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_atomicbuf.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_blend.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_clip.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_constbuf.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_depth.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_framebuffer.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_image.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_msaa.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_pixeltransfer.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_rasterizer.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_sampler.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_scissor.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_shader.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_stipple.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_storagebuf.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_tess.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_texture.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_viewport.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_bitmap.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_clear.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_copyimage.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_drawpixels.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_drawtex.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_eglimage.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_feedback.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_flush.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_rasterpos.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_readpixels.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_cb_texture.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_context.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_copytex.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_debug.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_draw.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_draw_feedback.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_draw_hw_select.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_extensions.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_format.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_gen_mipmap.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_interop.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_manager.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_nir_builtins.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_nir_lower_alpha_test.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_nir_lower_builtin.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_nir_lower_drawpixels.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_nir_lower_fog.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_nir_lower_point_size_mov.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_nir_lower_position_invariant.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_nir_lower_tex_src_plane.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_pbo.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_pbo_compute.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_program.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_sampler_view.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_scissor.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_shader_cache.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_texcompress_compute.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_texture.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_context.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_exec.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_exec_api.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_exec_draw.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_exec_eval.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_minmax_index.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_noop.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_save.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_save_api.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_save_draw.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/vbo/vbo_save_loopback.c"
    "${PROJECT_BINARY_DIR}/src/mesa/program/lex.yy.c"
    "${PROJECT_BINARY_DIR}/src/mesa/program/program_parse.tab.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/api_exec_init.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/api_trace.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/enums.c"
    "${PROJECT_BINARY_DIR}/src/mesa/format_fallback.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/unmarshal_table.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/marshal_generated0.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/marshal_generated1.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/marshal_generated2.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/marshal_generated3.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/marshal_generated4.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/marshal_generated5.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/marshal_generated6.c"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen/marshal_generated7.c"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/shader_query.cpp"
    "${PROJECT_SOURCE_DIR}/src/mesa/main/uniform_query.cpp"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_atom_array.cpp"
    "${PROJECT_SOURCE_DIR}/src/mesa/state_tracker/st_glsl_to_nir.cpp"
)
target_include_directories(mesa PRIVATE
    "${PROJECT_BINARY_DIR}/src/mesa"
    "${PROJECT_SOURCE_DIR}/src/mesa"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/mesa/main"
    "${PROJECT_BINARY_DIR}/src/compiler/nir"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir"
    "${PROJECT_BINARY_DIR}/src/compiler"
    "${PROJECT_SOURCE_DIR}/src/compiler"
    "${PROJECT_BINARY_DIR}/src/mesa/program"
    "${PROJECT_BINARY_DIR}/src/mesa/glapi/glapi/gen"
    "${PROJECT_BINARY_DIR}/src/compiler/glsl"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
    "${PROJECT_BINARY_DIR}/src/compiler/spirv"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(mesa PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(mesa PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-D_GDI32_>"
)
target_compile_options(mesa PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:CXX>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wgnu-pointer-arith>"
    "$<$<COMPILE_LANGUAGE:CXX>:-D_GDI32_>"
)

# gallium
add_library(gallium STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(gallium)
target_sources(gallium PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_ddebug/dd_draw.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/pipebuffer/pb_buffer_fenced.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/pipebuffer/pb_bufmgr_cache.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/pipebuffer/pb_bufmgr_debug.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/pipebuffer/pb_bufmgr_mm.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/pipebuffer/pb_bufmgr_slab.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/pipebuffer/pb_cache.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/pipebuffer/pb_validate.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_async_debug.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_bitmask.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_blitter.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_debug_describe.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_debug_flush.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_debug_image.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_debug_refcnt.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_draw.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_draw_quad.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_driconf.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_dump_defines.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_dump_state.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_framebuffer.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_gen_mipmap.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_helpers.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_index_modify.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_live_shader_cache.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_log.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_prim.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_prim_restart.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_pstipple.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_resource.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_sample_positions.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_sampler.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_screen.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_split_draw.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_suballoc.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_surface.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_texture.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_tile.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_transfer.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_transfer_helper.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_threaded_context.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_trace_gallium.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_upload_mgr.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_vbuf.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_vertex_state_cache.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/cso_cache/cso_cache.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/cso_cache/cso_context.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/cso_cache/cso_hash.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_context.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_fs.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_gs.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_mesh.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_mesh_prim.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_aaline.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_aapoint.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_clip.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_cull.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_flatshade.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_offset.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_pstipple.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_stipple.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_twoside.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_unfilled.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_user_cull.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_util.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_validate.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_vbuf.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_wide_line.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pipe_wide_point.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_prim_assembler.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt_emit.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt_fetch.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt_fetch_shade_emit.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt_fetch_shade_pipeline.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt_mesh_pipeline.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt_post_vs.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt_so_emit.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt_util.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt_vsplit.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_tess.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_vertex.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_vs.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_vs_exec.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_vs_variant.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_ddebug/dd_context.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_ddebug/dd_screen.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_noop/noop_pipe.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_noop/noop_state.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_trace/tr_context.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_trace/tr_dump.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_trace/tr_dump_state.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_trace/tr_screen.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_trace/tr_texture.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/driver_trace/tr_video.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/hud/font.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/hud/hud_context.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/hud/hud_cpu.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/hud/hud_nic.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/hud/hud_cpufreq.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/hud/hud_diskstat.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/hud/hud_sensors_temp.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/hud/hud_driver_query.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/hud/hud_fps.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/indices/u_primconvert.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/rtasm/rtasm_execmem.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/rtasm/rtasm_x86sse.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_aa_point.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_build.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_dump.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_dynamic_indexing.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_exec.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_from_mesa.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_info.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_iterate.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_parse.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_point_sprite.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_sanity.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_scan.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_strings.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_text.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_transform.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_two_side.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_ureg.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_util.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tgsi/tgsi_vpos.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/translate/translate.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/translate/translate_cache.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/translate/translate_generic.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/translate/translate_sse.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/nir/tgsi_to_nir.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/nir/nir_to_tgsi.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/nir/nir_draw_helpers.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_simple_shaders.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util/u_tests.c"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary/draw_nir_lower_opcodes.c"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary/tr_util.c"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary/u_tracepoints.c"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary/u_indices_gen.c"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary/u_unfilled_gen.c"
)
if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_sources(gallium PRIVATE
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_arit.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_arit_overflow.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_assert.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_bitarit.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_const.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_conv.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_coro.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_flow.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_format_aos_array.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_format_aos.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_format_float.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_format_s3tc.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_format.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_format_soa.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_format_srgb.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_format_yuv.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_gather.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_init_common.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_intr.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_ir_common.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_jit_sample.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_jit_types.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_logic.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_nir.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_nir_aos.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_nir_lower.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_nir_soa.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_pack.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_passmgr.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_printf.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_quad.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_sample_aos.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_sample.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_sample_soa.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_struct.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_swizzle.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_tgsi_action.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_tgsi.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_tgsi_info.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_tgsi_soa.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_type.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_llvm.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_pt_fetch_shade_pipeline_llvm.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/draw/draw_vs_llvm.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/nir/nir_to_tgsi_info.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_init.c"
        "${PROJECT_BINARY_DIR}/src/gallium/auxiliary/lp_bld_nir_no_integer_algebraic.c"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_debug.cpp"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/gallivm/lp_bld_misc.cpp"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tessellator/tessellator.cpp"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/tessellator/p_tessellator.cpp"
    )
endif()
target_include_directories(gallium PRIVATE
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
    "${PROJECT_BINARY_DIR}/src/loader"
    "${PROJECT_SOURCE_DIR}/src/loader"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/util"
    "${PROJECT_BINARY_DIR}/src/compiler/nir"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir"
    "${PROJECT_BINARY_DIR}/src/compiler"
    "${PROJECT_SOURCE_DIR}/src/compiler"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(gallium PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_include_directories(gallium PRIVATE
        "${MESA_LLVM_ROOT}/include"
    )
endif()
target_compile_options(gallium PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
)
if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_compile_options(gallium PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-pthread>"
    )
endif()
if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_compile_options(gallium PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-DXXH_FORCE_ALIGN_CHECK=0>"
        "$<$<COMPILE_LANGUAGE:CXX>:-DXXH_FORCE_MEMORY_ACCESS=0>"
        "$<$<COMPILE_LANGUAGE:CXX>:-pthread>"
        "$<$<COMPILE_LANGUAGE:CXX>:-Werror=pointer-arith>"
        "$<$<COMPILE_LANGUAGE:CXX>:-Werror=vla>"
        "$<$<COMPILE_LANGUAGE:CXX>:-Werror=gnu-empty-initializer>"
        "$<$<COMPILE_LANGUAGE:CXX>:-Wgnu-pointer-arith>"
    )
endif()

# galliumvl_stub
add_library(galliumvl_stub STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(galliumvl_stub)
target_sources(galliumvl_stub PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/vl/vl_stubs.c"
)
target_include_directories(galliumvl_stub PRIVATE
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(galliumvl_stub PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(galliumvl_stub PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
    "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
)

# wsgdi
add_library(wsgdi STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(wsgdi)
target_sources(wsgdi PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/winsys/sw/gdi/gdi_sw_winsys.c"
)
target_include_directories(wsgdi PRIVATE
    "${PROJECT_BINARY_DIR}/src/gallium/winsys/sw/gdi"
    "${PROJECT_SOURCE_DIR}/src/gallium/winsys/sw/gdi"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
    "${PROJECT_BINARY_DIR}/src/gallium/drivers"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers"
    "${PROJECT_BINARY_DIR}/src/gallium/frontends"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(wsgdi PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(wsgdi PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
)

# softpipe
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec" OR MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64")
    add_library(softpipe STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(softpipe)
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec" OR MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64")
        target_sources(softpipe PRIVATE
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_buffer.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_clear.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_context.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_compute.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_draw_arrays.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_fence.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_flush.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_fs_exec.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_image.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_prim_vbuf.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_quad_blend.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_quad_depth_test.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_quad_fs.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_quad_pipe.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_query.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_screen.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_setup.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_state_blend.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_state_clip.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_state_derived.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_state_image.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_state_rasterizer.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_state_sampler.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_state_shader.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_state_so.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_state_surface.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_state_vertex.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_surface.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_tex_sample.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_tex_tile_cache.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_texture.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe/sp_tile_cache.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec" OR MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64")
        target_include_directories(softpipe PRIVATE
            "${PROJECT_BINARY_DIR}/src/gallium/drivers/softpipe"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/softpipe"
            "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
            "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
            "${PROJECT_SOURCE_DIR}/src/gallium/include"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_BINARY_DIR}/src/compiler/nir"
            "${PROJECT_SOURCE_DIR}/src/compiler/nir"
            "${PROJECT_BINARY_DIR}/src/compiler"
            "${PROJECT_SOURCE_DIR}/src/compiler"
            "${PROJECT_BINARY_DIR}/src/util"
            "${PROJECT_BINARY_DIR}/src/util/format"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_include_directories(softpipe PRIVATE
            "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec" OR MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64")
        target_compile_options(softpipe PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
            "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
            "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
            "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
            "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
        )
    endif()
endif()

# vc4winsys
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    add_library(vc4winsys STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(vc4winsys)
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_sources(vc4winsys PRIVATE
            "${PROJECT_SOURCE_DIR}/src/gallium/winsys/vc4/d3dkmt/vc4_d3dkmt_winsys.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_include_directories(vc4winsys PRIVATE
            "${PROJECT_BINARY_DIR}/src/gallium/winsys/vc4/d3dkmt"
            "${PROJECT_SOURCE_DIR}/src/gallium/winsys/vc4/d3dkmt"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_SOURCE_DIR}/src/gallium/include"
            "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
            "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
            "${PROJECT_BINARY_DIR}/src/gallium/drivers"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers"
            "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
            "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos"
            "${PROJECT_BINARY_DIR}/src/util"
            "${PROJECT_BINARY_DIR}/src/util/format"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_compile_options(vc4winsys PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
        )
    endif()
endif()

# vc4
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    add_library(vc4 STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(vc4)
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_sources(vc4 PRIVATE
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_blit.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_bufmgr.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_cl.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_cl_dump.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_context.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_draw.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_emit.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_fence.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_formats.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_job.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_nir_lower_blend.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_nir_lower_io.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_nir_lower_txf_ms.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_opt_algebraic.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_opt_constant_folding.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_opt_copy_propagation.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_opt_dead_code.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_opt_peephole_sf.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_opt_small_immediates.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_opt_vpm.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_opt_coalesce_ff_writes.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_program.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qir.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qir_emit_uniform_stream_resets.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qir_live_variables.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qir_lower_uniforms.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qir_schedule.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qir_validate.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qpu.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qpu_disasm.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qpu_emit.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qpu_schedule.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_qpu_validate.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_query.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_register_allocate.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_reorder_uniforms.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_resource.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_screen.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_state.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_tiling.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_tiling_lt.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4/vc4_uniforms.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_include_directories(vc4 PRIVATE
            "${PROJECT_BINARY_DIR}/src/gallium/drivers/vc4"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/vc4"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_SOURCE_DIR}/src/gallium/include"
            "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
            "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
            "${PROJECT_BINARY_DIR}/src/broadcom"
            "${PROJECT_SOURCE_DIR}/src/broadcom"
            "${PROJECT_BINARY_DIR}/src/broadcom/cle"
            "${PROJECT_SOURCE_DIR}/src/broadcom/cle"
            "${PROJECT_BINARY_DIR}/src/gallium/drivers"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers"
            "${PROJECT_BINARY_DIR}/src/gallium/winsys"
            "${PROJECT_SOURCE_DIR}/src/gallium/winsys"
            "${PROJECT_BINARY_DIR}/src/compiler/nir"
            "${PROJECT_SOURCE_DIR}/src/compiler/nir"
            "${PROJECT_BINARY_DIR}/src/compiler"
            "${PROJECT_SOURCE_DIR}/src/compiler"
            "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
            "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos"
            "${PROJECT_BINARY_DIR}/src/util"
            "${PROJECT_BINARY_DIR}/src/util/format"
        )
    endif()
    if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
        target_compile_options(vc4 PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
            "$<$<COMPILE_LANGUAGE:C>:-DUSE_VC4_D3DKMT>"
        )
    endif()
endif()

# wgl
add_library(wgl STATIC EXCLUDE_FROM_ALL)
mesa_target_defaults(wgl)
target_sources(wgl PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_context.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_device.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_ext_context.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_ext_extensionsstring.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_ext_interop.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_ext_pbuffer.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_ext_pixelformat.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_ext_rendertexture.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_ext_swapinterval.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_framebuffer.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_getprocaddress.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_image.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_nopfuncs.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_pixelformat.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_st.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl/stw_tls.c"
)
target_include_directories(wgl PRIVATE
    "${PROJECT_BINARY_DIR}/src/gallium/frontends/wgl"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
    "${PROJECT_BINARY_DIR}/src/mesa"
    "${PROJECT_SOURCE_DIR}/src/mesa"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(wgl PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos"
    )
endif()
target_compile_options(wgl PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-DNO_REGEX>"
    "$<$<COMPILE_LANGUAGE:C>:-D_GDI32_>"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_compile_options(wgl PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-DHAVE_ROS_SHARED_TEXTURE>"
    )
endif()

# mesa_gallium
add_library(mesa_gallium SHARED EXCLUDE_FROM_ALL)
mesa_target_defaults(mesa_gallium)
target_sources(mesa_gallium PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/targets/wgl/wgl.c"
)
target_include_directories(mesa_gallium PRIVATE
    "${PROJECT_BINARY_DIR}/src/gallium/targets/wgl"
    "${PROJECT_SOURCE_DIR}/src/gallium/targets/wgl"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/src/mesa"
    "${PROJECT_SOURCE_DIR}/src/mesa"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
    "${PROJECT_BINARY_DIR}/src/gallium/frontends/wgl"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl"
    "${PROJECT_BINARY_DIR}/src/gallium/winsys"
    "${PROJECT_SOURCE_DIR}/src/gallium/winsys"
    "${PROJECT_BINARY_DIR}/src/gallium/winsys/sw"
    "${PROJECT_SOURCE_DIR}/src/gallium/winsys/sw"
    "${PROJECT_BINARY_DIR}/src/gallium/drivers"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers"
    "${PROJECT_BINARY_DIR}/src/compiler/nir"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir"
    "${PROJECT_BINARY_DIR}/src/compiler"
    "${PROJECT_SOURCE_DIR}/src/compiler"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(mesa_gallium PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos"
    )
endif()
if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_include_directories(mesa_gallium PRIVATE
        "${MESA_LLVM_ROOT}/include"
    )
endif()
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_compile_options(mesa_gallium PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-DGALLIUM_VC4>"
    )
endif()
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec" OR MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64")
    target_compile_options(mesa_gallium PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-DGALLIUM_SOFTPIPE>"
    )
endif()
target_compile_options(mesa_gallium PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
)
if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_compile_options(mesa_gallium PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-pthread>"
        "$<$<COMPILE_LANGUAGE:C>:-DGALLIUM_LLVMPIPE>"
    )
endif()
target_sources(mesa_gallium PRIVATE
    "${PROJECT_BINARY_DIR}/src/gallium/targets/wgl/gallium_wgl.def"
)
target_link_libraries(mesa_gallium PRIVATE
    "-Wl,-O1"
    "-Wl,--whole-archive"
    "wgl"
    "-Wl,--no-whole-archive"
    "-Wl,--nxcompat"
    "-Wl,--dynamicbase"
    "gallium"
    "nir"
    "compiler"
    "mesa_util"
    "mesa_util_simd"
    "blake3"
    "mesa_util_c11"
    "glsl"
    "glcpp"
    "mesa"
    "vtn"
    "wsgdi"
    "glapi_bridge"
    "glapi"
    "galliumvl_stub"
    "xmlconfig"
    "-lm"
    "-lkernel32"
    "-luser32"
    "-lwinspool"
    "-loleaut32"
    "-lcomdlg32"
)
target_link_libraries(mesa_gallium PRIVATE
    "-static-libgcc"
    "-static-libstdc++"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec" OR MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64")
    target_link_libraries(mesa_gallium PRIVATE
        "softpipe"
    )
endif()
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_link_libraries(mesa_gallium PRIVATE
        "vc4"
        "vc4winsys"
        "broadcom_cle"
        "broadcom_v3d"
        "v3d_neon"
        "${MESA_REACTOS_BUILD_DIR}/sdk/lib/3rdparty/zlib/libzlib.a"
        "${MESA_REACTOS_BUILD_DIR}/sdk/lib/rpi3vc4kmt/librpi3vc4kmt.a"
    )
endif()
target_link_libraries(mesa_gallium PRIVATE
    "-lws2_32"
)
target_link_libraries(mesa_gallium PRIVATE
    "-lsynchronization"
)
target_link_libraries(mesa_gallium PRIVATE
    "-lgdi32"
)
target_link_libraries(mesa_gallium PRIVATE
    "-lshell32"
    "-lole32"
    "-luuid"
    "-ladvapi32"
)
if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
    target_link_libraries(mesa_gallium PRIVATE
        "mesa_util_clflush"
        "mesa_util_clflushopt"
        "mesa_sse41"
    )
endif()
if(MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_link_libraries(mesa_gallium PRIVATE
        "-Wl,--subsystem,console"
    )
endif()
if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_link_libraries(mesa_gallium PRIVATE
        "llvmpipe"
        ${MESA_LLVM_LIBRARIES}
        "-pthread"
    )
endif()
if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64" OR MESA_PROFILE STREQUAL "llvm-arm64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_link_libraries(mesa_gallium PRIVATE
        "-lntdll"
        "-lpsapi"
    )
endif()
set_target_properties(mesa_gallium PROPERTIES PREFIX "" LINKER_LANGUAGE CXX)

# opengl32
add_library(opengl32 SHARED EXCLUDE_FROM_ALL)
mesa_target_defaults(opengl32)
target_sources(opengl32 PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/targets/libgl-gdi/stw_wgl.c"
)
target_include_directories(opengl32 PRIVATE
    "${PROJECT_BINARY_DIR}/src/gallium/targets/libgl-gdi"
    "${PROJECT_SOURCE_DIR}/src/gallium/targets/libgl-gdi"
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src/gallium/frontends/wgl"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/wgl"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_include_directories(opengl32 PRIVATE
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib"
    )
endif()
target_compile_options(opengl32 PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
    "$<$<COMPILE_LANGUAGE:C>:-D_GDI32_>"
)
target_sources(opengl32 PRIVATE
    "${PROJECT_BINARY_DIR}/src/gallium/targets/libgl-gdi/opengl32.def"
)
target_link_libraries(opengl32 PRIVATE
    "-Wl,-O1"
    "-Wl,--nxcompat"
    "-Wl,--dynamicbase"
    "mesa_gallium"
    "glapi_bridge"
    "mesa_util"
    "mesa_util_simd"
    "blake3"
    "mesa_util_c11"
    "-lm"
    "-lkernel32"
    "-luser32"
    "-lgdi32"
    "-lwinspool"
    "-lshell32"
    "-lole32"
    "-loleaut32"
    "-luuid"
    "-lcomdlg32"
    "-ladvapi32"
)
target_link_libraries(opengl32 PRIVATE
    "-static-libgcc"
    "-static-libstdc++"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    target_link_libraries(opengl32 PRIVATE
        "${MESA_REACTOS_BUILD_DIR}/sdk/lib/3rdparty/zlib/libzlib.a"
    )
endif()
target_link_libraries(opengl32 PRIVATE
    "-lsynchronization"
)
if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
    target_link_libraries(opengl32 PRIVATE
        "mesa_util_clflush"
        "mesa_util_clflushopt"
    )
endif()
if(MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    target_link_libraries(opengl32 PRIVATE
        "-Wl,--subsystem,console"
    )
endif()
set_target_properties(opengl32 PROPERTIES PREFIX "" LINKER_LANGUAGE CXX)

# mesa_util_clflush
if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
    add_library(mesa_util_clflush STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(mesa_util_clflush)
    if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
        target_sources(mesa_util_clflush PRIVATE
            "${PROJECT_SOURCE_DIR}/src/util/cache_ops_x86.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
        target_include_directories(mesa_util_clflush PRIVATE
            "${PROJECT_BINARY_DIR}/src/util"
            "${PROJECT_SOURCE_DIR}/src/util"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
        )
    endif()
    if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
        target_compile_options(mesa_util_clflush PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
        )
    endif()
endif()

# mesa_util_clflushopt
if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
    add_library(mesa_util_clflushopt STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(mesa_util_clflushopt)
    if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
        target_sources(mesa_util_clflushopt PRIVATE
            "${PROJECT_SOURCE_DIR}/src/util/cache_ops_x86_clflushopt.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
        target_include_directories(mesa_util_clflushopt PRIVATE
            "${PROJECT_BINARY_DIR}/src/util"
            "${PROJECT_SOURCE_DIR}/src/util"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
        )
    endif()
    if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
        target_compile_options(mesa_util_clflushopt PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
            "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>"
            "$<$<COMPILE_LANGUAGE:C>:-mclflushopt>"
        )
    endif()
endif()

# mesa_sse41
if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
    add_library(mesa_sse41 STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(mesa_sse41)
    if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
        target_sources(mesa_sse41 PRIVATE
            "${PROJECT_SOURCE_DIR}/src/mesa/main/sse_minmax.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
        target_include_directories(mesa_sse41 PRIVATE
            "${PROJECT_BINARY_DIR}/src/mesa"
            "${PROJECT_SOURCE_DIR}/src/mesa"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src/gallium/include"
            "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
            "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
        )
    endif()
    if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
        target_compile_options(mesa_sse41 PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
            "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
            "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
            "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
            "$<$<COMPILE_LANGUAGE:C>:-msse4.1>"
        )
    endif()
endif()

# llvmpipe
if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    add_library(llvmpipe STATIC EXCLUDE_FROM_ALL)
    mesa_target_defaults(llvmpipe)
    if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
        target_sources(llvmpipe PRIVATE
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_bld_alpha.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_bld_blend_aos.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_bld_blend.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_bld_blend_logicop.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_bld_depth.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_bld_interp.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_clear.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_context.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_cs_tpool.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_draw_arrays.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_fence.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_flush.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_jit.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_linear.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_linear_fastpath.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_linear_interp.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_linear_sampler.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_memory.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_perf.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_query.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_rast.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_rast_debug.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_rast_linear.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_rast_linear_fallback.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_rast_rect.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_rast_tri.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_scene.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_scene_queue.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_screen.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_setup.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_setup_analysis.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_setup_line.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_setup_point.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_setup_rect.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_setup_tri.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_setup_vbuf.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_blend.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_clip.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_derived.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_cs.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_fs.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_fs_analysis.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_fs_fastpath.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_fs_linear.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_fs_linear_llvm.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_gs.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_rasterizer.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_sampler.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_setup.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_so.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_surface.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_tess.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_vertex.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_state_vs.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_surface.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_tex_sample.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_texture.c"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe/lp_texture_handle.c"
        )
    endif()
    if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
        target_include_directories(llvmpipe PRIVATE
            "${PROJECT_BINARY_DIR}/src/gallium/drivers/llvmpipe"
            "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe"
            "${PROJECT_SOURCE_DIR}/src/gallium/include"
            "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
            "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
            "${PROJECT_BINARY_DIR}/include"
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_BINARY_DIR}/src/compiler/nir"
            "${PROJECT_SOURCE_DIR}/src/compiler/nir"
            "${PROJECT_BINARY_DIR}/src/compiler"
            "${PROJECT_SOURCE_DIR}/src/compiler"
            "${PROJECT_BINARY_DIR}/src/util"
            "${PROJECT_BINARY_DIR}/src/util/format"
            "${MESA_LLVM_ROOT}/include"
        )
    endif()
    if(MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
        target_compile_options(llvmpipe PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
            "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>"
            "$<$<COMPILE_LANGUAGE:C>:-pthread>"
            "$<$<COMPILE_LANGUAGE:C>:-Werror=pointer-arith>"
            "$<$<COMPILE_LANGUAGE:C>:-Werror=vla>"
            "$<$<COMPILE_LANGUAGE:C>:-Werror=gnu-empty-initializer>"
            "$<$<COMPILE_LANGUAGE:C>:-Wgnu-pointer-arith>"
        )
    endif()
endif()
