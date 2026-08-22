spec2def(windows.storage.applicationdata.dll windows.storage.applicationdata.spec)

list(APPEND SOURCE
    applicationdata.c
    main.c)

add_library(windows_storage_applicationdata MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.storage.applicationdata.def ${CMAKE_CURRENT_BINARY_DIR}/windows.storage.applicationdata_stubs.c)
set_target_properties(windows_storage_applicationdata PROPERTIES OUTPUT_NAME "windows.storage.applicationdata")
add_idl_reg_scripts(windows_storage_applicationdata winrt classes.idl)
set_module_type(windows_storage_applicationdata win32dll)
target_compile_definitions(windows_storage_applicationdata PRIVATE __WINESRC__)
target_link_libraries(windows_storage_applicationdata uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_storage_applicationdata combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_storage_applicationdata DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_storage_applicationdata)
