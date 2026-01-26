# Enable storport-based AHCI implementation (modern driver stack)
add_definitions(-DDEBUG -DAHCI_USE_STORPORT)

list(APPEND SOURCE
    ahciport.c)

add_library(ahciport MODULE ${SOURCE})

set_module_type(ahciport kernelmodedriver)
# Use storport instead of scsiport for modern AHCI support
add_importlibs(ahciport storport ntoskrnl hal)
add_cd_file(TARGET ahciport DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(ahciport ahciport.inf)
