
add_executable(bluetoothapis_winetest
    device.c
    gatt.c
    radio.c
    resource.rc
    sdp.c
    testlist.c)

set_module_type(bluetoothapis_winetest win32cui)
target_compile_definitions(bluetoothapis_winetest PRIVATE WINETEST_USE_DBG_SPRINTF)
add_importlibs(bluetoothapis_winetest bluetoothapis setupapi user32 msvcrt kernel32)
add_rostests_file(TARGET bluetoothapis_winetest)
