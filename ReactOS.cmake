# ReactOS Build Configuration
# This file contains all build metadata and configuration options

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

# Clang defaults
if(BUILD_ENVIRONMENT STREQUAL "Clang")
    if(NOT DEFINED CLANG_VERSION OR CLANG_VERSION STREQUAL "")
        set(CLANG_VERSION "19" CACHE STRING "Clang version suffix (e.g. 19)" FORCE)
    else()
        set(CLANG_VERSION "${CLANG_VERSION}" CACHE STRING "Clang version suffix (e.g. 19)")
    endif()
endif()

# Toolchain path (for MinGW cross-compiler)
if(NOT TOOLCHAIN_PATH)
    if(DEFINED ENV{ROS_TOOLCHAIN_PATH})
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
message(STATUS "  Enable ccache: ${ENABLE_CCACHE}")
if(BUILD_ENVIRONMENT STREQUAL "Clang")
    message(STATUS "  Clang Version: ${CLANG_VERSION}")
endif()
