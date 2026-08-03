add_executable(mspatcha_winetest
    apply_patch.c
    testlist.c
    mspatcha.rc)

set_module_type(mspatcha_winetest win32cui)
target_compile_definitions(mspatcha_winetest PRIVATE __WINESRC__ USE_WINE_TODOS)
target_link_libraries(mspatcha_winetest winetestlib)
add_importlibs(mspatcha_winetest mspatcha msvcrt kernel32)
add_rostests_file(TARGET mspatcha_winetest)
