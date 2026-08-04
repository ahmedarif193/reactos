spec2def(iertutil.dll iertutil.spec ADD_IMPORTLIB)
add_idl_headers(iertutil_idlheader iertutil_classes.idl)

list(APPEND SOURCE
    main.c
    uri.c
    ${CMAKE_CURRENT_BINARY_DIR}/iertutil_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/iertutil.def)

add_library(iertutil MODULE ${SOURCE})
set_module_type(iertutil win32dll)
add_idl_reg_scripts(iertutil winrt classes.idl)
add_idl_reg_scripts(iertutil registry iertutil_classes.idl)
add_dependencies(iertutil iertutil_idlheader)
target_link_libraries(iertutil uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(iertutil combase shlwapi rpcrt4 ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET iertutil DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(iertutil)
