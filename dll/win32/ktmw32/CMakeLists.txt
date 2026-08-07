
add_definitions(-D__WINESRC__)
spec2def(ktmw32.dll ktmw32.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ktmw32.c
    ${CMAKE_CURRENT_BINARY_DIR}/ktmw32_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/ktmw32.def)

add_library(ktmw32 MODULE ${SOURCE})
set_module_type(ktmw32 win32dll)
target_link_libraries(ktmw32 wine)
add_importlibs(ktmw32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET ktmw32 DESTINATION reactos/system32 FOR all)
