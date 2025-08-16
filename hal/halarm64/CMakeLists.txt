# ARM64 HAL

add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_
    -D_KERNEL_MODE)

include_directories(
    include
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include)

# Build the generic HAL library
add_subdirectory(generic)

# Create the HAL module
spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

add_library(hal MODULE
    $<TARGET_OBJECTS:generic_arm64>
    hal_arm64.rc
    ${CMAKE_CURRENT_BINARY_DIR}/hal.def)

target_link_libraries(hal libcntpr)
add_importlibs(hal ntoskrnl)
add_dependencies(hal psdk asm)
set_module_type(hal kerneldll ENTRYPOINT HalInitSystem 8)
add_cd_file(TARGET hal DESTINATION reactos/system32 NO_CAB FOR all)

# ARM64 compiler runtime will be linked by the toolchain if needed