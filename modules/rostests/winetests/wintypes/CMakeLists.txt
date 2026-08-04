add_definitions(-DUSE_WINE_TODOS -D__WINESRC__)
add_idl_headers(wintypes_test_idl wintypes_test.idl)

add_executable(wintypes_winetest wintypes.c testlist.c)
add_dependencies(wintypes_winetest wintypes_test_idl)
target_link_libraries(wintypes_winetest uuid)
set_module_type(wintypes_winetest win32cui)
add_importlibs(wintypes_winetest wintypes combase msvcrt kernel32)
add_rostests_file(TARGET wintypes_winetest)
