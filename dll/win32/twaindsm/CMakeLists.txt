spec2def(twaindsm.dll twaindsm.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    dsm_ctrl.c
    twain32_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/twaindsm_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/twaindsm.def)

add_library(twaindsm MODULE
    ${SOURCE}
    twain.rc)

set_module_type(twaindsm win32dll)
target_compile_definitions(twaindsm PRIVATE __WINESRC__)
target_link_libraries(twaindsm wine)
add_importlibs(twaindsm user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET twaindsm DESTINATION reactos/system32 FOR all)
set_wine_module(twaindsm)
