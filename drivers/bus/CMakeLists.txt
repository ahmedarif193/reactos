
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64" OR ARCH STREQUAL "arm64")
    # RISC-V currently discovers its platform through the device tree. Its
    # HAL does not implement the ACPI interrupt and PCI routing interfaces.
    add_subdirectory(acpi)
endif()
# ISA PnP accesses resources through the HAL; the bus determines its presence.
add_subdirectory(isapnp)
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    add_subdirectory(inteli2c)
endif()
add_subdirectory(pci)
add_subdirectory(pcix)
add_subdirectory(pcmcia)
add_subdirectory(reshub)
add_subdirectory(sd)
