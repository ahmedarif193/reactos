
get_filename_component(HAL_ARM64_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

function(reactos_hal_setup TARGET ENTRYPOINT)
    set_module_type(${TARGET} kerneldll ENTRYPOINT ${ENTRYPOINT})
    target_link_libraries(${TARGET} libcntpr)
    add_importlibs(${TARGET} ntoskrnl)
    add_dependencies(${TARGET} psdk asm)
    add_cd_file(TARGET ${TARGET} DESTINATION reactos/system32 NO_CAB FOR all)
    if(NOT MSVC)
        if(TARGET libgcc)
            target_link_libraries(${TARGET} libgcc)
        else()
            target_link_libraries(${TARGET} -lgcc)
        endif()
    endif()
endfunction()

add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_)

include_directories(
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include/internal
    ${REACTOS_SOURCE_DIR}/sdk/include)

list(APPEND SOURCES
    ${HAL_ARM64_SOURCE_DIR}/halarm64.c)

add_library(hal MODULE
    ${SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/hal.def)
reactos_hal_setup(hal 0)
