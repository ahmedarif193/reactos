
spec2def(dplayx.dll dplayx.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    dpclassfactory.c
    dplay.c
    dplaysp.c
    dplayx_global.c
    dplayx_main.c
    dplayx_messages.c
    dplobby.c
    lobbysp.c
    name_server.c)

add_library(dplayx MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dplayx.def)

add_idl_reg_scripts(dplayx registry dplayx.idl)
set_module_type(dplayx win32dll)
target_link_libraries(dplayx dxguid uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(dplayx winmm ole32 user32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dplayx DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(dplayx) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
