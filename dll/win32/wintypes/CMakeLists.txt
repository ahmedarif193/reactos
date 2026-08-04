spec2def(wintypes.dll wintypes.spec ADD_IMPORTLIB)
add_idl_headers(wintypes_private_idl wintypes_private.idl)

list(APPEND SOURCE
    buffer.c
    main.c
    map.c
    propertyset.c
    storage.c
    private.h)

add_library(wintypes MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/wintypes.def
    ${CMAKE_CURRENT_BINARY_DIR}/wintypes_stubs.c)

add_idl_reg_scripts(wintypes winrt classes.idl)
add_dependencies(wintypes wintypes_private_idl)
set_module_type(wintypes win32dll)
target_compile_definitions(wintypes PRIVATE __WINESRC__)
target_link_libraries(wintypes uuid wine wine_dll_canunload)
add_importlibs(wintypes combase msvcrt kernel32 ntdll)
add_cd_file(TARGET wintypes DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(wintypes)
