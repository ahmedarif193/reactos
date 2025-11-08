
get_filename_component(HAL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

set(HAL_ARCH "${ARCH}" CACHE STRING "HAL architecture (i386|amd64|arm64)")
set(HAL_ARCH_DIR "${HAL_SOURCE_DIR}/../arch/${HAL_ARCH}")
set(HAL_I386_DIR "${HAL_SOURCE_DIR}/../arch/i386")
set(HAL_AMD64_DIR "${HAL_SOURCE_DIR}/../arch/amd64")

add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_)

include_directories(
    ${HAL_ARCH_DIR}/include
    ${HAL_I386_DIR}/include
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/fast486)

function(add_hal _halname)
    cmake_parse_arguments(_haldata "" "" "SOURCES;COMPONENTS" ${ARGN})

    # Handle the spec file for the dll name
    spec2def(${_halname}.dll ../hal.spec ADD_IMPORTLIB)

    # Create the actual target
    foreach(_component ${_haldata_COMPONENTS})
        list(APPEND _haldata_SOURCES "$<TARGET_OBJECTS:lib_hal_${_component}>")
    endforeach()
    add_library(${_halname} MODULE
        ${_haldata_SOURCES}
        ${CMAKE_CURRENT_BINARY_DIR}/hal.def)

    # Stricter warnings for all builds (GCC/Clang only)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${_halname} PRIVATE
            -UNDEBUG
            $<$<COMPILE_LANGUAGE:C>:
                -Wextra -Wformat -Wformat-security
                -Wcast-align -Wpointer-arith -Wvla
                
                -Wsign-compare -Wimplicit-fallthrough>
            $<$<COMPILE_LANGUAGE:CXX>:
                -Wextra -Wformat -Wformat-security -Wpointer-arith>)
    endif()

    # Link common support libraries. Add 'setjmp' to provide _setjmp/_setjmp3 for x86 BIOS paths.
    if(${_halname} STREQUAL "hal")
        target_link_libraries(${_halname} libcntpr arbiter fast486 setjmp)
    else()
        target_link_libraries(${_halname} libcntpr fast486 setjmp)
    endif()

    add_importlibs(${_halname} ntoskrnl)
    #add_pch(${_halname} include/hal.h)
    add_dependencies(${_halname} psdk asm)
    set_module_type(${_halname} kerneldll ENTRYPOINT HalInitSystem 8)
    add_cd_file(TARGET ${_halname} DESTINATION reactos/system32 NO_CAB FOR all)
    if(MSVC)
        target_link_options(${_halname} PRIVATE /ignore:4216 /SECTION:INIT,ERWD)
    else()
        target_link_libraries(${_halname} libgcc)
    endif()
endfunction()

# The components
include(generic.cmake)
include(acpi.cmake)
include(apic.cmake)
include(up.cmake)
include(smp.cmake)

if(HAL_ARCH STREQUAL "i386")
    include(pcidata.cmake)
    include(legacy.cmake)
    include(pic.cmake)
    include(xbox.cmake)
    include(pc98.cmake)
    add_subdirectory(${HAL_I386_DIR}/minihal ${CMAKE_CURRENT_BINARY_DIR}/minihal)

    remove_definitions(-DSARCH_XBOX)
    remove_definitions(-DSARCH_PC98)

    # hal
    add_hal(hal SOURCES ${HAL_I386_DIR}/pic/halpic.rc COMPONENTS generic legacy up pic)
    add_hal(halacpi SOURCES ${HAL_I386_DIR}/acpi/halacpi.rc COMPONENTS generic acpi up pic)
    add_hal(halaacpi SOURCES ${HAL_I386_DIR}/apic/halaacpi.rc COMPONENTS generic acpi up apic)
    add_hal(halapic SOURCES ${HAL_I386_DIR}/apic/halapic.rc COMPONENTS generic legacy up apic)
    add_hal(halxbox SOURCES ${HAL_I386_DIR}/xbox/halxbox.rc COMPONENTS xbox up)
    add_hal(halpc98 SOURCES ${HAL_I386_DIR}/pc98/halpc98.rc COMPONENTS pc98 up)

    add_hal(halmacpi SOURCES ${HAL_I386_DIR}/smp/halmacpi.rc COMPONENTS generic acpi smp apic)
    #add_hal(halmp SOURCES mp/halmp.rc COMPONENTS generic legacy smp apic)

elseif(HAL_ARCH STREQUAL "amd64")
    include(pcidata.cmake)

    add_hal(hal SOURCES ${HAL_SOURCE} COMPONENTS generic acpi up apic)
    add_hal(halmp SOURCES ${HAL_SOURCE} COMPONENTS generic acpi smp apic)
    # Ship halmacpi.dll on amd64 as well to keep feature parity with the i386 build
    add_hal(halmacpi SOURCES ${HAL_I386_DIR}/smp/halmacpi.rc COMPONENTS generic acpi smp apic)

endif()
