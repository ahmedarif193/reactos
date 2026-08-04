spec2def(windows.globalization.dll windows.globalization.spec)

list(APPEND SOURCE
    geographic_region.c
    main.c
    private.h)

add_library(windows_globalization MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/windows.globalization.def
    ${CMAKE_CURRENT_BINARY_DIR}/windows.globalization_stubs.c)

set_target_properties(windows_globalization PROPERTIES OUTPUT_NAME "windows.globalization")
add_idl_reg_scripts(windows_globalization winrt classes.idl)
set_module_type(windows_globalization win32dll)
target_compile_definitions(windows_globalization PRIVATE __WINESRC__)
target_link_libraries(windows_globalization uuid wine wine_dll_canunload)
add_importlibs(windows_globalization combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_globalization DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_globalization)
