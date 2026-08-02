
add_definitions(
    -D_WINE)

spec2def(msxml.dll msxml.spec)

list(APPEND SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/msxml.def)

add_library(msxml MODULE ${SOURCE} rsrc.rc)
add_idl_reg_scripts(msxml regtypelib msxml_tlb.idl)
set_module_type(msxml win32dll)
target_link_libraries(msxml wine wine_dll_canunload wine_dll_register)
add_importlibs(msxml msxml3 msvcrt kernel32)
add_dependencies(msxml stdole2)
add_cd_file(TARGET msxml DESTINATION reactos/system32 FOR all)
set_wine_module(msxml)
