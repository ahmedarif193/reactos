# WDDM D3DKMT test surface: pin struct layouts to WDDM 3.2 (0x11007, the header
# maximum) so the selected WDDM 2.x/3.x structure surface is visible -- paging
# queues & GPU virtual addressing (2.0), HW queues (2.2), EnumAdapters3 (2.7),
# etc. -- and so tests can compare the visible Windows 11-era surface. Entry
# points are still resolved dynamically via GetProcAddress, so the same binary
# degrades gracefully on hosts (e.g. WDDM 2.0 ReactOS) that lack newer exports.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

list(APPEND SOURCE
    d3dkmt_adapter.c
    d3dkmt_device.c
    d3dkmt_alloc.c
    d3dkmt_sync.c
    d3dkmt_display.c
    d3dkmt_present.c
    d3dkmt_pagingqueue.c
    d3dkmt_residency.c
    d3dkmt_gpuva.c
    d3dkmt_hwqueue.c
    d3dkmt_sync2.c
    d3dkmt_videomem.c
    d3dkmt_enum2.c
    d3dkmt_surface.c
    d3dkmt_adapterinfo.c
    d3dkmt_adapterid.c
    d3dkmt_sharing.c
    d3dkmt_syncext.c
    d3dkmt_allocext.c
    d3dkmt_displayext.c
    d3dkmt_presentext.c
    d3dkmt_misc.c
    d3dkmt_teardown.c
    d3dkmt_renderpath.c
    d3dkmt_capsaudit.c
    d3dkmt_umdabi.c
    d3dkmt_abifreeze.c
    d3dkmt_procstress.c
    d3dkmt_umdload.c
    d3dkmt_pipeline.c
    d3dkmt_capture.c
    d3dkmt_privateioctl.c
    d3dkmt_cle.c
    d3dkmt_v3dsmoke.c
    d3dkmt_vc4kmt_smoke.c
    d3dkmt_parity.c
    vidmm_test.c
    present_test.c
    dwm_test.c
    display_test.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(d3dkmt_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

target_link_libraries(d3dkmt_apitest wine ${PSEH_LIB} vc4kmt)
set_module_type(d3dkmt_apitest win32cui)
add_importlibs(d3dkmt_apitest gdi32 user32 msvcrt kernel32 ntdll)
add_pch(d3dkmt_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET d3dkmt_apitest)
add_cd_file(TARGET d3dkmt_apitest DESTINATION reactos/system32 FOR all)
