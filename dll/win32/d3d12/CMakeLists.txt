
spec2def(d3d12.dll d3d12.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3d12_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3d12.def)

add_library(d3d12 MODULE ${SOURCE})
set_module_type(d3d12 win32dll)
target_link_libraries(d3d12 wine)
add_importlibs(d3d12 msvcrt kernel32 ntdll)
add_cd_file(TARGET d3d12 DESTINATION reactos/system32 FOR all)
