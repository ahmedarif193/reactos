# Select a local PDB subset without exceeding the fixed preinstalled image.

if(NOT DEFINED OUTPUT_DIR OR NOT DEFINED PREINSTALL_LIST OR
   NOT DEFINED IMAGE_SIZE_MB OR NOT DEFINED RESERVE_MB OR
   NOT DEFINED MAX_SYMBOL_MB)
    message(FATAL_ERROR "Incomplete rosprofiler symbol budget arguments")
endif()
if(NOT DEFINED FS_OVERHEAD_MB)
    set(FS_OVERHEAD_MB 4)
endif()
if(NOT DEFINED EMBEDDED_ROSSYM)
    set(EMBEDDED_ROSSYM OFF)
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(rperf_rounded_file_size path result)
    if(EXISTS "${path}" AND NOT IS_DIRECTORY "${path}")
        file(SIZE "${path}" _bytes)
        math(EXPR _rounded "((${_bytes} + 4095) / 4096) * 4096")
        set(${result} "${_rounded}" PARENT_SCOPE)
    else()
        set(${result} 0 PARENT_SCOPE)
    endif()
endfunction()

function(rperf_path_size path result)
    set(_total 0)
    if(IS_DIRECTORY "${path}")
        file(GLOB_RECURSE _files LIST_DIRECTORIES FALSE "${path}/*")
        foreach(_file IN LISTS _files)
            rperf_rounded_file_size("${_file}" _size)
            math(EXPR _total "${_total} + ${_size}")
        endforeach()
    else()
        rperf_rounded_file_size("${path}" _total)
    endif()
    set(${result} "${_total}" PARENT_SCOPE)
endfunction()

set(_base_bytes 0)
if(EXISTS "${PREINSTALL_LIST}")
    file(STRINGS "${PREINSTALL_LIST}" _entries)
    foreach(_entry IN LISTS _entries)
        if(_entry MATCHES "^[^=]+=(.*)$")
            set(_host_path "${CMAKE_MATCH_1}")
            if(NOT _host_path STREQUAL "${OUTPUT_DIR}")
                rperf_path_size("${_host_path}" _entry_bytes)
                math(EXPR _base_bytes "${_base_bytes} + ${_entry_bytes}")
            endif()
        endif()
    endforeach()
endif()

math(EXPR _image_bytes "${IMAGE_SIZE_MB} * 1024 * 1024")
math(EXPR _reserve_bytes "${RESERVE_MB} * 1024 * 1024")
math(EXPR _maximum_symbol_bytes "${MAX_SYMBOL_MB} * 1024 * 1024")
math(EXPR _filesystem_overhead_bytes "${FS_OVERHEAD_MB} * 1024 * 1024")
math(EXPR _available "${_image_bytes} - ${_base_bytes} - ${_reserve_bytes} - 1048576 - ${_filesystem_overhead_bytes}")
if(_available LESS 0)
    set(_available 0)
endif()
if(_available GREATER _maximum_symbol_bytes)
    set(_available "${_maximum_symbol_bytes}")
endif()

set(_manifest "ReactOS profiler symbol package\n")
string(APPEND _manifest "image_size_bytes=${_image_bytes}\n")
string(APPEND _manifest "estimated_base_payload_bytes=${_base_bytes}\n")
string(APPEND _manifest "reserved_free_bytes=${_reserve_bytes}\n")
string(APPEND _manifest
       "reserved_filesystem_overhead_bytes=${_filesystem_overhead_bytes}\n")
string(APPEND _manifest "pdb_budget_bytes=${_available}\n")

set(_selected_bytes 0)
set(_selected_count 0)
set(_skipped_count 0)
set(_rossym_fallback_count 0)
set(_module_offset_fallback_count 0)
set(_fallback_scope "pdb-candidates")
set(_fallback_counts_enumerated yes)
if(PACKAGE_PDBS AND EXISTS "${PDB_DIR}")
    file(GLOB _all_pdbs "${PDB_DIR}/*.pdb" "${PDB_DIR}/*.PDB")
    list(REMOVE_DUPLICATES _all_pdbs)
    list(SORT _all_pdbs)
    set(_ordered)
    set(_priorities
        rosprofiler.pdb
        dbghelp.pdb
        ntoskrnl.pdb
        hal.pdb
        advapi32.pdb
        ntdll.pdb
        kernel32.pdb
        kernelbase.pdb
        user32.pdb
        win32k.pdb
        gdi32.pdb)
    foreach(_name IN LISTS _priorities)
        foreach(_pdb IN LISTS _all_pdbs)
            get_filename_component(_candidate_name "${_pdb}" NAME)
            string(TOLOWER "${_candidate_name}" _candidate_name_lower)
            if(_candidate_name_lower STREQUAL _name)
                list(APPEND _ordered "${_pdb}")
                list(REMOVE_ITEM _all_pdbs "${_pdb}")
                break()
            endif()
        endforeach()
    endforeach()
    list(APPEND _ordered ${_all_pdbs})
    foreach(_pdb IN LISTS _ordered)
        file(SIZE "${_pdb}" _size)
        rperf_rounded_file_size("${_pdb}" _allocated_size)
        math(EXPR _candidate_total
             "${_selected_bytes} + ${_allocated_size}")
        get_filename_component(_name "${_pdb}" NAME)
        if(_candidate_total LESS_EQUAL _available)
            configure_file("${_pdb}" "${OUTPUT_DIR}/${_name}" COPYONLY)
            set(_selected_bytes "${_candidate_total}")
            math(EXPR _selected_count "${_selected_count} + 1")
            string(APPEND _manifest
                   "included=${_name},${_size},${_allocated_size}\n")
        else()
            math(EXPR _skipped_count "${_skipped_count} + 1")
            if(EMBEDDED_ROSSYM)
                math(EXPR _rossym_fallback_count
                     "${_rossym_fallback_count} + 1")
                string(APPEND _manifest
                       "embedded_rossym_fallback=${_name},${_size},${_allocated_size},budget\n")
            else()
                math(EXPR _module_offset_fallback_count
                     "${_module_offset_fallback_count} + 1")
                string(APPEND _manifest
                       "module_offset_fallback=${_name},${_size},${_allocated_size},budget\n")
            endif()
        endif()
    endforeach()
else()
    set(_fallback_scope "built-pe-images")
    set(_fallback_counts_enumerated no)
    if(EMBEDDED_ROSSYM)
        string(APPEND _manifest
               "mode=embedded-rossym (PDB packaging is Debug MSVC only)\n")
    else()
        string(APPEND _manifest
               "mode=module-offset (no packaged PDB or embedded rossym)\n")
    endif()
endif()

string(APPEND _manifest "fallback_count_scope=${_fallback_scope}\n")
string(APPEND _manifest
       "fallback_counts_enumerated=${_fallback_counts_enumerated}\n")
string(APPEND _manifest "included_pdb_count=${_selected_count}\n")
string(APPEND _manifest "included_pdb_bytes=${_selected_bytes}\n")
string(APPEND _manifest
       "embedded_rossym_fallback_count=${_rossym_fallback_count}\n")
string(APPEND _manifest
       "module_offset_fallback_count=${_module_offset_fallback_count}\n")
file(WRITE "${OUTPUT_DIR}/rosprofiler-symbols.txt" "${_manifest}")
if(PACKAGE_PDBS AND EXISTS "${PDB_DIR}")
    message(STATUS "Profiler symbols: ${_selected_count} PDBs (${_selected_bytes} bytes), ${_rossym_fallback_count} PDB candidates use embedded rsym, ${_module_offset_fallback_count} PDB candidates use module+offset; budget ${_available} bytes")
elseif(EMBEDDED_ROSSYM)
    message(STATUS "Profiler symbols: PDB packaging disabled; built PE images use embedded rsym when present; unused PDB budget ${_available} bytes")
else()
    message(STATUS "Profiler symbols: PDB packaging and embedded rsym disabled; unresolved built PE images use module+offset; unused PDB budget ${_available} bytes")
endif()
