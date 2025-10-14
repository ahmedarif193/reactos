# WOW64.DLL - Windows-on-Windows 64-bit Core Emulation Layer
#
# This is a 64-bit DLL that manages 32-bit process execution on amd64 systems.
# It must be installed in system32 (not SysWOW64) as it is part of the native
# 64-bit OS infrastructure.
#
# Architecture: amd64 only (64-bit)
# Location: %SystemRoot%\system32\wow64.dll
# Purpose: Core WOW64 process and thread management, syscall interception

if(NOT ARCH STREQUAL "amd64")
    return()
endif()

spec2def(wow64.dll wow64.spec ADD_IMPORTLIB)

add_library(wow64 MODULE
    wow64.c
    ${CMAKE_CURRENT_BINARY_DIR}/wow64.def)

set_module_type(wow64 win32dll UNICODE)
add_importlibs(wow64 msvcrt kernel32 ntdll wow64cpu)
add_dependencies(wow64 wow64cpu)

# Install to system32 (64-bit DLLs directory)
# This DLL is loaded by 64-bit system processes to manage 32-bit execution
add_cd_file(TARGET wow64 DESTINATION reactos/system32 FOR all)
