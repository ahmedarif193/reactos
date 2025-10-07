#####################################
# Generate the pcidata source files in the x86 build
#
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.c ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.h
    COMMAND native-bin2c ${HAL_I386_DIR}/legacy/bus/pci_classes.ids ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.c ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.h BINSTR ClassTable "DATA_SEG\(\"INITDATA\"\)" ${HAL_I386_DIR}/include/hal.h
    DEPENDS native-bin2c ${HAL_I386_DIR}/legacy/bus/pci_classes.ids
    VERBATIM)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.c ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.h
    COMMAND native-bin2c ${HAL_I386_DIR}/legacy/bus/pci_vendors.ids ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.c ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.h BINSTR VendorTable "DATA_SEG\(\"INITDATA\"\)" ${HAL_I386_DIR}/include/hal.h
    DEPENDS native-bin2c ${HAL_I386_DIR}/legacy/bus/pci_vendors.ids
    VERBATIM)
#####################################
