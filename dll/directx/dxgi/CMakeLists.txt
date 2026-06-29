
spec2def(dxgi.dll dxgi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    dxgi_main.c
    factory.c
    adapter.c
    output.c
    swapchain.c
    utils.c)

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_library(dxgi MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    dxgi.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxgi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/dxgi.def)

set_module_type(dxgi win32dll UNICODE)
target_link_libraries(dxgi wine)
add_importlibs(dxgi gdi32 user32 advapi32 msvcrt kernel32 ntdll)
add_pch(dxgi dxgi_private.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET dxgi DESTINATION reactos/system32 FOR all)
