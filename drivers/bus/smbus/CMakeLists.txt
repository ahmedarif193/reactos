add_definitions(-D__NTDRIVER__)

list(APPEND SOURCE
    smbus.c
    smbus.h)

add_library(smbus MODULE
    ${SOURCE}
    smbus.rc)

set_module_type(smbus kernelmodedriver)
target_link_libraries(smbus ${PSEH_LIB})
add_importlibs(smbus ntoskrnl hal)
add_pch(smbus smbus.h SOURCE)
add_cd_file(TARGET smbus DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(smbus smbus.inf)
add_registry_inf(smbus_reg.inf)
