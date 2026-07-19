list(APPEND SOURCE
    descriptor.c
    hidi2c.c
    transport.c
    hidi2c.h)

add_library(hidi2c MODULE ${SOURCE} hidi2c.rc)
set_module_type(hidi2c kernelmodedriver)
add_importlibs(hidi2c hidclass ntoskrnl hal)
add_pch(hidi2c hidi2c.h SOURCE)
add_cd_file(TARGET hidi2c DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(hidi2c hidi2c.inf)
