spec2def(windows.perception.stub.dll windows.perception.stub.spec)
add_idl_headers(windows_perception_stub_private_idl async_private.idl)

list(APPEND SOURCE
    anchor.c
    async.c
    holographic_space.c
    main.c
    observer.c)

add_library(windows_perception_stub MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.perception.stub.def ${CMAKE_CURRENT_BINARY_DIR}/windows.perception.stub_stubs.c)
set_target_properties(windows_perception_stub PROPERTIES OUTPUT_NAME "windows.perception.stub")
add_idl_reg_scripts(windows_perception_stub winrt classes.idl)
add_dependencies(windows_perception_stub windows_perception_stub_private_idl)
set_module_type(windows_perception_stub win32dll)
target_compile_definitions(windows_perception_stub PRIVATE __WINESRC__)
target_link_libraries(windows_perception_stub uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_perception_stub combase user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_perception_stub DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_perception_stub)
