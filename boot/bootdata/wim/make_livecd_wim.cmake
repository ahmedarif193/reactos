cmake_minimum_required(VERSION 3.14)

if(NOT INPUT_LIST OR NOT STAGE_DIR OR NOT OUTPUT_WIM OR NOT OUTPUT_LIST OR NOT WIMAGE_EXE OR NOT SOURCE_FREELDR_INI OR NOT OUTPUT_FREELDR_INI)
    message(FATAL_ERROR "make_livecd_wim.cmake: missing one of INPUT_LIST/STAGE_DIR/OUTPUT_WIM/OUTPUT_LIST/WIMAGE_EXE/SOURCE_FREELDR_INI/OUTPUT_FREELDR_INI")
endif()

if(EXISTS "${STAGE_DIR}")
    file(REMOVE_RECURSE "${STAGE_DIR}")
endif()
file(MAKE_DIRECTORY "${STAGE_DIR}")

file(STRINGS "${INPUT_LIST}" _input_lines ENCODING UTF-8)

set(_loader_lines "")

foreach(_line IN LISTS _input_lines)
    if(_line STREQUAL "")
        continue()
    endif()

    string(FIND "${_line}" "=" _eq_pos)
    if(_eq_pos EQUAL -1)
        list(APPEND _loader_lines "${_line}")
        continue()
    endif()

    math(EXPR _src_start "${_eq_pos} + 1")
    string(SUBSTRING "${_line}" 0 ${_eq_pos} _dest)
    string(SUBSTRING "${_line}" ${_src_start} -1 _src)

    string(REGEX REPLACE "^/" "" _dest_norm "${_dest}")

    set(_is_loader FALSE)
    if(_dest_norm MATCHES "^loader/"           OR
       _dest_norm MATCHES "^efi/"              OR
       _dest_norm MATCHES "^EFI/"              OR
       _dest_norm STREQUAL "freeldr.ini"       OR
       _dest_norm STREQUAL "autorun.inf"       OR
       _dest_norm STREQUAL "icon.ico"          OR
       _dest_norm STREQUAL "readme.txt"        OR
       _dest_norm STREQUAL "boot.catalog")
        set(_is_loader TRUE)
    endif()

    if(_is_loader)
        list(APPEND _loader_lines "${_line}")
        continue()
    endif()

    if(_src MATCHES "\\$<")
        message(WARNING "make_livecd_wim.cmake: unresolved generator expression in '${_line}', skipping")
        continue()
    endif()

    set(_abs_dest "${STAGE_DIR}/${_dest_norm}")
    if(IS_DIRECTORY "${_src}")
        file(MAKE_DIRECTORY "${_abs_dest}")
    elseif(EXISTS "${_src}")
        get_filename_component(_parent_dir "${_abs_dest}" DIRECTORY)
        if(NOT EXISTS "${_parent_dir}")
            file(MAKE_DIRECTORY "${_parent_dir}")
        endif()
        file(CREATE_LINK "${_src}" "${_abs_dest}" RESULT _link_rc COPY_ON_ERROR)
        if(NOT _link_rc STREQUAL "0")
            message(WARNING "make_livecd_wim.cmake: CREATE_LINK for '${_src}' failed: ${_link_rc}")
            continue()
        endif()
    else()
        message(WARNING "make_livecd_wim.cmake: source '${_src}' does not exist, skipping")
        continue()
    endif()
endforeach()

set(_wimage_threads 0)

if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(_env_j "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
    if(_env_j MATCHES "^[0-9]+$" AND NOT _env_j EQUAL 0)
        set(_wimage_threads ${_env_j})
    endif()
endif()

if(_wimage_threads EQUAL 0 AND EXISTS "/proc/self/stat")
    set(_walk_pid "self")
    set(_walk_max_levels 6)
    set(_walk_level 0)

    while(_walk_level LESS _walk_max_levels)
        if(NOT EXISTS "/proc/${_walk_pid}/stat")
            break()
        endif()
        file(READ "/proc/${_walk_pid}/stat" _walk_stat)
        if(NOT _walk_stat MATCHES "\\) [RSDTWXKPI] ([0-9]+) ")
            break()
        endif()
        set(_candidate_ppid "${CMAKE_MATCH_1}")
        if(NOT _candidate_ppid MATCHES "^[0-9]+$" OR _candidate_ppid EQUAL 0)
            break()
        endif()

        if(NOT EXISTS "/proc/${_candidate_ppid}/cmdline")
            break()
        endif()

        execute_process(
            COMMAND tr "\\0" "\n"
            INPUT_FILE "/proc/${_candidate_ppid}/cmdline"
            OUTPUT_VARIABLE _p_cmdline
            RESULT_VARIABLE _tr_rc
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _tr_rc EQUAL 0)
            break()
        endif()

        string(REPLACE "\n" ";" _p_argv "${_p_cmdline}")
        list(GET _p_argv 0 _p_arg0)
        get_filename_component(_p_arg0_base "${_p_arg0}" NAME)

        if(_p_arg0_base STREQUAL "ninja")
            set(_capture_next_j FALSE)
            foreach(_arg IN LISTS _p_argv)
                if(_arg STREQUAL "")
                    continue()
                endif()
                if(_capture_next_j)
                    if(_arg MATCHES "^[0-9]+$")
                        set(_wimage_threads ${_arg})
                    endif()
                    break()
                endif()
                if(_arg STREQUAL "-j")
                    set(_capture_next_j TRUE)
                elseif(_arg MATCHES "^-j([0-9]+)$")
                    set(_wimage_threads "${CMAKE_MATCH_1}")
                    break()
                endif()
            endforeach()
            break()
        endif()

        set(_walk_pid "${_candidate_ppid}")
        math(EXPR _walk_level "${_walk_level} + 1")
    endwhile()
endif()

if(_wimage_threads EQUAL 0)
    include(ProcessorCount)
    ProcessorCount(_wimage_threads)
    if(_wimage_threads LESS 2)
        set(_wimage_threads 1)
    endif()
endif()

set(_capture_wim "${OUTPUT_WIM}.capture")
file(REMOVE "${_capture_wim}")

execute_process(
    COMMAND "${WIMAGE_EXE}" capture "${STAGE_DIR}" "${_capture_wim}" "ReactOS LiveCD"
            --compress=xpress --boot --threads=${_wimage_threads}
    RESULT_VARIABLE _wimage_rc
    OUTPUT_VARIABLE _wimage_out
    ERROR_VARIABLE _wimage_out)

if(NOT _wimage_rc EQUAL 0)
    message(FATAL_ERROR "make_livecd_wim.cmake: wimage capture failed (rc=${_wimage_rc}):\n${_wimage_out}")
endif()

if(NOT EXISTS "${_capture_wim}")
    message(FATAL_ERROR "make_livecd_wim.cmake: wimage reported success but ${_capture_wim} does not exist")
endif()

file(REMOVE "${OUTPUT_WIM}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy "${_capture_wim}" "${OUTPUT_WIM}"
    RESULT_VARIABLE _copy_rc
    OUTPUT_VARIABLE _copy_out
    ERROR_VARIABLE _copy_out)

if(NOT _copy_rc EQUAL 0)
    message(FATAL_ERROR "make_livecd_wim.cmake: copying ${_capture_wim} to ${OUTPUT_WIM} failed (rc=${_copy_rc}):\n${_copy_out}")
endif()

file(REMOVE "${_capture_wim}")

if(NOT EXISTS "${OUTPUT_WIM}")
    message(FATAL_ERROR "make_livecd_wim.cmake: copied WIM ${OUTPUT_WIM} does not exist")
endif()

file(STRINGS "${SOURCE_FREELDR_INI}" _ini_lines ENCODING UTF-8)
file(WRITE "${OUTPUT_FREELDR_INI}" "")

set(_current_section "")
foreach(_line IN LISTS _ini_lines)
    set(_out_line "${_line}")

    if(_line MATCHES "^\\[([^]]+)\\]$")
        set(_current_section "${CMAKE_MATCH_1}")
    elseif(_current_section MATCHES "^LiveCD")
        if(_line STREQUAL "SystemPath=\\reactos")
            set(_out_line "SystemPath=ramdisk(0)\\reactos")
        elseif(_line MATCHES "^Options=(.*)$")
            set(_opts "${CMAKE_MATCH_1}")
            if(NOT _opts MATCHES "(^| )/RDPATH=")
                set(_opts "${_opts} /RDPATH=boot.wim")
            endif()
            set(_out_line "Options=${_opts}")
        endif()
    endif()

    file(APPEND "${OUTPUT_FREELDR_INI}" "${_out_line}\n")
endforeach()

file(WRITE "${OUTPUT_LIST}" "")
foreach(_line IN LISTS _loader_lines)
    if(_line MATCHES "^/?freeldr\\.ini=")
        continue()
    endif()
    file(APPEND "${OUTPUT_LIST}" "${_line}\n")
endforeach()

file(APPEND "${OUTPUT_LIST}" "/boot.wim=${OUTPUT_WIM}\n")
file(APPEND "${OUTPUT_LIST}" "/freeldr.ini=${OUTPUT_FREELDR_INI}\n")
