# WOW64WIN.DLL - WOW64 Win32k (USER32/GDI32) Thunking Layer
#
# This is a 64-bit DLL that provides syscall translation for Win32k (USER/GDI)
# subsystem calls made by 32-bit applications. It converts 32-bit window manager
# and graphics calls to 64-bit kernel-mode equivalents.
#
# Architecture: amd64 only (64-bit)
# Location: %SystemRoot%\system32\wow64win.dll
# Purpose: USER32/GDI32 syscall thunking from 32-bit to 64-bit

if(NOT ARCH STREQUAL "amd64")
    return()
endif()

# Source files
list(APPEND SOURCE
    wow64win.c)

# Spec file for export definitions
add_library(wow64win MODULE
    ${SOURCE}
    wow64win.spec)

# Include directories
target_include_directories(wow64win PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include
    ${REACTOS_SOURCE_DIR}/sdk/include/ndk
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos
    ${REACTOS_SOURCE_DIR}/win32ss/include)

# Set module properties
set_module_type(wow64win win32dll ENTRYPOINT DllMain 12)

# Add to build dependencies
add_importlibs(wow64win
    ntdll
    kernel32)

# Add debug definitions for DBG builds
if(DBG)
    target_compile_definitions(wow64win PRIVATE DBG=1)
endif()

# Install to system32 (64-bit DLLs directory)
# This is a 64-bit DLL that translates 32-bit Win32k syscalls to 64-bit
add_cd_file(TARGET wow64win DESTINATION reactos/system32 FOR all)
