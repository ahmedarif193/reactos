# WOW64CPU.DLL - WOW64 CPU Architecture Translation Layer
#
# This is a 64-bit DLL that provides CPU context switching between 64-bit and
# 32-bit execution modes. It handles register mapping, stack frame conversion,
# and instruction pointer translation.
#
# Architecture: amd64 only (64-bit)
# Location: %SystemRoot%\system32\wow64cpu.dll
# Purpose: x86-64 to x86 CPU context translation and mode switching

if(NOT ARCH STREQUAL "amd64")
    return()
endif()

spec2def(wow64cpu.dll wow64cpu.spec ADD_IMPORTLIB)

add_library(wow64cpu MODULE
    wow64cpu.c
    wow64cpu_amd64.S
    ${CMAKE_CURRENT_BINARY_DIR}/wow64cpu.def)

set_module_type(wow64cpu win32dll UNICODE)
target_link_libraries(wow64cpu pseh)
add_importlibs(wow64cpu msvcrt kernel32 ntdll wow64)

# Install to system32 (64-bit DLLs directory)
# This DLL performs low-level CPU context translation for WOW64
add_cd_file(TARGET wow64cpu DESTINATION reactos/system32 FOR all)
