# Build the 32-bit user-mode half of WoW64 from the same source revision.
# CMake selects one compiler architecture per configured tree, so the i386
# binaries come from a nested build tree configured for ARCH=i386. The set
# of targets to double-build is declared in wow64_targets.cmake — the
# single source of truth — and verified for completeness below. A single
# wow64_i386 target drives one nested build of all of them.

include("${REACTOS_SOURCE_DIR}/sdk/cmake/wow64_targets.cmake")

set(WOW64_I386_BINARY_DIR "${REACTOS_BINARY_DIR}/_wow64_i386")
set(WOW64_I386_TARGETS ${WOW64_I386_MODULES} ${WOW64_I386_EXECUTABLES})

# Walk a target's link closure to find the DLL modules it imports. ReactOS
# links DLL imports through "lib<module>" import libraries, so those names
# are mapped back to the module targets that provide them.
set_property(GLOBAL PROPERTY WOW64_I386_VISITED_TARGETS)
set_property(GLOBAL PROPERTY WOW64_I386_LINKED_MODULES)

function(_wow64_collect_linked_modules _target)
    get_property(_visited GLOBAL PROPERTY WOW64_I386_VISITED_TARGETS)
    if(_target IN_LIST _visited)
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY WOW64_I386_VISITED_TARGETS "${_target}")

    get_target_property(_type "${_target}" TYPE)
    if(_type STREQUAL "MODULE_LIBRARY")
        set_property(GLOBAL APPEND PROPERTY WOW64_I386_LINKED_MODULES "${_target}")
    endif()

    foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_links "${_target}" "${_property}")
        if(NOT _links OR _links MATCHES "-NOTFOUND$")
            continue()
        endif()

        foreach(_link IN LISTS _links)
            string(REGEX REPLACE "^\\$<LINK_ONLY:([^>]+)>$" "\\1" _dependency "${_link}")
            if(TARGET "${_dependency}")
                _wow64_collect_linked_modules("${_dependency}")
            endif()

            if(_dependency MATCHES "^lib(.+)$")
                set(_module "${CMAKE_MATCH_1}")
                if(TARGET "${_module}")
                    get_target_property(_dependency_type "${_module}" TYPE)
                    if(_dependency_type STREQUAL "MODULE_LIBRARY")
                        _wow64_collect_linked_modules("${_module}")
                    endif()
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()

foreach(_target IN LISTS WOW64_I386_TARGETS)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR "wow64_targets.cmake lists '${_target}', which is not a target in this tree")
    endif()
    _wow64_collect_linked_modules("${_target}")
endforeach()

# The manifest must cover the static link closure of everything it lists;
# fail with the exact names to add so the list cannot silently rot.
get_property(_wow64_linked_modules GLOBAL PROPERTY WOW64_I386_LINKED_MODULES)
set(_wow64_missing_modules)
foreach(_module IN LISTS _wow64_linked_modules)
    if(NOT _module IN_LIST WOW64_I386_TARGETS)
        list(APPEND _wow64_missing_modules "${_module}")
    endif()
endforeach()

if(_wow64_missing_modules)
    list(SORT _wow64_missing_modules)
    list(JOIN _wow64_missing_modules "\n    " _wow64_missing_text)
    message(FATAL_ERROR "WoW64 targets import DLLs that are not listed in "
        "sdk/cmake/wow64_targets.cmake. Add these modules to WOW64_I386_MODULES:\n"
        "    ${_wow64_missing_text}")
endif()

# The nested tree mirrors this one, so each i386 file lives at the same
# relative path its native counterpart occupies here.
function(_wow64_get_target_file _target _output)
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
    set(${_output} "${WOW64_I386_BINARY_DIR}/${_relative_dir}/${_prefix}${_output_name}${_suffix}" PARENT_SCOPE)
endfunction()

set(WOW64_I386_FILES)
foreach(_target IN LISTS WOW64_I386_TARGETS)
    _wow64_get_target_file("${_target}" _file)
    list(APPEND WOW64_I386_FILES "${_file}")
endforeach()

get_filename_component(_wow64_toolchain "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${REACTOS_SOURCE_DIR}")
set(_wow64_i386_cmake_args
    -DARCH:STRING=i386
    -DENABLE_WOW64:BOOL=OFF
    -DWOW64_I386_RUNTIME:BOOL=ON
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${_wow64_toolchain}
    -DDBG:BOOL=${DBG}
    -DHOST_TOOLS_DIR:PATH=${REACTOS_BINARY_DIR}/host-tools/bin
    -DOPTIMIZE:STRING=${OPTIMIZE}
    -DPCH:BOOL=${PCH}
    -DREACTOS_CLANG_LLVM_MINGW_ROOT:PATH=${REACTOS_CLANG_LLVM_MINGW_ROOT}
    -DREACTOS_TARGET_NT:STRING=${REACTOS_TARGET_NT}
    -DSEPARATE_DBG:BOOL=${SEPARATE_DBG}
    -DUSE_DUMMY_PSEH:BOOL=${USE_DUMMY_PSEH}
    -DWITH_DEBUG_SYMBOLS:BOOL=${WITH_DEBUG_SYMBOLS}
    -DENABLE_ROSTESTS:BOOL=${ENABLE_ROSTESTS})

# Reconfigure the i386 rules only when their configuration inputs change. Once
# generated, the nested Ninja graph tracks normal CMake/source dependencies.
string(JOIN "\n" _wow64_i386_configure_signature ${_wow64_i386_cmake_args})
set(_wow64_i386_signature_file "${REACTOS_BINARY_DIR}/CMakeFiles/wow64-i386-configure.txt")
set(_wow64_i386_configure_stamp "${REACTOS_BINARY_DIR}/CMakeFiles/wow64-i386-configure.stamp")
file(GENERATE OUTPUT "${_wow64_i386_signature_file}" CONTENT "${_wow64_i386_configure_signature}\n")

add_custom_command(
    OUTPUT "${_wow64_i386_configure_stamp}"
    COMMAND ${CMAKE_COMMAND} -S "${REACTOS_SOURCE_DIR}" -B "${WOW64_I386_BINARY_DIR}" -G "${CMAKE_GENERATOR}" ${_wow64_i386_cmake_args}
    COMMAND ${CMAKE_COMMAND} -E touch "${_wow64_i386_configure_stamp}"
    BYPRODUCTS "${WOW64_I386_BINARY_DIR}/build.ninja"
    DEPENDS "${_wow64_i386_signature_file}"
    COMMENT "Configuring i386 rules for WoW64 targets"
    VERBATIM)

add_custom_target(wow64_i386_configure DEPENDS "${_wow64_i386_configure_stamp}")
add_dependencies(wow64_i386_configure host-tools)

# One nested build produces every i386 file; the nested Ninja graph handles
# incrementality. Single modules can be rebuilt directly with
#   ninja -C <build>/_wow64_i386 <target>
list(LENGTH WOW64_I386_TARGETS _wow64_i386_target_count)
add_custom_target(wow64_i386 ALL
    COMMAND ${CMAKE_COMMAND} --build "${WOW64_I386_BINARY_DIR}" --target ${WOW64_I386_TARGETS}
    COMMAND ${CMAKE_COMMAND} -P "${REACTOS_SOURCE_DIR}/sdk/cmake/wow64-validate.cmake" -- ${WOW64_I386_FILES}
    BYPRODUCTS ${WOW64_I386_FILES}
    COMMENT "Building ${_wow64_i386_target_count} i386 WoW64 targets"
    USES_TERMINAL
    VERBATIM)
add_dependencies(wow64_i386 wow64_i386_configure)

add_cd_file(TARGET wow64_i386 FILE ${WOW64_I386_FILES} DESTINATION reactos/SysWOW64 FOR all)

message(STATUS "WoW64: double-building ${_wow64_i386_target_count} i386 targets into SysWOW64")
