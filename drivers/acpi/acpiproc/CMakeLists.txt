list(APPEND ACPIPROC_SOURCE
    acpiproc.c
    device.c
    idle.c
    throttle.c
    thermal.c
    perf.c
    pnp.c)

add_library(acpiproc MODULE
    ${ACPIPROC_SOURCE}
    acpiproc.rc)

set_module_type(acpiproc kernelmodedriver)
add_importlibs(acpiproc ntoskrnl hal)
add_cd_file(TARGET acpiproc DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(acpiproc acpiproc.inf)
