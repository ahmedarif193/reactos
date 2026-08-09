# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

spec2def(portabledevicetypes.dll portabledevicetypes.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    collections.c
    main.c
    values.c
    private.h)

add_library(portabledevicetypes MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/portabledevicetypes.def)

add_idl_reg_scripts(portabledevicetypes registry classes.idl)
set_module_type(portabledevicetypes win32dll UNICODE)
target_compile_definitions(portabledevicetypes PRIVATE __WINESRC__)
target_link_libraries(portabledevicetypes uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(portabledevicetypes propsys ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET portabledevicetypes DESTINATION reactos/system32 FOR all)
set_wine_module(portabledevicetypes)
