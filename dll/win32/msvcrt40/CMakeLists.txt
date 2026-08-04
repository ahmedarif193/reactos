
spec2def(msvcrt40.dll msvcrt40.spec)

add_library(msvcrt40 MODULE
    ${CMAKE_CURRENT_BINARY_DIR}/msvcrt40_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcrt40.def)

set_module_type(msvcrt40 win32dll ENTRYPOINT 0)
add_importlibs(msvcrt40 ntdll)

add_cd_file(TARGET msvcrt40 DESTINATION reactos/system32 FOR all)
