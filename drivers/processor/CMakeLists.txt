
add_subdirectory(processr)
add_subdirectory(pnpcpu)

if(ARCH STREQUAL "arm64")
    add_subdirectory(acpipagr)
endif()
