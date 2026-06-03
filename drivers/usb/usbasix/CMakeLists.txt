add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNTDDI_VERSION=0x06010000)

add_library(usbasix MODULE
    usbasix.c
    usbasix.rc)

set_module_type(usbasix kernelmodedriver)
add_importlibs(usbasix ndis ntoskrnl hal usbd)
add_cd_file(TARGET usbasix DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(usbasix usbasix.inf)
