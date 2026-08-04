include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_idl_headers(schedsvc_winetest_idlheaders atsvc.idl schrpc.idl)
add_rpc_files(client atsvc.idl schrpc.idl)

add_executable(schedsvc_winetest
    atsvcapi.c
    rpcapi.c
    testlist.c
    ${CMAKE_CURRENT_BINARY_DIR}/atsvc_c.c
    ${CMAKE_CURRENT_BINARY_DIR}/schrpc_c.c)
set_module_type(schedsvc_winetest win32cui)
target_link_libraries(schedsvc_winetest wine)
add_importlibs(schedsvc_winetest advapi32 rpcrt4 ole32 msvcrt kernel32 ntdll)
add_dependencies(schedsvc_winetest schedsvc_winetest_idlheaders)
add_rostests_file(TARGET schedsvc_winetest)
