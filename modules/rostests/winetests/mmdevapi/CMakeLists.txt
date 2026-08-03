
add_definitions(-DUSE_WINE_TODOS -DWINETEST_USE_DBGSTR_LONGLONG -D_USE_MATH_DEFINES)

add_executable(mmdevapi_winetest
    capture.c
    dependency.c
    mmdevenum.c
    propstore.c
    render.c
    spatialaudio.c
    testlist.c)

set_module_type(mmdevapi_winetest win32cui)
target_link_libraries(mmdevapi_winetest uuid)
add_importlibs(mmdevapi_winetest ole32 version user32 advapi32 winmm msvcrt kernel32 ntdll)
add_dependencies(mmdevapi_winetest dxsdk)
add_rostests_file(TARGET mmdevapi_winetest)
