
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    add_subdirectory(i8042prt)
    add_subdirectory(inport)
    add_subdirectory(sermouse)
endif()
add_subdirectory(kbdclass)
add_subdirectory(mouclass)
