# drivers/directx — WDDM DirectX kernel subsystem (Phase B, core)
#
# Build order matters:
#   1. dxgmms1  (legacy standalone module; not used by active dxgkrnl policy)
#   2. dxgmms2  (typed WDDM 2.x boundary; no dxgkrnl dependency)
#   3. dxgkrnl  (imports dxgmms2, which reports no functional ownership yet)
#
# This directory is entered only for a REACTOS_GRAPHICS_DRIVER_MODEL=WDDM
# build. XPDM builds retain the legacy VideoPort miniports instead.

add_subdirectory(dxgmms1)
add_subdirectory(dxgmms2)
add_subdirectory(dxgkrnl)
add_subdirectory(softgpu)   # basic-display fallback WDDM miniport (yields to real drivers, drives dxgkrnl)
if((ARCH STREQUAL "arm64") AND RPI_SUPPORT)
    add_subdirectory(rpi5vc4)  # Raspberry Pi 5 (BCM2712 HVS) WDDM2 display-only miniport
endif()
