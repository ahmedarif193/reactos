# FEX ARM64EC emulation module for x64 binary support on ARM64 ReactOS.
#
# This builds FEX's arm64ecfex.dll from the FEX submodule using FEX's own
# CMake build system as an external project, then deploys the resulting DLL.
#
# Disabled by default. Configure with -DENABLE_FEX_ARM64EC=ON.

if(NOT ARCH STREQUAL "arm64")
    message(FATAL_ERROR "FEX ARM64EC module is only supported on ARM64 builds")
endif()

set(FEX_UPSTREAM_DIR "${REACTOS_SOURCE_DIR}/submodules/fex-arm64ec")
set(FEX_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fex-arm64ec-src")

if(NOT EXISTS "${FEX_UPSTREAM_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "FEX source not found at ${FEX_UPSTREAM_DIR}. "
        "Run: git submodule update --init --recursive -- submodules/fex-arm64ec")
endif()

# FEX requires its External/* dependencies.
if(NOT EXISTS "${FEX_UPSTREAM_DIR}/External/fmt/CMakeLists.txt")
    message(FATAL_ERROR "FEX dependencies are incomplete (External/fmt/ missing). "
        "Run: git submodule update --init --recursive -- submodules/fex-arm64ec")
endif()

if(NOT EXISTS "${FEX_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Prepared FEX ARM64EC source not found at ${FEX_SOURCE_DIR}. "
        "Run configure.sh for the ARM64 build with -DENABLE_FEX_ARM64EC=ON")
endif()

include(ExternalProject)

set(FEX_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/fex-arm64ec-build")
set(FEX_DLL_SOURCE "${FEX_BINARY_DIR}/Bin/libarm64ecfex.dll")
set(FEX_DLL_DEST   "${CMAKE_CURRENT_BINARY_DIR}/arm64ecfex.dll")

# FEX requires Python 3.9+ for IR/config code generation.
# Use find_program instead of find_package(Python) to avoid internal
# target conflicts with ReactOS's CMakeMacros overlay.
find_program(FEX_PYTHON_EXECUTABLE python3 REQUIRED)

ExternalProject_Add(fex-arm64ec-build
    SOURCE_DIR "${FEX_SOURCE_DIR}"
    BINARY_DIR "${FEX_BINARY_DIR}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        # FEX's ARM64EC Module.cpp needs CONTEXT with AMD64 fields (Rax etc).
        # Only the arm64ec-w64-mingw32 target provides this hybrid CONTEXT.
        # The ARM64EC target is selected explicitly to expose hybrid CONTEXT.
        -DCMAKE_C_COMPILER=${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/arm64ec-w64-mingw32-clang
        -DCMAKE_CXX_COMPILER=${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/arm64ec-w64-mingw32-clang++
        -DCMAKE_ASM_COMPILER=${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/arm64ec-w64-mingw32-clang
        -DCMAKE_C_COMPILER_TARGET=arm64ec-w64-mingw32
        -DCMAKE_CXX_COMPILER_TARGET=arm64ec-w64-mingw32
        -DCMAKE_ASM_COMPILER_TARGET=arm64ec-w64-mingw32
        -DCMAKE_AR=${CMAKE_AR}
        -DCMAKE_DLLTOOL=${CMAKE_DLLTOOL}
        -DCMAKE_LINKER=${CMAKE_LINKER}
        -DCMAKE_RC_COMPILER=${CMAKE_RC_COMPILER}
        -DCMAKE_SYSROOT=${CMAKE_SYSROOT}
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
        -DCMAKE_C_FLAGS=-D__reactos__
        -DCMAKE_CXX_FLAGS=-D__reactos__
        -DCMAKE_ASM_FLAGS=-D__reactos__
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
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FEX_DLL_SOURCE}"
        "${FEX_DLL_DEST}"
    BUILD_BYPRODUCTS "${FEX_DLL_DEST}"
    USES_TERMINAL_BUILD OFF
)

# Deploy uncompressed so ntdll can load the emulator during process startup.
add_cd_file(
    FILE "${FEX_DLL_DEST}"
    DESTINATION reactos/system32
    NAME_ON_CD arm64ecfex.dll
    NO_CAB
    FOR all)

# Ensure the DLL is built before packaging.
add_dependencies(bootcd fex-arm64ec-build)
add_dependencies(livecd fex-arm64ec-build)

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

message(STATUS "FEX ARM64EC: submodule   = ${FEX_UPSTREAM_DIR}")
message(STATUS "FEX ARM64EC: source dir  = ${FEX_SOURCE_DIR}")
message(STATUS "FEX ARM64EC: output DLL   = ${FEX_DLL_DEST}")
message(STATUS "FEX ARM64EC: deploy path  = reactos/system32/arm64ecfex.dll")
