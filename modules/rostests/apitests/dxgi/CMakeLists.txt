
add_executable(dxgi_apitest
    dxgi.c
    dwm_render.cpp
    testlist.c)

target_include_directories(dxgi_apitest PRIVATE ${REACTOS_SOURCE_DIR}/base/system/dwm)
set_module_type(dxgi_apitest win32cui)
add_importlibs(dxgi_apitest user32 msvcrt kernel32)
add_rostests_file(TARGET dxgi_apitest)
