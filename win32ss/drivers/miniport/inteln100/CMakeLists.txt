list(APPEND SOURCE
    inteln100.c
    inteln100.h)

add_library(inteln100 MODULE ${SOURCE} inteln100.rc)
set_module_type(inteln100 kernelmodedriver)
add_importlibs(inteln100 ntoskrnl videoprt)
target_link_libraries(inteln100 libcntpr)
add_cd_file(TARGET inteln100 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(inteln100 inteln100.inf)
add_registry_inf(inteln100_reg.inf)
