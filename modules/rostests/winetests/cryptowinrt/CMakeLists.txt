add_definitions(-DUSE_WINE_TODOS -D__WINESRC__)

add_executable(cryptowinrt_winetest crypto.c testlist.c)
target_link_libraries(cryptowinrt_winetest uuid)
set_module_type(cryptowinrt_winetest win32cui)
add_importlibs(cryptowinrt_winetest combase msvcrt kernel32)
add_rostests_file(TARGET cryptowinrt_winetest)
