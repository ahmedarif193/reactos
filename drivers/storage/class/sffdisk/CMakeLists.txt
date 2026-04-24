
add_library(sffdisk MODULE
    sffdisk.c
    ioctl.c
    readwrite.c
    sffdisk.rc)

target_link_libraries(sffdisk sdbuslib)
set_module_type(sffdisk kernelmodedriver)
add_importlibs(sffdisk ntoskrnl hal)
add_cd_file(TARGET sffdisk DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_registry_inf(sffdisk_reg.inf)
add_driver_inf(sffdisk sffdisk.inf)
