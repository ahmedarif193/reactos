# PROJECT:     ReactOS Build System
# PURPOSE:     Build AMD64 command payloads executed through FEX
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

include("${REACTOS_SOURCE_DIR}/sdk/cmake/fex_amd64_targets.cmake")

set(FEX_AMD64_BINARY_DIR "${REACTOS_BINARY_DIR}/_fex_amd64")

foreach(_target IN LISTS FEX_AMD64_COMMAND_TARGETS)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR "fex_amd64_targets.cmake lists '${_target}', which is not a target in this tree")
    endif()
endforeach()

function(_fex_amd64_get_target_file _target _output)
    get_target_property(_binary_dir "${_target}" BINARY_DIR)
    get_target_property(_output_name "${_target}" OUTPUT_NAME)
    get_target_property(_prefix "${_target}" PREFIX)
    get_target_property(_suffix "${_target}" SUFFIX)

    if(NOT _output_name OR _output_name MATCHES "-NOTFOUND$")
        set(_output_name "${_target}")
    endif()
    if(NOT _prefix OR _prefix MATCHES "-NOTFOUND$")
        set(_prefix "${CMAKE_EXECUTABLE_PREFIX}")
    endif()
    if(NOT _suffix OR _suffix MATCHES "-NOTFOUND$")
        set(_suffix "${CMAKE_EXECUTABLE_SUFFIX}")
    endif()

    file(RELATIVE_PATH _relative_dir "${REACTOS_BINARY_DIR}" "${_binary_dir}")
    set(${_output} "${FEX_AMD64_BINARY_DIR}/${_relative_dir}/${_prefix}${_output_name}${_suffix}" PARENT_SCOPE)
endfunction()

set(FEX_AMD64_COMMAND_FILES)
set(FEX_AMD64_ALIAS_FILES)
set(FEX_AMD64_ALIAS_SOURCES)
foreach(_alias IN LISTS FEX_AMD64_COMMAND_ALIASES)
    if(NOT _alias MATCHES "^([^=]+)=(.+)$")
        message(FATAL_ERROR "Invalid FEX AMD64 command alias '${_alias}'; expected target=filename")
    endif()
    set(_alias_target "${CMAKE_MATCH_1}")
    set(_alias_name "${CMAKE_MATCH_2}")
    if(NOT _alias_target IN_LIST FEX_AMD64_COMMAND_TARGETS)
        message(FATAL_ERROR "FEX AMD64 alias target '${_alias_target}' is not in the command target list")
    endif()
    _fex_amd64_get_target_file("${_alias_target}" _alias_source)
    list(APPEND FEX_AMD64_COMMAND_FILES "${_alias_source}")
    list(APPEND FEX_AMD64_ALIAS_SOURCES "${_alias_source}")
    list(APPEND FEX_AMD64_ALIAS_FILES
        "${REACTOS_BINARY_DIR}/CMakeFiles/fex-amd64-aliases/${_alias_name}")
endforeach()

get_filename_component(_fex_amd64_toolchain "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${REACTOS_SOURCE_DIR}")
if(HOST_TOOLS_DIR)
    set(_fex_amd64_host_tools_dir "${HOST_TOOLS_DIR}")
else()
    set(_fex_amd64_host_tools_dir "${REACTOS_BINARY_DIR}/host-tools/bin")
endif()

set(_fex_amd64_cmake_args
    -DARCH:STRING=amd64
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${_fex_amd64_toolchain}
    -DDBG:BOOL=${DBG}
    -DENABLE_FEX_ARM64EC:BOOL=OFF
    -DENABLE_ROSTESTS:BOOL=OFF
    -DENABLE_WOW64:BOOL=OFF
    -DNVS:BOOL=ON
    -DHOST_TOOLS_DIR:PATH=${_fex_amd64_host_tools_dir}
    -DKD_DEBUGGER:STRING=NONE
    -DMESA_GALLIUM_FROM_SOURCE:BOOL=OFF
    -DOPTIMIZE:STRING=${OPTIMIZE}
    -DPCH:BOOL=${PCH}
    -DREACTOS_CLANG_LLVM_MINGW_ROOT:PATH=${REACTOS_CLANG_LLVM_MINGW_ROOT}
    -DREACTOS_GRAPHICS_DRIVER_MODEL:STRING=${REACTOS_GRAPHICS_DRIVER_MODEL}
    -DREACTOS_TARGET_NT:STRING=${REACTOS_TARGET_NT}
    -DREACTOS_WDDM_LEVEL:STRING=${REACTOS_WDDM_LEVEL}
    -DROSCONFIG_PROFILE:STRING=generic
    -DROSCONFIG_SKIP_OVERRIDES:BOOL=ON
    -DSEPARATE_DBG:BOOL=${SEPARATE_DBG}
    -DUSE_DUMMY_PSEH:BOOL=${USE_DUMMY_PSEH}
    -DWITH_DEBUG_SYMBOLS:BOOL=${WITH_DEBUG_SYMBOLS})

string(JOIN "\n" _fex_amd64_configure_signature ${_fex_amd64_cmake_args})
set(_fex_amd64_signature_file "${REACTOS_BINARY_DIR}/CMakeFiles/fex-amd64-configure.txt")
set(_fex_amd64_configure_stamp "${REACTOS_BINARY_DIR}/CMakeFiles/fex-amd64-configure.stamp")
file(GENERATE OUTPUT "${_fex_amd64_signature_file}" CONTENT "${_fex_amd64_configure_signature}\n")

add_custom_command(
    OUTPUT "${_fex_amd64_configure_stamp}"
    COMMAND ${CMAKE_COMMAND} -S "${REACTOS_SOURCE_DIR}" -B "${FEX_AMD64_BINARY_DIR}" -G "${CMAKE_GENERATOR}" ${_fex_amd64_cmake_args}
    COMMAND ${CMAKE_COMMAND} -E touch "${_fex_amd64_configure_stamp}"
    BYPRODUCTS "${FEX_AMD64_BINARY_DIR}/build.ninja"
    DEPENDS "${_fex_amd64_signature_file}"
    COMMENT "Configuring AMD64 command payloads for FEX"
    VERBATIM)

add_custom_target(fex_amd64_configure DEPENDS "${_fex_amd64_configure_stamp}")
add_dependencies(fex_amd64_configure host-tools)

if(CMAKE_GENERATOR STREQUAL "Ninja")
    set(_fex_amd64_heal COMMAND ${CMAKE_MAKE_PROGRAM} -C "${FEX_AMD64_BINARY_DIR}" -t recompact)
else()
    set(_fex_amd64_heal)
endif()

set(_fex_amd64_validate)
if(VALIDATE_COMPAT_BINARIES)
    set(_fex_amd64_validate COMMAND ${CMAKE_COMMAND}
        -P "${REACTOS_SOURCE_DIR}/sdk/cmake/fex-amd64-validate.cmake" -- ${FEX_AMD64_COMMAND_FILES})
endif()

include("${REACTOS_SOURCE_DIR}/sdk/cmake/nested-build.cmake")

add_custom_target(fex_amd64_commands ALL
    ${_fex_amd64_heal}
    COMMAND ${REACTOS_NESTED_BUILD} "${FEX_AMD64_BINARY_DIR}" --target ${FEX_AMD64_COMMAND_TARGETS}
    ${_fex_amd64_validate}
    BYPRODUCTS ${FEX_AMD64_COMMAND_FILES}
    COMMENT "Building AMD64 graphics commands for FEX"
    VERBATIM)
add_dependencies(fex_amd64_commands fex_amd64_configure)

list(LENGTH FEX_AMD64_ALIAS_FILES _fex_amd64_alias_count)
math(EXPR _fex_amd64_alias_last "${_fex_amd64_alias_count} - 1")
foreach(_alias_index RANGE 0 ${_fex_amd64_alias_last})
    list(GET FEX_AMD64_ALIAS_SOURCES ${_alias_index} _alias_source)
    list(GET FEX_AMD64_ALIAS_FILES ${_alias_index} _alias_file)
    add_custom_command(
        OUTPUT "${_alias_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${REACTOS_BINARY_DIR}/CMakeFiles/fex-amd64-aliases"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_alias_source}" "${_alias_file}"
        DEPENDS fex_amd64_commands "${_alias_source}"
        COMMENT "Creating FEX AMD64 command alias ${_alias_file}"
        VERBATIM)
endforeach()

add_custom_target(fex_amd64_aliases DEPENDS ${FEX_AMD64_ALIAS_FILES})
add_cd_file(TARGET fex_amd64_aliases FILE ${FEX_AMD64_ALIAS_FILES}
    DESTINATION reactos/system32 FOR all)

message(STATUS "FEX: building ${_fex_amd64_alias_count} prefixed AMD64 graphics commands")
