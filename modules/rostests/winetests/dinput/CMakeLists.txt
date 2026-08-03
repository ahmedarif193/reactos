add_definitions(-DUSE_WINE_TODOS -DWINETEST_USE_DBG_SPRINTF)

foreach(TEST_DRIVER driver_bus driver_hid driver_hid_poll)
    add_library(dinput_${TEST_DRIVER} MODULE ${TEST_DRIVER}.c)
    set_module_type(dinput_${TEST_DRIVER} kernelmodedriver)
    set_target_properties(dinput_${TEST_DRIVER} PROPERTIES
        OUTPUT_NAME ${TEST_DRIVER}
        PREFIX ""
        SUFFIX ".dll")
    target_compile_definitions(dinput_${TEST_DRIVER} PRIVATE __WINESRC__)
    target_include_directories(dinput_${TEST_DRIVER} BEFORE PRIVATE
        ${REACTOS_SOURCE_DIR}/sdk/include/wine)
    if(TEST_DRIVER STREQUAL "driver_bus")
        add_importlibs(dinput_${TEST_DRIVER} ntoskrnl hal)
    else()
        add_importlibs(dinput_${TEST_DRIVER} hidclass ntoskrnl hal)
    endif()
endforeach()

list(APPEND SOURCE
    device8.c
    dinput.c
    force_feedback.c
    hid.c
    hotplug.c
    joystick8.c
    testlist.c)

add_executable(dinput_winetest
    ${SOURCE}
    dinput_winetest.rc)

target_compile_definitions(dinput_winetest PRIVATE
    __WINESRC__
    WINETEST_WAIT_CHILD_PROCESS_INFO)
target_include_directories(dinput_winetest BEFORE PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include/wine)
target_link_libraries(dinput_winetest dinput_data_formats uuid)
set_module_type(dinput_winetest win32cui)
add_importlibs(dinput_winetest
    dinput
    dinput8
    ole32
    version
    user32
    advapi32
    hid
    crypt32
    newdev
    setupapi
    wintrust
    winmm
    msvcrt
    kernel32)
add_dependencies(dinput_winetest
    dinput_driver_bus
    dinput_driver_hid
    dinput_driver_hid_poll)
set_source_files_properties(dinput_winetest.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_BINARY_DIR}/driver_bus.dll;${CMAKE_CURRENT_BINARY_DIR}/driver_hid.dll;${CMAKE_CURRENT_BINARY_DIR}/driver_hid_poll.dll")
add_rostests_file(TARGET dinput_winetest)
