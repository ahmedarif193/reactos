
spec2def(dwrite.dll dwrite.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/dwrite_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/dwrite.def)

add_library(dwrite MODULE ${SOURCE})
set_module_type(dwrite win32dll)
target_link_libraries(dwrite wine)
add_importlibs(dwrite msvcrt kernel32 ntdll)
add_cd_file(TARGET dwrite DESTINATION reactos/system32 FOR all)
