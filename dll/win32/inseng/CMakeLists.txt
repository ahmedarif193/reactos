
spec2def(inseng.dll inseng.spec)

list(APPEND SOURCE
    inseng_main.c)

add_library(inseng MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/inseng_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/inseng.def)

set_module_type(inseng win32dll)
add_idl_reg_scripts(inseng registry inseng_classes.idl)
target_link_libraries(inseng uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(inseng ole32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET inseng DESTINATION reactos/system32 FOR all)
set_wine_module(inseng)
