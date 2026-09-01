spec2def(dxgi.dll dxgi.spec ADD_IMPORTLIB)

set(DXGI_USE_D3DKMT_ADAPTER_ORDER FALSE)
if(REACTOS_USE_WDDM AND (REACTOS_WDDM_TARGET_LEVEL GREATER_EQUAL 1200))
    # D3DKMTEnumAdapters2 is part of the Win8+/WDDM 1.2 thunk surface.
    remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
    add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)
    set(DXGI_USE_D3DKMT_ADAPTER_ORDER TRUE)
endif()

list(APPEND SOURCE
    adapter.c
    device.c
    dxgi_main.c
    factory.c
    output.c
    resource.c
    swapchain.c
    utils.c)

if(DXGI_USE_D3DKMT_ADAPTER_ORDER)
    list(APPEND SOURCE reactos.c)
endif()

add_library(dxgi MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxgi.def
    ${CMAKE_CURRENT_BINARY_DIR}/dxgi_stubs.c)

set_module_type(dxgi win32dll)
target_compile_definitions(dxgi PRIVATE __WINESRC__)
if(DXGI_USE_D3DKMT_ADAPTER_ORDER)
    target_compile_definitions(dxgi PRIVATE REACTOS_DXGI_D3DKMT_ADAPTER_ORDER)
endif()
target_include_directories(dxgi PRIVATE ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/vkd3d/include)
target_link_libraries(dxgi vkd3d dx10guid uuid wine)
add_importlibs(dxgi dcomp d3dwine gdi32 user32 msvcrt kernel32 ntdll)
add_dependencies(dxgi d3d_idl_headers wineheaders)
add_cd_file(TARGET dxgi DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(dxgi)
