
add_rpc_files(client atsvc.idl)
generate_idl_iids(mstask_local.idl)
spec2def(mstask.dll mstask.spec)

list(APPEND SOURCE
    factory.c
    mstask_main.c
    task.c
    task_scheduler.c
    task_trigger.c
    ${CMAKE_CURRENT_BINARY_DIR}/atsvc_c.c
    ${CMAKE_CURRENT_BINARY_DIR}/mstask_local_i.c
    ${CMAKE_CURRENT_BINARY_DIR}/mstask_stubs.c)

add_library(mstask MODULE
    ${SOURCE}
    rsrc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/mstask.def)

set_module_type(mstask win32dll)
target_link_libraries(mstask uuid wine wine_dll_register)
add_importlibs(mstask rpcrt4 ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET mstask DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(mstask) # CORE-5743: No CONST_VTABLE
