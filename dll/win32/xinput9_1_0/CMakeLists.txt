
spec2def(xinput9_1_0.dll xinput9_1_0.spec)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/xinput9_1_0.def)

add_library(xinput9_1_0 MODULE ${SOURCE} version.rc)
set_module_type(xinput9_1_0 win32dll)
target_compile_definitions(xinput9_1_0 PRIVATE __WINESRC__)
target_link_libraries(xinput9_1_0 wine)
add_importlibs(xinput9_1_0 msvcrt kernel32 ntdll)
add_cd_file(TARGET xinput9_1_0 DESTINATION reactos/system32 FOR all)
set_wine_module(xinput9_1_0)
