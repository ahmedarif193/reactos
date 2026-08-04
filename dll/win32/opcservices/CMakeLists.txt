include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib)

spec2def(opcservices.dll opcservices.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    compress.c
    factory.c
    package.c
    uri.c
    opc_private.h)

add_library(opcservices MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/opcservices.def)

add_idl_reg_scripts(opcservices registry opcservices.idl)
set_module_type(opcservices win32dll)
target_compile_definitions(opcservices PRIVATE __WINESRC__)
target_link_libraries(opcservices uuid wine wine_dll_register wine_dll_canunload zlib)
add_importlibs(opcservices ole32 advapi32 urlmon xmllite oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET opcservices DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(opcservices)
