# Retain the common 64-bit DLL spacing inside the Sv39 user range.
# The x64 table starts near 8 TiB, beyond Sv39's 256 GiB user half.
# Translate it down to place ntdll at 0x3fb7000000; do not mask addresses.
include("${CMAKE_CURRENT_LIST_DIR}/baseaddress64.cmake")

get_cmake_property(_riscv_base_variables VARIABLES)
foreach(_riscv_base_variable IN LISTS _riscv_base_variables)
    if(_riscv_base_variable MATCHES "^baseaddress_")
        math(EXPR _riscv_image_base "${${_riscv_base_variable}} - 0x7c000000000" OUTPUT_FORMAT HEXADECIMAL)
        # Keep the DLL arena below 255 GiB, leaving room below the user limit.
        if(_riscv_image_base LESS 0x10000 OR _riscv_image_base GREATER_EQUAL 0x3fc0000000)
            message(FATAL_ERROR "${_riscv_base_variable} is outside the RISC-V DLL arena")
        endif()
        set(${_riscv_base_variable} ${_riscv_image_base})
    endif()
endforeach()
unset(_riscv_base_variables)
unset(_riscv_base_variable)
unset(_riscv_image_base)
