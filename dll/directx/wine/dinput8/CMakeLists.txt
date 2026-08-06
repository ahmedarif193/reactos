
add_definitions(-DDIRECTINPUT_VERSION=0x0800 -D_USE_MATH_DEFINES)
spec2def(dinput8.dll dinput8.spec ADD_IMPORTLIB)
set(DINPUT_SOURCE_FOLDER ../dinput)

list(APPEND SOURCE
    ${DINPUT_SOURCE_FOLDER}/ansi.c
    ${DINPUT_SOURCE_FOLDER}/config.c
    ${DINPUT_SOURCE_FOLDER}/data_formats.c
    ${DINPUT_SOURCE_FOLDER}/device.c
    ${DINPUT_SOURCE_FOLDER}/dinput.c
    ${DINPUT_SOURCE_FOLDER}/dinput_main.c
    ${DINPUT_SOURCE_FOLDER}/joystick_hid.c
    ${DINPUT_SOURCE_FOLDER}/keyboard.c
    ${DINPUT_SOURCE_FOLDER}/mouse.c
    ${CMAKE_CURRENT_BINARY_DIR}/dinput8.def)

add_library(dinput8 MODULE
    ${SOURCE}
    ${DINPUT_SOURCE_FOLDER}/dinput.rc
    version.rc)
add_idl_reg_scripts(dinput8 registry dinput8.idl)

set_module_type(dinput8 win32dll)
target_link_libraries(dinput8 dxguid uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(dinput8 cfgmgr32 comctl32 ole32 user32_vista user32 advapi32 hid setupapi msvcrt kernel32_vista kernel32 ntdll)
add_cd_file(TARGET dinput8 DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(dinput8) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
