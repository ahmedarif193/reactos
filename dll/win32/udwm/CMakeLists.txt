# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

spec2def(uDWM.dll udwm.spec ADD_IMPORTLIB)

add_library(udwm MODULE
    udwm.c
    ${CMAKE_CURRENT_BINARY_DIR}/uDWM.def)

set_target_properties(udwm PROPERTIES OUTPUT_NAME "uDWM")
set_module_type(udwm win32dll UNICODE)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_link_options(udwm PRIVATE
        -Wl,-Xlink=/version:10.0
        -Wl,--major-subsystem-version,10
        -Wl,--minor-subsystem-version,0)
endif()
target_link_libraries(udwm uuid)
add_importlibs(udwm dwmcore msvcrt kernel32 ntdll)
add_cd_file(TARGET udwm DESTINATION reactos/system32 FOR all)
