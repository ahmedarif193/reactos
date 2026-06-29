
list(APPEND SOURCE
    DxgiFactory.c
    DxgiAdapter.c
    DxgiOutput.c
    DxgiSwapChain.c)

list(APPEND PCH_SKIP_SOURCE
    guid.c
    testlist.c)

add_executable(dxgi_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

set_module_type(dxgi_apitest win32cui)
target_link_libraries(dxgi_apitest wine uuid)
add_importlibs(dxgi_apitest dxgi ole32 gdi32 user32 msvcrt kernel32 ntdll)
add_pch(dxgi_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET dxgi_apitest)
add_cd_file(TARGET dxgi_apitest DESTINATION reactos/system32 FOR all)
