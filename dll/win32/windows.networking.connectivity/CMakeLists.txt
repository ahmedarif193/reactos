spec2def(windows.networking.connectivity.dll windows.networking.connectivity.spec)

add_definitions(-DDO_NO_IMPORTS)

list(APPEND SOURCE
    main.c
    network_information.c
    private.h)

add_library(windows_networking_connectivity MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/windows.networking.connectivity.def
    ${CMAKE_CURRENT_BINARY_DIR}/windows.networking.connectivity_stubs.c)

set_target_properties(windows_networking_connectivity PROPERTIES OUTPUT_NAME "windows.networking.connectivity")
add_idl_reg_scripts(windows_networking_connectivity winrt classes.idl)
set_module_type(windows_networking_connectivity win32dll)
target_compile_definitions(windows_networking_connectivity PRIVATE __WINESRC__)
target_link_libraries(windows_networking_connectivity uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(windows_networking_connectivity combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_networking_connectivity DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_networking_connectivity)
