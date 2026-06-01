list(APPEND SOURCE
    rpi5fb.c
    rpi5fb.h)

add_library(rpi5fb MODULE ${SOURCE} rpi5fb.rc)
set_module_type(rpi5fb kernelmodedriver)
add_importlibs(rpi5fb ntoskrnl videoprt)
target_link_libraries(rpi5fb libcntpr)
add_cd_file(TARGET rpi5fb DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rpi5fb rpi5fb.inf)
add_registry_inf(rpi5fb_reg.inf)
