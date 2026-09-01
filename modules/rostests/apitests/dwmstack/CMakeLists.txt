# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(dwmstack_apitest dwmstack.c testlist.c)
set_module_type(dwmstack_apitest win32cui)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_link_options(dwmstack_apitest PRIVATE
        -Wl,-Xlink=/version:10.0
        -Wl,--major-subsystem-version,10
        -Wl,--minor-subsystem-version,0)
endif()
target_link_libraries(dwmstack_apitest wine uuid)
add_importlibs(dwmstack_apitest advapi32 ole32 user32 gdi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET dwmstack_apitest)
