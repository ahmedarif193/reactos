#
# ReactOS USB RNDIS Network Driver
#

add_definitions(
    -DNDIS_MINIPORT_DRIVER
    -DNDIS51_MINIPORT=1)

list(APPEND SOURCE
    usbrndis.c
    rndisusb.c
    rndisctl.c
    rndisdata.c
    rndisoid.c)

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_library(usbrndis MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    usbrndis.rc)

set_module_type(usbrndis kernelmodedriver)
target_link_libraries(usbrndis ${PSEH_LIB})
add_importlibs(usbrndis ndis ntoskrnl hal usbd)
add_pch(usbrndis usbrndis.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET usbrndis DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(usbrndis usbrndis.inf)
