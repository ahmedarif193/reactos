
get_filename_component(HAL_ARM_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(HAL_ARM_ARCH_DIR "${HAL_ARM_SOURCE_DIR}/../arch/arm")
set(HAL_COMMON_DIR "${HAL_ARM_SOURCE_DIR}/../arch/common")

spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_)

include_directories(
    ${HAL_ARM_ARCH_DIR}/include
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include)

list(APPEND SOURCES
    ${HAL_COMMON_DIR}/generic/sysinfo_stubs.c
    ${HAL_ARM_ARCH_DIR}/omap3/halinit_up.c
    ${HAL_ARM_ARCH_DIR}/omap3/halup.rc)

add_library(hal MODULE
    ${SOURCES})

set_module_type(hal kerneldll ENTRYPOINT 0)
