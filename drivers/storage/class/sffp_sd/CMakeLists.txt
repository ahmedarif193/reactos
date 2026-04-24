
add_library(sffp_sd MODULE
    sffp_sd.c
    sffp_sd.rc)

target_link_libraries(sffp_sd sdbuslib)
set_module_type(sffp_sd kernelmodedriver)
add_importlibs(sffp_sd ntoskrnl hal)
add_cd_file(TARGET sffp_sd DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_registry_inf(sffp_sd_reg.inf)
add_driver_inf(sffp_sd sffp_sd.inf)
