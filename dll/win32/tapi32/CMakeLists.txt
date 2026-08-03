
spec2def(tapi32.dll tapi32.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    assisted.c
    line.c
    phone.c
    ${CMAKE_CURRENT_BINARY_DIR}/tapi32_stubs.c)

add_rc_deps(tapi.rc ${CMAKE_CURRENT_SOURCE_DIR}/tapi.rgs)

add_library(tapi32 MODULE
    ${SOURCE}
    tapi.rc
    ${CMAKE_CURRENT_BINARY_DIR}/tapi32.def)

set_module_type(tapi32 win32dll)
target_compile_definitions(tapi32 PRIVATE __WINESRC__)
target_link_libraries(tapi32 wine)
add_importlibs(tapi32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET tapi32 DESTINATION reactos/system32 FOR all)
set_wine_module(tapi32)
