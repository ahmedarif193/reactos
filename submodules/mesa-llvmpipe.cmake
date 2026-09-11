# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# Optional modern Mesa build. Upstream sources retain their original licenses.

if(NOT ENABLE_MESA_LLVMPIPE)
    return()
endif()
if(CMAKE_VERSION VERSION_LESS 3.24)
    message(FATAL_ERROR "The optional Mesa LLVMpipe build requires CMake 3.24 or newer.")
endif()
if(NOT ARCH MATCHES "^(amd64|arm64)$" OR ARM64EC_RUNTIME)
    message(FATAL_ERROR "Mesa LLVMpipe requires a native amd64 or arm64 ReactOS target.")
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR MSVC)
    message(FATAL_ERROR "Mesa LLVMpipe currently requires the llvm-mingw Clang toolchain.")
endif()

set(MESA_SOURCE_DIR "${REACTOS_SOURCE_DIR}/submodules/mesa")
if(NOT EXISTS "${MESA_SOURCE_DIR}/meson.build")
    message(FATAL_ERROR "Initialize Mesa first: git submodule update --init --depth 1 -- submodules/mesa")
endif()

set(MESA_BUILD_JOBS "4" CACHE STRING "Maximum parallel jobs in the Mesa and LLVM builds")
if(NOT MESA_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "MESA_BUILD_JOBS must be a positive integer.")
endif()
set(MESA_LLVM_ROOT "" CACHE PATH "Optional prebuilt Windows static LLVM installation for the selected architecture")
set(MESA_LLVM_MINGW_ROOT "${REACTOS_CLANG_LLVM_MINGW_ROOT}" CACHE PATH "llvm-mingw toolchain used by modern Mesa")

if(ARCH STREQUAL "amd64")
    set(MESA_CPU x86_64)
    set(MESA_LLVM_TARGET X86)
else()
    set(MESA_CPU aarch64)
    set(MESA_LLVM_TARGET AArch64)
endif()
set(MESA_TRIPLE "${MESA_CPU}-w64-mingw32")
# Do not cache architecture-specific tool selections across reconfiguration.
find_program(MESA_CC NAMES ${MESA_TRIPLE}-clang HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_CXX NAMES ${MESA_TRIPLE}-clang++ HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_WINDRES NAMES ${MESA_TRIPLE}-windres HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_AR NAMES llvm-ar HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_RANLIB NAMES llvm-ranlib HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_STRIP NAMES llvm-strip HINTS "${MESA_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
find_program(MESA_MESON NAMES meson REQUIRED)
execute_process(COMMAND "${MESA_MESON}" --version OUTPUT_VARIABLE _mesa_meson_version OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _mesa_meson_status)
# Older Meson releases omit LLVM 22 from their CMake dependency search.
if(NOT _mesa_meson_status EQUAL 0 OR _mesa_meson_version VERSION_LESS 1.12.0)
    message(FATAL_ERROR "Modern Mesa with LLVM 22 requires Meson 1.12.0 or newer; set MESA_MESON to a suitable executable (found ${_mesa_meson_version}).")
endif()
find_program(MESA_NINJA NAMES ninja REQUIRED)
find_program(MESA_PYTHON NAMES python3 python REQUIRED)
execute_process(COMMAND "${MESA_PYTHON}" -c "import mako, packaging, yaml" RESULT_VARIABLE _mesa_python_status ERROR_VARIABLE _mesa_python_error)
if(NOT _mesa_python_status EQUAL 0)
    message(FATAL_ERROR "Mesa needs Python mako, packaging and PyYAML modules for ${MESA_PYTHON}: ${_mesa_python_error}")
endif()

set(MESA_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/mesa-llvmpipe")
set(MESA_BINARY_DIR "${MESA_WORK_DIR}/build")
set(MESA_DLL "${MESA_WORK_DIR}/mesadrv.dll")
set(MESA_LICENSES "${MESA_WORK_DIR}/mesa-licenses.zip")
file(MAKE_DIRECTORY "${MESA_WORK_DIR}")
include(ExternalProject)

set(_mesa_llvm_dependency)
if(MESA_LLVM_ROOT)
    get_filename_component(MESA_LLVM_PREFIX "${MESA_LLVM_ROOT}" ABSOLUTE)
    if(NOT EXISTS "${MESA_LLVM_PREFIX}/lib/cmake/llvm/LLVMConfig.cmake")
        message(FATAL_ERROR "MESA_LLVM_ROOT must contain lib/cmake/llvm/LLVMConfig.cmake and Windows static LLVM libraries.")
    endif()
    set(MESA_LLVM_LICENSE_FILE "" CACHE FILEPATH "LLVM license file accompanying a prebuilt MESA_LLVM_ROOT")
    if(NOT EXISTS "${MESA_LLVM_LICENSE_FILE}")
        message(FATAL_ERROR "Set MESA_LLVM_LICENSE_FILE to the license shipped with the prebuilt LLVM libraries.")
    endif()
else()
    set(MESA_LLVM_PREFIX "${MESA_WORK_DIR}/llvm-install")
    set(MESA_LLVM_LICENSE_FILE "${MESA_WORK_DIR}/llvm-source/llvm/LICENSE.TXT")
    # LLVM bootstraps its host TableGen executables when cross-compiling; do
    # not run target Windows llvm-config/TableGen on the build host.
    ExternalProject_Add(mesa-llvm
        PREFIX "${MESA_WORK_DIR}/llvm-prefix"
        URL https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/llvm-project-22.1.8.src.tar.xz
        URL_HASH SHA256=922f1817a0df7b1489272d18134ee0087a8b068828f87ac63b9861b1a9965888
        TIMEOUT 900
        INACTIVITY_TIMEOUT 120
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${MESA_WORK_DIR}/llvm-source"
        SOURCE_SUBDIR llvm
        BINARY_DIR "${MESA_WORK_DIR}/llvm-build"
        CMAKE_GENERATOR Ninja
        CMAKE_ARGS
            -DCMAKE_MAKE_PROGRAM=${MESA_NINJA}
            -DCMAKE_SYSTEM_NAME=Windows
            -DCMAKE_SYSTEM_PROCESSOR=${MESA_CPU}
            -DCMAKE_C_COMPILER=${MESA_CC}
            -DCMAKE_CXX_COMPILER=${MESA_CXX}
            -DCMAKE_RC_COMPILER=${MESA_WINDRES}
            -DCMAKE_AR=${MESA_AR}
            -DCMAKE_RANLIB=${MESA_RANLIB}
            -DCMAKE_C_COMPILER_LAUNCHER=
            -DCMAKE_CXX_COMPILER_LAUNCHER=
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${MESA_LLVM_PREFIX}
            -DLLVM_HOST_TRIPLE=${MESA_TRIPLE}
            -DLLVM_DEFAULT_TARGET_TRIPLE=${MESA_TRIPLE}
            -DLLVM_TARGETS_TO_BUILD=${MESA_LLVM_TARGET}
            -DLLVM_TARGET_ARCH=${MESA_LLVM_TARGET}
            -DLLVM_ENABLE_PROJECTS=
            -DLLVM_ENABLE_RUNTIMES=
            -DLLVM_ENABLE_RTTI=ON
            -DLLVM_ENABLE_EH=OFF
            -DLLVM_ENABLE_ASSERTIONS=OFF
            -DLLVM_ENABLE_ZLIB=OFF
            -DLLVM_ENABLE_ZSTD=OFF
            -DLLVM_ENABLE_LIBXML2=OFF
            -DLLVM_ENABLE_LIBEDIT=OFF
            -DLLVM_ENABLE_TERMINFO=OFF
            -DLLVM_ENABLE_FFI=OFF
            -DLLVM_INCLUDE_TESTS=OFF
            -DLLVM_INCLUDE_EXAMPLES=OFF
            -DLLVM_INCLUDE_BENCHMARKS=OFF
            -DLLVM_INCLUDE_DOCS=OFF
            -DLLVM_BUILD_TOOLS=OFF
            -DLLVM_BUILD_LLVM_DYLIB=OFF
            -DLLVM_LINK_LLVM_DYLIB=OFF
            -DBUILD_SHARED_LIBS=OFF
        BUILD_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1 CMAKE_BUILD_PARALLEL_LEVEL=${MESA_BUILD_JOBS} ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel ${MESA_BUILD_JOBS}
        INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
        USES_TERMINAL_BUILD TRUE)
    set(_mesa_llvm_dependency mesa-llvm)
endif()

# Meson machine files use forward slashes on every host and quoted strings.
foreach(_mesa_path MESA_CC MESA_CXX MESA_WINDRES MESA_AR MESA_STRIP MESA_PYTHON MESA_LLVM_PREFIX CMAKE_COMMAND)
    file(TO_CMAKE_PATH "${${_mesa_path}}" ${_mesa_path}_INI)
    string(REPLACE "'" "\\'" ${_mesa_path}_INI "${${_mesa_path}_INI}")
endforeach()
configure_file("${CMAKE_CURRENT_LIST_DIR}/mesa-cross.ini.in" "${MESA_WORK_DIR}/cross.ini" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/mesa-native.ini.in" "${MESA_WORK_DIR}/native.ini" @ONLY)

ExternalProject_Add(mesa-llvmpipe-build
    DEPENDS ${_mesa_llvm_dependency}
    PREFIX "${MESA_WORK_DIR}/prefix"
    SOURCE_DIR "${MESA_SOURCE_DIR}"
    BINARY_DIR "${MESA_BINARY_DIR}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1 ${MESA_MESON} setup --reconfigure <BINARY_DIR> <SOURCE_DIR>
        --cross-file "${MESA_WORK_DIR}/cross.ini"
        --native-file "${MESA_WORK_DIR}/native.ini"
        --wrap-mode=nofallback
        --buildtype=release
        -Db_ndebug=true
        -Dplatforms=windows
        -Dgallium-drivers=llvmpipe
        -Dgallium-wgl-dll-name=mesadrv
        -Dllvm=enabled
        -Dshared-llvm=disabled
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
        -Dzlib=disabled
        -Dzstd=disabled
        -Dlibunwind=disabled
        -Dvalgrind=disabled
        -Dbuild-tests=false
    BUILD_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1 ${MESA_NINJA} -C <BINARY_DIR> -j ${MESA_BUILD_JOBS} src/gallium/targets/wgl/mesadrv.dll
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${MESA_BINARY_DIR}/src/gallium/targets/wgl/mesadrv.dll"
    USES_TERMINAL_BUILD TRUE)

# Declare the packaged files at the step that actually creates them. This
# also regenerates either file if it was removed after a successful build.
add_custom_command(
    OUTPUT "${MESA_DLL}" "${MESA_LICENSES}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MESA_BINARY_DIR}/src/gallium/targets/wgl/mesadrv.dll" "${MESA_DLL}"
    COMMAND ${MESA_STRIP} --strip-debug "${MESA_DLL}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MESA_WORK_DIR}/license-bundle"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MESA_SOURCE_DIR}/docs/license.rst" "${MESA_WORK_DIR}/license-bundle/Mesa-LICENSE.rst"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${MESA_SOURCE_DIR}/licenses" "${MESA_WORK_DIR}/license-bundle/mesa"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MESA_LLVM_LICENSE_FILE}" "${MESA_WORK_DIR}/license-bundle/LLVM-LICENSE.TXT"
    COMMAND ${CMAKE_COMMAND} -E chdir "${MESA_WORK_DIR}/license-bundle" ${CMAKE_COMMAND} -E tar cf "${MESA_LICENSES}" --format=zip Mesa-LICENSE.rst mesa LLVM-LICENSE.TXT
    DEPENDS mesa-llvmpipe-build "${MESA_BINARY_DIR}/src/gallium/targets/wgl/mesadrv.dll"
    VERBATIM)

add_custom_target(mesa-llvmpipe DEPENDS "${MESA_DLL}" "${MESA_LICENSES}")
add_cd_file(FILE "${MESA_DLL}" TARGET mesa-llvmpipe DESTINATION reactos/system32 FOR all)
add_cd_file(FILE "${MESA_LICENSES}" TARGET mesa-llvmpipe DESTINATION reactos/3rdParty FOR all)
message(STATUS "Modern Mesa: LLVMpipe ${MESA_CPU}, source ${MESA_SOURCE_DIR}, Windows LLVM ${MESA_LLVM_PREFIX}")
