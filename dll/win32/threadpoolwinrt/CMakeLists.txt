spec2def(threadpoolwinrt.dll threadpoolwinrt.spec)

add_library(threadpoolwinrt MODULE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/threadpoolwinrt.def
    ${CMAKE_CURRENT_BINARY_DIR}/threadpoolwinrt_stubs.c)

add_idl_reg_scripts(threadpoolwinrt winrt classes.idl)
set_module_type(threadpoolwinrt win32dll)
target_compile_definitions(threadpoolwinrt PRIVATE __WINESRC__)
target_link_libraries(threadpoolwinrt uuid wine wine_dll_canunload)
add_importlibs(threadpoolwinrt combase msvcrt kernel32 ntdll)
add_cd_file(TARGET threadpoolwinrt DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(threadpoolwinrt)
