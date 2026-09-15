# SPDX-License-Identifier: MIT
# Native CMake generator graph for the Windows Lavapipe ICD.

set(_mesa_vk_xml "${PROJECT_SOURCE_DIR}/src/vulkan/registry/vk.xml")
set(_mesa_vk_util "${PROJECT_SOURCE_DIR}/src/vulkan/util")
set(_mesa_vk_runtime "${PROJECT_SOURCE_DIR}/src/vulkan/runtime")
set(_mesa_lvp "${PROJECT_SOURCE_DIR}/src/gallium/frontends/lavapipe")

set(_mesa_driconf_inputs
    "${PROJECT_SOURCE_DIR}/src/util/00-mesa-defaults.conf")
if(MESA_ARCH MATCHES "^arm64")
    list(APPEND _mesa_driconf_inputs
        "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/00-v3d-defaults.conf")
endif()
list(APPEND _mesa_driconf_inputs
    "${_mesa_lvp}/00-lavapipe-defaults.conf")
mesa_generate(
    OUTPUT "${PROJECT_BINARY_DIR}/src/util/driconf_static.h"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/src/util/driconf_static.py"
        ${_mesa_driconf_inputs}
        "${PROJECT_BINARY_DIR}/src/util/driconf_static.h"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/src/util/driconf_static.py"
        ${_mesa_driconf_inputs}
        "${Python3_EXECUTABLE}")

mesa_generate(
    OUTPUT
        "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_dispatch_table.c"
        "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_dispatch_table.h"
    COMMAND
        "${Python3_EXECUTABLE}" "${_mesa_vk_util}/vk_dispatch_table_gen.py"
        --xml "${_mesa_vk_xml}"
        --out-c "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_dispatch_table.c"
        --out-h "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_dispatch_table.h"
        --beta false
    DEPENDS
        "${_mesa_vk_util}/vk_dispatch_table_gen.py"
        "${_mesa_vk_util}/vk_entrypoints.py"
        "${_mesa_vk_util}/vk_extensions.py"
        "${_mesa_vk_xml}" "${Python3_EXECUTABLE}")

mesa_generate(
    OUTPUT
        "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_enum_to_str.c"
        "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_enum_to_str.h"
        "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_enum_defines.h"
    COMMAND
        "${Python3_EXECUTABLE}" "${_mesa_vk_util}/gen_enum_to_str.py"
        --xml "${_mesa_vk_xml}"
        --out-c "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_enum_to_str.c"
        --out-h "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_enum_to_str.h"
        --out-d "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_enum_defines.h"
        --beta false
    DEPENDS
        "${_mesa_vk_util}/gen_enum_to_str.py"
        "${_mesa_vk_util}/vk_extensions.py"
        "${_mesa_vk_xml}" "${Python3_EXECUTABLE}")

mesa_generate(
    OUTPUT "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_struct_type_cast.h"
    COMMAND
        "${Python3_EXECUTABLE}" "${_mesa_vk_util}/vk_struct_type_cast_gen.py"
        --xml "${_mesa_vk_xml}"
        --out "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_struct_type_cast.h"
        --beta false
    DEPENDS
        "${_mesa_vk_util}/vk_struct_type_cast_gen.py"
        "${_mesa_vk_util}/vk_extensions.py"
        "${_mesa_vk_xml}" "${Python3_EXECUTABLE}")

mesa_generate(
    OUTPUT
        "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_extensions.c"
        "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_extensions.h"
    COMMAND
        "${Python3_EXECUTABLE}" "${_mesa_vk_util}/vk_extensions_gen.py"
        --xml "${_mesa_vk_xml}"
        --out-c "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_extensions.c"
        --out-h "${PROJECT_BINARY_DIR}/src/vulkan/util/vk_extensions.h"
    DEPENDS
        "${_mesa_vk_util}/vk_extensions_gen.py"
        "${_mesa_vk_util}/vk_extensions.py"
        "${_mesa_vk_xml}" "${Python3_EXECUTABLE}")

macro(mesa_vk_entrypoints output_dir stem prefix)
    set(_mesa_vk_entrypoint_prefixes --prefix "${prefix}")
    if("${prefix}" STREQUAL "vk_cmd_enqueue")
        list(APPEND _mesa_vk_entrypoint_prefixes --prefix vk_cmd_enqueue_unless_primary)
    endif()
    mesa_generate(
        OUTPUT
            "${PROJECT_BINARY_DIR}/${output_dir}/${stem}_entrypoints.h"
            "${PROJECT_BINARY_DIR}/${output_dir}/${stem}_entrypoints.c"
        COMMAND
            "${Python3_EXECUTABLE}" "${_mesa_vk_util}/vk_entrypoints_gen.py"
            --xml "${_mesa_vk_xml}" --proto --weak
            --out-h "${PROJECT_BINARY_DIR}/${output_dir}/${stem}_entrypoints.h"
            --out-c "${PROJECT_BINARY_DIR}/${output_dir}/${stem}_entrypoints.c"
            ${_mesa_vk_entrypoint_prefixes} --beta false
        DEPENDS
            "${_mesa_vk_util}/vk_entrypoints_gen.py"
            "${_mesa_vk_util}/vk_entrypoints.py"
            "${_mesa_vk_util}/vk_extensions.py"
            "${_mesa_vk_xml}" "${Python3_EXECUTABLE}")
endmacro()

mesa_vk_entrypoints(src/vulkan/wsi wsi_common wsi)
mesa_vk_entrypoints(src/vulkan/runtime vk_common vk_common)
mesa_vk_entrypoints(src/vulkan/runtime vk_cmd_enqueue vk_cmd_enqueue)
mesa_vk_entrypoints(src/gallium/frontends/lavapipe lvp lvp)

mesa_generate(
    OUTPUT
        "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_cmd_queue.c"
        "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_cmd_queue.h"
    COMMAND
        "${Python3_EXECUTABLE}" "${_mesa_vk_util}/vk_cmd_queue_gen.py"
        --xml "${_mesa_vk_xml}"
        --out-c "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_cmd_queue.c"
        --out-h "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_cmd_queue.h"
        --beta false
    DEPENDS
        "${_mesa_vk_util}/vk_cmd_queue_gen.py"
        "${_mesa_vk_util}/vk_entrypoints.py"
        "${_mesa_vk_util}/vk_extensions.py"
        "${_mesa_vk_xml}" "${Python3_EXECUTABLE}")

mesa_generate(
    OUTPUT
        "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_dispatch_trampolines.c"
        "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_dispatch_trampolines.h"
    COMMAND
        "${Python3_EXECUTABLE}" "${_mesa_vk_util}/vk_dispatch_trampolines_gen.py"
        --xml "${_mesa_vk_xml}"
        --out-c "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_dispatch_trampolines.c"
        --out-h "${PROJECT_BINARY_DIR}/src/vulkan/runtime/vk_dispatch_trampolines.h"
        --beta false
    DEPENDS
        "${_mesa_vk_util}/vk_dispatch_trampolines_gen.py"
        "${_mesa_vk_util}/vk_entrypoints.py"
        "${_mesa_vk_util}/vk_extensions.py"
        "${_mesa_vk_xml}" "${Python3_EXECUTABLE}")

macro(mesa_vk_runtime_generated stem script header)
    set(_mesa_vk_outputs "${PROJECT_BINARY_DIR}/src/vulkan/runtime/${stem}.c")
    set(_mesa_vk_output_args --out-c "${PROJECT_BINARY_DIR}/src/vulkan/runtime/${stem}.c")
    if("${header}" STREQUAL "HEADER")
        list(APPEND _mesa_vk_outputs "${PROJECT_BINARY_DIR}/src/vulkan/runtime/${stem}.h")
        list(APPEND _mesa_vk_output_args --out-h "${PROJECT_BINARY_DIR}/src/vulkan/runtime/${stem}.h")
    endif()
    set(_mesa_vk_beta)
    if(NOT "${stem}" STREQUAL "vk_format_info")
        set(_mesa_vk_beta --beta false)
    endif()
    mesa_generate(
        OUTPUT ${_mesa_vk_outputs}
        COMMAND
            "${Python3_EXECUTABLE}" "${_mesa_vk_runtime}/${script}"
            --xml "${_mesa_vk_xml}" ${_mesa_vk_output_args} ${_mesa_vk_beta}
        DEPENDS
            "${_mesa_vk_runtime}/${script}"
            "${_mesa_vk_util}/vk_extensions.py"
            "${_mesa_vk_xml}" "${Python3_EXECUTABLE}")
endmacro()

mesa_vk_runtime_generated(vk_format_info vk_format_info_gen.py HEADER)
mesa_vk_runtime_generated(vk_physical_device_features ../util/vk_physical_device_features_gen.py HEADER)
mesa_vk_runtime_generated(vk_physical_device_properties ../util/vk_physical_device_properties_gen.py HEADER)
mesa_vk_runtime_generated(vk_physical_device_spirv_caps ../util/vk_physical_device_spirv_caps_gen.py NO_HEADER)
mesa_vk_runtime_generated(vk_synchronization_helpers ../util/vk_synchronization_helpers_gen.py NO_HEADER)

mesa_generate(
    OUTPUT
        "${PROJECT_BINARY_DIR}/src/gallium/frontends/lavapipe/lvp_drirc.c"
        "${PROJECT_BINARY_DIR}/src/gallium/frontends/lavapipe/lvp_drirc.h"
    COMMAND
        "${Python3_EXECUTABLE}" "${_mesa_lvp}/lvp_drirc_gen.py"
        --import-path "${PROJECT_SOURCE_DIR}/src/util"
        --drirc-src "${PROJECT_BINARY_DIR}/src/gallium/frontends/lavapipe/lvp_drirc.c"
        --drirc-hdr "${PROJECT_BINARY_DIR}/src/gallium/frontends/lavapipe/lvp_drirc.h"
        --validate "${_mesa_lvp}/00-lavapipe-defaults.conf"
    DEPENDS
        "${_mesa_lvp}/lvp_drirc_gen.py"
        "${_mesa_lvp}/00-lavapipe-defaults.conf"
        "${PROJECT_SOURCE_DIR}/src/util/drirc_gen.py"
        "${Python3_EXECUTABLE}")

if(MESA_ARCH MATCHES "^arm64")
    set(_mesa_vulkan_cpu_family aarch64)
else()
    set(_mesa_vulkan_cpu_family x86_64)
endif()
set(MESA_VULKAN_DEF "${PROJECT_BINARY_DIR}/src/vulkan/vulkan_api.def")
mesa_generate(
    OUTPUT "${MESA_VULKAN_DEF}"
    COMMAND
        "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/bin/gen_vs_module_defs.py"
        --in_file "${PROJECT_SOURCE_DIR}/src/vulkan/vulkan_api.def.in"
        --out_file "${MESA_VULKAN_DEF}"
        --compiler_abi gcc --compiler_id clang --cpu_family "${_mesa_vulkan_cpu_family}"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/src/vulkan/vulkan_api.def.in"
        "${PROJECT_SOURCE_DIR}/bin/gen_vs_module_defs.py"
        "${Python3_EXECUTABLE}")

set(MESA_LAVAPIPE_MANIFEST "${PROJECT_BINARY_DIR}/src/gallium/targets/lavapipe/lvp_icd.json")
mesa_generate(
    OUTPUT "${MESA_LAVAPIPE_MANIFEST}"
    COMMAND
        "${Python3_EXECUTABLE}" "${_mesa_vk_util}/vk_icd_gen.py"
        --api-version 1.4 --xml "${_mesa_vk_xml}"
        --sizeof-pointer "${CMAKE_SIZEOF_VOID_P}"
        --icd-lib-path . --icd-filename vulkan_lvp.dll
        --out "${MESA_LAVAPIPE_MANIFEST}" --use-backslash
    DEPENDS
        "${_mesa_vk_util}/vk_icd_gen.py"
        "${_mesa_vk_xml}" "${Python3_EXECUTABLE}")

set(_mesa_radix_dir "${_mesa_vk_runtime}/radix_sort/shaders")
set(_mesa_radix_deps
    "${_mesa_radix_dir}/bufref.h"
    "${_mesa_radix_dir}/prefix_limits.h"
    "${_mesa_radix_dir}/prefix.h"
    "${_mesa_radix_dir}/push.h"
    "${_mesa_radix_dir}/scatter.glsl"
    "${_mesa_radix_dir}/config.h")
macro(mesa_radix_shader output_name input_name key_words)
    set(_mesa_radix_define)
    if(NOT "${key_words}" STREQUAL "0")
        set(_mesa_radix_define "-DRS_KEYVAL_DWORDS=${key_words}")
    endif()
    mesa_generate(
        OUTPUT "${PROJECT_BINARY_DIR}/src/vulkan/runtime/radix_sort/shaders/${output_name}.spv.h"
        COMMAND
            "${MESA_GLSLANG_VALIDATOR}" -V --target-env spirv1.5 -x
            -o "${PROJECT_BINARY_DIR}/src/vulkan/runtime/radix_sort/shaders/${output_name}.spv.h"
            "${_mesa_radix_dir}/${input_name}" ${_mesa_radix_define} --quiet
        DEPENDS "${_mesa_radix_dir}/${input_name}" ${_mesa_radix_deps} "${MESA_GLSLANG_VALIDATOR}")
endmacro()

mesa_radix_shader(u64_init init.comp 2)
mesa_radix_shader(u96_init init.comp 3)
mesa_radix_shader(fill fill.comp 0)
mesa_radix_shader(u64_histogram histogram.comp 2)
mesa_radix_shader(u96_histogram histogram.comp 3)
mesa_radix_shader(u64_prefix prefix.comp 2)
mesa_radix_shader(u96_prefix prefix.comp 3)
mesa_radix_shader(u64_scatter_0_even scatter_0_even.comp 2)
mesa_radix_shader(u96_scatter_0_even scatter_0_even.comp 3)
mesa_radix_shader(u64_scatter_0_odd scatter_0_odd.comp 2)
mesa_radix_shader(u96_scatter_0_odd scatter_0_odd.comp 3)
mesa_radix_shader(u64_scatter_1_even scatter_1_even.comp 2)
mesa_radix_shader(u96_scatter_1_even scatter_1_even.comp 3)
mesa_radix_shader(u64_scatter_1_odd scatter_1_odd.comp 2)
mesa_radix_shader(u96_scatter_1_odd scatter_1_odd.comp 3)
mesa_radix_shader(u96_scatter_2_even scatter_2_even.comp 3)
mesa_radix_shader(u96_scatter_2_odd scatter_2_odd.comp 3)

set(_mesa_bvh_dir "${_mesa_vk_runtime}/bvh")
set(_mesa_bvh_deps
    "${_mesa_bvh_dir}/leaf.h"
    "${_mesa_bvh_dir}/vk_bvh_defines.h"
    "${_mesa_bvh_dir}/vk_bvh_helpers.h"
    "${_mesa_bvh_dir}/vk_debug.h"
    "${PROJECT_SOURCE_DIR}/src/compiler/spirv/spirv_internal_exts.h")
set(_mesa_bvh_preamble
    "-P#extension GL_GOOGLE_include_directive : require"
    "-P#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require"
    "-P#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require"
    "-P#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require"
    "-P#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require"
    "-P#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require"
    "-P#extension GL_EXT_scalar_block_layout : require"
    "-P#extension GL_EXT_buffer_reference : require"
    "-P#extension GL_EXT_buffer_reference2 : require"
    "-P#extension GL_KHR_memory_scope_semantics : require"
    "-P#extension GL_KHR_shader_subgroup_arithmetic : require"
    "-P#extension GL_KHR_shader_subgroup_basic : require"
    "-P#extension GL_KHR_shader_subgroup_shuffle : require"
    "-P#extension GL_KHR_shader_subgroup_ballot : require"
    "-P#extension GL_KHR_shader_subgroup_clustered : require"
    "-P#extension GL_KHR_shader_subgroup_vote : require"
    "-P#extension GL_EXT_shader_atomic_int64 : require"
    "-P#extension GL_EXT_spirv_intrinsics : require")
macro(mesa_bvh_shader name)
    mesa_generate(
        OUTPUT "${PROJECT_BINARY_DIR}/src/vulkan/runtime/bvh/${name}.spv.h"
        COMMAND
            "${MESA_GLSLANG_VALIDATOR}" -V
            "-I${_mesa_bvh_dir}" "-I${PROJECT_SOURCE_DIR}/src/compiler/spirv"
            --target-env spirv1.5 -x
            -o "${PROJECT_BINARY_DIR}/src/vulkan/runtime/bvh/${name}.spv.h"
            "${_mesa_bvh_dir}/${name}.comp" --quiet ${_mesa_bvh_preamble}
        DEPENDS "${_mesa_bvh_dir}/${name}.comp" ${_mesa_bvh_deps} "${MESA_GLSLANG_VALIDATOR}")
endmacro()

mesa_bvh_shader(lbvh_generate_ir)
mesa_bvh_shader(lbvh_main)
mesa_bvh_shader(leaf)
mesa_bvh_shader(morton)
mesa_bvh_shader(ploc_internal)
mesa_bvh_shader(hploc_internal)

mesa_generate(
    OUTPUT "${PROJECT_BINARY_DIR}/src/vulkan/runtime/astc_spv.h"
    COMMAND
        "${MESA_GLSLANG_VALIDATOR}" -V -S comp -x
        -o "${PROJECT_BINARY_DIR}/src/vulkan/runtime/astc_spv.h"
        "${PROJECT_SOURCE_DIR}/src/compiler/glsl/astc_decoder.glsl" --quiet
    DEPENDS
        "${PROJECT_SOURCE_DIR}/src/compiler/glsl/astc_decoder.glsl"
        "${MESA_GLSLANG_VALIDATOR}")
