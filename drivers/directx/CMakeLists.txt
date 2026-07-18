# drivers/directx — WDDM DirectX kernel subsystem (Phase B, core)
#
# Build order matters:
#   1. dxgmms1  (legacy standalone module; not used by active dxgkrnl policy)
#   2. dxgmms2  (typed WDDM 2.x boundary; no dxgkrnl dependency)
#   3. dxgkrnl  (imports dxgmms2, which reports no functional ownership yet)
#
# Miniports (softgpu/kmdod/viogpu3d) and the win32k WDDM switch are brought in
# as later Phase B steps. Gated behind WDDM_DIRECTX so it stays out of standard
# ReactOS builds that do not yet include WDDM support.

add_subdirectory(dxgmms1)
add_subdirectory(dxgmms2)
add_subdirectory(dxgkrnl)
add_subdirectory(softgpu)   # basic-display fallback WDDM miniport (yields to real drivers, drives dxgkrnl)
if(REACTOS_USE_WDDM AND (ARCH STREQUAL "arm64") AND RPI_SUPPORT)
    add_subdirectory(rpi5vc4)  # Raspberry Pi 5 (BCM2712 HVS) WDDM2 display-only miniport
endif()
