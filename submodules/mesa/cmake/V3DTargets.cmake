# SPDX-License-Identifier: MIT
# Native CMake target graph for the ReactOS V3D Gallium driver.

function(mesa_v3d_target_defaults target)
    mesa_target_defaults(${target})
    target_include_directories(${target} PRIVATE
        "${PROJECT_BINARY_DIR}/include"
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_BINARY_DIR}/src"
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_BINARY_DIR}/src/util"
        "${PROJECT_BINARY_DIR}/src/util/format"
        "${PROJECT_BINARY_DIR}/src/compiler"
        "${PROJECT_SOURCE_DIR}/src/compiler"
        "${PROJECT_BINARY_DIR}/src/compiler/nir"
        "${PROJECT_SOURCE_DIR}/src/compiler/nir"
        "${PROJECT_BINARY_DIR}/src/broadcom"
        "${PROJECT_SOURCE_DIR}/src/broadcom"
        "${PROJECT_BINARY_DIR}/src/broadcom/cle"
        "${PROJECT_SOURCE_DIR}/src/broadcom/cle"
        "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
        "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
        "${PROJECT_SOURCE_DIR}/src/gallium/include"
        "${PROJECT_BINARY_DIR}/src/gallium/drivers"
        "${PROJECT_SOURCE_DIR}/src/gallium/drivers"
        "${PROJECT_BINARY_DIR}/src/gallium/drivers/v3d"
        "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d"
        "${PROJECT_BINARY_DIR}/src/gallium/winsys"
        "${PROJECT_SOURCE_DIR}/src/gallium/winsys"
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos"
        "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib")
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
        "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
        "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>")
endfunction()

set(_mesa_v3d_version_sources
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3dx_draw.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3dx_emit.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3dx_format_table.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3dx_job.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3dx_rcl.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3dx_state.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3dx_tfu.c")

foreach(_mesa_v3d_version 42 71)
    add_library(v3d-v${_mesa_v3d_version} STATIC EXCLUDE_FROM_ALL)
    mesa_v3d_target_defaults(v3d-v${_mesa_v3d_version})
    target_sources(v3d-v${_mesa_v3d_version} PRIVATE ${_mesa_v3d_version_sources})
    target_compile_definitions(v3d-v${_mesa_v3d_version} PRIVATE
        V3D_BUILD_NEON
        USE_V3D_SIMULATOR=0
        V3D_VERSION=${_mesa_v3d_version})

    add_library(v3d-perfcntrs-v${_mesa_v3d_version} STATIC EXCLUDE_FROM_ALL)
    mesa_v3d_target_defaults(v3d-perfcntrs-v${_mesa_v3d_version})
    target_sources(v3d-perfcntrs-v${_mesa_v3d_version} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/broadcom/perfcntrs/v3dx_counter.c")
    target_include_directories(v3d-perfcntrs-v${_mesa_v3d_version} PRIVATE
        "${PROJECT_BINARY_DIR}/src/broadcom/perfcntrs"
        "${PROJECT_SOURCE_DIR}/src/broadcom/perfcntrs")
    target_compile_definitions(v3d-perfcntrs-v${_mesa_v3d_version} PRIVATE
        USE_V3D_SIMULATOR=0
        V3D_VERSION=${_mesa_v3d_version})
endforeach()

add_library(broadcom_perfcntrs STATIC EXCLUDE_FROM_ALL)
mesa_v3d_target_defaults(broadcom_perfcntrs)
target_sources(broadcom_perfcntrs PRIVATE
    "${PROJECT_SOURCE_DIR}/src/broadcom/perfcntrs/v3d_perfcntrs.c")
target_include_directories(broadcom_perfcntrs PRIVATE
    "${PROJECT_BINARY_DIR}/src/broadcom/perfcntrs"
    "${PROJECT_SOURCE_DIR}/src/broadcom/perfcntrs")
target_compile_options(broadcom_perfcntrs PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-Wno-override-init>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-initializer-overrides>")
target_link_libraries(broadcom_perfcntrs PUBLIC
    v3d-perfcntrs-v42
    v3d-perfcntrs-v71)

add_library(v3dwinsys STATIC EXCLUDE_FROM_ALL)
mesa_v3d_target_defaults(v3dwinsys)
target_sources(v3dwinsys PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/winsys/v3d/d3dkmt/v3d_d3dkmt_winsys.c")
target_include_directories(v3dwinsys PRIVATE
    "${PROJECT_BINARY_DIR}/src/gallium/winsys/v3d/d3dkmt"
    "${PROJECT_SOURCE_DIR}/src/gallium/winsys/v3d/d3dkmt")

add_library(v3d STATIC EXCLUDE_FROM_ALL)
mesa_v3d_target_defaults(v3d)
target_sources(v3d PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_blit.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_bufmgr.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_cl.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_context.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_disk_cache.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_fence.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_formats.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_job.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_program.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_query.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_query_perfcnt.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_query_pipe.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_resource.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_screen.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers/v3d/v3d_uniforms.c")
target_compile_definitions(v3d PRIVATE
    V3D_BUILD_NEON
    USE_V3D_SIMULATOR=0)
target_link_libraries(v3d PUBLIC
    v3d-v42
    v3d-v71)

target_compile_definitions(mesa_gallium PRIVATE GALLIUM_V3D)
target_link_libraries(mesa_gallium PRIVATE
    v3d
    v3dwinsys
    broadcom_perfcntrs
    v3d-perfcntrs-v42
    v3d-perfcntrs-v71
    v3d-v42
    v3d-v71
    "${MESA_REACTOS_BUILD_DIR}/sdk/lib/vc4kmt/libvc4kmt.a")
