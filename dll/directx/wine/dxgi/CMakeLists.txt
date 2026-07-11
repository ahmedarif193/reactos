spec2def(dxgi.dll dxgi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    dxgi_main.c)

add_library(dxgi MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxgi.def
    ${CMAKE_CURRENT_BINARY_DIR}/dxgi_stubs.c)

set_module_type(dxgi win32dll)
# NT10 / WDDM3.0 interface version (exposes D3DKMTEnumAdapters2 & co.).
target_compile_definitions(dxgi PRIVATE DXGKDDI_INTERFACE_VERSION=0xF003)
target_link_libraries(dxgi uuid wine)
add_importlibs(dxgi gdi32 user32 msvcrt kernel32 ntdll)
# Native dxgkrnl-backed DXGI is the system dxgi.dll (Win11 model); DXVK's
# Vulkan-based dxgi is app-local only (see dll/3rdparty/dxvk).
add_cd_file(TARGET dxgi DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(dxgi)
