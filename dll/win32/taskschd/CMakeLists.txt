
include_directories(${CMAKE_CURRENT_BINARY_DIR})
spec2def(taskschd.dll taskschd.spec ADD_IMPORTLIB)
add_idl_headers(taskschd_rpc_header schrpc.idl)
add_rpc_files(client schrpc.idl)

list(APPEND SOURCE
    folder.c
    folder_collection.c
    regtask.c
    task.c
    taskschd.c)

add_library(taskschd MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/schrpc_c.c
    ${CMAKE_CURRENT_BINARY_DIR}/taskschd_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/taskschd.def)

add_idl_reg_scripts(taskschd regtypelib taskschd_tlb.idl)
set_module_type(taskschd win32dll)
target_link_libraries(taskschd uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(taskschd oleaut32 ole32 advapi32 xmllite rpcrt4 msvcrt kernel32 ntdll)
add_dependencies(taskschd taskschd_rpc_header stdole2)
add_cd_file(TARGET taskschd DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(taskschd) # CORE-5743: No ARRAY_SIZE macro
