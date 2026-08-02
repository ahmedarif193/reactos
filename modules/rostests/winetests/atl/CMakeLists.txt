
add_definitions(-D_ATL_VER=_ATL_VER_30)

list(APPEND SOURCE
    atl_ax.c
    module.c
    registrar.c
    testlist.c)

add_executable(atl_winetest ${SOURCE})
target_include_directories(atl_winetest BEFORE PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
target_link_libraries(atl_winetest wine uuid)
set_module_type(atl_winetest win32cui)
add_importlibs(atl_winetest atl oleaut32 ole32 rpcrt4 user32 gdi32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET atl_winetest)
