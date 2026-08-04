add_definitions(-DUSE_WINE_TODOS)

add_executable(msvcirt_winetest msvcirt.c testlist.c)
set_module_type(msvcirt_winetest win32cui)
add_importlibs(msvcirt_winetest msvcrt kernel32)
add_rostests_file(TARGET msvcirt_winetest)
