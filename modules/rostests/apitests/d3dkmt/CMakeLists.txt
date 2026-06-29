remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x300E)

list(APPEND SOURCE
    d3dkmt_adapter.c
    d3dkmt_device.c
    d3dkmt_alloc.c
    d3dkmt_sync.c
    d3dkmt_display.c
    d3dkmt_present.c
    vidmm_test.c
    present_test.c
    dwm_test.c
    display_test.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(d3dkmt_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

target_link_libraries(d3dkmt_apitest wine ${PSEH_LIB})
set_module_type(d3dkmt_apitest win32cui)
add_importlibs(d3dkmt_apitest gdi32 user32 msvcrt kernel32 ntdll)
add_pch(d3dkmt_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET d3dkmt_apitest)
add_cd_file(TARGET d3dkmt_apitest DESTINATION reactos/system32 FOR all)
