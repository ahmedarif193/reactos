spec2def(windows.ui.core.textinput.dll windows.ui.core.textinput.spec)

list(APPEND SOURCE
    main.c
    services.c
    vector.c
    weakref.c)

add_library(windows_ui_core_textinput MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.ui.core.textinput.def ${CMAKE_CURRENT_BINARY_DIR}/windows.ui.core.textinput_stubs.c)
set_target_properties(windows_ui_core_textinput PROPERTIES OUTPUT_NAME "windows.ui.core.textinput")
add_idl_reg_scripts(windows_ui_core_textinput winrt classes.idl)
set_module_type(windows_ui_core_textinput win32dll)
target_compile_definitions(windows_ui_core_textinput PRIVATE __WINESRC__)
target_link_libraries(windows_ui_core_textinput uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_ui_core_textinput combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_ui_core_textinput DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_ui_core_textinput)
