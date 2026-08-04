add_definitions(
    -DUSE_WINE_TODOS
    -DWINETEST_USE_DBG_SPRINTF
    -DWINETEST_USE_DBGSTR_LONGLONG
    -D_USE_MATH_DEFINES
    -D__WINESRC__)

add_executable(d2d1_winetest
    d2d1.c
    testlist.c)

target_include_directories(d2d1_winetest BEFORE PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
target_link_libraries(d2d1_winetest dx10guid uuid)
set_module_type(d2d1_winetest win32cui)
add_dependencies(d2d1_winetest d3d_idl_headers psdk)
add_importlibs(d2d1_winetest d2d1 d3d10_1 d3d11 dwrite d3dcompiler_47 user32 advapi32 ole32 gdi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET d2d1_winetest)
