
# PROJECT:     ReactOS Desktop Window Manager
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:     Build script for dwm.exe
# COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
#
# dwm.exe is a user-mode service that composites all visible top-level
# windows into a single full-screen back buffer, then presents it to
# the display through the WDDM D3DKMT interface.
#
# Phase 1: CPU-based compositing (memcpy/BitBlt)
# Phase 2: GPU-accelerated compositing via D3D (future)

include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/ddk
    ${REACTOS_SOURCE_DIR}/sdk/include/psdk
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/wine)

list(APPEND SOURCE
    dwm.c
    compositor.c
    dwm.h)

add_executable(dwm ${SOURCE})

# win32gui: links against the GUI subsystem (no console window)
set_module_type(dwm win32gui UNICODE)

target_link_libraries(dwm wine)
add_importlibs(dwm gdi32 user32 advapi32 msvcrt kernel32 ntdll)
# Repeat wine after import libs so its kernel32/msvcrt refs resolve
target_link_libraries(dwm wine)

add_cd_file(TARGET dwm DESTINATION reactos/system32 FOR all)

add_executable(glxgears glxgears.c)
set_module_type(glxgears win32gui)
add_importlibs(glxgears user32 gdi32 opengl32 glu32 msvcrt kernel32)
add_cd_file(TARGET glxgears DESTINATION reactos/system32 FOR all)

# WDDM test scripts — placed in system32 for easy access
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/wddm_test.cmd DESTINATION reactos/system32 FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/wddm_apitest_all.cmd DESTINATION reactos/system32 FOR all)
