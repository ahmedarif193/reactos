
add_subdirectory(csr)
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_subdirectory(mvdm)
endif()
add_subdirectory(win)
