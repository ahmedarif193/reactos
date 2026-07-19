spec2def(dxgi.dll dxgi.spec ADD_IMPORTLIB)

# D3DKMTEnumAdapters2 is part of the Win8+ thunk surface; match the WDDM 2.0 kernel stack contract.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x10003)

list(APPEND SOURCE
    adapter.c
    device.c
    dxgi_main.c
    factory.c
    output.c
    reactos.c
    resource.c
    swapchain.c
    utils.c)

add_library(dxgi MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxgi.def
    ${CMAKE_CURRENT_BINARY_DIR}/dxgi_stubs.c)

set_module_type(dxgi win32dll)
target_include_directories(dxgi PRIVATE ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/vkd3d/include)
target_link_libraries(dxgi dx10guid uuid wine)
add_importlibs(dxgi d3dwine gdi32 user32 msvcrt kernel32 ntdll)
add_dependencies(dxgi d3d_idl_headers wineheaders)
add_cd_file(TARGET dxgi DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(dxgi)
