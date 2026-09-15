# SPDX-License-Identifier: MIT
# Native CMake target graph for Mesa's Windows Lavapipe ICD.

function(mesa_vulkan_target_defaults target)
    mesa_target_defaults(${target})
    target_include_directories(${target} PRIVATE
        "${PROJECT_BINARY_DIR}/include"
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_BINARY_DIR}/src"
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_BINARY_DIR}/src/util"
        "${PROJECT_SOURCE_DIR}/src/util"
        "${PROJECT_BINARY_DIR}/src/util/format"
        "${PROJECT_BINARY_DIR}/src/compiler"
        "${PROJECT_SOURCE_DIR}/src/compiler"
        "${PROJECT_BINARY_DIR}/src/compiler/nir"
        "${PROJECT_SOURCE_DIR}/src/compiler/nir"
        "${PROJECT_BINARY_DIR}/src/compiler/spirv"
        "${PROJECT_SOURCE_DIR}/src/compiler/spirv"
        "${PROJECT_SOURCE_DIR}/src/gallium/include"
        "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
        "${PROJECT_BINARY_DIR}/src/gallium/drivers/llvmpipe"
        "${PROJECT_SOURCE_DIR}/src/gallium/drivers/llvmpipe"
        "${PROJECT_BINARY_DIR}/src/gallium/winsys"
        "${PROJECT_SOURCE_DIR}/src/gallium/winsys"
        "${PROJECT_BINARY_DIR}/src/vulkan/util"
        "${PROJECT_SOURCE_DIR}/src/vulkan/util"
        "${PROJECT_BINARY_DIR}/src/vulkan/wsi"
        "${PROJECT_SOURCE_DIR}/src/vulkan/wsi"
        "${PROJECT_BINARY_DIR}/src/vulkan/runtime"
        "${PROJECT_SOURCE_DIR}/src/vulkan/runtime"
        "${PROJECT_BINARY_DIR}/src/vulkan/runtime/bvh"
        "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/bvh"
        "${PROJECT_BINARY_DIR}/src/gallium/frontends/lavapipe"
        "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe")
    target_include_directories(${target} SYSTEM PRIVATE
        "${MESA_LLVM_ROOT}/include"
        "${MESA_DIRECTX_HEADERS_ROOT}/include")
    target_compile_definitions(${target} PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        MESA_VK_LOG=0
        XXH_FORCE_ALIGN_CHECK=0
        XXH_FORCE_MEMORY_ACCESS=0)
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:-pthread>")
endfunction()

add_library(vulkan_util STATIC EXCLUDE_FROM_ALL)
mesa_vulkan_target_defaults(vulkan_util)
target_sources(vulkan_util PRIVATE
    "${PROJECT_SOURCE_DIR}/src/vulkan/util/vk_alloc.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/util/vk_format.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/util/vk_util.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_dispatch_table.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_enum_to_str.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_extensions.c")

add_library(pipe_loader_static STATIC EXCLUDE_FROM_ALL)
mesa_vulkan_target_defaults(pipe_loader_static)
target_sources(pipe_loader_static PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/pipe-loader/pipe_loader.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary/pipe-loader/pipe_loader_sw.c")

add_library(wsw STATIC EXCLUDE_FROM_ALL)
mesa_vulkan_target_defaults(wsw)
target_sources(wsw PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/winsys/sw/wrapper/wrapper_sw_winsys.c")

add_library(ws_null STATIC EXCLUDE_FROM_ALL)
mesa_vulkan_target_defaults(ws_null)
target_sources(ws_null PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/winsys/sw/null/null_sw_winsys.c")

add_library(lavapipe_st STATIC EXCLUDE_FROM_ALL)
mesa_vulkan_target_defaults(lavapipe_st)
set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_instance.c"
    PROPERTIES COMPILE_DEFINITIONS VK_LITE_RUNTIME_INSTANCE=0)
target_sources(lavapipe_st PRIVATE
    # Vulkan WSI, folded into the archive to retain its weak entrypoints.
    "${PROJECT_SOURCE_DIR}/src/vulkan/wsi/wsi_common.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/wsi/wsi_common_win32.cpp"
    "${PROJECT_BINARY_DIR}/src/vulkan/wsi/wsi_common_entrypoints.c"

    # Common Vulkan runtime.
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/rmv/vk_rmv_common.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/rmv/vk_rmv_exporter.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_blend.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_buffer.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_buffer_view.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_cmd_copy.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_cmd_enqueue.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_command_buffer.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_command_pool.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_debug_report.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_debug_utils.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_deferred_operation.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_descriptor_set_layout.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_descriptors.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_descriptor_update_template.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_device.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_device_generated_commands.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_device_memory.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_fence.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_framebuffer.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_graphics_state.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_image.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_log.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_meta_object_list.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_object.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_physical_device.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_pipeline_layout.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_query_pool.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_queue.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_render_pass.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_sampler.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_semaphore.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_standard_sample_locations.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_sync.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_sync_binary.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_sync_dummy.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_sync_timeline.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_synchronization.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_video.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_ycbcr_conversion.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_cmd_enqueue_entrypoints.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_cmd_queue.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_common_entrypoints.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_dispatch_trampolines.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_format_info.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_physical_device_features.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_physical_device_properties.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_physical_device_spirv_caps.c"
    "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_synchronization_helpers.c"

    # Full Vulkan runtime and its compute helpers.
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_meta.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_meta_blit_resolve.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_meta_clear.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_meta_copy_fill_update.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_meta_draw_rects.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_nir.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_nir_convert_ycbcr.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_nir_lower_descriptor_heaps.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_pipeline.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_pipeline_cache.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_shader.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_shader_module.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_texcompress_etc2.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/radix_sort/common/vk/barrier.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/radix_sort/common/util.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/radix_sort/radix_sort_u64.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/radix_sort/radix_sort_u96.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/radix_sort/radix_sort_vk.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_acceleration_structure.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_texcompress_astc.c"
    "${PROJECT_SOURCE_DIR}/src/vulkan/runtime/vk_instance.c"

    # Lavapipe state tracker.
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/nir/lvp_nir_lower_cooperative_matrix.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/nir/lvp_nir_lower_descriptor_heaps.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/nir/lvp_nir_lower_exec_graph.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/nir/lvp_nir_lower_input_attachments.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/nir/lvp_nir_lower_pipeline_layout.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/nir/lvp_nir_lower_push_constants.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/nir/lvp_nir_lower_ray_queries.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/nir/lvp_nir_lower_sparse_residency.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/nir/lvp_nir_opt_robustness.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/nir/lvp_nir_ray_tracing.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_acceleration_structure.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_device.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_device_generated_commands.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_cmd_buffer.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_descriptor_set.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_execute.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_util.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_image.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_formats.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_pipe_sync.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_pipeline.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_pipeline_cache.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_query.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_ray_tracing_pipeline.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe/lvp_wsi.c"
    "${PROJECT_BINARY_DIR}/src/gallium/frontends/lavapipe/lvp_entrypoints.c"
    "${PROJECT_BINARY_DIR}/src/gallium/frontends/lavapipe/lvp_drirc.c")

add_library(vulkan_lvp SHARED EXCLUDE_FROM_ALL)
mesa_vulkan_target_defaults(vulkan_lvp)
target_sources(vulkan_lvp PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/targets/lavapipe/lavapipe_target.c"
    "${MESA_VULKAN_DEF}")
target_link_libraries(vulkan_lvp PRIVATE
    "-Wl,-O1"
    "-Wl,--whole-archive"
    lavapipe_st
    "-Wl,--no-whole-archive"
    "-Wl,--nxcompat"
    "-Wl,--dynamicbase"
    "-Wl,--gc-sections"
    "-Wl,--start-group"
    pipe_loader_static
    xmlconfig
    mesa_util
    mesa_util_simd
    blake3
    mesa_util_c11
    gallium
    nir
    compiler
    wsw
    ws_null
    llvmpipe
    vulkan_util
    vtn
    ${MESA_LLVM_LIBRARIES}
    "-Wl,--end-group"
    "-static-libgcc"
    "-static-libstdc++"
    "-pthread"
    "-lm"
    "-ladvapi32"
    "-lntdll"
    "-lole32"
    "-lpsapi"
    "-lshell32"
    "-luuid"
    "-lws2_32"
    "-lsynchronization"
    "-lkernel32"
    "-luser32"
    "-lgdi32"
    "-lwinspool"
    "-loleaut32"
    "-lcomdlg32"
    "-Wl,--subsystem,console")
set_target_properties(vulkan_lvp PROPERTIES
    PREFIX ""
    LINKER_LANGUAGE CXX
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/src/gallium/targets/lavapipe")

add_custom_target(lvp_icd DEPENDS "${MESA_LAVAPIPE_MANIFEST}")
