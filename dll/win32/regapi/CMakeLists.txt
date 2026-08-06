
spec2def(regapi.dll regapi.spec)

list(APPEND SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/regapi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/regapi.def)

add_library(regapi MODULE ${SOURCE})
set_module_type(regapi win32dll)
add_importlibs(regapi msvcrt kernel32 ntdll)
add_cd_file(TARGET regapi DESTINATION reactos/system32 FOR all)
set_wine_module(regapi)
