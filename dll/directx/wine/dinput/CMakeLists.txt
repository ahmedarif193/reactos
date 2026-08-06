
add_definitions(-DDIRECTINPUT_VERSION=0x0700 -D_USE_MATH_DEFINES)
spec2def(dinput.dll dinput.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ansi.c
    config.c
    device.c
    dinput.c
    dinput_main.c
    joystick_hid.c
    keyboard.c
    mouse.c)

add_library(dinput MODULE
    ${SOURCE}
    data_formats.c
    dinput.rc
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dinput.def)
add_idl_reg_scripts(dinput registry dinput_classes.idl)
set_wine_module_FIXME(dinput) # CORE-5743: No ARRAY_SIZE macro

add_library(dinput_data_formats data_formats.c)
add_dependencies(dinput_data_formats psdk)
set_wine_module_FIXME(dinput_data_formats) # CORE-5743: No ARRAY_SIZE macro

set_module_type(dinput win32dll)
target_link_libraries(dinput dxguid uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(dinput cfgmgr32 comctl32 ole32 user32_vista user32 advapi32 hid setupapi msvcrt kernel32_vista kernel32 ntdll)
add_cd_file(TARGET dinput DESTINATION reactos/system32 FOR all)
