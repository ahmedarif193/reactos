spec2def(windows.gaming.input.dll windows.gaming.input.spec)
add_idl_headers(windows_gaming_input_private_idl async_private.idl provider.idl)

list(APPEND SOURCE
    async.c
    condition_effect.c
    constant_effect.c
    controller.c
    event_handlers.c
    force_feedback.c
    gamepad.c
    main.c
    manager.c
    periodic_effect.c
    provider.c
    racing_wheel.c
    ramp_effect.c
    vector.c)

add_library(windows_gaming_input MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.gaming.input.def ${CMAKE_CURRENT_BINARY_DIR}/windows.gaming.input_stubs.c)
set_target_properties(windows_gaming_input PROPERTIES OUTPUT_NAME "windows.gaming.input")
add_idl_reg_scripts(windows_gaming_input winrt classes.idl)
add_dependencies(windows_gaming_input windows_gaming_input_private_idl)
set_module_type(windows_gaming_input win32dll)
target_compile_definitions(windows_gaming_input PRIVATE __WINESRC__ _USE_MATH_DEFINES)
target_link_libraries(windows_gaming_input uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_gaming_input combase user32 dinput8 setupapi hid msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_gaming_input DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_gaming_input)
