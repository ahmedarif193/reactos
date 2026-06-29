
include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers)

list(APPEND SOURCE
    sdbus.c
    hardware.c
    fdo.c
    pdo.c
    enum.c
    hotplug.c
    request.c
    interface.c
    power.c
    sdio.c
    emmc.c
    hardware.h
    sdbus.h)

add_library(sdbus MODULE ${SOURCE} sdbus.rc)
set_module_type(sdbus kernelmodedriver)
add_importlibs(sdbus ntoskrnl hal)
add_pch(sdbus sdbus.h SOURCE)
add_cd_file(TARGET sdbus DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_registry_inf(sdbus_reg.inf)
add_driver_inf(sdbus sdbus.inf)
