
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    add_subdirectory(inport)
    add_subdirectory(sermouse)
endif()
# PS/2 controllers are platform devices; their port and interrupt accesses
# go through the HAL and do not require an x86 processor.
add_subdirectory(i8042prt)
add_subdirectory(kbdclass)
add_subdirectory(mouclass)
