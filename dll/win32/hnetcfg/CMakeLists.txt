
spec2def(hnetcfg.dll hnetcfg.spec)

list(APPEND SOURCE
    apps.c
    hnetcfg.c
    manager.c
    policy.c
    port.c
    profile.c
    service.c)

add_library(hnetcfg MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/hnetcfg.def)

add_idl_reg_scripts(hnetcfg registry hnetcfg.idl)
add_idl_reg_scripts(hnetcfg regtypelib hnetcfg_tlb.idl)
set_module_type(hnetcfg win32dll)
add_dependencies(hnetcfg stdole2)
target_link_libraries(hnetcfg wine wine_dll_register wine_dll_canunload uuid)
add_importlibs(hnetcfg ole32 oleaut32 advapi32 mpr msvcrt kernel32 ntdll)
add_delay_importlibs(hnetcfg ws2_32 winhttp shcore xmllite)
add_cd_file(TARGET hnetcfg DESTINATION reactos/system32 FOR all)
target_compile_definitions(hnetcfg PRIVATE __WINESRC__)
set_wine_module(hnetcfg)
