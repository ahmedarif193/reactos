# dxgkrnl.sys — WDDM Display Graphics Kernel
#
# Port driver that miniport drivers call into via DxgkInitialize().
# Provides the WDDM DDI contract between the OS and GPU miniport drivers.

spec2def(dxgkrnl.sys dxgkrnl.spec ADD_IMPORTLIB)

# dxgkrnl exposes a WDDM DDI surface on top of NT kernel declarations. The
# floor is Windows 8, not Windows 7: registering the miniport's runtime power
# components uses the Power Framework (PoFxRegisterDevice and friends), which
# <potypes.h> declares only from NTDDI_WIN8 upwards, and which ntoskrnl
# exports at the same level.
remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0602 -DWINVER=0x0602)
add_definitions(-DNTDDI_VERSION=0x06020000)    # NTDDI_WIN8

# Compile the shared graphics stack against the audited WDDM 3.2 header surface
# while runtime capability reporting remains limited to completed paths.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

list(APPEND SOURCE
    dxgkrnl.c
    dxgmms2_client.c
    iommu.c
    adapter.c
    adapter_map_core.c
    adapter_start_core.c
    legacy.c
    pnp.c
    vidmm.c
    paging.c
    gpuva.c
    gpuva_core.c
    fence_core.c
    node_stats_core.c
    dma_core.c
    residency_core.c
    paging_core.c
    display_core.c
    object_core.c
    keyedmutex_core.c
    capture.c
    handles.c
    device_work_core.c
    present_queue_core.c
    submit_reservation_core.c
    tracked_work_core.c
    vidmm_worker_drain_core.c
    context_sync.c
    context_sync_core.c
    context.c
    context_order.c
    dma.c
    present.c
    presenttrace.c
    redirection.c
    composition_frame.c
    ntgdi_exports.c
    sync.c
    keyedmutex.c
    overlay.c
    trimnotify.c
    sync_wait_core.c
    vidsch.c
    vidpn.c
    d3dkmt.c
    display.c
    debug.c)

if(REACTOS_WDDM_TARGET_LEVEL GREATER_EQUAL 3200)
    list(APPEND SOURCE feature_query_core.c)
endif()

add_library(dxgkrnl MODULE
    ${SOURCE}
    dxgkrnl.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxgkrnl.def)

target_compile_definitions(dxgkrnl PRIVATE
    REACTOS_WDDM_TARGET_LEVEL=${REACTOS_WDDM_TARGET_LEVEL})

set_module_type(dxgkrnl kernelmodedriver)
target_link_libraries(dxgkrnl ${PSEH_LIB} wdmguid)
add_importlibs(dxgkrnl dxgmms2 ntoskrnl hal)
add_pch(dxgkrnl dxgkrnl_private.h "${SOURCE}")
add_cd_file(TARGET dxgkrnl DESTINATION reactos/system32/drivers FOR all)

# Preregister the WDDM kernel display stack (dxgmms2/dxgkrnl/softgpu) into the
# livecd SYSTEM hive so it loads at boot and replaces the legacy XPDM path.
add_registry_inf(wddm_reg.inf)
