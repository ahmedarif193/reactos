# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(sensorsapi_winetest
    sensorsapi.c
    testlist.c)

set_module_type(sensorsapi_winetest win32cui)
target_link_libraries(sensorsapi_winetest uuid)
add_importlibs(sensorsapi_winetest ole32 oleaut32 propsys msvcrt kernel32)
add_rostests_file(TARGET sensorsapi_winetest)
