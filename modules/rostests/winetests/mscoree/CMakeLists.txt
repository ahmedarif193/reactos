
add_executable(mscoree_winetest
    comtest.c
    debugging.c
    metahost.c
    mscoree.c
    testlist.c
    resource.rc)

add_idl_headers(mscoree_winetest_idlheader interfaces.idl)

target_link_libraries(mscoree_winetest wine uuid)
set_module_type(mscoree_winetest win32cui)
add_importlibs(mscoree_winetest ole32 shlwapi shell32 advapi32 msvcrt kernel32 ntdll)
add_dependencies(mscoree_winetest mscoree_winetest_idlheader)
add_rostests_file(TARGET mscoree_winetest)
