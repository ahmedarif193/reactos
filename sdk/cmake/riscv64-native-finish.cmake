# GenFw owns ELF relocation conversion. riscv64-pe supplies only NT header and
# directory metadata, after checking the address-preserving layout contract.
execute_process(COMMAND "${NM}" --defined-only --extern-only --format=posix "${ELF}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _symbols ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Cannot read native image symbols: ${_error}")
endif()
foreach(_symbol export_start export_end import_start import_end iat_start iat_end text_start text_end data_start data_end)
    if(NOT _symbols MATCHES "(^|\n)__riscv_${_symbol} [A-Za-z] ([0-9a-fA-F]+) ")
        message(FATAL_ERROR "Missing native image layout symbol __riscv_${_symbol}")
    endif()
    set(_${_symbol} "0x${CMAKE_MATCH_2}")
endforeach()
if(NOT _text_start EQUAL 4096)
    message(FATAL_ERROR "Native ELF text must begin at RVA 0x1000")
endif()
set(_directories)
foreach(_directory export import iat text data)
    math(EXPR _size "${_${_directory}_end} - ${_${_directory}_start}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND _directories "${_${_directory}_start}" "${_size}")
endforeach()
list(JOIN _directories "," _directories)

execute_process(COMMAND "${GENFW}" -e UEFI_APPLICATION -o "${OUTPUT}.tmp" "${ELF}"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "RISC-V ELF-to-PE conversion failed: ${_result}")
endif()
execute_process(COMMAND "${PEFIXUP}" "${MODE}" "${_directories}" "${OUTPUT}.tmp"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "RISC-V native PE finalization failed: ${_result}")
endif()
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
