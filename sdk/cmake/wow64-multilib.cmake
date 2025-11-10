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
    ws2_32;ole32;oleaut32;shell32;shlwapi
    wow64smoke
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

set(_i386_toolchain_path_args)
if(DEFINED TOOLCHAIN_PATH AND NOT "${TOOLCHAIN_PATH}" STREQUAL "")
    list(APPEND _i386_toolchain_path_args "-DTOOLCHAIN_PATH:PATH=${TOOLCHAIN_PATH}")
endif()
if(DEFINED TOOLCHAIN_PREFIX AND NOT "${TOOLCHAIN_PREFIX}" STREQUAL "")
    list(APPEND _i386_toolchain_path_args "-DTOOLCHAIN_PREFIX:STRING=${TOOLCHAIN_PREFIX}")
endif()

set(_i386_stdlibs "-lstdc++")
set(_i386_gxx "${GXX_EXECUTABLE}")
if(NOT _i386_gxx AND DEFINED TOOLCHAIN_PATH AND NOT "${TOOLCHAIN_PATH}" STREQUAL "" AND DEFINED MINGW_TOOLCHAIN_PREFIX AND NOT "${MINGW_TOOLCHAIN_PREFIX}" STREQUAL "")
    set(_i386_gxx "${TOOLCHAIN_PATH}/${MINGW_TOOLCHAIN_PREFIX}g++")
    if(NOT EXISTS "${_i386_gxx}")
        unset(_i386_gxx)
    endif()
endif()
if(_i386_gxx)
    execute_process(
        COMMAND ${_i386_gxx} -m32 -print-file-name=libstdc++.a
        OUTPUT_VARIABLE _i386_libstdcxx_location
        ERROR_QUIET)
    string(STRIP "${_i386_libstdcxx_location}" _i386_libstdcxx_location)
    if(_i386_libstdcxx_location AND EXISTS "${_i386_libstdcxx_location}")
        get_filename_component(_i386_libstdcxx_dir "${_i386_libstdcxx_location}" DIRECTORY)
        set(_i386_stdlibs "-L${_i386_libstdcxx_dir} -lstdc++")
    endif()
    unset(_i386_libstdcxx_location)
    unset(_i386_libstdcxx_dir)
endif()

set(_i386_compiler_args)
if(NOT CMAKE_TOOLCHAIN_FILE)
    list(APPEND _i386_compiler_args
        -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER})
endif()

set(_i386_linker_arg)
if(DEFINED TOOLCHAIN_PATH AND NOT "${TOOLCHAIN_PATH}" STREQUAL "" AND MINGW_TOOLCHAIN_PREFIX MATCHES "^x86_64-w64-mingw32-")
    set(_i386_multilib_ld "${TOOLCHAIN_PATH}/${MINGW_TOOLCHAIN_PREFIX}ld")
    if(EXISTS "${_i386_multilib_ld}")
        list(APPEND _i386_linker_arg "-DCMAKE_LINKER:FILEPATH=${_i386_multilib_ld}")
    endif()
    unset(_i386_multilib_ld)
endif()

# Configure subbuild
if(NOT TARGET wow64_multilib_i386)
ExternalProject_Add(wow64_multilib_i386
    SOURCE_DIR "${REACTOS_TOP_SOURCE_DIR}"
    BINARY_DIR "${WOW64_MULTILIB_SUBBUILD_DIR}"
    CMAKE_ARGS
        ${_i386_generator}
        -DARCH=i386
        -DPCH=OFF
        -DWOW64_MULTILIB=OFF
        # Ensure C-linkage targets that pull in C++ objects get libstdc++
        # This avoids undefined references (operator new/delete, atexit,
        # __cxa_guard_*) when linking mixed C/C++ code as C targets.
        "-DCMAKE_C_STANDARD_LIBRARIES:STRING=${_i386_stdlibs}"
        "-DCMAKE_CXX_STANDARD_LIBRARIES:STRING=${_i386_stdlibs}"
        # Reuse the same MinGW toolchain prefix/suffix as the top-level build
        # so an x86_64-w64-mingw32 multilib toolchain can be used to emit i386 code
        -DMINGW_TOOLCHAIN_PREFIX:STRING=${MINGW_TOOLCHAIN_PREFIX}
        -DMINGW_TOOLCHAIN_SUFFIX:STRING=${MINGW_TOOLCHAIN_SUFFIX}
        ${_i386_toolchain_path_args}
        # Forward explicit tool binaries when available to avoid PATH lookups
        ${_i386_compiler_args}
        -DCMAKE_ASM_COMPILER:FILEPATH=${CMAKE_ASM_COMPILER}
        -DCMAKE_AR:FILEPATH=${CMAKE_AR}
        -DCMAKE_RANLIB:FILEPATH=${CMAKE_RANLIB}
        -DCMAKE_NM:FILEPATH=${CMAKE_NM}
        -DCMAKE_OBJCOPY:FILEPATH=${CMAKE_OBJCOPY}
        -DCMAKE_OBJDUMP:FILEPATH=${CMAKE_OBJDUMP}
        -DCMAKE_READELF:FILEPATH=${CMAKE_READELF}
        ${_i386_linker_arg}
        -DCMAKE_MC_COMPILER:FILEPATH=${CMAKE_MC_COMPILER}
        -DCMAKE_RC_COMPILER:FILEPATH=${CMAKE_RC_COMPILER}
        -DCMAKE_DLLTOOL:FILEPATH=${CMAKE_DLLTOOL}
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

unset(_i386_compiler_args)

# Expose the produced sysroot path to the including scope
set(WOW64_MULTILIB_SYSROOT "${WOW64_MULTILIB_SUBBUILD_DIR}/reactos" CACHE PATH "Multilib i386 sysroot")

# Convenience target that other packaging targets can depend on
add_custom_target(wow64_multilib_stage DEPENDS wow64_multilib_i386)
