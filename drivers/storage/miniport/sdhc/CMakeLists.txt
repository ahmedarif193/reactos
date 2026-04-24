
add_library(sdhc MODULE
    sdhc.c
    sdhc.rc)

set_module_type(sdhc kernelmodedriver)
add_importlibs(sdhc sdport ntoskrnl hal)
add_cd_file(TARGET sdhc DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_registry_inf(sdhc_reg.inf)
