set(FEX_ARM64EC_AVAILABLE OFF)
if(ARCH STREQUAL "arm64" AND ENABLE_FEX_ARM64EC)
    include(fex-arm64ec.cmake)
endif()
set(FEX_ARM64EC_AVAILABLE "${FEX_ARM64EC_AVAILABLE}" PARENT_SCOPE)

if(ENABLE_MESA_LLVMPIPE)
    include(mesa-llvmpipe.cmake)
endif()
