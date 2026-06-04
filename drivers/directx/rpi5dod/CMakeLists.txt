# rpi5dod.sys -- ReactOS Raspberry Pi 5 display-only WDDM miniport
#
# A WDDM "display only" driver (DOD) for real Raspberry Pi 5 hardware.
# It registers with dxgkrnl via DxgkInitializeDisplayOnlyDriver and
# implements DxgkDdiPresentDisplayOnly (the complete-frame blt path used
# for a tear-free kernel D3DKMTPresent).  Unlike kmdod (PCI VGA), rpi5dod
# binds as a ROOT-enumerated device and scans out the firmware GOP / HVS
# framebuffer.  See rpi5dod.h for the full design.
#
# Build order in drivers/directx: dxgmms1, dxgkrnl (exports
# DxgkInitializeDisplayOnlyDriver), then rpi5dod.

spec2def(rpi5dod.sys rpi5dod.spec ADD_IMPORTLIB)

# Target Windows 8.1 / WDDM 1.3 -- DOD needs >= Win8; the DOD init table
# (KMDDOD_INITIALIZATION_DATA) and the rotation fields require WDDM 1.3.
remove_definitions(-D_WIN32_WINNT=0x502 -DWINVER=0x502)
add_definitions(-D_WIN32_WINNT=0x0602 -DWINVER=0x0602 -DNTDDI_VERSION=0x06020000)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x4003)

list(APPEND SOURCE
    rpi5dod_ddi.c
    rpi5dod_vidpn.c
    rpi5dod_blt.c
    rpi5dod_hvs.c
    rpi5dod.h
    rpi5dod_hvs.h)

add_library(rpi5dod MODULE
    ${SOURCE}
    rpi5dod.rc
    ${CMAKE_CURRENT_BINARY_DIR}/rpi5dod.def)

set_module_type(rpi5dod kernelmodedriver)
add_importlibs(rpi5dod dxgkrnl ntoskrnl hal)
add_cd_file(TARGET rpi5dod DESTINATION reactos/system32/drivers FOR all)

# Registry / INF installation data
add_registry_inf(rpi5dod_reg.inf)
