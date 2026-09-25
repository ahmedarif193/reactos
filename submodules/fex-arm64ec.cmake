# FEX ARM64EC emulation module for x64 binary support on ARM64 ReactOS.
#
# This builds FEX's arm64ecfex.dll from the vendored FEX source using FEX's own
# CMake build system as an external project, then deploys the resulting DLL.
#
# Enabled by default on ARM64. Configure with -DENABLE_FEX_ARM64EC=OFF to
# exclude it explicitly.

if(NOT ARCH STREQUAL "arm64")
    message(FATAL_ERROR "FEX ARM64EC module is only supported on ARM64 builds")
endif()

set(FEX_SOURCE_DIR "${REACTOS_SOURCE_DIR}/submodules/fex-arm64ec")
set(FEX_ARM64EC_UNAVAILABLE_REASON)

if(NOT EXISTS "${FEX_SOURCE_DIR}/CMakeLists.txt")
    set(FEX_ARM64EC_UNAVAILABLE_REASON "vendored source is missing at ${FEX_SOURCE_DIR}")
elseif(NOT EXISTS "${FEX_SOURCE_DIR}/External/fmt/CMakeLists.txt" OR
       NOT EXISTS "${FEX_SOURCE_DIR}/External/range-v3/CMakeLists.txt" OR
       NOT EXISTS "${FEX_SOURCE_DIR}/External/rpmalloc/CMakeLists.txt" OR
       NOT EXISTS "${FEX_SOURCE_DIR}/External/unordered_dense/CMakeLists.txt" OR
       NOT EXISTS "${FEX_SOURCE_DIR}/External/xxhash/cmake_unofficial/CMakeLists.txt")
    set(FEX_ARM64EC_UNAVAILABLE_REASON "vendored source dependencies are incomplete")
elseif(NOT EXISTS "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/arm64ec-w64-mingw32-clang" OR
       NOT EXISTS "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/arm64ec-w64-mingw32-clang++")
    set(FEX_ARM64EC_UNAVAILABLE_REASON "the ARM64EC Clang toolchain is unavailable")
endif()

if(NOT FEX_ARM64EC_UNAVAILABLE_REASON)
    find_program(FEX_LLVM_STRIP llvm-strip HINTS "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin")
    if(NOT FEX_LLVM_STRIP OR NOT EXISTS "${FEX_LLVM_STRIP}")
        set(FEX_ARM64EC_UNAVAILABLE_REASON "llvm-strip is unavailable")
    endif()
endif()

if(NOT FEX_ARM64EC_UNAVAILABLE_REASON)
    # FEX requires Python 3.9+ for IR/config code generation. Use find_program
    # instead of find_package(Python) to avoid internal target conflicts with
    # ReactOS's CMakeMacros overlay.
    find_program(FEX_PYTHON_EXECUTABLE python3)
    if(NOT FEX_PYTHON_EXECUTABLE OR NOT EXISTS "${FEX_PYTHON_EXECUTABLE}")
        set(FEX_ARM64EC_UNAVAILABLE_REASON "Python 3 is unavailable")
    endif()
endif()

if(NOT EXISTS "${FEX_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "FEX ARM64EC: the fex-arm64ec feed is not checked out; skipping it. "
        "Run scripts/feeds update fex-arm64ec to build it.")
    return()
endif()

if(FEX_ARM64EC_UNAVAILABLE_REASON)
    message(FATAL_ERROR "FEX ARM64EC is enabled but unavailable: ${FEX_ARM64EC_UNAVAILABLE_REASON}. "
        "Use -DENABLE_FEX_ARM64EC=OFF to disable it explicitly.")
endif()

set(FEX_ARM64EC_AVAILABLE ON)
include(ExternalProject)

# FEX is a compiler, and its own optimisation level multiplies into every block
# it translates.  Forwarding a Debug CMAKE_BUILD_TYPE builds the JIT -O0 and
# makes cold code catastrophically slow -- measured at 47.7us to translate one
# ten-byte stub, against 9ns to run it once translated -- which is paid on
# first execution and is what makes an emulated program crawl while it loads
# and run normally afterwards.  Debugging ReactOS does not require an
# unoptimised emulator, so keep both front-ends in Release.
set(FEX_ARM64EC_BUILD_TYPE "Release" CACHE STRING
    "CMAKE_BUILD_TYPE used for the FEX emulators themselves" FORCE)
option(ENABLE_FEX_UNIT_TESTS "Build FEX's native ARM64 instruction-test runner and assembly corpora" OFF)

# FEX picks exactly one Windows front-end per configure:
#   Source/Windows/CMakeLists.txt
#     if (ARCHITECTURE_arm64ec)  -> ARM64EC -> libarm64ecfex.dll, x86-64 guests
#     elseif (ARCHITECTURE_arm64) -> WOW64  -> libwow64fex.dll,   i386 guests
# They are mutually exclusive, so shipping both needs a second build of the
# same source configured for plain aarch64.  It shares FEX_ARM64EC_BUILD_TYPE:
# the i386 emulator is a compiler too and pays the same -O0 penalty.
set(FEX_WOW64_AVAILABLE ON)
set(FEX_WOW64_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/fex-wow64-build")
set(FEX_WOW64_DLL_SOURCE "${FEX_WOW64_BINARY_DIR}/Bin/libwow64fex.dll")
set(FEX_WOW64_DLL_DEST   "${CMAKE_CURRENT_BINARY_DIR}/wow64fex.dll")
set(FEX_WOW64_DLL_SYMBOLS "${REACTOS_BINARY_DIR}/symbols/wow64fex.dll")
set(FEX_WOW64_BUILD_TARGETS wow64fex)
if(ENABLE_FEX_UNIT_TESTS)
    list(APPEND FEX_WOW64_BUILD_TARGETS TestHarnessRunner asm_files 32bit_asm_files)
endif()

set(FEX_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/fex-arm64ec-build")
set(FEX_DLL_SOURCE "${FEX_BINARY_DIR}/Bin/libarm64ecfex.dll")
set(FEX_DLL_DEST   "${CMAKE_CURRENT_BINARY_DIR}/arm64ecfex.dll")
set(FEX_DLL_SYMBOLS "${REACTOS_BINARY_DIR}/symbols/arm64ecfex.dll")
set(FEX_ARM64EC_INCLUDE_DIR
    "${REACTOS_CLANG_LLVM_MINGW_ROOT}/aarch64-w64-mingw32/include")
set(FEX_ARM64EC_CXX_INCLUDE_DIR "${FEX_ARM64EC_INCLUDE_DIR}/c++/v1")
set(FEX_ARM64EC_LIBRARY_DIR
    "${REACTOS_CLANG_LLVM_MINGW_ROOT}/aarch64-w64-mingw32/lib")

function(fex_discard_stale_build _name _binary_dir)
    set(_cache "${_binary_dir}/CMakeCache.txt")
    if(NOT EXISTS "${_cache}")
        return()
    endif()
    file(STRINGS "${_cache}" _home REGEX "^CMAKE_HOME_DIRECTORY:INTERNAL=")
    string(REPLACE "CMAKE_HOME_DIRECTORY:INTERNAL=" "" _home "${_home}")
    if(_home STREQUAL FEX_SOURCE_DIR)
        return()
    endif()
    message(STATUS "FEX: discarding ${_binary_dir} configured from ${_home}")
    file(REMOVE_RECURSE "${_binary_dir}" "${CMAKE_CURRENT_BINARY_DIR}/${_name}-prefix")
endfunction()

fex_discard_stale_build(fex-arm64ec-build "${FEX_BINARY_DIR}")
fex_discard_stale_build(fex-wow64-build "${FEX_WOW64_BINARY_DIR}")
file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/fex-arm64ec-src")

ExternalProject_Add(fex-arm64ec-build
    EXCLUDE_FROM_ALL TRUE
    SOURCE_DIR "${FEX_SOURCE_DIR}"
    BINARY_DIR "${FEX_BINARY_DIR}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    # Let FEX's build graph check the tracked source on every invocation.
    # An ExternalProject build stamp cannot track edits inside the source tree.
    BUILD_ALWAYS TRUE
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${FEX_ARM64EC_BUILD_TYPE}
        # FEX's ARM64EC Module.cpp needs CONTEXT with AMD64 fields (Rax etc).
        # Only the arm64ec-w64-mingw32 target provides this hybrid CONTEXT.
        # Let the target-prefixed compiler wrappers select it. Passing the
        # target explicitly bypasses llvm-mingw's mapped C/C++ include paths
        # when Clang 23 scans module dependencies.
        -DCMAKE_C_COMPILER=${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/arm64ec-w64-mingw32-clang
        -DCMAKE_CXX_COMPILER=${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/arm64ec-w64-mingw32-clang++
        -DCMAKE_ASM_COMPILER=${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/arm64ec-w64-mingw32-clang
        -DCMAKE_AR=${CMAKE_AR}
        -DCMAKE_DLLTOOL=${CMAKE_DLLTOOL}
        -DCMAKE_LINKER=${CMAKE_LINKER}
        -DCMAKE_RC_COMPILER=${CMAKE_RC_COMPILER}
        -DCMAKE_SYSROOT=${CMAKE_SYSROOT}
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
        # clang-scan-deps does not retain the target-prefixed wrapper's mapped
        # include paths in Clang 23, so provide the llvm-mingw target headers.
        "-DCMAKE_C_FLAGS=-D__REACTOS__ -isystem${FEX_ARM64EC_INCLUDE_DIR}"
        "-DCMAKE_CXX_FLAGS=-D__REACTOS__ -isystem${FEX_ARM64EC_CXX_INCLUDE_DIR} -isystem${FEX_ARM64EC_INCLUDE_DIR}"
        -DCMAKE_ASM_FLAGS=-D__REACTOS__
        # llvm-mingw shares ARM64 headers and import libraries with ARM64EC,
        # but Clang 23's ARM64EC driver no longer maps the library directory
        # when FEX links with -nostdlib.
        "-DCMAKE_SHARED_LINKER_FLAGS=-L${FEX_ARM64EC_LIBRARY_DIR}"
        # This is a Windows ARM64EC cross-build. FEX's default native tuning
        # probes Linux /proc/cpuinfo from the build host, which is not useful.
        -DTUNE_CPU=none
        # Avoid resolving a host fmt package while cross-compiling.
        -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON
        -DREACTOS=ON
        # ARM64EC-clang also defines __x86_64 which confuses FEX's
        # host-arch check. Allow x86_64 host builds.
        -DENABLE_X86_HOST_DEBUG=ON
        # Force FEX to detect the ARM64EC architecture to build ARM64EC module.
        -DCMAKE_SYSTEM_NAME=Windows
        -DCMAKE_SYSTEM_PROCESSOR=arm64ec
        # Disable everything we do not need.
        -DBUILD_TESTING=OFF
        -DBUILD_FEX_LINUX_TESTS=OFF
        -DBUILD_THUNKS=OFF
        -DBUILD_FEXCONFIG=OFF
        # Only the emulator DLL is staged; skip unused JSON install-data scans.
        -DINSTALL_CONFIG_FILES:BOOL=OFF
        -DENABLE_ASSERTIONS=ON
        -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF
        -DENABLE_GDB_SYMBOLS=OFF
        -DENABLE_VIXL_DISASSEMBLER=OFF
        -DENABLE_VIXL_SIMULATOR=OFF
        -DENABLE_ZYDIS=OFF
        -DENABLE_FEXCORE_PROFILER=OFF
        -DENABLE_OFFLINE_TELEMETRY=OFF
        -DENABLE_LTO=OFF
        -DUSE_PDB_DEBUGINFO=OFF
        -DOVERRIDE_VERSION=ReactOS
        -DOVERRIDE_HASH=0000000000000000000000000000000000000000
        # Python for code generation.
        -DPython_EXECUTABLE=${FEX_PYTHON_EXECUTABLE}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target arm64ecfex
    INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory "${REACTOS_BINARY_DIR}/symbols"
    COMMAND ${FEX_LLVM_STRIP} --only-keep-debug "${FEX_DLL_SOURCE}" -o "${FEX_DLL_SYMBOLS}"
    COMMAND ${FEX_LLVM_STRIP} --strip-debug "${FEX_DLL_SOURCE}" -o "${FEX_DLL_DEST}"
    BUILD_BYPRODUCTS "${FEX_DLL_DEST}" "${FEX_DLL_SYMBOLS}"
    USES_TERMINAL_BUILD OFF
)

# The i386 emulator uses a separate build directory and output from ARM64EC,
# so the two nested builds can run concurrently.
ExternalProject_Add(fex-wow64-build
    EXCLUDE_FROM_ALL TRUE
    SOURCE_DIR "${FEX_SOURCE_DIR}"
    BINARY_DIR "${FEX_WOW64_BINARY_DIR}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    BUILD_ALWAYS TRUE
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${FEX_ARM64EC_BUILD_TYPE}
        -DCMAKE_C_COMPILER=${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-clang
        -DCMAKE_CXX_COMPILER=${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-clang++
        -DCMAKE_ASM_COMPILER=${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-clang
        -DCMAKE_AR=${CMAKE_AR}
        # Generic llvm-dlltool defaults to x64; WOW64 needs ARM64 import libraries.
        -DCMAKE_DLLTOOL=${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-dlltool
        -DCMAKE_LINKER=${CMAKE_LINKER}
        -DCMAKE_RC_COMPILER=${CMAKE_RC_COMPILER}
        -DCMAKE_SYSROOT=${CMAKE_SYSROOT}
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
        "-DCMAKE_C_FLAGS=-D__REACTOS__ -isystem${FEX_ARM64EC_INCLUDE_DIR}"
        "-DCMAKE_CXX_FLAGS=-D__REACTOS__ -isystem${FEX_ARM64EC_CXX_INCLUDE_DIR} -isystem${FEX_ARM64EC_INCLUDE_DIR}"
        -DCMAKE_ASM_FLAGS=-D__REACTOS__
        # ReactOS disables FEX's CRT substitutes; match the ARM64EC CRT libraries.
        "-DCMAKE_SHARED_LINKER_FLAGS=-L${FEX_ARM64EC_LIBRARY_DIR} -lucrt -lmingwex"
        -DTUNE_CPU=none
        -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON
        -DREACTOS=ON
        -DCMAKE_SYSTEM_NAME=Windows
        -DCMAKE_SYSTEM_PROCESSOR=aarch64
        -DBUILD_TESTING=${ENABLE_FEX_UNIT_TESTS}
        -DBUILD_FEX_LINUX_TESTS=OFF
        -DBUILD_THUNKS=OFF
        -DBUILD_FEXCONFIG=OFF
        -DINSTALL_CONFIG_FILES:BOOL=OFF
        -DENABLE_ASSERTIONS=ON
        -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF
        -DENABLE_GDB_SYMBOLS=OFF
        -DENABLE_VIXL_DISASSEMBLER=OFF
        -DENABLE_VIXL_SIMULATOR=OFF
        -DENABLE_ZYDIS=OFF
        -DENABLE_FEXCORE_PROFILER=OFF
        -DENABLE_OFFLINE_TELEMETRY=OFF
        -DENABLE_LTO=OFF
        -DUSE_PDB_DEBUGINFO=OFF
        -DOVERRIDE_VERSION=ReactOS
        -DOVERRIDE_HASH=0000000000000000000000000000000000000000
        -DPython_EXECUTABLE=${FEX_PYTHON_EXECUTABLE}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target ${FEX_WOW64_BUILD_TARGETS}
    INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory "${REACTOS_BINARY_DIR}/symbols"
    COMMAND ${FEX_LLVM_STRIP} --only-keep-debug "${FEX_WOW64_DLL_SOURCE}" -o "${FEX_WOW64_DLL_SYMBOLS}"
    COMMAND ${FEX_LLVM_STRIP} --strip-debug "${FEX_WOW64_DLL_SOURCE}" -o "${FEX_WOW64_DLL_DEST}"
    BUILD_BYPRODUCTS "${FEX_WOW64_DLL_DEST}" "${FEX_WOW64_DLL_SYMBOLS}"
    USES_TERMINAL_BUILD OFF
)
# Deploy uncompressed so ntdll can load the emulator during process startup.
add_cd_file(
    TARGET fex-arm64ec-build
    FILE "${FEX_DLL_DEST}"
    DESTINATION reactos/system32
    NAME_ON_CD arm64ecfex.dll
    NO_CAB
    OPTIONAL
    FOR all)

add_cd_file(
    TARGET fex-wow64-build
    FILE "${FEX_WOW64_DLL_DEST}"
    DESTINATION reactos/system32
    NAME_ON_CD wow64fex.dll
    NO_CAB
    OPTIONAL
    FOR all)
add_dependencies(submodules fex-arm64ec-build fex-wow64-build)

if(ENABLE_FEX_ARM64EC_TEST_PAYLOADS)
    set(FEX_ARM64EC_AMD64_TEST_BINARY
        "${REACTOS_SOURCE_DIR}/output-Clang-amd64-debug/modules/rostests/win32/cmd/cmd_rostest.exe"
        CACHE FILEPATH "Optional AMD64 test binary to deploy as cmd_rostest_x64.exe")

    if(EXISTS "${FEX_ARM64EC_AMD64_TEST_BINARY}")
        set(FEX_ARM64EC_AMD64_TEST_DEST "${CMAKE_CURRENT_BINARY_DIR}/cmd_rostest_x64.exe")
        add_custom_command(
            OUTPUT "${FEX_ARM64EC_AMD64_TEST_DEST}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FEX_ARM64EC_AMD64_TEST_BINARY}"
                "${FEX_ARM64EC_AMD64_TEST_DEST}"
            COMMENT "Copying optional AMD64 cmd_rostest.exe for CHPE testing")
        add_custom_target(fex-arm64ec-amd64-test-binary
            DEPENDS "${FEX_ARM64EC_AMD64_TEST_DEST}")
        add_dependencies(bootcd fex-arm64ec-amd64-test-binary)
        add_dependencies(livecd fex-arm64ec-amd64-test-binary)

        add_cd_file(
            FILE "${FEX_ARM64EC_AMD64_TEST_DEST}"
            DESTINATION reactos/system32
            NAME_ON_CD cmd_rostest_x64.exe
            NO_CAB
            FOR all)
        message(STATUS "FEX ARM64EC: AMD64 test binary = ${FEX_ARM64EC_AMD64_TEST_BINARY}")
    endif()

    set(FEX_ARM64EC_AMD64_NTDLL_APITEST_BINARY
        "${REACTOS_SOURCE_DIR}/output-Clang-amd64-debug/modules/rostests/apitests/ntdll/ntdll_apitest.exe"
        CACHE FILEPATH "Optional AMD64 ntdll_apitest binary to deploy as ntdll_apitest_x64.exe")

    if(EXISTS "${FEX_ARM64EC_AMD64_NTDLL_APITEST_BINARY}")
        set(FEX_ARM64EC_AMD64_NTDLL_APITEST_DEST "${CMAKE_CURRENT_BINARY_DIR}/ntdll_apitest_x64.exe")
        add_custom_command(
            OUTPUT "${FEX_ARM64EC_AMD64_NTDLL_APITEST_DEST}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FEX_ARM64EC_AMD64_NTDLL_APITEST_BINARY}"
                "${FEX_ARM64EC_AMD64_NTDLL_APITEST_DEST}"
            COMMENT "Copying optional AMD64 ntdll_apitest.exe for CHPE testing")
        add_custom_target(fex-arm64ec-amd64-ntdll-apitest-binary
            DEPENDS "${FEX_ARM64EC_AMD64_NTDLL_APITEST_DEST}")
        add_dependencies(bootcd fex-arm64ec-amd64-ntdll-apitest-binary)
        add_dependencies(livecd fex-arm64ec-amd64-ntdll-apitest-binary)

        add_cd_file(
            FILE "${FEX_ARM64EC_AMD64_NTDLL_APITEST_DEST}"
            DESTINATION reactos/system32
            NAME_ON_CD ntdll_apitest_x64.exe
            NO_CAB
            FOR all)
        message(STATUS "FEX ARM64EC: AMD64 ntdll apitest binary = ${FEX_ARM64EC_AMD64_NTDLL_APITEST_BINARY}")
    endif()
else()
    # Older configurations created these cache entries unconditionally. Drop
    # them when diagnostics are disabled so a neighboring AMD64 tree cannot
    # remain an implicit input to an ordinary ARM64 image.
    unset(FEX_ARM64EC_AMD64_TEST_BINARY CACHE)
    unset(FEX_ARM64EC_AMD64_NTDLL_APITEST_BINARY CACHE)
endif()

message(STATUS "FEX ARM64EC: source dir  = ${FEX_SOURCE_DIR}")
message(STATUS "FEX ARM64EC: output DLL   = ${FEX_DLL_DEST}")
message(STATUS "FEX ARM64EC: deploy path  = reactos/system32/arm64ecfex.dll")
