
add_executable(dxgi_apitest
    dxgi.c
    texture_dimensions.cpp
    composition.cpp
    composition_lifetime.cpp
    completion_event.cpp
    buffer_copy.cpp
    copy_depth_stencil.cpp
    volume_readback.cpp
    dwm_render.cpp
    testlist.c)

target_include_directories(dxgi_apitest PRIVATE ${REACTOS_SOURCE_DIR}/base/system/dwm)
set_module_type(dxgi_apitest win32cui)
add_importlibs(dxgi_apitest user32 msvcrt kernel32)
add_rostests_file(TARGET dxgi_apitest)
