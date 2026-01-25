#
# ReactOS USB RNDIS Network Driver
#
# Using NDIS 6.20 with NET_BUFFER_LIST send/receive.
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER)

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

# Suppress warnings during development
target_compile_options(usbrndis PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable)

add_importlibs(usbrndis ndis ntoskrnl hal usbd)
add_pch(usbrndis usbrndis.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET usbrndis DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(usbrndis usbrndis.inf)
