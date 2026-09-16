# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
# Hardware Direct3D 10/11 user-mode driver backed by the ReactOS V3D winsys.

set(_mesa_d3d10umd_sources
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/Adapter.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/Debug.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/Device.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/Draw.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/DxgiFns.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/Format.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/InputAssembly.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/OutputMerger.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/Query.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/Rasterizer.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/Resource.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/Shader.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/ShaderDump.cpp"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/ShaderParse.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd/ShaderTGSI.c")

add_library(rpi5vc4d3d SHARED EXCLUDE_FROM_ALL
    ${_mesa_d3d10umd_sources}
    "${PROJECT_SOURCE_DIR}/src/gallium/targets/d3d10umd/d3d10_v3d.c"
    "${PROJECT_SOURCE_DIR}/src/gallium/targets/d3d10umd/rpi5vc4d3d.def")
mesa_target_defaults(rpi5vc4d3d)

# The UMD declarations are part of the ReactOS DDK.  Keep this include ahead
# of llvm-mingw so the driver and runtime are built from one ABI definition.
target_include_directories(rpi5vc4d3d BEFORE PRIVATE
    "${MESA_REACTOS_SOURCE_DIR}/sdk/include/ddk")
target_include_directories(rpi5vc4d3d PRIVATE
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/winddk"
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/src/util"
    "${PROJECT_BINARY_DIR}/src/util/format"
    "${PROJECT_BINARY_DIR}/src/compiler"
    "${PROJECT_SOURCE_DIR}/src/compiler"
    "${PROJECT_BINARY_DIR}/src/compiler/nir"
    "${PROJECT_SOURCE_DIR}/src/compiler/nir"
    "${PROJECT_SOURCE_DIR}/src/gallium/include"
    "${PROJECT_BINARY_DIR}/src/gallium/auxiliary"
    "${PROJECT_SOURCE_DIR}/src/gallium/auxiliary"
    "${PROJECT_BINARY_DIR}/src/gallium/frontends/d3d10umd"
    "${PROJECT_SOURCE_DIR}/src/gallium/frontends/d3d10umd"
    "${PROJECT_BINARY_DIR}/src/gallium/drivers"
    "${PROJECT_SOURCE_DIR}/src/gallium/drivers"
    "${PROJECT_BINARY_DIR}/src/gallium/winsys"
    "${PROJECT_SOURCE_DIR}/src/gallium/winsys"
    "${PROJECT_SOURCE_DIR}/src/gallium/winsys/v3d/d3dkmt"
    "${PROJECT_BINARY_DIR}/src/broadcom"
    "${PROJECT_SOURCE_DIR}/src/broadcom"
    "${PROJECT_BINARY_DIR}/src/broadcom/cle"
    "${PROJECT_SOURCE_DIR}/src/broadcom/cle"
    "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos"
    "${MESA_REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib")
target_compile_definitions(rpi5vc4d3d PRIVATE GALLIUM_V3D)
target_compile_options(rpi5vc4d3d PRIVATE
    "-idirafter${MESA_REACTOS_SOURCE_DIR}/sdk/include/psdk"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_ALIGN_CHECK=0>"
    "$<$<COMPILE_LANGUAGE:C>:-DXXH_FORCE_MEMORY_ACCESS=0>")
target_link_libraries(rpi5vc4d3d PRIVATE
    "-Wl,-O1"
    "-Wl,--gc-sections"
    "-Wl,--nxcompat"
    "-Wl,--dynamicbase"
    gallium
    nir
    compiler
    mesa_util
    mesa_util_simd
    blake3
    mesa_util_c11
    vtn
    xmlconfig
    broadcom_cle
    broadcom_v3d
    v3d_neon
    v3d
    v3dwinsys
    broadcom_perfcntrs
    v3d-perfcntrs-v42
    v3d-perfcntrs-v71
    v3d-v42
    v3d-v71
    "${MESA_REACTOS_BUILD_DIR}/sdk/lib/vc4kmt/libvc4kmt.a"
    "${MESA_REACTOS_BUILD_DIR}/sdk/lib/3rdparty/zlib/libzlib.a"
    "-static-libgcc"
    "-static-libstdc++"
    "-lm"
    "-lkernel32"
    "-luser32"
    "-lgdi32"
    "-ladvapi32"
    "-lsynchronization"
    "-lws2_32")
set_target_properties(rpi5vc4d3d PROPERTIES
    PREFIX ""
    LINKER_LANGUAGE CXX
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/src/gallium/targets/d3d10umd")
