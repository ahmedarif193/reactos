
spec2def(d3dxof.dll d3dxof.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    d3dxof.c
    main.c
    mszip.c
    parsing.c)

add_library(d3dxof MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d3dxof.def)

add_idl_reg_scripts(d3dxof registry d3dxof.idl)
set_module_type(d3dxof win32dll)
target_link_libraries(d3dxof dxguid uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(d3dxof ole32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET d3dxof DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(d3dxof) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
