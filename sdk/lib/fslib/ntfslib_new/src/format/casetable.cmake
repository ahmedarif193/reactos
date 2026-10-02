#
# PROJECT:     LiberNT NTFS library
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Give a host build of the formatter the case table of the system
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

set(NTFSLIB_CASE_TABLE_FILE "${CMAKE_CURRENT_LIST_DIR}/../../../../../../media/nls/l_intl.nls")

function(ntfslib_add_case_table _target)
    get_filename_component(_source "${NTFSLIB_CASE_TABLE_FILE}" ABSOLUTE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_source}")
    file(READ "${_source}" _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ntfscasetable.h.new"
        "static const unsigned char NtfsCaseTable[] = { ${_bytes} };\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/ntfscasetable.h.new"
        "${CMAKE_CURRENT_BINARY_DIR}/ntfscasetable.h")
    target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()
