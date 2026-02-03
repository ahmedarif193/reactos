if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    add_subdirectory(intelppm)
endif()
# TODO: arm64ppm needs ARM64-specific implementation, not x86 intelppm code
# elseif(ARCH STREQUAL "arm64")
#     add_subdirectory(arm64ppm)
add_subdirectory(cmbatt)
