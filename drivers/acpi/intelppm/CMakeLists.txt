list(APPEND INTELPPM_SOURCE
    intelppm.c
    device.c
    idle.c
    throttle.c
    thermal.c
    perf.c
    pnp.c)

add_library(intelppm MODULE
    ${INTELPPM_SOURCE}
    intelppm.rc)

set_module_type(intelppm kernelmodedriver)
add_importlibs(intelppm ntoskrnl hal)
add_cd_file(TARGET intelppm DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(intelppm intelppm.inf)
