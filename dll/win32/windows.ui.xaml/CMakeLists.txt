spec2def(windows.ui.xaml.dll windows.ui.xaml.spec)

list(APPEND SOURCE
    color_helper.c
    main.c)

add_library(windows_ui_xaml MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.ui.xaml.def ${CMAKE_CURRENT_BINARY_DIR}/windows.ui.xaml_stubs.c)
set_target_properties(windows_ui_xaml PROPERTIES OUTPUT_NAME "windows.ui.xaml")
add_idl_reg_scripts(windows_ui_xaml winrt classes.idl)
set_module_type(windows_ui_xaml win32dll)
target_compile_definitions(windows_ui_xaml PRIVATE __WINESRC__)
target_link_libraries(windows_ui_xaml uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_ui_xaml combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_ui_xaml DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_ui_xaml)
