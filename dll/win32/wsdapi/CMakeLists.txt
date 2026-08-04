spec2def(wsdapi.dll wsdapi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    address.c
    discovery.c
    memory.c
    msgparams.c
    network.c
    soap.c
    xml.c
    wsdapi_internal.h)

add_library(wsdapi MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/wsdapi.def
    ${CMAKE_CURRENT_BINARY_DIR}/wsdapi_stubs.c)

set_module_type(wsdapi win32dll)
target_compile_definitions(wsdapi PRIVATE __WINESRC__)
target_link_libraries(wsdapi uuid wine)
add_importlibs(wsdapi bcrypt iphlpapi rpcrt4 user32 webservices ws2_32 msvcrt kernel32 ntdll)
add_cd_file(TARGET wsdapi DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(wsdapi)
