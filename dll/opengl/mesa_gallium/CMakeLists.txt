# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR MSVC)
    message(FATAL_ERROR "The Mesa Gallium source build requires llvm-mingw Clang; use MESA_GALLIUM_FROM_SOURCE=OFF for the packaged ICDs.")
endif()

set(MESA_SOURCE_DIR "${REACTOS_SOURCE_DIR}/submodules/mesa")
if(NOT EXISTS "${MESA_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "The vendored Mesa source is missing at ${MESA_SOURCE_DIR}")
endif()
# Parallelism is inherited at build time; discard the old fixed job limit.
unset(MESA_BUILD_JOBS CACHE)
set(MESA_LLVM_MINGW_ROOT "${REACTOS_CLANG_LLVM_MINGW_ROOT}" CACHE PATH "llvm-mingw toolchain used by modern Mesa")
if(ARCH STREQUAL "arm64")
    set(MESA_CPU aarch64)
    set(MESA_TRIPLE aarch64-w64-mingw32)
    if(ARM64EC_RUNTIME)
        set(MESA_TRIPLE arm64ec-w64-mingw32)
    endif()
    if(ENABLE_MESA_LLVMPIPE)
        set(MESA_GALLIUM_DRIVERS vc4,v3d,llvmpipe)
    else()
        set(MESA_GALLIUM_DRIVERS vc4,v3d,softpipe)
    endif()
elseif(ARCH STREQUAL "amd64")
    set(MESA_CPU x86_64)
    set(MESA_TRIPLE x86_64-w64-mingw32)
    if(ENABLE_MESA_LLVMPIPE)
        set(MESA_GALLIUM_DRIVERS llvmpipe)
    else()
        set(MESA_GALLIUM_DRIVERS softpipe)
    endif()
elseif(ARCH STREQUAL "i386")
    set(MESA_CPU x86)
    set(MESA_TRIPLE i686-w64-mingw32)
    set(MESA_GALLIUM_DRIVERS v3d,softpipe)
else()
    message(FATAL_ERROR "Mesa Gallium requires a native i386, amd64 or arm64 target.")
endif()
find_program(MESA_CC NAMES ${MESA_TRIPLE}-clang HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_CXX NAMES ${MESA_TRIPLE}-clang++ HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_WINDRES NAMES ${MESA_TRIPLE}-windres HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_AR NAMES llvm-ar HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_STRIP NAMES llvm-strip HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_NINJA NAMES ninja REQUIRED)
find_program(MESA_PYTHON NAMES python3 python REQUIRED)
set(MESA_LLVM_ROOT "" CACHE PATH "Optional prebuilt Windows static LLVM installation for the selected architecture")
if(ENABLE_MESA_LLVMPIPE)
    if(MESA_LLVM_ROOT)
        get_filename_component(MESA_LLVM_PREFIX "${MESA_LLVM_ROOT}" ABSOLUTE)
    else()
        set(MESA_LLVM_PREFIX "${REACTOS_BINARY_DIR}/submodules/mesa-llvmpipe/llvm-install")
    endif()
else()
    set(MESA_LLVM_PREFIX "")
endif()
execute_process(COMMAND "${MESA_PYTHON}" -c "import mako, packaging, yaml"
    RESULT_VARIABLE _mesa_python_status ERROR_VARIABLE _mesa_python_error)
if(NOT _mesa_python_status EQUAL 0)
    message(FATAL_ERROR "Mesa needs Python mako, packaging and PyYAML modules for ${MESA_PYTHON}: ${_mesa_python_error}")
endif()
include("${REACTOS_SOURCE_DIR}/sdk/cmake/nested-build.cmake")

set(MESA_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/mesa-source")
set(MESA_BINARY_DIR "${MESA_WORK_DIR}/cmake-build")
set(MESA_SUPPORT_DIR "${MESA_WORK_DIR}/reactos-release")
set(MESA_DLL "${MESA_BINARY_DIR}/src/gallium/targets/wgl/mesa_gallium.dll")
set(MESA_D3D_DLL "${MESA_BINARY_DIR}/src/gallium/targets/d3d10umd/rpi5vc4d3d.dll")
set(MESA_LAVAPIPE_DLL "${MESA_BINARY_DIR}/src/gallium/targets/lavapipe/vulkan_lvp.dll")
set(MESA_LAVAPIPE_MANIFEST "${MESA_BINARY_DIR}/src/gallium/targets/lavapipe/lvp_icd.json")
set(MESA_DIRECTX_HEADERS_PREFIX "${REACTOS_BINARY_DIR}/submodules/mesa-llvmpipe/directx-headers-install")
set(MESA_GALLIUM_ICD_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mesa-icd")
set(MESA_GALLIUM_ICD_STAGE "${MESA_GALLIUM_ICD_STAGE_DIR}/mesa_gallium.dll")
set(MESA_D3D_UMD_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mesa-d3d")
set(MESA_D3D_UMD_STAGE "${MESA_D3D_UMD_STAGE_DIR}/rpi5vc4d3d.dll")
include(ExternalProject)
set(_mesa_support_dependency)
set(_mesa_platform_options)
if(ARCH STREQUAL "arm64" OR ARCH STREQUAL "i386")
    if(HOST_TOOLS_DIR)
        set(_mesa_host_tools_dir "${HOST_TOOLS_DIR}")
    else()
        set(_mesa_host_tools_dir "${REACTOS_BINARY_DIR}/host-tools/bin")
    endif()
    set(_mesa_abi_args)
    if(ARCH STREQUAL "arm64")
        list(APPEND _mesa_abi_args -DARM64EC_RUNTIME:BOOL=${ARM64EC_RUNTIME})
        if(ARM64EC_RUNTIME)
            list(APPEND _mesa_abi_args
                -DARM64EC_NATIVE_BINARY_DIR:PATH=${ARM64EC_NATIVE_BINARY_DIR})
        endif()
        set(_mesa_support_targets rpi3vc4kmt vc4kmt zlib)
        set(_mesa_support_byproducts
            "${MESA_SUPPORT_DIR}/sdk/lib/rpi3vc4kmt/librpi3vc4kmt.a"
            "${MESA_SUPPORT_DIR}/sdk/lib/vc4kmt/libvc4kmt.a"
            "${MESA_SUPPORT_DIR}/sdk/lib/3rdparty/zlib/libzlib.a")
    else()
        set(_mesa_support_targets vc4kmt zlib)
        set(_mesa_support_byproducts
            "${MESA_SUPPORT_DIR}/sdk/lib/vc4kmt/libvc4kmt.a"
            "${MESA_SUPPORT_DIR}/sdk/lib/3rdparty/zlib/libzlib.a")
    endif()
    # Build the graphics support libraries with the normal ReactOS toolchain
    # in an isolated Release configuration even
    # when the destination OS is Debug. No other OS binaries are built here.
    ExternalProject_Add(mesa_gallium_support
        PREFIX "${MESA_WORK_DIR}/support-prefix"
        SOURCE_DIR "${REACTOS_SOURCE_DIR}"
        BINARY_DIR "${MESA_SUPPORT_DIR}"
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        CMAKE_GENERATOR Ninja
        CMAKE_ARGS
            -DCMAKE_TOOLCHAIN_FILE=${REACTOS_SOURCE_DIR}/toolchain-clang.cmake
            -DHOST_TOOLS_DIR:PATH=${_mesa_host_tools_dir}
            -DARCH=${ARCH}
            ${_mesa_abi_args}
            -DREACTOS_CLANG_LLVM_MINGW_ROOT=${MESA_LLVM_MINGW_ROOT}
            -DREACTOS_GRAPHICS_DRIVER_MODEL=WDDM
            -DREACTOS_WDDM_LEVEL=${REACTOS_WDDM_LEVEL}
            -DCMAKE_BUILD_TYPE=Release
            -DOPTIMIZE=6
            -DDBG=OFF
            -DSEPARATE_DBG=OFF
            -DWITH_DEBUG_SYMBOLS=OFF
            -DENABLE_FEX_ARM64EC=OFF
            -DENABLE_MESA_LLVMPIPE=OFF
            -DMESA_GALLIUM_FROM_SOURCE=OFF
            -DENABLE_ROSTESTS=OFF
            -DRPI3_SUPPORT=OFF
            -DRPI5_SUPPORT=OFF
        BUILD_COMMAND ${REACTOS_NESTED_BUILD} <BINARY_DIR> --target ${_mesa_support_targets}
        BUILD_ALWAYS TRUE
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS ${_mesa_support_byproducts}
        USES_TERMINAL_BUILD TRUE)
    # The support build imports these tools, including in nested ARM64EC builds.
    # Ensure they exist and are current before configuring or building support.
    ExternalProject_Add_StepDependencies(mesa_gallium_support configure host-tools)
    set(_mesa_support_dependency mesa_gallium_support)
    set(_mesa_platform_options
        -DMESA_REACTOS_SOURCE_DIR:PATH=${REACTOS_SOURCE_DIR}
        -DMESA_REACTOS_BUILD_DIR:PATH=${MESA_SUPPORT_DIR})
endif()

set(_mesa_arch ${ARCH})
if(ARM64EC_RUNTIME)
    set(_mesa_arch arm64ec)
endif()
include("${REACTOS_SOURCE_DIR}/submodules/mesa-cmake.cmake")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(_mesa_assertions OFF)
else()
    set(_mesa_assertions ON)
endif()
set(_mesa_build_targets mesa_gallium)
set(_mesa_build_byproducts "${MESA_DLL}")
set(_mesa_d3d_umd_enabled OFF)
if((ARCH STREQUAL "arm64" OR ARCH STREQUAL "i386") AND NOT ARM64EC_RUNTIME)
    set(_mesa_d3d_umd_enabled ON)
    list(APPEND _mesa_build_targets mesa_d3d)
    list(APPEND _mesa_build_byproducts "${MESA_D3D_DLL}")
endif()
if(ENABLE_MESA_LLVMPIPE)
    list(APPEND _mesa_build_targets vulkan_lvp lvp_icd)
    list(APPEND _mesa_build_byproducts "${MESA_LAVAPIPE_DLL}" "${MESA_LAVAPIPE_MANIFEST}")
endif()
ExternalProject_Add(mesa_gallium_build
    PREFIX "${MESA_WORK_DIR}/cmake-prefix"
    SOURCE_DIR "${MESA_SOURCE_DIR}"
    BINARY_DIR "${MESA_BINARY_DIR}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        ${MESA_CMAKE_ARGS}
        -DMESA_ARCH:STRING=${_mesa_arch}
        -DMESA_WGL_DLL_NAME:STRING=mesa_gallium
        -DMESA_LLVMPIPE:BOOL=${ENABLE_MESA_LLVMPIPE}
        -DMESA_LAVAPIPE:BOOL=${ENABLE_MESA_LLVMPIPE}
        -DMESA_ASSERTIONS:BOOL=${_mesa_assertions}
        -DMESA_LLVM_ROOT:PATH=${MESA_LLVM_PREFIX}
        -DMESA_DIRECTX_HEADERS_ROOT:PATH=${MESA_DIRECTX_HEADERS_PREFIX}
        ${_mesa_platform_options}
    BUILD_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1
        ${REACTOS_NESTED_BUILD} <BINARY_DIR> --target ${_mesa_build_targets}
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${_mesa_build_byproducts}
    USES_TERMINAL_BUILD TRUE)

if(ENABLE_MESA_LLVMPIPE)
    # These producers are declared later under submodules/. Attach them here,
    # where ExternalProject's configure-step custom command is scoped.
    set(_mesa_configure_dependencies directx-headers-build)
    if(NOT MESA_LLVM_ROOT)
        list(APPEND _mesa_configure_dependencies mesa-llvm)
    endif()
    ExternalProject_Add_StepDependencies(mesa_gallium_build configure
        ${_mesa_configure_dependencies})
endif()

if(_mesa_support_dependency)
    # Build the support libraries before CMake checks for them, without depending
    # on the ExternalProject completion stamp that changes on every build.
    # Mesa's CMake build tracks changes to the libraries themselves.
    ExternalProject_Add_StepDependencies(mesa_gallium_build configure ${_mesa_support_dependency})
endif()

add_custom_command(
    OUTPUT "${MESA_GALLIUM_ICD_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MESA_GALLIUM_ICD_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MESA_DLL}" "${MESA_GALLIUM_ICD_STAGE}"
    COMMAND ${MESA_STRIP} --strip-debug "${MESA_GALLIUM_ICD_STAGE}"
    DEPENDS mesa_gallium_build "${MESA_DLL}"
    VERBATIM)
add_custom_target(mesa_gallium DEPENDS "${MESA_GALLIUM_ICD_STAGE}")
add_cd_file(FILE "${MESA_GALLIUM_ICD_STAGE}" TARGET mesa_gallium DESTINATION reactos/system32 FOR all)
if(_mesa_d3d_umd_enabled)
    add_custom_command(
        OUTPUT "${MESA_D3D_UMD_STAGE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MESA_D3D_UMD_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MESA_D3D_DLL}" "${MESA_D3D_UMD_STAGE}"
        COMMAND ${MESA_STRIP} --strip-debug "${MESA_D3D_UMD_STAGE}"
        DEPENDS mesa_gallium_build "${MESA_D3D_DLL}"
        VERBATIM)
    add_custom_target(mesa_d3d_umd DEPENDS "${MESA_D3D_UMD_STAGE}")
    add_dependencies(mesa_gallium mesa_d3d_umd)
    add_cd_file(FILE "${MESA_D3D_UMD_STAGE}" TARGET mesa_d3d_umd DESTINATION reactos/system32 FOR all)
endif()
message(STATUS "Mesa Gallium OpenGL ICD (${ARCH}: ${MESA_GALLIUM_DRIVERS}): Release build from ${MESA_SOURCE_DIR}")
