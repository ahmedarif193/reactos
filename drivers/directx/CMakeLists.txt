# drivers/directx — WDDM DirectX kernel subsystem
#
# Build order matters:
#   1. dxgmms1   (no dxgkrnl dependency; produces import library)
#   2. dxgkrnl   (imports ntoskrnl/hal; links dxgmms1 import lib)
#   3. softgpu   (WDDM test miniport; links dxgkrnl)
#
# The directx subdirectory is gated behind the WDDM_DIRECTX option so
# it is not compiled in standard ReactOS builds that do not yet include
# WDDM support.

add_subdirectory(dxgmms1)
add_subdirectory(dxgkrnl)
add_subdirectory(softgpu)
add_subdirectory(kmdod)

option(ENABLE_VIOGPU3D_EXPERIMENTAL "Build the upstream virtio-gpu WDDM 3D miniport porting target" ON)
if(ENABLE_VIOGPU3D_EXPERIMENTAL)
    add_subdirectory(viogpu3d)
endif()
