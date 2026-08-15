list(APPEND SOURCE
    rpi5vc4.c
    rpi5vc4_hvs.c
    rpi5vc4_hvs.h
    rpi5vc4_crtc.c
    rpi5vc4_crtc.h
    rpi5vc4_v3d.c
    rpi5vc4_v3d_exec.c
    rpi5vc4_v3d.h
    rpi5vc4.h)

add_library(rpi5vc4 MODULE ${SOURCE} rpi5vc4.rc)
set_module_type(rpi5vc4 kernelmodedriver)
add_importlibs(rpi5vc4 ntoskrnl videoprt)
target_link_libraries(rpi5vc4 libcntpr)
add_cd_file(TARGET rpi5vc4 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rpi5vc4 rpi5vc4.inf)
add_registry_inf(rpi5vc4_reg.inf)
