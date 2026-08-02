
spec2def(inetcomm.dll inetcomm.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    imaptransport.c
    inetcomm_main.c
    internettransport.c
    mimeintl.c
    mimeole.c
    pop3transport.c
    protocol.c
    smtptransport.c)

add_library(inetcomm MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/inetcomm_stubs.c
    inetcomm.rc
    ${CMAKE_CURRENT_BINARY_DIR}/inetcomm.def)

set_module_type(inetcomm win32dll)
add_idl_reg_scripts(inetcomm registry inetcomm.idl)
target_link_libraries(inetcomm oldnames uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(inetcomm ole32 oleaut32 ws2_32 user32 propsys urlmon msvcrt kernel32_vista kernel32 ntdll)
add_cd_file(TARGET inetcomm DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(inetcomm) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
