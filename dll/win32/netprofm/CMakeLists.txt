spec2def(netprofm.dll netprofm.spec)

list(APPEND SOURCE
    list.c
    main.c
    netprofm_private.h)

add_library(netprofm MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/netprofm.def)

add_idl_reg_scripts(netprofm registry netprofm.idl)
set_module_type(netprofm win32dll)
target_compile_definitions(netprofm PRIVATE __WINESRC__)
target_link_libraries(netprofm uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(netprofm iphlpapi oleaut32 ole32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET netprofm DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(netprofm)
