
# PROJECT:     ReactOS API tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Task Manager 11 API tests
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(taskmgr11_apitest
    battery.c
    testlist.c)

target_include_directories(taskmgr11_apitest PRIVATE ${REACTOS_SOURCE_DIR}/base/applications/taskmgr11)
set_module_type(taskmgr11_apitest win32cui)
add_importlibs(taskmgr11_apitest setupapi msvcrt kernel32)
add_rostests_file(TARGET taskmgr11_apitest)
