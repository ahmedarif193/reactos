
spec2def(xinput1_3.dll xinput1_3.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/xinput1_3.def)

add_library(xinput1_3 MODULE ${SOURCE} version.rc)
set_module_type(xinput1_3 win32dll)
target_compile_definitions(xinput1_3 PRIVATE __WINESRC__)
target_link_libraries(xinput1_3 wine)
add_importlibs(xinput1_3 hid setupapi advapi32 user32 msvcrt kernel32_vista kernel32 ntdll)
add_cd_file(TARGET xinput1_3 DESTINATION reactos/system32 FOR all)
set_wine_module(xinput1_3)
