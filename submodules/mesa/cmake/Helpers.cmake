# SPDX-License-Identifier: MIT
function(mesa_generate)
    cmake_parse_arguments(PARSE_ARGV 0 GEN "" "CAPTURE" "OUTPUT;COMMAND;DEPENDS")
    if(GEN_UNPARSED_ARGUMENTS OR NOT GEN_OUTPUT OR NOT GEN_COMMAND)
        message(FATAL_ERROR "Invalid Mesa generator rule: ${GEN_UNPARSED_ARGUMENTS}")
    endif()
    foreach(_output IN LISTS GEN_OUTPUT)
        get_filename_component(_directory "${_output}" DIRECTORY)
        file(MAKE_DIRECTORY "${_directory}")
    endforeach()
    if(GEN_CAPTURE)
        set(_command "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/cmake/capture.py" "${GEN_CAPTURE}" ${GEN_COMMAND})
        list(APPEND GEN_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/capture.py")
    else()
        set(_command ${GEN_COMMAND})
    endif()
    add_custom_command(OUTPUT ${GEN_OUTPUT}
        COMMAND ${_command}
        DEPENDS ${GEN_DEPENDS}
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        VERBATIM)
    set(MESA_GENERATED_OUTPUTS ${MESA_GENERATED_OUTPUTS} ${GEN_OUTPUT} PARENT_SCOPE)
endfunction()

function(mesa_target_defaults target)
    # Generated public headers cross target boundaries. Finish generation before
    # any compilation; compiler depfiles then track the exact headers in use.
    add_dependencies(${target} mesa_generated)
    foreach(_option IN LISTS MESA_C_OPTIONS)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${_option}>")
    endforeach()
    foreach(_option IN LISTS MESA_CXX_OPTIONS)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${_option}>")
    endforeach()
    if(MESA_ASSERTIONS)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:-UNDEBUG>")
    else()
        target_compile_definitions(${target} PRIVATE NDEBUG)
    endif()
endfunction()
