
add_subdirectory(csr)
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
add_subdirectory(mvdm)
endif()
add_subdirectory(win)
