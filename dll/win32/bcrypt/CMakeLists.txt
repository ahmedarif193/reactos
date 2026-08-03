
spec2def(bcrypt.dll bcrypt.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    bcrypt_main.c
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/bcrypt_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/bcrypt.def)

add_library(bcrypt MODULE ${SOURCE})
set_module_type(bcrypt win32dll)
target_compile_definitions(bcrypt PRIVATE __WINESRC__ __WINE_PE_BUILD)
target_link_libraries(bcrypt wine symcrypt)
add_importlibs(bcrypt advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET bcrypt DESTINATION reactos/system32 FOR all)
set_wine_module(bcrypt)
