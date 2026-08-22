spec2def(windows.devices.bluetooth.dll windows.devices.bluetooth.spec)
add_idl_headers(windows_devices_bluetooth_private_idl async_private.idl)

list(APPEND SOURCE
    advertisement.c
    async.c
    bluetoothadapter.c
    bluetoothdevice.c
    gatt.c
    main.c
    vector.c)

set_source_files_properties(bluetoothadapter.c PROPERTIES COMPILE_FLAGS "-include reactos_compat.h")
add_library(windows_devices_bluetooth MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.devices.bluetooth.def ${CMAKE_CURRENT_BINARY_DIR}/windows.devices.bluetooth_stubs.c)
set_target_properties(windows_devices_bluetooth PROPERTIES OUTPUT_NAME "windows.devices.bluetooth")
add_idl_reg_scripts(windows_devices_bluetooth winrt classes.idl)
add_dependencies(windows_devices_bluetooth windows_devices_bluetooth_private_idl)
set_module_type(windows_devices_bluetooth win32dll)
target_compile_definitions(windows_devices_bluetooth PRIVATE __WINESRC__)
target_link_libraries(windows_devices_bluetooth uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_devices_bluetooth bluetoothapis combase setupapi msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_devices_bluetooth DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_devices_bluetooth)
