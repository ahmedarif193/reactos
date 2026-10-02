
add_subdirectory(acpi)
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
