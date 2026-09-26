
list(APPEND HAL_LEGACY_SOURCE
    legacy/bus/bushndlr.c
    legacy/bus/cmosbus.c
    legacy/bus/isabus.c
    legacy/bus/pcibus.c
    legacy/bus/sysbus.c
    legacy/bussupp.c
    legacy/halpnpdd.c
    legacy/halpcat.c
    smp/mps/mps.c)

add_library(lib_hal_legacy OBJECT ${HAL_LEGACY_SOURCE})
add_dependencies(lib_hal_legacy bugcodes xdk)
# bussupp.c names devices from the shared PCI tables
target_include_directories(lib_hal_legacy PRIVATE $<TARGET_PROPERTY:halcommon,INTERFACE_INCLUDE_DIRECTORIES>)
add_dependencies(lib_hal_legacy halcommon)
#add_pch(lib_hal_legacy include/hal.h)
