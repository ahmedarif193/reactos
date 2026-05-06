
list(APPEND SOURCE
    rp1.c
    rp1.h)

add_library(rp1 MODULE ${SOURCE} rp1.rc)
set_module_type(rp1 kernelmodedriver)
add_importlibs(rp1 ntoskrnl hal)
add_cd_file(TARGET rp1 DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(rp1 rp1.inf)
