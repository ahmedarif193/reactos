# ELF resolves instruction references until the toolchain has a RISC-V COFF
# linker. Generate standard PE name tables and integer-ABI import thunks here;
# GenFw supplies image sections and base relocations, not an NT calling ABI.
find_program(RISCV_GENFW NAMES GenFw HINTS "${EDK2_SOURCE_DIR}/BaseTools/Source/C/bin" REQUIRED)
find_program(RISCV_NM NAMES llvm-nm HINTS "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin" REQUIRED)
find_program(RISCV_ELF_LD NAMES ld.lld HINTS "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin" REQUIRED)
set(RISCV_NATIVE_LDS "${REACTOS_SOURCE_DIR}/sdk/cmake/riscv64-native.lds")
set(RISCV_NATIVE_FINISH "${REACTOS_SOURCE_DIR}/sdk/cmake/riscv64-native-finish.cmake")
set(RISCV_NATIVE_TABLES "${REACTOS_SOURCE_DIR}/sdk/cmake/riscv64-native-tables.cmake")
set(RISCV_NATIVE_LINKER "${REACTOS_SOURCE_DIR}/sdk/cmake/riscv64-native-link.sh")

function(add_riscv_native_image target filename entrypoint)
    cmake_parse_arguments(image "" "" "SOURCES;EXPORTS;FORWARDERS;IMPORTS" ${ARGN})
    if(image_UNPARSED_ARGUMENTS OR (NOT image_EXPORTS AND NOT image_FORWARDERS))
        message(FATAL_ERROR "Invalid native image description for ${target}")
    endif()
    set(_export_names ${image_EXPORTS})
    foreach(_forwarder IN LISTS image_FORWARDERS)
        if(NOT _forwarder MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=([A-Za-z0-9_.]+\\.[A-Za-z_][A-Za-z0-9_]*)$")
            message(FATAL_ERROR "Invalid native image forwarder ${_forwarder}")
        endif()
        list(APPEND _export_names "${CMAKE_MATCH_1}")
    endforeach()
    list(LENGTH _export_names _export_count)
    list(REMOVE_DUPLICATES _export_names)
    list(LENGTH _export_names _unique_export_count)
    if(NOT _export_count EQUAL _unique_export_count)
        message(FATAL_ERROR "Duplicate native export in ${target}")
    endif()
    list(SORT _export_names)
    if(_export_count GREATER 65535)
        message(FATAL_ERROR "Too many native exports for ${target}")
    endif()

    set(_tables "${CMAKE_CURRENT_BINARY_DIR}/${target}_pe.S")
    add_custom_command(OUTPUT "${_tables}"
        COMMAND ${CMAKE_COMMAND} "-DFILENAME=${filename}" "-DEXPORTS=${image_EXPORTS}"
            "-DFORWARDERS=${image_FORWARDERS}" "-DIMPORTS=${image_IMPORTS}"
            "-DOUTPUT=${_tables}" -P "${RISCV_NATIVE_TABLES}"
        DEPENDS "${RISCV_NATIVE_TABLES}" VERBATIM)

    add_executable(${target} ${image_SOURCES} "${_tables}")
    get_filename_component(_name "${filename}" NAME_WE)
    get_filename_component(_suffix "${filename}" LAST_EXT)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${_name}" SUFFIX "${_suffix}" LINKER_LANGUAGE C)
    target_link_libraries(${target} PRIVATE riscv_nt)
    target_link_options(${target} PRIVATE -nostdlib "--ld-path=${RISCV_NATIVE_LINKER}"
        "-Wl,--riscv-native-lld=${RISCV_ELF_LD}"
        "-Wl,--entry=${entrypoint},--no-undefined,--fatal-warnings,--emit-relocs,--no-relax,--error-limit=0"
        "-Wl,-T,${RISCV_NATIVE_LDS}")
    if(entrypoint STREQUAL "0")
        set(_mode kerneldll)
    else()
        set(_mode kernel)
    endif()
    # The public CMake target is the PE image, just as for other architectures.
    # ELF is an internal link intermediate, not an installable module.
    set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET>.elf <LINK_LIBRARIES>" PARENT_SCOPE)
    get_target_property(_pe_tool native-riscv64-pe IMPORTED_LOCATION)
    add_dependencies(${target} native-riscv64-pe)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${RISCV_NATIVE_LDS}" "${RISCV_NATIVE_FINISH}" "${RISCV_NATIVE_LINKER}" "${RISCV_ELF_LD}" "${RISCV_GENFW}" "${_pe_tool}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DELF=$<TARGET_FILE:${target}>.elf" "-DOUTPUT=$<TARGET_FILE:${target}>"
            "-DGENFW=${RISCV_GENFW}" "-DNM=${RISCV_NM}"
            "-DPEFIXUP=${_pe_tool}" "-DMODE=${_mode}"
            -P "${RISCV_NATIVE_FINISH}"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/${filename}.elf"
        VERBATIM)
    set_target_properties(${target} PROPERTIES RISCV_EXPORTS "${_export_names}" REACTOS_MODULE_TYPE "${_mode}")
endfunction()
