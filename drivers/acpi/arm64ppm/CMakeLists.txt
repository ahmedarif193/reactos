list(APPEND ARM64PPM_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/../intelppm/intelppm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../intelppm/device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../intelppm/idle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../intelppm/throttle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../intelppm/thermal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../intelppm/perf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../intelppm/pnp.c)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../intelppm)

add_library(arm64ppm MODULE
    ${ARM64PPM_SOURCE}
    arm64ppm.rc)

set_module_type(arm64ppm kernelmodedriver)
add_importlibs(arm64ppm ntoskrnl hal)
add_cd_file(TARGET arm64ppm DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(arm64ppm arm64ppm.inf)
