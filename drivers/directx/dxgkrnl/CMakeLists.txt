# dxgkrnl.sys — WDDM Display Graphics Kernel
#
# Port driver that miniport drivers call into via DxgkInitialize().
# Provides the WDDM DDI contract between the OS and GPU miniport drivers.

spec2def(dxgkrnl.sys dxgkrnl.spec ADD_IMPORTLIB)

# dxgkrnl targets the Windows 7 WDDM 1.1 contract. Override the global
# ReactOS XP target so the correct prototypes and NTDDI guards are resolved.
remove_definitions(-D_WIN32_WINNT=0x502)
remove_definitions(-DWINVER=0x502)
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)    # NTDDI_WIN7

list(APPEND SOURCE
    dxgkrnl.c
    adapter.c
    legacy.c
    pnp.c
    vidmm.c
    context.c
    dma.c
    present.c
    sync.c
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
add_importlibs(dxgkrnl ntoskrnl hal)
add_pch(dxgkrnl dxgkrnl_private.h "${SOURCE}")
add_cd_file(TARGET dxgkrnl DESTINATION reactos/system32/drivers FOR all)
