spec2def(windows.networking.hostname.dll windows.networking.hostname.spec)

list(APPEND SOURCE
    hostname.c
    main.c
    private.h)

add_library(windows_networking_hostname MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/windows.networking.hostname.def
    ${CMAKE_CURRENT_BINARY_DIR}/windows.networking.hostname_stubs.c)

set_target_properties(windows_networking_hostname PROPERTIES OUTPUT_NAME "windows.networking.hostname")
add_idl_reg_scripts(windows_networking_hostname winrt classes.idl)
set_module_type(windows_networking_hostname win32dll)
target_compile_definitions(windows_networking_hostname PRIVATE __WINESRC__)
target_link_libraries(windows_networking_hostname uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(windows_networking_hostname combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_networking_hostname DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_networking_hostname)
