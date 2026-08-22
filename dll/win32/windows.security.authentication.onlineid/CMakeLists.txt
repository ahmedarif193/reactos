spec2def(windows.security.authentication.onlineid.dll windows.security.authentication.onlineid.spec)

list(APPEND SOURCE
    authenticator.c
    main.c
    ticket.c)

add_library(windows_security_authentication_onlineid MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.security.authentication.onlineid.def ${CMAKE_CURRENT_BINARY_DIR}/windows.security.authentication.onlineid_stubs.c)
set_target_properties(windows_security_authentication_onlineid PROPERTIES OUTPUT_NAME "windows.security.authentication.onlineid")
add_idl_reg_scripts(windows_security_authentication_onlineid winrt classes.idl)
set_module_type(windows_security_authentication_onlineid win32dll)
target_compile_definitions(windows_security_authentication_onlineid PRIVATE __WINESRC__)
target_link_libraries(windows_security_authentication_onlineid uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_security_authentication_onlineid combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_security_authentication_onlineid DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_security_authentication_onlineid)
