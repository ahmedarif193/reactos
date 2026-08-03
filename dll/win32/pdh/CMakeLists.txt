
spec2def(pdh.dll pdh.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    pdh_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/pdh_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/pdh.def)

add_library(pdh MODULE ${SOURCE})
set_module_type(pdh win32dll)
target_compile_definitions(pdh PRIVATE __WINESRC__)
target_link_libraries(pdh wine)
add_importlibs(pdh msvcrt kernel32_vista kernel32 ntdll)
add_cd_file(TARGET pdh DESTINATION reactos/system32 FOR all)
set_wine_module(pdh)
