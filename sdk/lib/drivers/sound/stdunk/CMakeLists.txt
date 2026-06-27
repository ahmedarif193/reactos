
add_library(stdunk cunknown.cpp)
add_dependencies(stdunk xdk)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(stdunk_native cunknown.cpp)
    set_arm64ec_native_target(stdunk_native)
    add_dependencies(stdunk_native xdk)
endif()
