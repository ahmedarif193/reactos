
spec2def(dpwsockx.dll dpwsockx.spec)

add_library(dpwsockx MODULE
    dpwsockx_main.c
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dpwsockx_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/dpwsockx.def)

set_module_type(dpwsockx win32dll)
target_link_libraries(dpwsockx dxguid wine)
add_importlibs(dpwsockx ws2_32 msvcrt kernel32_vista kernel32 ntdll)
add_cd_file(TARGET dpwsockx DESTINATION reactos/system32 FOR all)
set_wine_module(dpwsockx)
