
add_definitions(-D__WINESRC__)
spec2def(wevtapi.dll wevtapi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/wevtapi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/wevtapi.def)

add_library(wevtapi MODULE ${SOURCE})
set_module_type(wevtapi win32dll)
target_link_libraries(wevtapi wine)
add_importlibs(wevtapi advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET wevtapi DESTINATION reactos/system32 FOR all)
