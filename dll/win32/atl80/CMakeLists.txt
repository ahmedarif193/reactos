
add_definitions(
    -D_ATL_VER=_ATL_VER_80)

spec2def(atl80.dll atl80.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ${REACTOS_SOURCE_DIR}/dll/win32/atl/atl.c
    atl80.c
    ${REACTOS_SOURCE_DIR}/dll/win32/atl/atl_ax.c
    ${REACTOS_SOURCE_DIR}/dll/win32/atl/registrar.c
    ${CMAKE_CURRENT_BINARY_DIR}/atl80_stubs.c)

add_library(atl80 MODULE
    ${SOURCE}
    atl80.rc
    ${CMAKE_CURRENT_BINARY_DIR}/atl80.def)

add_idl_reg_scripts(atl80 regtypelib ${REACTOS_SOURCE_DIR}/dll/win32/atl/atl_lib.idl)
set_module_type(atl80 win32dll)
target_link_libraries(atl80 uuid wine)
add_importlibs(atl80 oleaut32 user32 ole32 gdi32 advapi32 advapi32_vista shlwapi msvcrt kernel32 ntdll)
add_cd_file(TARGET atl80 DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(atl80) # CORE-5743: No ARRAY_SIZE macro
