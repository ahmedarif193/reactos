
spec2def(bluetoothapis.dll bluetoothapis.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    gatt.c
    main.c
    sdp.c
    ${CMAKE_CURRENT_BINARY_DIR}/bluetoothapis_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/bluetoothapis.def)

add_library(bluetoothapis MODULE ${SOURCE} bluetoothapis.rc)
set_module_type(bluetoothapis win32dll)

target_compile_definitions(bluetoothapis PRIVATE __WINESRC__)
target_link_libraries(bluetoothapis wine wine_dll_canunload)
add_importlibs(bluetoothapis setupapi cfgmgr32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET bluetoothapis DESTINATION reactos/system32 FOR all)
set_wine_module(bluetoothapis)
