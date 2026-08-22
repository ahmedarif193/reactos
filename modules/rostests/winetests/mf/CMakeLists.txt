add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG
    -DWINETEST_USE_DBG_SPRINTF)

add_executable(mf_winetest
    mf.c
    topology.c
    transform.c
    resource.rc
    testlist.c)
target_link_libraries(mf_winetest dmoguids mfuuid strmiids uuid wmcodecdspuuid wine)
set_module_type(mf_winetest win32cui)
add_importlibs(mf_winetest mf mfplat ole32 user32 propsys msdmo d3d9 d3d11 dxva2 msvfw32 oleaut32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET mf_winetest)
