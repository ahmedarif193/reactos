
spec2def(twain_32.dll twain_32.spec)

add_library(twain_32 MODULE
    ${CMAKE_CURRENT_BINARY_DIR}/twain_32_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/twain_32.def)

set_module_type(twain_32 win32dll)
add_importlibs(twain_32 twaindsm msvcrt kernel32 ntdll)
add_cd_file(TARGET twain_32 DESTINATION reactos FOR all)
