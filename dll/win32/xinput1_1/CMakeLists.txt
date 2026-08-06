
spec2def(xinput1_1.dll xinput1_1.spec)

list(APPEND SOURCE
    ../xinput1_3/main.c
    ${CMAKE_CURRENT_BINARY_DIR}/xinput1_1.def)

add_library(xinput1_1 MODULE ${SOURCE} version.rc)
set_module_type(xinput1_1 win32dll)
target_compile_definitions(xinput1_1 PRIVATE __WINESRC__)
target_link_libraries(xinput1_1 wine)
add_importlibs(xinput1_1 hid setupapi advapi32 user32 msvcrt kernel32_vista kernel32 ntdll)
add_cd_file(TARGET xinput1_1 DESTINATION reactos/system32 FOR all)
set_wine_module(xinput1_1)
