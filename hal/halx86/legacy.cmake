
list(APPEND HAL_LEGACY_SOURCE
    ${HAL_COMMON_DIR}/legacy/bus/bushndlr.c
    ${HAL_I386_DIR}/legacy/bus/cmosbus.c
    ${HAL_I386_DIR}/legacy/bus/isabus.c
    ${HAL_COMMON_DIR}/legacy/bus/sysbus.c
    ${HAL_COMMON_DIR}/legacy/bussupp.c
    ${HAL_COMMON_DIR}/legacy/bus/pcibus.c
    ${HAL_I386_DIR}/legacy/acpi_ecam_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.c
    ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.c
    ${HAL_I386_DIR}/legacy/halpnpdd.c
    ${HAL_I386_DIR}/legacy/halpcat.c
    ${HAL_I386_DIR}/smp/mps/mps.c)

list(APPEND HAL_LEGACY_SOURCE
    ${HAL_I386_DIR}/generic/apic_stubs.c)

add_library(lib_hal_legacy OBJECT ${HAL_LEGACY_SOURCE})
add_dependencies(lib_hal_legacy bugcodes xdk)
#add_pch(lib_hal_legacy include/hal.h)
