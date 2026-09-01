# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

spec2def(cfg_guarded.dll cfg_guarded.spec ADD_IMPORTLIB)
add_library(cfg_guarded MODULE
    cfg_guarded.c
    ${CMAKE_CURRENT_BINARY_DIR}/cfg_guarded.def)
set_module_type(cfg_guarded win32dll)
target_compile_options(cfg_guarded PRIVATE -mguard=cf)
target_link_options(cfg_guarded PRIVATE -Wl,--guard-cf)
add_importlibs(cfg_guarded msvcrt kernel32 ntdll)

spec2def(cfg_plain.dll cfg_plain.spec ADD_IMPORTLIB)
add_library(cfg_plain MODULE
    cfg_plain.c
    ${CMAKE_CURRENT_BINARY_DIR}/cfg_plain.def)
set_module_type(cfg_plain win32dll)
add_importlibs(cfg_plain msvcrt kernel32 ntdll)

add_executable(cfg_child cfg_child.c)
set_module_type(cfg_child win32cui)
target_compile_options(cfg_child PRIVATE -mguard=cf)
target_link_options(cfg_child PRIVATE -Wl,--guard-cf)
add_importlibs(cfg_child msvcrt kernel32 ntdll)
add_dependencies(cfg_child cfg_guarded cfg_plain)

add_executable(cfg_apitest cfg.c testlist.c)
set_module_type(cfg_apitest win32cui)
target_compile_options(cfg_apitest PRIVATE -mguard=cf)
target_link_options(cfg_apitest PRIVATE -Wl,--guard-cf)
target_link_libraries(cfg_apitest wine)
add_importlibs(cfg_apitest msvcrt kernel32 ntdll)
add_dependencies(cfg_apitest cfg_child cfg_guarded cfg_plain)

add_rostests_file(TARGET cfg_apitest)
add_rostests_file(TARGET cfg_child)
add_rostests_file(TARGET cfg_guarded)
add_rostests_file(TARGET cfg_plain)
