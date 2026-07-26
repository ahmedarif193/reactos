# dxgkrnl.sys — WDDM Display Graphics Kernel
#
# Port driver that miniport drivers call into via DxgkInitialize().
# Provides the WDDM DDI contract between the OS and GPU miniport drivers.

spec2def(dxgkrnl.sys dxgkrnl.spec ADD_IMPORTLIB)

# dxgkrnl uses Windows 7 kernel declarations while exposing a WDDM 2.0 DDI
# surface. Replace the configured tree-wide target for the required NT APIs.
remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)    # NTDDI_WIN7

# dxgkrnl implements the WDDM 2.x DDI surface (ReclaimAllocations, GPU virtual
# addressing, SetVidPnSourceOwner1, ...). The global default DXGKDDI version
# (0x1052) gates those types out, so raise it to WDDM 2.0.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

list(APPEND SOURCE
    dxgkrnl.c
    dxgmms2_client.c
    adapter.c
    legacy.c
    pnp.c
    vidmm.c
    paging.c
    gpuva.c
    gpuva_core.c
    fence_core.c
    dma_core.c
    residency_core.c
    paging_core.c
    display_core.c
    object_core.c
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
    sync.c
    sync_wait_core.c
    vidsch.c
    vidpn.c
    d3dkmt.c
    display.c
    debug.c)

add_library(dxgkrnl MODULE
    ${SOURCE}
    dxgkrnl.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxgkrnl.def)

set_module_type(dxgkrnl kernelmodedriver)
add_importlibs(dxgkrnl dxgmms2 ntoskrnl hal)
add_pch(dxgkrnl dxgkrnl_private.h "${SOURCE}")
add_cd_file(TARGET dxgkrnl DESTINATION reactos/system32/drivers FOR all)

# Preregister the WDDM kernel display stack (dxgmms1/dxgkrnl/softgpu) into the
# livecd SYSTEM hive so it loads at boot and replaces the legacy XPDM path.
add_registry_inf(wddm_reg.inf)
