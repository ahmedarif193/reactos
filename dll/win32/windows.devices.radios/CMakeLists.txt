spec2def(windows.devices.radios.dll windows.devices.radios.spec)
add_idl_headers(windows_devices_radios_private_idl async_private.idl)

list(APPEND SOURCE
    async.c
    main.c
    vector.c)

add_library(windows_devices_radios MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.devices.radios.def ${CMAKE_CURRENT_BINARY_DIR}/windows.devices.radios_stubs.c)
set_target_properties(windows_devices_radios PROPERTIES OUTPUT_NAME "windows.devices.radios")
add_idl_reg_scripts(windows_devices_radios winrt classes.idl)
add_dependencies(windows_devices_radios windows_devices_radios_private_idl)
set_module_type(windows_devices_radios win32dll)
target_compile_definitions(windows_devices_radios PRIVATE __WINESRC__)
target_link_libraries(windows_devices_radios uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_devices_radios combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_devices_radios DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_devices_radios)
