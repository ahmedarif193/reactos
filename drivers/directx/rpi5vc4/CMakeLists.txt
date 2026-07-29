# PROJECT:  ReactOS Raspberry Pi 5 WDDM display-only miniport
# LICENSE:  GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:  Build file for rpi5vc4.sys — WDDM2 KMDOD for the BCM2712
#           HVS/PixelValve display pipeline (firmware GOP framebuffer).

# rpi5vc4 MUST speak the exact same DDI contract as dxgkrnl so that
# KMDDOD_INITIALIZATION_DATA, DXGK_INTERFACE and every DXGKARG_* structure
# have byte-identical layouts between the port driver and this miniport.
# Mirror dxgkrnl/softgpu CMakeLists exactly.
remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)    # NTDDI_WIN7

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
# Compile with WDDM 3.0 declarations needed by the source. This is a header
# surface, not the miniport's advertised level: DriverEntry deliberately
# declares only the WDDM 2.0 callback/behavior contract it implements.
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0xF003)

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
target_link_libraries(rpi5vc4 displib libcntpr)
# Deliberately NOT importing dxgkrnl.sys.  Measured on Win11 ARM64:
# dxgkrnl.sys exports 325 symbols and not one of them is DxgkInitialize --
# on Windows that entry comes from displib.lib, a *static* library the
# miniport links in, which finds dxgkrnl at run time and calls into it.
# The genuine viogpudo.sys confirms the resulting shape: it imports only
# ntoskrnl.exe and HAL.dll.  Linking dxgkrnl as an import library would
# make this binary less Win11-portable, not more.
add_importlibs(rpi5vc4 ntoskrnl hal)
add_pch(rpi5vc4 rpi5vc4.h "${SOURCE}")
add_cd_file(TARGET rpi5vc4 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rpi5vc4 rpi5vc4.inf)
add_registry_inf(rpi5vc4_reg.inf)
