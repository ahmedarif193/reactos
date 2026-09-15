# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF

foreach(_required PATCH_EXECUTABLE SOURCE_DIR PATCH_FILE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${_required} for the Vulkan loader patch step")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/loader/loader_windows.c" _loader_windows)
string(FIND "${_loader_windows}" "ReactOS images can use different SystemRoot drive letters" _patch_marker)
if(NOT _patch_marker EQUAL -1)
    return()
endif()

execute_process(
    COMMAND "${PATCH_EXECUTABLE}" -p1 -i "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _apply_result
    OUTPUT_VARIABLE _apply_output
    ERROR_VARIABLE _apply_error)
if(NOT _apply_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply Vulkan loader ReactOS patch: ${_apply_output}${_apply_error}")
endif()
