add_definitions(-D__WINESRC__)
spec2def(cryptsp.dll cryptsp.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    arc4.c
    crypt.c
    lmhash.c
    ${CMAKE_CURRENT_BINARY_DIR}/cryptsp_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/cryptsp.def)

add_library(cryptsp MODULE ${SOURCE})
set_module_type(cryptsp win32dll ENTRYPOINT 0)
target_link_libraries(cryptsp wine)
add_importlibs(cryptsp advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET cryptsp DESTINATION reactos/system32 FOR all)
set_wine_module(cryptsp)
