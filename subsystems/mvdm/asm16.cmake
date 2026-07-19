## EXPERIMENTAL!!

# We need to use almost the same tricks as the ones used for MSVC 'add_asm_files'
# support because we are going to compile ASM files for a fixed target (16-bit x86)
# that is different from the main target.

if(NOT MSVC)
###
### For GCC / Clang
###

# Clang's LLVM integrated assembler does not emit 16-bit ELF relocations for
# the i386-unknown-elf target (errors out on `mov bx, offset Sym`, `.word Sym`,
# etc.), so route 16-bit MASM-style assembly through the i686 GNU `as` from
# the RosBE GCC toolchain.
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # llvm-mingw installs an i686-w64-mingw32-as wrapper that forwards back
    # into Clang's integrated assembler, so a plain name lookup is not enough:
    # only a real GNU as can emit the 16-bit relocations needed here. Validate
    # every candidate (including a cached or user-provided one) by its
    # --version banner.
    function(_asm16_validate_gas _outvar _candidate)
        set(${_outvar} FALSE PARENT_SCOPE)
        if(NOT _candidate OR NOT EXISTS "${_candidate}")
            return()
        endif()
        execute_process(COMMAND "${_candidate}" --version
            OUTPUT_VARIABLE _version_text
            RESULT_VARIABLE _version_result
            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_version_result EQUAL 0 AND _version_text MATCHES "GNU assembler")
            set(${_outvar} TRUE PARENT_SCOPE)
        endif()
    endfunction()

    # Re-validate a cached or user-provided value once (it may point at the
    # llvm-mingw wrapper), then trust the cache on later reconfigures
    if(ASM16_GAS AND NOT ASM16_GAS_VALIDATED)
        _asm16_validate_gas(_asm16_gas_ok "${ASM16_GAS}")
        if(_asm16_gas_ok)
            set(ASM16_GAS_VALIDATED TRUE CACHE INTERNAL "")
        else()
            message(STATUS "asm16.cmake: ${ASM16_GAS} is not a GNU as, discarding it")
            unset(ASM16_GAS CACHE)
        endif()
    endif()

    if(NOT ASM16_GAS)
        # 16-bit assembly always needs the i686 GNU as, regardless of the
        # target arch: prefer the toolchain clang.cmake already located
        # (i386 builds), then the RosBE layout that keeps gcc-i686 next to
        # the llvm-mingw install
        set(_asm16_gas_hints "")
        if(REACTOS_CLANG_GCC_TOOLCHAIN)
            list(APPEND _asm16_gas_hints "${REACTOS_CLANG_GCC_TOOLCHAIN}/bin")
        endif()
        if(REACTOS_CLANG_LLVM_MINGW_ROOT)
            get_filename_component(_rosbe_toolchains_root "${REACTOS_CLANG_LLVM_MINGW_ROOT}" DIRECTORY)
            list(APPEND _asm16_gas_hints "${_rosbe_toolchains_root}/gcc-i686/bin")
        endif()
        if(CMAKE_HOST_WIN32)
            set(_asm16_exe_suffix ".exe")
        else()
            set(_asm16_exe_suffix "")
        endif()
        set(_asm16_path_dirs "$ENV{PATH}")
        if(NOT CMAKE_HOST_WIN32)
            string(REPLACE ":" ";" _asm16_path_dirs "${_asm16_path_dirs}")
        endif()
        foreach(_asm16_dir IN LISTS _asm16_gas_hints _asm16_path_dirs)
            set(_asm16_candidate "${_asm16_dir}/i686-w64-mingw32-as${_asm16_exe_suffix}")
            _asm16_validate_gas(_asm16_gas_ok "${_asm16_candidate}")
            if(_asm16_gas_ok)
                set(ASM16_GAS "${_asm16_candidate}" CACHE FILEPATH
                    "GNU as used for 16-bit MASM-style assembly" FORCE)
                set(ASM16_GAS_VALIDATED TRUE CACHE INTERNAL "")
                break()
            endif()
        endforeach()
    endif()

    if(NOT ASM16_GAS)
        message(FATAL_ERROR
            "asm16.cmake: cannot find a GNU i686-w64-mingw32-as for 16-bit "
            "MASM-style assembly (llvm-mingw's as wrapper does not qualify). "
            "Pass -DREACTOS_CLANG_GCC_TOOLCHAIN=<rosbe mingw-gcc dir> or place "
            "a GNU binutils i686-w64-mingw32-as on PATH.")
    endif()
    message(STATUS "asm16.cmake: using GNU as: ${ASM16_GAS}")
endif()

function(add_asm16_bin _target _binary_file _base_address)
    set(_concatenated_asm_file ${CMAKE_CURRENT_BINARY_DIR}/${_target}.asm)
    set(_preprocessed_file ${CMAKE_CURRENT_BINARY_DIR}/${_target}.pp.s)
    set(_object_file ${CMAKE_CURRENT_BINARY_DIR}/${_target}.o)

    # unset(_source_file_list)

    get_defines(_directory_defines)
    get_includes(_directory_includes)
    get_directory_property(_defines COMPILE_DEFINITIONS)

    # Build a list of all the defines needed.
    foreach(_source_file ${ARGN})
        get_filename_component(_source_file_full_path ${_source_file} ABSOLUTE)
        get_source_file_property(_defines_semicolon_list ${_source_file_full_path} COMPILE_DEFINITIONS)

        # unset(_source_file_defines)

        foreach(_define ${_defines_semicolon_list})
            if(NOT ${_define} STREQUAL "NOTFOUND")
                list(APPEND _source_file_defines -D${_define})
            endif()
        endforeach()

        list(APPEND _source_file_list ${_source_file_full_path})
    endforeach()

    # We do not support 16-bit ASM linking so the only way to compile
    # many ASM files is by concatenating them into a single one and
    # compile the resulting file.
    concatenate_files(${_concatenated_asm_file} ${_source_file_list})
    set_source_files_properties(${_concatenated_asm_file} PROPERTIES GENERATED TRUE)

    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        # Step 1: CPP preprocess via Clang. We feed the result to GNU `as`,
        # not Clang's integrated assembler, so undefine __clang__ to keep
        # asm.inc on the GAS path (otherwise it adds case-only aliases that
        # collide under MinGW GAS's case-insensitive macro names).
        add_custom_command(
            OUTPUT ${_preprocessed_file}
            COMMAND ${CMAKE_ASM_COMPILER} -E -x assembler-with-cpp -U__clang__ -MMD -MF ${_preprocessed_file}.d -MT ${_preprocessed_file} -o ${_preprocessed_file} -I${REACTOS_SOURCE_DIR}/sdk/include/asm -I${REACTOS_BINARY_DIR}/sdk/include/asm ${_directory_includes} ${_source_file_defines} ${_directory_defines} -D__ASM__ ${_concatenated_asm_file}
            DEPENDS ${_concatenated_asm_file}
            DEPFILE ${_preprocessed_file}.d)

        # Step 2: Assemble with GNU as (handles 16-bit relocations).
        add_custom_command(
            OUTPUT ${_object_file}
            COMMAND ${ASM16_GAS} ${_preprocessed_file} -o ${_object_file}
            DEPENDS ${_preprocessed_file})

        # Step 3: Flatten to a raw binary at the expected real-mode load address
        add_custom_command(
            OUTPUT ${_binary_file}
            COMMAND native-obj2bin ${_object_file} ${_binary_file} ${_base_address}
            DEPENDS ${_object_file} native-obj2bin)
    else()
        ##
        ## All this part is the same as CreateBootSectorTarget
        ##
        add_custom_command(
            OUTPUT ${_object_file}
            COMMAND ${CMAKE_ASM_COMPILER} -x assembler-with-cpp -o ${_object_file} -I${REACTOS_SOURCE_DIR}/sdk/include/asm -I${REACTOS_BINARY_DIR}/sdk/include/asm ${_directory_includes} ${_source_file_defines} ${_directory_defines} -D__ASM__ -c ${_concatenated_asm_file}
            DEPENDS ${_concatenated_asm_file})

        add_custom_command(
            OUTPUT ${_binary_file}
            COMMAND native-obj2bin ${_object_file} ${_binary_file} ${_base_address}
            # COMMAND objcopy --output-target binary --image-base 0x${_base_address} ${_object_file} ${_binary_file}
            DEPENDS ${_object_file} native-obj2bin)
    endif()

    add_custom_target(${_target} ALL DEPENDS ${_binary_file})
    # set_target_properties(${_target} PROPERTIES OUTPUT_NAME ${_target} SUFFIX ".bin")
    set_target_properties(${_target} PROPERTIES BINARY_PATH ${_binary_file})
    add_clean_target(${_target})
endfunction()

else()
###
### For MSVC
###
function(add_asm16_bin _target _binary_file _base_address)
    set(_concatenated_asm_file ${CMAKE_CURRENT_BINARY_DIR}/${_target}.asm)
    set(_preprocessed_asm_file ${CMAKE_CURRENT_BINARY_DIR}/${_target}.tmp)
    set(_object_file ${CMAKE_CURRENT_BINARY_DIR}/${_target}.obj)

    # unset(_source_file_list)

    get_defines(_directory_defines)
    get_includes(_directory_includes)
    get_directory_property(_defines COMPILE_DEFINITIONS)

    # Build a list of all the defines needed.
    foreach(_source_file ${ARGN})
        get_filename_component(_source_file_full_path ${_source_file} ABSOLUTE)
        get_source_file_property(_defines_semicolon_list ${_source_file_full_path} COMPILE_DEFINITIONS)

        # unset(_source_file_defines)

        foreach(_define ${_defines_semicolon_list})
            if(NOT ${_define} STREQUAL "NOTFOUND")
                list(APPEND _source_file_defines -D${_define})
            endif()
        endforeach()

        list(APPEND _source_file_list ${_source_file_full_path})
    endforeach()

    # We do not support 16-bit ASM linking so the only way to compile
    # many ASM files is by concatenating them into a single one and
    # compile the resulting file.
    concatenate_files(${_concatenated_asm_file} ${_source_file_list})
    set_source_files_properties(${_concatenated_asm_file} PROPERTIES GENERATED TRUE)

    ##
    ## All this part is the same as CreateBootSectorTarget
    ##
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(_no_std_includes_flag "-nostdinc")
    else()
        set(_no_std_includes_flag "/X")
    endif()

    add_custom_command(
        OUTPUT ${_preprocessed_asm_file}
        #COMMAND ${CMAKE_C_COMPILER} /nologo ${_no_std_includes_flag} /I${REACTOS_SOURCE_DIR}/sdk/include/asm /I${REACTOS_BINARY_DIR}/sdk/include/asm ${_directory_includes} ${_source_file_defines} ${_directory_defines} /D__ASM__ /D_USE_ML /EP /c ${_concatenated_asm_file} > ${_preprocessed_asm_file}
        COMMAND cl /nologo /X /I${REACTOS_SOURCE_DIR}/sdk/include/asm /I${REACTOS_BINARY_DIR}/sdk/include/asm ${_directory_includes} ${_source_file_defines} ${_directory_defines} /D__ASM__ /D_USE_ML /EP /c ${_concatenated_asm_file} > ${_preprocessed_asm_file}
        DEPENDS ${_concatenated_asm_file})

    if(MSVC_VERSION GREATER_EQUAL 1936)
        set(_quiet_flag "/quiet")
    endif()
    set(_pp_asm16_compile_command ${CMAKE_ASM16_COMPILER} /nologo ${_quiet_flag} /Cp /Fo${_object_file} /c /Ta ${_preprocessed_asm_file})

    add_custom_command(
        OUTPUT ${_object_file}
        COMMAND ${_pp_asm16_compile_command}
        DEPENDS ${_preprocessed_asm_file})

    add_custom_command(
        OUTPUT ${_binary_file}
        COMMAND native-obj2bin ${_object_file} ${_binary_file} ${_base_address}
        DEPENDS ${_object_file} native-obj2bin)

    add_custom_target(${_target} ALL DEPENDS ${_binary_file})
    # set_target_properties(${_target} PROPERTIES OUTPUT_NAME ${_target} SUFFIX ".bin")
    set_target_properties(${_target} PROPERTIES BINARY_PATH ${_binary_file})
    add_clean_target(${_target})
endfunction()

endif()
