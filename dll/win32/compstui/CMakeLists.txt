
spec2def(compstui.dll compstui.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    compstui_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/compstui.def)

add_library(compstui MODULE ${SOURCE} compstui.rc)
set_module_type(compstui win32dll)
target_link_libraries(compstui wine)
add_importlibs(compstui comctl32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET compstui DESTINATION reactos/system32 FOR all)
set_wine_module(compstui)
