spec2def(ncrypt.dll ncrypt.spec ADD_IMPORTLIB)

add_library(ncrypt MODULE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/ncrypt_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/ncrypt.def)

set_module_type(ncrypt win32dll)
target_link_libraries(ncrypt wine)
add_importlibs(ncrypt bcrypt msvcrt kernel32 ntdll)
add_cd_file(TARGET ncrypt DESTINATION reactos/system32 FOR all)
set_wine_module(ncrypt)
