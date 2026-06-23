
spec2def(dxgi.dll dxgi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/dxgi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/dxgi.def)

add_library(dxgi MODULE ${SOURCE})
set_module_type(dxgi win32dll)
target_link_libraries(dxgi wine)
add_importlibs(dxgi msvcrt kernel32 ntdll)
add_cd_file(TARGET dxgi DESTINATION reactos/system32 FOR all)
