list(APPEND SOURCE
    hardware.c
    i2cdesignware.c
    i2cdesignware.h)

add_library(i2cdesignware MODULE
    ${SOURCE}
    guid.c
    i2cdesignware.rc)

set_module_type(i2cdesignware kernelmodedriver)
add_importlibs(i2cdesignware ntoskrnl hal)
add_pch(i2cdesignware i2cdesignware.h "guid.c")
add_cd_file(TARGET i2cdesignware DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(i2cdesignware i2cdesignware.inf)
