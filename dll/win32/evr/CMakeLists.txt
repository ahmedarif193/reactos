spec2def(evr.dll evr.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    evr.c
    main.c
    mixer.c
    presenter.c
    sample.c)

add_library(evr MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/evr.def ${CMAKE_CURRENT_BINARY_DIR}/evr_stubs.c)
add_idl_headers(evr_idlheaders evr_classes.idl)
add_idl_reg_scripts(evr registry evr_classes.idl)
set_module_type(evr win32dll)
target_compile_definitions(evr PRIVATE __WINESRC__)
target_link_libraries(evr mfuuid strmiids strmbase uuid dxguid wine wine_dll_canunload wine_dll_register)
add_importlibs(evr ole32 oleaut32 user32 d3d9 dxva2 msvcrt kernel32 ntdll)
add_delay_importlibs(evr mfplat)
add_dependencies(evr dxsdk evr_idlheaders)
add_cd_file(TARGET evr DESTINATION reactos/system32 FOR all)
set_wine_module(evr)
