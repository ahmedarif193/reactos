
add_subdirectory(csr)
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" AND
   (ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64"))
    add_subdirectory(mvdm)
endif()
add_subdirectory(win)
