if((ARCH STREQUAL "arm64" OR ARCH STREQUAL "arm64ec") AND ENABLE_FEX_ARM64EC)
    include(fex-arm64ec.cmake)
endif()
