add_definitions(-DUSE_WINE_TODOS -D__WINESRC__)

add_executable(opcservices_winetest opcservices.c testlist.c)
target_link_libraries(opcservices_winetest uuid)
set_module_type(opcservices_winetest win32cui)
add_importlibs(opcservices_winetest ole32 urlmon oleaut32 msvcrt kernel32)
add_rostests_file(TARGET opcservices_winetest)
