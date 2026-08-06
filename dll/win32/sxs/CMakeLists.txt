
spec2def(sxs.dll sxs.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    cache.c
    name.c
    sxs.c)

add_library(sxs MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/sxs_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/sxs.def)

set_module_type(sxs win32dll)
target_compile_definitions(sxs PRIVATE __WINESRC__)
target_link_libraries(sxs uuid wine)
add_importlibs(sxs oleaut32 ole32 msvcrt kernel32 ntdll)
add_cd_file(TARGET sxs DESTINATION reactos/system32 FOR all)
set_wine_module(sxs)
