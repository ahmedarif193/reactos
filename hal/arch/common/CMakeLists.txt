
# Code every HAL shares, whatever the processor architecture. A HAL links
# only the members it references: the PCI name tables serve the HALs that
# dump the PCI bus at boot.

add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_)

foreach(_table classes vendors)
    if(_table STREQUAL "classes")
        set(_symbol ClassTable)
    else()
        set(_symbol VendorTable)
    endif()
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pci_${_table}.c ${CMAKE_CURRENT_BINARY_DIR}/pci_${_table}.h
        COMMAND native-bin2c ${CMAKE_CURRENT_SOURCE_DIR}/legacy/bus/pci_${_table}.ids ${CMAKE_CURRENT_BINARY_DIR}/pci_${_table}.c ${CMAKE_CURRENT_BINARY_DIR}/pci_${_table}.h BINSTR ${_symbol} "DATA_SEG\(\"INITDATA\"\)" ${CMAKE_CURRENT_SOURCE_DIR}/include/pcidata.h
        DEPENDS native-bin2c ${CMAKE_CURRENT_SOURCE_DIR}/legacy/bus/pci_${_table}.ids
        VERBATIM)
endforeach()

list(APPEND SOURCE
    generic/display.c
    generic/memory.c
    generic/misc.c
    generic/sysinfo_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.c
    ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.c)

add_library(halcommon ${SOURCE})
add_dependencies(halcommon bugcodes xdk psdk)

# Consumers include pci_classes.h and pci_vendors.h
target_include_directories(halcommon INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
