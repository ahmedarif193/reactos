
spec2def(olepro32.dll olepro32.spec)
add_typelib(olepro.idl)

list(APPEND SOURCE
    olepro32stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/olepro32.def)

add_library(olepro32 MODULE ${SOURCE} version.rc)
add_dependencies(olepro32 stdole2)
set_module_type(olepro32 win32dll)
add_idl_reg_scripts(olepro32 regtypelib olepro.idl)
target_link_libraries(olepro32 wine wine_dll_canunload wine_dll_register)
add_importlibs(olepro32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET olepro32 DESTINATION reactos/system32 FOR all)
set_wine_module(olepro32)
