spec2def(windows.devices.usb.dll windows.devices.usb.spec)

list(APPEND SOURCE
    main.c
    usbdevice.c)

add_library(windows_devices_usb MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.devices.usb.def ${CMAKE_CURRENT_BINARY_DIR}/windows.devices.usb_stubs.c)
set_target_properties(windows_devices_usb PROPERTIES OUTPUT_NAME "windows.devices.usb")
add_idl_reg_scripts(windows_devices_usb winrt classes.idl)
set_module_type(windows_devices_usb win32dll)
target_compile_definitions(windows_devices_usb PRIVATE __WINESRC__)
target_link_libraries(windows_devices_usb uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_devices_usb combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_devices_usb DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_devices_usb)
