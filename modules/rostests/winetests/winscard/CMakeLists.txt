
add_definitions(-DUSE_WINE_TODOS)

add_executable(winscard_winetest
    winscard.c
    testlist.c)

set_module_type(winscard_winetest win32cui)
add_importlibs(winscard_winetest winscard msvcrt kernel32)
add_rostests_file(TARGET winscard_winetest)
