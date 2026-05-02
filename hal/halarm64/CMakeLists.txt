
get_filename_component(HAL_ARM64_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(HAL_COMMON_DIR "${REACTOS_SOURCE_DIR}/hal/arch/common")

spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

function(reactos_hal_setup TARGET ENTRYPOINT)
    set_module_type(${TARGET} kerneldll ENTRYPOINT ${ENTRYPOINT})
    target_link_libraries(${TARGET} libcntpr)
    add_importlibs(${TARGET} ntoskrnl)
    add_dependencies(${TARGET} psdk asm)
    add_cd_file(TARGET ${TARGET} DESTINATION reactos/system32 NO_CAB FOR all)
    if(NOT MSVC)
        if(TARGET libgcc)
            target_link_libraries(${TARGET} libgcc)
        else()
            target_link_libraries(${TARGET} -lgcc)
        endif()
    endif()
endfunction()

add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_)

include_directories(
    ${HAL_COMMON_DIR}/include
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include/internal
    ${REACTOS_SOURCE_DIR}/sdk/include
    ${HAL_ARM64_SOURCE_DIR}/gic
    ${CMAKE_CURRENT_BINARY_DIR})

#####################################
# Generate the pcidata source files for ARM64
#
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.c ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.h
    COMMAND native-bin2c ${HAL_COMMON_DIR}/legacy/bus/pci_classes.ids ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.c ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.h BINSTR ClassTable "DATA_SEG\(\"INITDATA\"\)" ${HAL_COMMON_DIR}/include/hal.h
    DEPENDS native-bin2c ${HAL_COMMON_DIR}/legacy/bus/pci_classes.ids
    VERBATIM)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.c ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.h
    COMMAND native-bin2c ${HAL_COMMON_DIR}/legacy/bus/pci_vendors.ids ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.c ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.h BINSTR VendorTable "DATA_SEG\(\"INITDATA\"\)" ${HAL_COMMON_DIR}/include/hal.h
    DEPENDS native-bin2c ${HAL_COMMON_DIR}/legacy/bus/pci_vendors.ids
    VERBATIM)
#####################################

#####################################
# GIC (Generic Interrupt Controller) source files
#
list(APPEND GIC_SOURCES
    ${HAL_ARM64_SOURCE_DIR}/gic/gic_common.c
    ${HAL_ARM64_SOURCE_DIR}/gic/gic_init.c
    ${HAL_ARM64_SOURCE_DIR}/gic/gic_its.c
    ${HAL_ARM64_SOURCE_DIR}/gic/gic_redist.c
    ${HAL_ARM64_SOURCE_DIR}/gic/gicv2.c
    ${HAL_ARM64_SOURCE_DIR}/gic/gicv3.c)
#####################################

list(APPEND SOURCES
    ${HAL_COMMON_DIR}/acpi/halacpi.c
    ${HAL_COMMON_DIR}/acpi/halpnpdd.c
    ${HAL_COMMON_DIR}/acpi/rsdt.c
    ${HAL_COMMON_DIR}/acpi/hpet.c
    ${HAL_COMMON_DIR}/acpi/waet.c
    ${HAL_COMMON_DIR}/acpi/arm64.c
    ${HAL_COMMON_DIR}/generic/sysinfo_stubs.c
    ${HAL_ARM64_SOURCE_DIR}/halarm64.c
    ${HAL_ARM64_SOURCE_DIR}/halarm64_acpi_int.c
    ${HAL_ARM64_SOURCE_DIR}/bcm2712_pci.c
    ${HAL_ARM64_SOURCE_DIR}/smp.c
    ${HAL_ARM64_SOURCE_DIR}/smp_trampoline.S
    ${HAL_ARM64_SOURCE_DIR}/kernstubs.c
    ${GIC_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.c
    ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.c)

add_library(hal MODULE
    ${SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/hal.def)
reactos_hal_setup(hal 0)

# Ship halmacpi.dll on ARM64 to match livecd.ini LiveCD_Macpi boot option
add_library(halmacpi MODULE
    ${SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/hal.def)
reactos_hal_setup(halmacpi 0)

# Ensure the PCI data headers are generated before compiling halacpi.c
set_source_files_properties(${HAL_COMMON_DIR}/acpi/halacpi.c PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/pci_classes.h;${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.h")

# DWARF sections are preserved directly in the binary for rossym_new to parse
