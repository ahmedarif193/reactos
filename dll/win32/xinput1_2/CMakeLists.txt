
spec2def(xinput1_2.dll xinput1_2.spec)

list(APPEND SOURCE
    ../xinput1_3/main.c
    ${CMAKE_CURRENT_BINARY_DIR}/xinput1_2.def)

add_library(xinput1_2 MODULE ${SOURCE} version.rc)
set_module_type(xinput1_2 win32dll)
target_compile_definitions(xinput1_2 PRIVATE __WINESRC__)
target_link_libraries(xinput1_2 wine)
add_importlibs(xinput1_2 hid setupapi advapi32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET xinput1_2 DESTINATION reactos/system32 FOR all)
set_wine_module(xinput1_2)
