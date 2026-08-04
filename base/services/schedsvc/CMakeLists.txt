
include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_idl_headers(schedsvc_idlheaders atsvc.idl schrpc.idl)
add_rpc_files(server atsvc.idl schrpc.idl)
spec2def(schedsvc.dll schedsvc.spec ADD_IMPORTLIB)

add_library(schedsvc MODULE
    atsvc.c
    schedsvc.c
    svc_main.c
    schedsvc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/atsvc_s.c
    ${CMAKE_CURRENT_BINARY_DIR}/schrpc_s.c
    ${CMAKE_CURRENT_BINARY_DIR}/schedsvc.def)

set_module_type(schedsvc win32dll UNICODE)
target_link_libraries(schedsvc wine)
add_importlibs(schedsvc rpcrt4 advapi32 ole32 xmllite msvcrt kernel32 ntdll)
add_dependencies(schedsvc schedsvc_idlheaders)
add_cd_file(TARGET schedsvc DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(schedsvc) # CORE-5743: No ARRAY_SIZE macro
