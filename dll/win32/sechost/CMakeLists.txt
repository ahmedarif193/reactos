add_definitions(-D__WINESRC__)

include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/wine
    ${CMAKE_CURRENT_BINARY_DIR})

add_rpc_files(client svcctl.idl plugplay.idl)
spec2def(sechost.dll sechost.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    security.c
    service.c
    trace.c
    ${CMAKE_CURRENT_BINARY_DIR}/svcctl_c.c
    ${CMAKE_CURRENT_BINARY_DIR}/plugplay_c.c
    ${CMAKE_CURRENT_BINARY_DIR}/sechost_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/sechost.def)

add_library(sechost MODULE ${SOURCE})
set_module_type(sechost win32dll ENTRYPOINT 0)
target_link_libraries(sechost wine)
add_delay_importlibs(sechost rpcrt4)
add_importlibs(sechost kernelbase msvcrt kernel32 ntdll)
add_cd_file(TARGET sechost DESTINATION reactos/system32 FOR all)
set_wine_module(sechost)
