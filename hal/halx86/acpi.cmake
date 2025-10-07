
include_directories(${HAL_I386_DIR}/include ${REACTOS_SOURCE_DIR}/drivers/bus/acpi/acpica/include)

list(APPEND HAL_ACPI_SOURCE
    ${HAL_I386_DIR}/acpi/halacpi.c
    ${HAL_I386_DIR}/acpi/halpnpdd.c
    ${HAL_I386_DIR}/acpi/busemul.c
    ${HAL_I386_DIR}/acpi/madt.c
    ${HAL_I386_DIR}/legacy/bus/pcibus.c
    ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.c
    ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.c)

# Needed to compile while using ACPICA
if(HAL_ARCH STREQUAL "amd64")
    add_definitions(-DWIN64)
endif()

add_library(lib_hal_acpi OBJECT ${HAL_ACPI_SOURCE})
add_pch(lib_hal_acpi ${HAL_I386_DIR}/include/hal.h ${HAL_ACPI_SOURCE})
add_dependencies(lib_hal_acpi bugcodes xdk)
