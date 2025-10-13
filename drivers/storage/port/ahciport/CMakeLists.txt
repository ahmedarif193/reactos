add_definitions(-DDEBUG)

list(APPEND SOURCE
    ahciport.c)

add_library(ahciport MODULE ${SOURCE})

set_module_type(ahciport kernelmodedriver)
add_importlibs(ahciport scsiport ntoskrnl hal)
add_cd_file(TARGET ahciport DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(ahciport ahciport.inf)
