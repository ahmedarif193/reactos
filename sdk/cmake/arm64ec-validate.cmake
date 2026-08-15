# PROJECT:     ReactOS Build System
# PURPOSE:     Validate ARM64EC PE build artifacts
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(_files)
set(_collect_files FALSE)
math(EXPR _last_argument "${CMAKE_ARGC} - 1")
foreach(_index RANGE 0 ${_last_argument})
    set(_argument_name "CMAKE_ARGV${_index}")
    set(_argument "${${_argument_name}}")
    if(_collect_files)
        list(APPEND _files "${_argument}")
    elseif(_argument STREQUAL "--")
        set(_collect_files TRUE)
    endif()
endforeach()

if(NOT _files)
    message(FATAL_ERROR "No ARM64EC files were provided for validation")
endif()

foreach(_file IN LISTS _files)
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "Missing ARM64EC file: ${_file}")
    endif()

    file(SIZE "${_file}" _file_size)
    if(_file_size LESS 64)
        message(FATAL_ERROR "ARM64EC file is too small for a DOS header: ${_file}")
    endif()

    file(READ "${_file}" _dos_header LIMIT 64 HEX)
    string(SUBSTRING "${_dos_header}" 0 4 _dos_signature)
    if(NOT _dos_signature STREQUAL "4d5a")
        message(FATAL_ERROR "ARM64EC file has no DOS header: ${_file}")
    endif()

    string(SUBSTRING "${_dos_header}" 120 2 _offset_0)
    string(SUBSTRING "${_dos_header}" 122 2 _offset_1)
    string(SUBSTRING "${_dos_header}" 124 2 _offset_2)
    string(SUBSTRING "${_dos_header}" 126 2 _offset_3)
    math(EXPR _pe_offset "0x${_offset_3}${_offset_2}${_offset_1}${_offset_0}")
    math(EXPR _pe_header_end "${_pe_offset} + 26")
    if(_pe_header_end GREATER _file_size)
        message(FATAL_ERROR "ARM64EC file has an out-of-range PE header: ${_file}")
    endif()

    file(READ "${_file}" _pe_header OFFSET ${_pe_offset} LIMIT 26 HEX)
    string(SUBSTRING "${_pe_header}" 0 8 _pe_signature)
    string(SUBSTRING "${_pe_header}" 8 4 _machine)
    string(SUBSTRING "${_pe_header}" 48 4 _optional_magic)
    if(NOT _pe_signature STREQUAL "50450000" OR NOT _optional_magic STREQUAL "0b02")
        message(FATAL_ERROR "ARM64EC file is not PE32+: ${_file}")
    endif()

    # A641 identifies intermediate ARM64EC objects. Final x64-compatible
    # ARM64EC images retain IMAGE_FILE_MACHINE_AMD64 (0x8664) in the raw PE
    # header and are distinguished by their ARM64X/CHPE metadata.
    if(NOT _machine STREQUAL "6486")
        message(FATAL_ERROR "ARM64EC image has final PE machine 0x${_machine}, expected little-endian 8664: ${_file}")
    endif()
    if(NOT DEFINED LLVM_READOBJ OR NOT EXISTS "${LLVM_READOBJ}")
        message(FATAL_ERROR "LLVM_READOBJ is required to validate ARM64EC CHPE metadata")
    endif()
    execute_process(
        COMMAND "${LLVM_READOBJ}" --file-headers --coff-load-config "${_file}"
        RESULT_VARIABLE _readobj_result
        OUTPUT_VARIABLE _load_config
        ERROR_VARIABLE _readobj_error)
    if(NOT _readobj_result EQUAL 0)
        message(FATAL_ERROR "llvm-readobj failed for ${_file}: ${_readobj_error}")
    endif()
    if(NOT _load_config MATCHES "Format: COFF-ARM64EC" OR
       NOT _load_config MATCHES "Machine: IMAGE_FILE_MACHINE_ARM64EC \\(0xA641\\)")
        message(FATAL_ERROR "Final PE image is not identified as ARM64EC/ARM64X: ${_file}")
    endif()
    if(NOT _load_config MATCHES "CHPEMetadataPointer: 0x[1-9A-Fa-f][0-9A-Fa-f]*")
        message(FATAL_ERROR "ARM64EC file has no nonzero CHPE metadata pointer: ${_file}")
    endif()

    execute_process(
        COMMAND "${LLVM_READOBJ}" --coff-imports "${_file}"
        RESULT_VARIABLE _imports_result
        OUTPUT_VARIABLE _imports
        ERROR_VARIABLE _imports_error)
    if(NOT _imports_result EQUAL 0)
        message(FATAL_ERROR "llvm-readobj failed to read imports from ${_file}: ${_imports_error}")
    endif()

    # These exports are variadic. Calling them directly from ARM64EC into
    # ARM64 corrupts arguments because the two ABIs use different register
    # and stack layouts. They must resolve through ntdll_chpe.dll.
    string(REPLACE "\r\n" "\n" _imports "${_imports}")
    string(REPLACE "\n" ";" _import_lines "${_imports}")
    set(_import_dll "")
    foreach(_import_line IN LISTS _import_lines)
        if(_import_line MATCHES "^  Name: (.+)$")
            string(TOLOWER "${CMAKE_MATCH_1}" _import_dll)
        elseif(_import_dll STREQUAL "ntdll.dll" AND _import_line MATCHES "^  Symbol: ([^ ]+) ")
            set(_import_symbol "${CMAKE_MATCH_1}")
            if(_import_symbol MATCHES "^(DbgPrint|DbgPrintEx|DbgPrintReturnControlC|EtwTraceMessage|_snprintf|_snwprintf|_swprintf|sprintf|sscanf|swprintf)$")
                message(FATAL_ERROR "ARM64EC image imports variadic native ABI export ntdll.dll!${_import_symbol}; route it through ntdll_chpe.dll: ${_file}")
            endif()
        endif()
    endforeach()
endforeach()

list(LENGTH _files _file_count)
message(STATUS "Validated ${_file_count} ARM64EC PE32+ files")
