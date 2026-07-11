# PROJECT:  ReactOS Raspberry Pi 5 WDDM display-only miniport
# LICENSE:  GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:  Build file for rpi5vc4.sys — WDDM2 KMDOD for the BCM2712
#           HVS/PixelValve display pipeline (firmware GOP framebuffer).

# rpi5vc4 MUST speak the exact same DDI contract as dxgkrnl so that
# KMDDOD_INITIALIZATION_DATA, DXGK_INTERFACE and every DXGKARG_* structure
# have byte-identical layouts between the port driver and this miniport.
# Mirror dxgkrnl/softgpu CMakeLists exactly.
remove_definitions(-D_WIN32_WINNT=0x502)
remove_definitions(-DWINVER=0x502)
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)    # NTDDI_WIN7

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x5023)

list(APPEND SOURCE
    rpi5vc4.c
    rpi5vc4_wddm.c
    rpi5vc4_vidpn.c
    rpi5vc4_present.c
    rpi5vc4_hvs.c
    rpi5vc4_crtc.c
    rpi5vc4_v3d.c
    rpi5vc4_mbox.c)

add_library(rpi5vc4 MODULE ${SOURCE} rpi5vc4.rc)
set_module_type(rpi5vc4 kernelmodedriver)
add_importlibs(rpi5vc4 dxgkrnl ntoskrnl hal)
target_link_libraries(rpi5vc4 libcntpr)
add_pch(rpi5vc4 rpi5vc4.h "${SOURCE}")
add_cd_file(TARGET rpi5vc4 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rpi5vc4 rpi5vc4.inf)
add_registry_inf(rpi5vc4_reg.inf)
