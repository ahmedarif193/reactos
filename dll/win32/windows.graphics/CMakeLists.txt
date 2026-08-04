spec2def(windows.graphics.dll windows.graphics.spec)

list(APPEND SOURCE
    main.c
    private.h)

add_library(windows_graphics MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/windows.graphics.def
    ${CMAKE_CURRENT_BINARY_DIR}/windows.graphics_stubs.c)

set_target_properties(windows_graphics PROPERTIES OUTPUT_NAME "windows.graphics")
add_idl_reg_scripts(windows_graphics winrt classes.idl)
set_module_type(windows_graphics win32dll)
target_compile_definitions(windows_graphics PRIVATE __WINESRC__)
target_link_libraries(windows_graphics uuid wine wine_dll_canunload)
add_importlibs(windows_graphics combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_graphics DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_graphics)
