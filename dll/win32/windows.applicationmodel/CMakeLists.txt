spec2def(windows.applicationmodel.dll windows.applicationmodel.spec)

list(APPEND SOURCE
    designmode.c
    main.c
    package.c)

add_library(windows_applicationmodel MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.applicationmodel.def ${CMAKE_CURRENT_BINARY_DIR}/windows.applicationmodel_stubs.c)
set_target_properties(windows_applicationmodel PROPERTIES OUTPUT_NAME "windows.applicationmodel")
add_idl_reg_scripts(windows_applicationmodel winrt classes.idl)
set_module_type(windows_applicationmodel win32dll)
target_compile_definitions(windows_applicationmodel PRIVATE __WINESRC__)
target_link_libraries(windows_applicationmodel uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_applicationmodel combase kernelbase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_applicationmodel DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_applicationmodel)
