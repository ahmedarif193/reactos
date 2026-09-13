# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR MSVC)
    message(FATAL_ERROR "The Mesa Gallium source build requires llvm-mingw Clang; use MESA_GALLIUM_FROM_SOURCE=OFF for the packaged ICDs.")
endif()

set(MESA_SOURCE_DIR "${REACTOS_SOURCE_DIR}/submodules/mesa")
if(NOT EXISTS "${MESA_SOURCE_DIR}/meson.build")
    message(FATAL_ERROR "Initialize Mesa first: git submodule update --init --depth 1 -- submodules/mesa")
endif()
set(MESA_BUILD_JOBS "4" CACHE STRING "Maximum parallel jobs in the Mesa and LLVM builds")
if(NOT MESA_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "MESA_BUILD_JOBS must be a positive integer.")
endif()
set(MESA_LLVM_MINGW_ROOT "${REACTOS_CLANG_LLVM_MINGW_ROOT}" CACHE PATH "llvm-mingw toolchain used by modern Mesa")
if(ARCH STREQUAL "arm64")
    set(MESA_CPU aarch64)
    set(MESA_TRIPLE aarch64-w64-mingw32)
    if(ARM64EC_RUNTIME)
        set(MESA_TRIPLE arm64ec-w64-mingw32)
    endif()
    set(MESA_GALLIUM_DRIVERS vc4,v3d,softpipe)
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
find_program(MESA_MESON NAMES meson REQUIRED)
find_program(MESA_NINJA NAMES ninja REQUIRED)
find_program(MESA_PYTHON NAMES python3 python REQUIRED)
execute_process(COMMAND "${MESA_PYTHON}" -c "import mako, packaging, yaml"
    RESULT_VARIABLE _mesa_python_status ERROR_VARIABLE _mesa_python_error)
if(NOT _mesa_python_status EQUAL 0)
    message(FATAL_ERROR "Mesa needs Python mako, packaging and PyYAML modules for ${MESA_PYTHON}: ${_mesa_python_error}")
endif()

set(MESA_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/mesa-source")
set(MESA_BINARY_DIR "${MESA_WORK_DIR}/build")
set(MESA_SUPPORT_DIR "${MESA_WORK_DIR}/reactos-release")
set(MESA_DLL "${MESA_BINARY_DIR}/src/gallium/targets/wgl/mesa_gallium.dll")
set(MESA_GALLIUM_ICD_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mesa-icd")
set(MESA_GALLIUM_ICD_STAGE "${MESA_GALLIUM_ICD_STAGE_DIR}/mesa_gallium.dll")
set(MESA_LLVM_PREFIX "")
file(MAKE_DIRECTORY "${MESA_WORK_DIR}")
foreach(_mesa_path MESA_CC MESA_CXX MESA_WINDRES MESA_AR MESA_STRIP MESA_PYTHON MESA_LLVM_PREFIX CMAKE_COMMAND)
    file(TO_CMAKE_PATH "${${_mesa_path}}" ${_mesa_path}_INI)
    string(REPLACE "'" "\\'" ${_mesa_path}_INI "${${_mesa_path}_INI}")
endforeach()
configure_file("${REACTOS_SOURCE_DIR}/submodules/mesa-cross.ini.in" "${MESA_WORK_DIR}/cross.ini" @ONLY)
configure_file("${REACTOS_SOURCE_DIR}/submodules/mesa-native.ini.in" "${MESA_WORK_DIR}/native.ini" @ONLY)

include(ExternalProject)
set(_mesa_support_dependency)
set(_mesa_platform_options -Dzlib=disabled)
if(ARCH STREQUAL "arm64")
    set(_mesa_abi_args -DARM64EC_RUNTIME:BOOL=${ARM64EC_RUNTIME})
    if(ARM64EC_RUNTIME)
        list(APPEND _mesa_abi_args
            -DARM64EC_NATIVE_BINARY_DIR:PATH=${ARM64EC_NATIVE_BINARY_DIR})
    endif()
    # Mesa statically links both KMT transports and zlib. Build those targets with
    # the normal ReactOS toolchain in an isolated Release configuration even
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
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel ${MESA_BUILD_JOBS} --target rpi3vc4kmt vc4kmt zlib
        BUILD_ALWAYS TRUE
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS
            "${MESA_SUPPORT_DIR}/sdk/lib/rpi3vc4kmt/librpi3vc4kmt.a"
            "${MESA_SUPPORT_DIR}/sdk/lib/vc4kmt/libvc4kmt.a"
            "${MESA_SUPPORT_DIR}/sdk/lib/3rdparty/zlib/libzlib.a"
        USES_TERMINAL_BUILD TRUE)
    set(_mesa_support_dependency mesa_gallium_support)
    set(_mesa_platform_options
        -Dzlib=enabled
        -Dreactos-source-dir=${REACTOS_SOURCE_DIR}
        -Dreactos-build-dir=${MESA_SUPPORT_DIR})
endif()

ExternalProject_Add(mesa_gallium_build
    DEPENDS ${_mesa_support_dependency}
    PREFIX "${MESA_WORK_DIR}/prefix"
    SOURCE_DIR "${MESA_SOURCE_DIR}"
    BINARY_DIR "${MESA_BINARY_DIR}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1
        ${MESA_MESON} setup --reconfigure <BINARY_DIR> <SOURCE_DIR>
        --cross-file "${MESA_WORK_DIR}/cross.ini"
        --native-file "${MESA_WORK_DIR}/native.ini"
        --wrap-mode=nofallback
        --buildtype=release
        -Db_ndebug=true
        -Dc_args=-D__REACTOS__
        -Dcpp_args=-D__REACTOS__
        -Dplatforms=windows
        -Dgallium-drivers=${MESA_GALLIUM_DRIVERS}
        -Dgallium-wgl-dll-name=mesa_gallium
        ${_mesa_platform_options}
        -Dllvm=disabled
        -Dvulkan-drivers=
        -Dglx=disabled
        -Degl=disabled
        -Dgbm=disabled
        -Dgles1=disabled
        -Dgles2=disabled
        -Dglvnd=disabled
        -Dgallium-va=disabled
        -Dvideo-codecs=
        -Dxmlconfig=disabled
        -Dzstd=disabled
        -Dlibunwind=disabled
        -Dvalgrind=disabled
        -Dbuild-tests=false
    BUILD_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1
        ${MESA_NINJA} -C <BINARY_DIR> -j ${MESA_BUILD_JOBS} src/gallium/targets/wgl/mesa_gallium.dll
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${MESA_DLL}"
    USES_TERMINAL_BUILD TRUE)

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
