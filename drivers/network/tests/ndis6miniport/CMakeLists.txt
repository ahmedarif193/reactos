
add_definitions(
    -DNDIS620
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNTDDI_VERSION=0x06010000)

list(APPEND SOURCE
    ndis6miniport.c
    ${CMAKE_CURRENT_BINARY_DIR}/ndis6miniport.def)

spec2def(ndis6miniport.sys ndis6miniport.spec)

add_library(ndis6miniport MODULE ${SOURCE})
set_module_type(ndis6miniport kernelmodedriver)
add_importlibs(ndis6miniport ntoskrnl ndis hal)
add_cd_file(TARGET ndis6miniport DESTINATION reactos/system32/drivers FOR all)
