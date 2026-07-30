# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(BOOTFONT_DATA_C ${CMAKE_CURRENT_BINARY_DIR}/bootfont_data.c)
set(BOOTFONT_DATA_H ${CMAKE_CURRENT_BINARY_DIR}/bootfont_data.h)

add_custom_command(
    OUTPUT ${BOOTFONT_DATA_C} ${BOOTFONT_DATA_H}
    COMMAND native-bin2c
            ${REACTOS_SOURCE_DIR}/media/fonts/lucon.ttf
            ${BOOTFONT_DATA_C}
            ${BOOTFONT_DATA_H}
            BIN
            bootfont_data
    DEPENDS
            native-bin2c
            ${REACTOS_SOURCE_DIR}/media/fonts/lucon.ttf)

add_library(bootfont STATIC
    bootfont.c
    ${BOOTFONT_DATA_C}
    ${BOOTFONT_DATA_H})

target_include_directories(bootfont PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(bootfont PUBLIC freetype)
add_dependencies(bootfont psdk)
