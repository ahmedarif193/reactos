spec2def(windows.storage.dll windows.storage.spec)
add_idl_headers(windows_storage_private_idl async_private.idl)

list(APPEND SOURCE
    async.c
    main.c
    streams.c)

add_library(windows_storage MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.storage.def ${CMAKE_CURRENT_BINARY_DIR}/windows.storage_stubs.c)
set_target_properties(windows_storage PROPERTIES OUTPUT_NAME "windows.storage")
add_idl_reg_scripts(windows_storage winrt classes.idl)
add_dependencies(windows_storage windows_storage_private_idl)
set_module_type(windows_storage win32dll)
target_compile_definitions(windows_storage PRIVATE __WINESRC__)
target_link_libraries(windows_storage uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_storage shell32 combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_storage DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_storage)
