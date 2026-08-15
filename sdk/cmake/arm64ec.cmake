# PROJECT:     ReactOS Build System
# PURPOSE:     Build the ARM64EC user-mode runtime used by FEX
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

# Build the ARM64EC user-mode half used by FEX from the same source revision.
# This follows the WoW64 nested-build model: the primary build remains ARM64,
# an explicit manifest selects the secondary ABI targets, and the generated PE
# files are validated before they are added to the ISO.

include("${REACTOS_SOURCE_DIR}/sdk/cmake/arm64ec_targets.cmake")

set(ARM64EC_BINARY_DIR "${REACTOS_BINARY_DIR}/_fex_arm64ec")

foreach(_target IN LISTS ARM64EC_RUNTIME_MODULES)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR "arm64ec_targets.cmake lists '${_target}', which is not a target in this tree")
    endif()
endforeach()

# A nested build mirrors the primary tree, so target output paths can be
# derived from the already configured primary target without hard-coded paths.
function(_arm64ec_get_target_file _target _output)
    get_target_property(_binary_dir "${_target}" BINARY_DIR)
    get_target_property(_type "${_target}" TYPE)
    get_target_property(_output_name "${_target}" OUTPUT_NAME)
    get_target_property(_prefix "${_target}" PREFIX)
    get_target_property(_suffix "${_target}" SUFFIX)

    if(NOT _output_name OR _output_name MATCHES "-NOTFOUND$")
        set(_output_name "${_target}")
    endif()
    if(NOT _prefix OR _prefix MATCHES "-NOTFOUND$")
        if(_type STREQUAL "EXECUTABLE")
            set(_prefix "${CMAKE_EXECUTABLE_PREFIX}")
        else()
            set(_prefix "${CMAKE_SHARED_MODULE_PREFIX}")
        endif()
    endif()
    if(NOT _suffix OR _suffix MATCHES "-NOTFOUND$")
        if(_type STREQUAL "EXECUTABLE")
            set(_suffix "${CMAKE_EXECUTABLE_SUFFIX}")
        else()
            set(_suffix "${CMAKE_SHARED_MODULE_SUFFIX}")
        endif()
    endif()

    file(RELATIVE_PATH _relative_dir "${REACTOS_BINARY_DIR}" "${_binary_dir}")
    set(${_output} "${ARM64EC_BINARY_DIR}/${_relative_dir}/${_prefix}${_output_name}${_suffix}" PARENT_SCOPE)
endfunction()

set(ARM64EC_RUNTIME_FILES)
foreach(_target IN LISTS ARM64EC_RUNTIME_MODULES)
    _arm64ec_get_target_file("${_target}" _file)
    list(APPEND ARM64EC_RUNTIME_FILES "${_file}")
endforeach()

set(ARM64EC_ALIAS_FILES)
set(ARM64EC_ALIAS_SOURCES)
foreach(_alias IN LISTS ARM64EC_RUNTIME_ALIASES)
    if(NOT _alias MATCHES "^([^=]+)=(.+)$")
        message(FATAL_ERROR "Invalid ARM64EC runtime alias '${_alias}'; expected target=filename")
    endif()

    set(_alias_target "${CMAKE_MATCH_1}")
    set(_alias_name "${CMAKE_MATCH_2}")
    if(NOT _alias_target IN_LIST ARM64EC_RUNTIME_MODULES)
        message(FATAL_ERROR "ARM64EC runtime alias target '${_alias_target}' is not in the target list")
    endif()

    _arm64ec_get_target_file("${_alias_target}" _alias_source)
    list(APPEND ARM64EC_ALIAS_SOURCES "${_alias_source}")
    list(APPEND ARM64EC_ALIAS_FILES "${REACTOS_BINARY_DIR}/CMakeFiles/fex-arm64ec-aliases/${_alias_name}")
endforeach()

get_filename_component(_fex_toolchain "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${REACTOS_SOURCE_DIR}")
if(HOST_TOOLS_DIR)
    set(_arm64ec_host_tools_dir "${HOST_TOOLS_DIR}")
else()
    set(_arm64ec_host_tools_dir "${REACTOS_BINARY_DIR}/host-tools/bin")
endif()

set(_fex_common_cmake_args
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${_fex_toolchain}
    -DDBG:BOOL=${DBG}
    -DENABLE_FEX_ARM64EC:BOOL=OFF
    -DENABLE_ROSTESTS:BOOL=OFF
    -DENABLE_WOW64:BOOL=OFF
    -DHOST_TOOLS_DIR:PATH=${_arm64ec_host_tools_dir}
    -DOPTIMIZE:STRING=${OPTIMIZE}
    -DPCH:BOOL=${PCH}
    -DREACTOS_CLANG_LLVM_MINGW_ROOT:PATH=${REACTOS_CLANG_LLVM_MINGW_ROOT}
    -DREACTOS_TARGET_NT:STRING=${REACTOS_TARGET_NT}
    -DROSCONFIG_PROFILE:STRING=generic
    -DROSCONFIG_SKIP_OVERRIDES:BOOL=ON
    -DSEPARATE_DBG:BOOL=${SEPARATE_DBG}
    -DUSE_DUMMY_PSEH:BOOL=${USE_DUMMY_PSEH}
    -DWITH_DEBUG_SYMBOLS:BOOL=${WITH_DEBUG_SYMBOLS})

set(_arm64ec_cmake_args
    -DARCH:STRING=arm64
    -DARM64EC_RUNTIME:BOOL=ON
    -DARM64EC_NATIVE_BINARY_DIR:PATH=${REACTOS_BINARY_DIR}
    ${_fex_common_cmake_args})

function(_fex_add_nested_configure _name _binary_dir _arguments)
    string(JOIN "\n" _configure_signature ${${_arguments}})
    set(_signature_file "${REACTOS_BINARY_DIR}/CMakeFiles/${_name}-configure.txt")
    set(_configure_stamp "${REACTOS_BINARY_DIR}/CMakeFiles/${_name}-configure.stamp")
    file(GENERATE OUTPUT "${_signature_file}" CONTENT "${_configure_signature}\n")

    add_custom_command(
        OUTPUT "${_configure_stamp}"
        COMMAND ${CMAKE_COMMAND} -S "${REACTOS_SOURCE_DIR}" -B "${_binary_dir}" -G "${CMAKE_GENERATOR}" ${${_arguments}}
        COMMAND ${CMAKE_COMMAND} -E touch "${_configure_stamp}"
        BYPRODUCTS "${_binary_dir}/build.ninja"
        DEPENDS "${_signature_file}"
        COMMENT "Configuring ${_name} nested build"
        VERBATIM)
    add_custom_target(${_name}_configure DEPENDS "${_configure_stamp}")
    add_dependencies(${_name}_configure host-tools)
endfunction()

_fex_add_nested_configure(fex_arm64ec "${ARM64EC_BINARY_DIR}" _arm64ec_cmake_args)

# The nested configure consumes the native kernel-offset header, so order it
# after the primary build has generated that header.
add_dependencies(fex_arm64ec_configure asm)

find_program(FEX_LLVM_READOBJ llvm-readobj HINTS "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin" REQUIRED)

list(LENGTH ARM64EC_RUNTIME_MODULES _arm64ec_target_count)
add_custom_target(fex_arm64ec_runtime ALL
    COMMAND ${CMAKE_COMMAND} --build "${ARM64EC_BINARY_DIR}" --target ${ARM64EC_RUNTIME_MODULES}
    COMMAND ${CMAKE_COMMAND} -DLLVM_READOBJ:FILEPATH=${FEX_LLVM_READOBJ} -P "${REACTOS_SOURCE_DIR}/sdk/cmake/arm64ec-validate.cmake" -- ${ARM64EC_RUNTIME_FILES}
    BYPRODUCTS ${ARM64EC_RUNTIME_FILES}
    COMMENT "Building ${_arm64ec_target_count} ARM64EC FEX runtime DLLs"
    USES_TERMINAL
    VERBATIM)
add_dependencies(fex_arm64ec_runtime fex_arm64ec_configure)

add_cd_file(TARGET fex_arm64ec_runtime FILE ${ARM64EC_RUNTIME_FILES}
    DESTINATION reactos/system32/arm64ec FOR all)

if(ARM64EC_ALIAS_FILES)
    list(LENGTH ARM64EC_ALIAS_FILES _arm64ec_alias_count)
    math(EXPR _arm64ec_alias_last "${_arm64ec_alias_count} - 1")
    foreach(_alias_index RANGE 0 ${_arm64ec_alias_last})
        list(GET ARM64EC_ALIAS_SOURCES ${_alias_index} _alias_source)
        list(GET ARM64EC_ALIAS_FILES ${_alias_index} _alias_file)
        add_custom_command(
            OUTPUT "${_alias_file}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${REACTOS_BINARY_DIR}/CMakeFiles/fex-arm64ec-aliases"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_alias_source}" "${_alias_file}"
            DEPENDS fex_arm64ec_runtime "${_alias_source}"
            COMMENT "Creating ARM64EC runtime alias ${_alias_file}"
            VERBATIM)
    endforeach()

    add_custom_target(fex_arm64ec_aliases DEPENDS ${ARM64EC_ALIAS_FILES})
    add_cd_file(TARGET fex_arm64ec_aliases FILE ${ARM64EC_ALIAS_FILES}
        DESTINATION reactos/system32/arm64ec FOR all)
endif()

message(STATUS "FEX: double-building ${_arm64ec_target_count} ARM64EC runtime DLLs into system32/arm64ec")
