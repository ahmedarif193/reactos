# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

spec2def(dwmcore.dll dwmcore.spec ADD_IMPORTLIB)

add_library(dwmcore MODULE
    dwmcore.c
    ../../../base/system/dwm/dwm.c
    ../../../base/system/dwm/dxsurface.c
    ${CMAKE_CURRENT_BINARY_DIR}/dwmcore.def)

target_include_directories(dwmcore PRIVATE
    ${REACTOS_SOURCE_DIR}/base/system/dwm)

set_module_type(dwmcore win32dll UNICODE)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_link_options(dwmcore PRIVATE
        -Wl,-Xlink=/version:10.0
        -Wl,--major-subsystem-version,10
        -Wl,--minor-subsystem-version,0)
endif()
target_link_libraries(dwmcore uuid)
add_importlibs(dwmcore win32u user32 gdi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dwmcore DESTINATION reactos/system32 FOR all)
