# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

spec2def(sensorsapi.dll sensorsapi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    collection.c
    guids.c
    main.c
    manager.c
    report.c
    sensor.c
    private.h)

add_library(sensorsapi MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/sensorsapi.def)

add_idl_reg_scripts(sensorsapi registry classes.idl)
set_module_type(sensorsapi win32dll UNICODE)
target_compile_definitions(sensorsapi PRIVATE __WINESRC__)
target_link_libraries(sensorsapi uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(sensorsapi setupapi propsys ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET sensorsapi DESTINATION reactos/system32 FOR all)
set_wine_module(sensorsapi)
