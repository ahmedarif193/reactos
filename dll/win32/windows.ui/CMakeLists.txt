spec2def(windows.ui.dll windows.ui.spec)

list(APPEND SOURCE
    accessibilitysettings.c
    corewindow.c
    inputpane.c
    main.c
    uisettings.c
    uiviewsettings.c
    weakref.c
    private.h
    weakref.h)

add_library(windows_ui MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.ui.def ${CMAKE_CURRENT_BINARY_DIR}/windows.ui_stubs.c)
set_target_properties(windows_ui PROPERTIES OUTPUT_NAME "windows.ui")
add_idl_reg_scripts(windows_ui winrt classes.idl)
set_module_type(windows_ui win32dll)
target_compile_definitions(windows_ui PRIVATE __WINESRC__)
target_link_libraries(windows_ui uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_ui combase advapi32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_ui DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_ui)
