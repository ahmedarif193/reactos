
if(NOT (ARCH STREQUAL "arm" OR ARCH STREQUAL "riscv64"))
    # RISC-V currently discovers its platform through the device tree. Its
    # HAL does not implement the ACPI interrupt and PCI routing interfaces.
    add_subdirectory(acpi)
endif()
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    add_subdirectory(isapnp)
    add_subdirectory(inteli2c)
endif()
add_subdirectory(pci)
add_subdirectory(pcix)
add_subdirectory(pcmcia)
add_subdirectory(reshub)
add_subdirectory(sd)
