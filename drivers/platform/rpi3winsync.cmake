# Copyright 2026 Ahmed Arif <arif193@gmail.com>

# rpi3winsync is an exact Windows 10 BSP source snapshot. It is a contract
# reference and is not compiled until its Windows framework dependencies have
# compatible ReactOS implementations.
set(_rpi3winsync_root "${CMAKE_CURRENT_LIST_DIR}/rpi3winsync")
set(_rpi3winsync_commit "88ee238c9debecce810d208cac1e5f36add3d2a1")

set(_rpi3winsync_required_files
    LICENSE
    drivers/audio/bcm2836/adapter.cpp
    drivers/gpio/bcm2836/BcmGpio.cpp
    drivers/i2c/bcm2836/device.cpp
    drivers/mailbox/bcm2836/mailbox.c
    drivers/misc/vchiq/slots.c
    drivers/pwm/bcm2836/pwm.cpp
    drivers/sd/bcm2836/bcm2836sdhc/bcm2836sdhc.c
    drivers/sd/bcm2836/rpisdhc/rpisdhc.cpp
    drivers/spi/bcm2836/controller.cpp
    drivers/spi/bcmauxspi/bcmauxspi.cpp
    drivers/uart/bcm2836/miniUart/pnp.c
    drivers/uart/bcm2836/serPL011/PL011device.cpp)

foreach(_rpi3winsync_file IN LISTS _rpi3winsync_required_files)
    if(NOT EXISTS "${_rpi3winsync_root}/${_rpi3winsync_file}")
        message(FATAL_ERROR
            "RPi3: rpi3winsync is incomplete; run: git submodule update --init "
            "drivers/platform/rpi3winsync")
    endif()
endforeach()

# A Git checkout can prove that the populated submodule matches the parent
# gitlink. Exported source archives have no Git metadata and use the required
# file check above.
if(EXISTS "${_rpi3winsync_root}/.git")
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${_rpi3winsync_root}" rev-parse HEAD
            OUTPUT_VARIABLE _rpi3winsync_head
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _rpi3winsync_git_result)
        if(NOT _rpi3winsync_git_result EQUAL 0 OR
           NOT _rpi3winsync_head STREQUAL _rpi3winsync_commit)
            message(FATAL_ERROR
                "RPi3: rpi3winsync must be pinned to ${_rpi3winsync_commit}")
        endif()
    endif()
endif()

add_custom_target(rpi3winsync SOURCES rpi3winsync.md)
set_target_properties(rpi3winsync PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
