add_definitions(-D_MSVCIRT -D__WINESRC__)

include_directories(BEFORE ${REACTOS_SOURCE_DIR}/dll/win32/msvcrt)

spec2def(msvcirt.dll msvcirt.spec)

list(APPEND SOURCE
    exception.c
    msvcirt.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcirt_stubs.c)

add_library(msvcirt MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/msvcirt.def)

set_module_type(msvcirt win32dll)
target_link_libraries(msvcirt wine)
add_importlibs(msvcirt msvcrt kernel32 ntdll)
add_cd_file(TARGET msvcirt DESTINATION reactos/system32 FOR all)
