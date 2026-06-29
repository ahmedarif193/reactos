if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "sort_win32k_def.cmake requires INPUT and OUTPUT")
endif()

file(READ "${INPUT}" _def_text)
string(REPLACE "\r\n" "\n" _def_text "${_def_text}")
string(REPLACE "\r" "\n" _def_text "${_def_text}")
string(FIND "${_def_text}" "EXPORTS\n" _exports_pos)

if(_exports_pos EQUAL -1)
    message(FATAL_ERROR "Could not find EXPORTS section in ${INPUT}")
endif()

math(EXPR _exports_body_pos "${_exports_pos} + 8")
string(SUBSTRING "${_def_text}" 0 ${_exports_body_pos} _prefix_text)
string(SUBSTRING "${_def_text}" ${_exports_body_pos} -1 _exports_text)
string(REPLACE "\n" ";" _def_lines "${_exports_text}")

set(_export_lines)

foreach(_line IN LISTS _def_lines)
    if(NOT "${_line}" STREQUAL "")
        list(APPEND _export_lines "${_line}")
    endif()
endforeach()

list(SORT _export_lines)
string(REPLACE ";" "\n" _exports_text "${_export_lines}")
file(WRITE "${OUTPUT}" "${_prefix_text}\n${_exports_text}\n")
