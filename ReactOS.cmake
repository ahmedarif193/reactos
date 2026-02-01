# ReactOS Build Configuration
# This file contains all build metadata and configuration options

# Export compile commands for clangd IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# WOW64 multilib: build a 32-bit SysWOW64 subset inside amd64 builds
set(WOW64_MULTILIB OFF CACHE BOOL "Build a 32-bit SysWOW64 subset inside amd64 builds (no external i386 root required)")

# Build type (Debug, Release, MinSizeRel, RelWithDebInfo)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build." FORCE)
endif()

# Target architecture (i386, amd64, arm, arm64)
if(NOT ARCH)
    if(DEFINED ENV{ROS_ARCH})
        set(ARCH "$ENV{ROS_ARCH}" CACHE STRING "Target architecture" FORCE)
    else()
        set(ARCH "amd64" CACHE STRING "Target architecture" FORCE)
    endif()
endif()

# Build environment
if(NOT DEFINED BUILD_ENVIRONMENT OR BUILD_ENVIRONMENT STREQUAL "")
    if(CMAKE_TOOLCHAIN_FILE MATCHES "toolchain-clang\\.cmake$")
        set(_REACTOS_DEFAULT_BUILD_ENV "Clang")
    else()
        set(_REACTOS_DEFAULT_BUILD_ENV "MinGW")
    endif()
    set(BUILD_ENVIRONMENT "${_REACTOS_DEFAULT_BUILD_ENV}" CACHE STRING "Build environment (MinGW, Clang, MSVC)" FORCE)
    unset(_REACTOS_DEFAULT_BUILD_ENV)
endif()

# Host detection (initial cache files may run before CMAKE_HOST_* is set)
set(_REACTOS_HOST_SYSTEM_NAME "${CMAKE_HOST_SYSTEM_NAME}")
if(NOT _REACTOS_HOST_SYSTEM_NAME)
    execute_process(COMMAND uname -s OUTPUT_VARIABLE _REACTOS_HOST_SYSTEM_NAME OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
set(_REACTOS_HOST_SYSTEM_PROCESSOR "${CMAKE_HOST_SYSTEM_PROCESSOR}")
if(NOT _REACTOS_HOST_SYSTEM_PROCESSOR)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE _REACTOS_HOST_SYSTEM_PROCESSOR OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

set(_REACTOS_LLVM_MINGW_ROOT "")
set(_REACTOS_LLVM_MINGW_HAS_BIN FALSE)
if(_REACTOS_HOST_SYSTEM_NAME STREQUAL "Linux" AND BUILD_ENVIRONMENT STREQUAL "Clang")
    if(DEFINED ENV{ROS_LLVM_MINGW_ROOT})
        set(_REACTOS_LLVM_MINGW_ROOT "$ENV{ROS_LLVM_MINGW_ROOT}")
    elseif(DEFINED ENV{LLVM_MINGW_ROOT})
        set(_REACTOS_LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
    else()
        set(_REACTOS_LLVM_MINGW_ROOT "$ENV{HOME}/mingw-toolchains/llvm-mingw-20251202-ucrt-ubuntu-22.04-x86_64")
    endif()
    if(EXISTS "${_REACTOS_LLVM_MINGW_ROOT}/bin")
        set(_REACTOS_LLVM_MINGW_HAS_BIN TRUE)
    endif()
endif()

# Clang defaults
if(BUILD_ENVIRONMENT STREQUAL "Clang")
    if(_REACTOS_HOST_SYSTEM_NAME STREQUAL "Darwin" AND _REACTOS_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
        if(NOT DEFINED CLANG_VERSION)
            set(CLANG_VERSION "" CACHE STRING "Clang version suffix (e.g. 21)" FORCE)
        else()
            set(CLANG_VERSION "${CLANG_VERSION}" CACHE STRING "Clang version suffix (e.g. 21)")
        endif()
    elseif(_REACTOS_LLVM_MINGW_HAS_BIN AND (ARCH STREQUAL "amd64" OR ARCH STREQUAL "x86_64" OR ARCH STREQUAL "arm64" OR ARCH STREQUAL "aarch64"))
        if(NOT DEFINED CLANG_VERSION)
            set(CLANG_VERSION "" CACHE STRING "Clang version suffix (e.g. 21)" FORCE)
        else()
            set(CLANG_VERSION "${CLANG_VERSION}" CACHE STRING "Clang version suffix (e.g. 21)")
        endif()
    else()
        if(NOT DEFINED CLANG_VERSION OR CLANG_VERSION STREQUAL "")
            set(CLANG_VERSION "21" CACHE STRING "Clang version suffix (e.g. 21)" FORCE)
        else()
            set(CLANG_VERSION "${CLANG_VERSION}" CACHE STRING "Clang version suffix (e.g. 21)")
        endif()
    endif()
endif()

# Toolchain path (for MinGW cross-compiler)
if(NOT TOOLCHAIN_PATH)
    if(BUILD_ENVIRONMENT STREQUAL "Clang" AND _REACTOS_HOST_SYSTEM_NAME STREQUAL "Darwin" AND _REACTOS_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
        set(TOOLCHAIN_PATH "/Users/mac/mingw-toolchains/llvm-mingw-20251216-ucrt-macos-universal/bin"
            CACHE PATH "Path to toolchain binaries" FORCE)
    elseif(BUILD_ENVIRONMENT STREQUAL "Clang" AND _REACTOS_HOST_SYSTEM_NAME STREQUAL "Linux"
        AND _REACTOS_LLVM_MINGW_HAS_BIN AND (ARCH STREQUAL "amd64" OR ARCH STREQUAL "x86_64" OR ARCH STREQUAL "arm64" OR ARCH STREQUAL "aarch64"))
        set(TOOLCHAIN_PATH "${_REACTOS_LLVM_MINGW_ROOT}/bin"
            CACHE PATH "Path to toolchain binaries" FORCE)
    elseif(DEFINED ENV{ROS_TOOLCHAIN_PATH})
        set(TOOLCHAIN_PATH "$ENV{ROS_TOOLCHAIN_PATH}" CACHE PATH "Path to toolchain binaries" FORCE)
    else()
        set(_default_toolchain_root "$ENV{HOME}/mingw-toolchains")
        if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "x86_64")
            set(_toolchain_dir "${_default_toolchain_root}/x86_64-w64-mingw32/bin")
        elseif(ARCH STREQUAL "i386" OR ARCH STREQUAL "x86")
            set(_toolchain_dir "${_default_toolchain_root}/i686-w64-mingw32/bin")
        elseif(ARCH STREQUAL "arm")
            set(_toolchain_dir "${_default_toolchain_root}/arm-w64-mingw32/bin")
        elseif(ARCH STREQUAL "arm64" OR ARCH STREQUAL "aarch64")
            set(_toolchain_dir "${_default_toolchain_root}/aarch64-w64-mingw32/bin")
        else()
            set(_toolchain_dir "${_default_toolchain_root}/x86_64-w64-mingw32/bin")
        endif()
        set(TOOLCHAIN_PATH "${_toolchain_dir}" CACHE PATH "Path to toolchain binaries" FORCE)
    endif()
endif()

# Set toolchain prefix based on architecture
if(NOT TOOLCHAIN_PREFIX)
    if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "x86_64")
        set(TOOLCHAIN_PREFIX "x86_64-w64-mingw32" CACHE STRING "Toolchain prefix" FORCE)
    elseif(ARCH STREQUAL "i386" OR ARCH STREQUAL "x86")
        set(TOOLCHAIN_PREFIX "i686-w64-mingw32" CACHE STRING "Toolchain prefix" FORCE)
    elseif(ARCH STREQUAL "arm")
        set(TOOLCHAIN_PREFIX "arm-w64-mingw32" CACHE STRING "Toolchain prefix" FORCE)
    elseif(ARCH STREQUAL "arm64")
        set(TOOLCHAIN_PREFIX "aarch64-w64-mingw32" CACHE STRING "Toolchain prefix" FORCE)
    endif()
endif()

if(BUILD_ENVIRONMENT STREQUAL "Clang" AND _REACTOS_HOST_SYSTEM_NAME STREQUAL "Linux")
    if(NOT DEFINED ROS_GNU_MINGW_TOOLCHAIN_PATH OR ROS_GNU_MINGW_TOOLCHAIN_PATH STREQUAL "")
        if(DEFINED ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH} AND NOT "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
            set(ROS_GNU_MINGW_TOOLCHAIN_PATH "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}"
                CACHE PATH "Path to GNU MinGW toolchain (bin)")
        elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
            set(_ros_gnu_toolchain_bin "$ENV{HOME}/mingw-toolchains/${TOOLCHAIN_PREFIX}/bin")
            if(EXISTS "${_ros_gnu_toolchain_bin}")
                set(ROS_GNU_MINGW_TOOLCHAIN_PATH "${_ros_gnu_toolchain_bin}"
                    CACHE PATH "Path to GNU MinGW toolchain (bin)")
            endif()
            unset(_ros_gnu_toolchain_bin)
        endif()
    endif()
endif()

# Toolchain file
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/toolchain-gcc.cmake" CACHE FILEPATH "Path to toolchain file" FORCE)
endif()

# Generator
if(NOT CMAKE_GENERATOR)
    set(CMAKE_GENERATOR "Ninja" CACHE STRING "CMake generator to use" FORCE)
endif()

# Output directory name pattern
if(NOT REACTOS_OUTPUT_PATH)
    string(TOLOWER ${ARCH} ARCH_LOWER)
    set(REACTOS_OUTPUT_PATH "output-${BUILD_ENVIRONMENT}-${ARCH_LOWER}" CACHE STRING "Output directory path" FORCE)
endif()

# Enable ccache
if(NOT DEFINED ENABLE_CCACHE)
    set(ENABLE_CCACHE OFF CACHE BOOL "Enable ccache for faster rebuilds" FORCE)
endif()

# Additional CMake options
set(REACTOS_CMAKE_OPTIONS "" CACHE STRING "Additional CMake options")

# Export toolchain variables for CMake toolchain file
set(ENV{TOOLCHAIN_PATH} "${TOOLCHAIN_PATH}")
set(ENV{TOOLCHAIN_PREFIX} "${TOOLCHAIN_PREFIX}")

# Print configuration summary
message(STATUS "ReactOS Build Configuration:")
message(STATUS "  Architecture: ${ARCH}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Environment: ${BUILD_ENVIRONMENT}")
message(STATUS "  Output Path: ${REACTOS_OUTPUT_PATH}")
message(STATUS "  Generator: ${CMAKE_GENERATOR}")
message(STATUS "  Toolchain Path: ${TOOLCHAIN_PATH}")
message(STATUS "  Toolchain Prefix: ${TOOLCHAIN_PREFIX}")
message(STATUS "  Toolchain File: ${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "  WOW64 multilib: ${WOW64_MULTILIB}")
message(STATUS "  Enable ccache: ${ENABLE_CCACHE}")
if(BUILD_ENVIRONMENT STREQUAL "Clang")
    message(STATUS "  Clang Version: ${CLANG_VERSION}")
endif()
