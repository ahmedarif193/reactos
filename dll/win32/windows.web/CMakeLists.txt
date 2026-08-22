spec2def(windows.web.dll windows.web.spec)

list(APPEND SOURCE
    json_array.c
    json_object.c
    json_value.c
    main.c)

add_library(windows_web MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.web.def ${CMAKE_CURRENT_BINARY_DIR}/windows.web_stubs.c)
set_target_properties(windows_web PROPERTIES OUTPUT_NAME "windows.web")
add_idl_reg_scripts(windows_web winrt classes.idl)
set_module_type(windows_web win32dll)
target_compile_definitions(windows_web PRIVATE __WINESRC__)
target_link_libraries(windows_web uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_web combase ucrtbase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_web DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_web)
