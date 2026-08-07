add_definitions(-DUSE_WINE_TODOS)

add_executable(dbgeng_winetest dbgeng.c testlist.c)
target_compile_definitions(dbgeng_winetest PRIVATE CONST_VTABLE)
set_module_type(dbgeng_winetest win32cui)
add_importlibs(dbgeng_winetest dbgeng msvcrt kernel32)
add_rostests_file(TARGET dbgeng_winetest)
