
spec2def(msvcrt20.dll msvcrt20.spec)

add_library(msvcrt20 MODULE
    msvcrt20.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcrt20_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcrt20.def)

set_module_type(msvcrt20 win32dll ENTRYPOINT 0)
add_importlibs(msvcrt20 msvcrt kernel32 ntdll)

add_cd_file(TARGET msvcrt20 DESTINATION reactos/system32 FOR all)
