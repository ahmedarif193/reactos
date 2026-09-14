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
    set(MESA_GALLIUM_DRIVERS vc4,softpipe)
elseif(ARCH STREQUAL "amd64")
    set(MESA_CPU x86_64)
    set(MESA_TRIPLE x86_64-w64-mingw32)
    set(MESA_GALLIUM_DRIVERS softpipe)
elseif(ARCH STREQUAL "i386")
    set(MESA_CPU x86)
    set(MESA_TRIPLE i686-w64-mingw32)
    set(MESA_GALLIUM_DRIVERS softpipe)
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
set(MESA_GALLIUM_ICD_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mesa-icd")
set(MESA_GALLIUM_ICD_STAGE "${MESA_GALLIUM_ICD_STAGE_DIR}/mesa_gallium.dll")
include(ExternalProject)
set(_mesa_support_dependency)
set(_mesa_platform_options)
if(ARCH STREQUAL "arm64")
    if(HOST_TOOLS_DIR)
        set(_mesa_host_tools_dir "${HOST_TOOLS_DIR}")
    else()
        set(_mesa_host_tools_dir "${REACTOS_BINARY_DIR}/host-tools/bin")
    endif()
    set(_mesa_abi_args -DARM64EC_RUNTIME:BOOL=${ARM64EC_RUNTIME})
    if(ARM64EC_RUNTIME)
        list(APPEND _mesa_abi_args
            -DARM64EC_NATIVE_BINARY_DIR:PATH=${ARM64EC_NATIVE_BINARY_DIR})
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
            -DARCH=arm64
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
        BUILD_COMMAND ${REACTOS_NESTED_BUILD} <BINARY_DIR> --target rpi3vc4kmt vc4kmt zlib
        BUILD_ALWAYS TRUE
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS
            "${MESA_SUPPORT_DIR}/sdk/lib/rpi3vc4kmt/librpi3vc4kmt.a"
            "${MESA_SUPPORT_DIR}/sdk/lib/vc4kmt/libvc4kmt.a"
            "${MESA_SUPPORT_DIR}/sdk/lib/3rdparty/zlib/libzlib.a"
        USES_TERMINAL_BUILD FALSE)
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
        -DMESA_LLVMPIPE:BOOL=OFF
        -DMESA_ASSERTIONS:BOOL=ON
        ${_mesa_platform_options}
    BUILD_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1
        ${REACTOS_NESTED_BUILD} <BINARY_DIR> --target mesa_gallium
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${MESA_DLL}"
    USES_TERMINAL_BUILD FALSE)

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
message(STATUS "Mesa Gallium OpenGL ICD (${ARCH}: ${MESA_GALLIUM_DRIVERS}): Release build from ${MESA_SOURCE_DIR}")
