# WOW64 Multilib Subbuild
# Builds selected 32-bit (i386) user-mode modules inside the amd64 build tree
# and exposes a sysroot path that can be grafted into reactos/SysWOW64.

include(ExternalProject)

if(NOT ARCH STREQUAL "amd64")
    message(FATAL_ERROR "wow64-multilib.cmake included on non-amd64 build")
endif()

# Only define the multilib subbuild from the top-level project configuration,
# not from nested ExternalProject or sub-builds (e.g. host-tools).
if(NOT DEFINED REACTOS_TOP_SOURCE_DIR OR NOT CMAKE_SOURCE_DIR STREQUAL REACTOS_TOP_SOURCE_DIR)
    return()
endif()

# Subbuild directory for the i386 build
# Use the top-level build directory for the subbuild, independent of any subdir context
# Place the subbuild under the top-level binary directory
set(WOW64_MULTILIB_SUBBUILD_DIR "${REACTOS_TOP_BINARY_DIR}/_wow64_i386")

# Allow overriding modules to build via cache variable
set(WOW64_MULTILIB_I386_TARGETS
    ntdll;kernel32;msvcrt;advapi32;rpcrt4;user32;gdi32
    CACHE STRING "i386 targets to build for WOW64 multilib")

# Propagate generator; default to current
set(_i386_generator "")
if(CMAKE_GENERATOR)
    set(_i386_generator "-G${CMAKE_GENERATOR}")
endif()

# Match build type if single-config
set(_i386_build_type "")
if(CMAKE_BUILD_TYPE)
    set(_i386_build_type "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif()

# Optional toolchain file forwarding
set(_i386_toolchain "")
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_i386_toolchain "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif()

# Configure subbuild
if(NOT TARGET wow64_multilib_i386)
ExternalProject_Add(wow64_multilib_i386
    SOURCE_DIR "${REACTOS_TOP_SOURCE_DIR}"
    BINARY_DIR "${WOW64_MULTILIB_SUBBUILD_DIR}"
    CMAKE_ARGS
        ${_i386_generator}
        -DARCH=i386
        -DWOW64_MULTILIB=OFF
        -DREACTOS_TOP_SOURCE_DIR:PATH=${REACTOS_TOP_SOURCE_DIR}
        -DREACTOS_TOP_BINARY_DIR:PATH=${REACTOS_TOP_BINARY_DIR}
        -DHOST_TOOLS_DIR:PATH=${REACTOS_TOP_BINARY_DIR}/host-tools/bin
        ${_i386_build_type}
        ${_i386_toolchain}
    BUILD_COMMAND
        ${CMAKE_COMMAND} --build "${WOW64_MULTILIB_SUBBUILD_DIR}" --target ${WOW64_MULTILIB_I386_TARGETS}
    INSTALL_COMMAND ""
    USES_TERMINAL_BUILD TRUE
)
endif()

# Expose the produced sysroot path to the including scope
set(WOW64_MULTILIB_SYSROOT "${WOW64_MULTILIB_SUBBUILD_DIR}/reactos" CACHE PATH "Multilib i386 sysroot")

# Convenience target that other packaging targets can depend on
add_custom_target(wow64_multilib_stage DEPENDS wow64_multilib_i386)
