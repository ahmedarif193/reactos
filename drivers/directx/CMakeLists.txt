# drivers/directx — WDDM DirectX kernel subsystem (Phase B, core)
#
# Build order matters:
#   1. dxgmms1  (no dxgkrnl dependency; produces an import library)
#   2. dxgkrnl  (imports ntoskrnl/hal; links the dxgmms1 import lib)
#
# Miniports (softgpu/kmdod/viogpu3d) and the win32k WDDM switch are brought in
# as later Phase B steps. Gated behind WDDM_DIRECTX so it stays out of standard
# ReactOS builds that do not yet include WDDM support.

add_subdirectory(dxgmms1)
add_subdirectory(dxgkrnl)
add_subdirectory(softgpu)   # basic-display fallback WDDM miniport (yields to real drivers, drives dxgkrnl)
if(ARCH STREQUAL "arm64")
    add_subdirectory(rpi5vc4)  # Raspberry Pi 5 (BCM2712 HVS) WDDM2 display-only miniport
endif()
