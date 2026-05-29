cmake_minimum_required(VERSION 3.16)

foreach(_required PARTITION_FILE INPUT_LIST PARTITION_SECTORS PARTITION_START_SECTOR VOLUME_LABEL)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "make_preinstall_ntfs.cmake missing required variable ${_required}")
    endif()
endforeach()

function(_find_required_program _outvar)
    find_program(${_outvar} NAMES ${ARGN})
    if(NOT ${_outvar})
        string(REPLACE ";" ", " _names "${ARGN}")
        message(FATAL_ERROR "make_preinstall_ntfs.cmake requires one of: ${_names}")
    endif()
    set(${_outvar} "${${_outvar}}" PARENT_SCOPE)
endfunction()

function(_run_or_record _resultvar _command_desc)
    if(NOT "${${_resultvar}}" STREQUAL "")
        return()
    endif()

    set(_cmd ${ARGN})
    execute_process(
        COMMAND ${_cmd}
        RESULT_VARIABLE _rv
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
    if(NOT _rv EQUAL 0)
        string(STRIP "${_out}" _out)
        string(STRIP "${_err}" _err)
        set(_details "${_err}")
        if(_details STREQUAL "")
            set(_details "${_out}")
        endif()
        if(_details STREQUAL "")
            set(_details "exit code ${_rv}")
        endif()
        set(${_resultvar} "${_command_desc}: ${_details}" PARENT_SCOPE)
    endif()
endfunction()

function(_normalize_image_path _outvar _input)
    string(REPLACE "\\" "/" _path "${_input}")
    string(REGEX REPLACE "^/+" "" _path "${_path}")
    set(${_outvar} "${_path}" PARENT_SCOPE)
endfunction()

_find_required_program(TRUNCATE_EXE truncate)
_find_required_program(MKFS_NTFS_EXE mkfs.ntfs mkntfs)

if(NOT DEFINED MKNTFSIMG_EXE OR MKNTFSIMG_EXE STREQUAL "")
    _find_required_program(NTFS_3G_EXE ntfs-3g)
    find_program(FUSERMOUNT_EXE NAMES fusermount3 fusermount)
    if(NOT FUSERMOUNT_EXE)
        message(FATAL_ERROR "make_preinstall_ntfs.cmake requires fusermount3 or fusermount")
    endif()
endif()

math(EXPR _partition_bytes "${PARTITION_SECTORS} * 512")
set(_mount_dir "${PARTITION_FILE}.mnt")
set(_failure "")
set(_mounted FALSE)

file(REMOVE_RECURSE "${_mount_dir}")
file(REMOVE "${PARTITION_FILE}")
file(MAKE_DIRECTORY "${_mount_dir}")

_run_or_record(_failure "truncate partition image"
    "${TRUNCATE_EXE}" -s "${_partition_bytes}" "${PARTITION_FILE}")
_run_or_record(_failure "mkfs.ntfs partition image"
    "${MKFS_NTFS_EXE}"
    -f
    -F
    -s 512
    -p "${PARTITION_START_SECTOR}"
    -H 255
    -S 63
    -L "${VOLUME_LABEL}"
    "${PARTITION_FILE}")

if(_failure STREQUAL "" AND DEFINED MKNTFSIMG_EXE AND NOT MKNTFSIMG_EXE STREQUAL "")
    _run_or_record(_failure "native-mkntfsimg populate"
        "${MKNTFSIMG_EXE}" "${PARTITION_FILE}" "-addfiles" "${INPUT_LIST}")
    if(NOT _failure STREQUAL "")
        message(FATAL_ERROR "${_failure}")
    endif()
    return()
endif()

if(_failure STREQUAL "")
    execute_process(
        COMMAND "${NTFS_3G_EXE}" "${PARTITION_FILE}" "${_mount_dir}"
        RESULT_VARIABLE _mount_rv
        OUTPUT_VARIABLE _mount_out
        ERROR_VARIABLE _mount_err)
    if(_mount_rv EQUAL 0)
        set(_mounted TRUE)
    else()
        string(STRIP "${_mount_err}" _mount_err)
        string(STRIP "${_mount_out}" _mount_out)
        if(_mount_err STREQUAL "")
            set(_mount_err "${_mount_out}")
        endif()
        if(_mount_err STREQUAL "")
            set(_mount_err "exit code ${_mount_rv}")
        endif()
        set(_failure "ntfs-3g mount failed: ${_mount_err}")
    endif()
endif()

if(_failure STREQUAL "")
    file(STRINGS "${INPUT_LIST}" _entries)
    foreach(_entry IN LISTS _entries)
        string(STRIP "${_entry}" _entry)
        if(_entry STREQUAL "" OR _entry MATCHES "^#")
            continue()
        endif()

        string(FIND "${_entry}" "=" _equals)
        if(_equals EQUAL -1)
            set(_image_path "${_entry}")
            set(_host_path "")
        else()
            string(SUBSTRING "${_entry}" 0 ${_equals} _image_path)
            math(EXPR _host_index "${_equals} + 1")
            string(SUBSTRING "${_entry}" ${_host_index} -1 _host_path)
        endif()

        _normalize_image_path(_image_path "${_image_path}")
        if(_image_path STREQUAL "")
            continue()
        endif()

        set(_dest_path "${_mount_dir}/${_image_path}")

        if(_host_path STREQUAL "")
            file(MAKE_DIRECTORY "${_dest_path}")
            continue()
        endif()

        if(IS_DIRECTORY "${_host_path}")
            file(MAKE_DIRECTORY "${_dest_path}")
            file(COPY "${_host_path}/" DESTINATION "${_dest_path}")
        else()
            get_filename_component(_parent_dir "${_dest_path}" DIRECTORY)
            get_filename_component(_dest_name "${_dest_path}" NAME)
            get_filename_component(_src_name "${_host_path}" NAME)
            file(MAKE_DIRECTORY "${_parent_dir}")
            # The LST contains duplicate entries for some destination paths
            # (e.g. reactos/bin/testdata/test.otf is mapped first to
            # testdata/old/test.otf and then overridden to testdata/test.otf).
            # mkisofs honors the LAST occurrence; the LiveCD build relies on
            # that. CMake's file(COPY) silently does NOT overwrite an
            # existing destination file when timestamps tie, so the second
            # entry would be a no-op without an explicit delete.
            file(REMOVE "${_dest_path}")
            file(COPY "${_host_path}" DESTINATION "${_parent_dir}")
            if(NOT _dest_name STREQUAL _src_name)
                file(RENAME "${_parent_dir}/${_src_name}" "${_dest_path}")
            endif()
        endif()
    endforeach()
endif()

if(_mounted)
    execute_process(
        COMMAND "${FUSERMOUNT_EXE}" -u "${_mount_dir}"
        RESULT_VARIABLE _umount_rv
        OUTPUT_VARIABLE _umount_out
        ERROR_VARIABLE _umount_err)
    if(NOT _umount_rv EQUAL 0 AND _failure STREQUAL "")
        string(STRIP "${_umount_err}" _umount_err)
        string(STRIP "${_umount_out}" _umount_out)
        if(_umount_err STREQUAL "")
            set(_umount_err "${_umount_out}")
        endif()
        if(_umount_err STREQUAL "")
            set(_umount_err "exit code ${_umount_rv}")
        endif()
        set(_failure "fuse unmount failed: ${_umount_err}")
    endif()
endif()

file(REMOVE_RECURSE "${_mount_dir}")

if(NOT _failure STREQUAL "")
    message(FATAL_ERROR "${_failure}")
endif()
