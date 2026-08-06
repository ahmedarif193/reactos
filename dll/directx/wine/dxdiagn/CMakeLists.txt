
add_definitions(-DWINE_REGISTER_DLL)

spec2def(dxdiagn.dll dxdiagn.spec)

list(APPEND SOURCE
    container.c
    dxdiag_main.c
    provider.c)

add_library(dxdiagn MODULE
    ${SOURCE}
    dxdiagn.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxdiagn.def)

add_idl_reg_scripts(dxdiagn registry dxdiagn.idl)
set_module_type(dxdiagn win32dll)
target_compile_definitions(dxdiagn PRIVATE __WINESRC__)
target_link_libraries(dxdiagn strmiids dxguid uuid wine wine_dll_register)
add_dependencies(dxdiagn wineheaders)
add_importlibs(dxdiagn d3d9 ddraw dsound version ole32 oleaut32 user32 advapi32 psapi msvcrt kernel32 ntdll)
add_cd_file(TARGET dxdiagn DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(dxdiagn) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
