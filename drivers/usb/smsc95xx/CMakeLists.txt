add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNTDDI_VERSION=0x06010000)

add_library(smsc95xx MODULE
    smsc95xx.c
    smsc95xx.rc)

set_module_type(smsc95xx kernelmodedriver)
add_importlibs(smsc95xx ndis ntoskrnl hal usbd)
add_cd_file(TARGET smsc95xx DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(smsc95xx smsc95xx.inf)
