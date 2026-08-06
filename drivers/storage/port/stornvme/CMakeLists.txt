list(APPEND SOURCE
    core.c
    pci.c
    power.c
    queue.c
    scsi.c)

add_library(stornvme MODULE ${SOURCE} stornvme.rc)

set_module_type(stornvme kernelmodedriver)
add_importlibs(stornvme storport ntoskrnl hal)

add_cd_file(TARGET stornvme DESTINATION reactos/system32/drivers NO_CAB FOR all)
