spec2def(cryptowinrt.dll cryptowinrt.spec)
add_idl_headers(cryptowinrt_private_idl async_private.idl)

list(APPEND SOURCE
    async.c
    credentials.c
    main.c
    private.h)

add_library(cryptowinrt MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/cryptowinrt.def
    ${CMAKE_CURRENT_BINARY_DIR}/cryptowinrt_stubs.c)

add_idl_reg_scripts(cryptowinrt winrt classes.idl)
add_dependencies(cryptowinrt cryptowinrt_private_idl)
set_module_type(cryptowinrt win32dll)
target_compile_definitions(cryptowinrt PRIVATE __WINESRC__)
target_link_libraries(cryptowinrt uuid wine wine_dll_canunload)
add_importlibs(cryptowinrt combase bcrypt msvcrt kernel32 ntdll)
add_delay_importlibs(cryptowinrt crypt32)
add_cd_file(TARGET cryptowinrt DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(cryptowinrt)
