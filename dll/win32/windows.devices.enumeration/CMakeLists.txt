spec2def(windows.devices.enumeration.dll windows.devices.enumeration.spec)
add_idl_headers(windows_devices_enumeration_private_idl async_private.idl)
BISON_TARGET(aqs aqs.y ${CMAKE_CURRENT_BINARY_DIR}/aqs.tab.c)

list(APPEND SOURCE
    access.c
    aqs.c
    async.c
    event_handlers.c
    information.c
    main.c
    vector.c
    weakref.c)

add_library(windows_devices_enumeration MODULE ${SOURCE} ${BISON_aqs_OUTPUTS} ${CMAKE_CURRENT_BINARY_DIR}/windows.devices.enumeration.def ${CMAKE_CURRENT_BINARY_DIR}/windows.devices.enumeration_stubs.c)
set_target_properties(windows_devices_enumeration PROPERTIES OUTPUT_NAME "windows.devices.enumeration")
add_idl_reg_scripts(windows_devices_enumeration winrt classes.idl)
add_dependencies(windows_devices_enumeration windows_devices_enumeration_private_idl)
set_module_type(windows_devices_enumeration win32dll)
target_compile_definitions(windows_devices_enumeration PRIVATE __WINESRC__)
target_link_libraries(windows_devices_enumeration uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_devices_enumeration cfgmgr32 combase propsys msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_devices_enumeration DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_devices_enumeration)
