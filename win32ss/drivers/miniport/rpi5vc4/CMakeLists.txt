# PROJECT:     ReactOS Raspberry Pi 5 framebuffer miniport
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:     Raspberry Pi 5 VC4 miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

list(APPEND SOURCE
    rpi5vc4.c
    rpi5vc4_hvs.c
    rpi5vc4_hvs.h
    rpi5vc4_crtc.c
    rpi5vc4_crtc.h
    rpi5vc4_platform.c
    rpi5vc4_v3d.c
    rpi5vc4_v3d_exec.c
    rpi5vc4_v3d.h
    rpi5vc4.h)

add_library(rpi5vc4 MODULE ${SOURCE} rpi5vc4.rc)
set_module_type(rpi5vc4 kernelmodedriver)
add_importlibs(rpi5vc4 ntoskrnl videoprt)
target_link_libraries(rpi5vc4 libcntpr)
add_cd_file(TARGET rpi5vc4 DESTINATION reactos/system32/drivers FOR all)
set(RPI5VC4_OPENGL_ICD rpi5vc4ogl.dll)
if(TARGET mesa_gallium AND NOT RPI5VC4_MESA_V3D_ICD)
    set(RPI5VC4_OPENGL_ICD mesa_gallium.dll)
endif()
configure_file(rpi5vc4.inf "${CMAKE_CURRENT_BINARY_DIR}/configured/rpi5vc4.inf" @ONLY)
configure_file(rpi5vc4_reg.inf "${CMAKE_CURRENT_BINARY_DIR}/configured/rpi5vc4_reg.inf" @ONLY)
add_driver_inf(rpi5vc4 "${CMAKE_CURRENT_BINARY_DIR}/configured/rpi5vc4.inf")
add_registry_inf("${CMAKE_CURRENT_BINARY_DIR}/configured/rpi5vc4_reg.inf")

# The rpi5vc4ogl ICD sources in this directory build from
# dll/opengl/rpi5vc4ogl so the DLL also ships under the WDDM display model.
