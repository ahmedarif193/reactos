# Copyright 2026 Ahmed Arif <arif193@gmail.com>

# rpi3winsync is an exact, directly tracked Windows 10 BSP source snapshot.
# Projects are enabled individually as their Windows framework dependencies
# gain compatible ReactOS implementations.
set(_rpi3winsync_root "${CMAKE_CURRENT_LIST_DIR}/rpi3winsync")

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
            "RPi3: the tracked rpi3winsync source snapshot is incomplete")
    endif()
endforeach()

# The mailbox, PWM, and mini-UART drivers are enabled through adjacent
# ReactOS CMakeLists.txt files.
# Keep the remaining upstream projects visible to generators and IDEs, but do
# not compile them yet. Each entry remains disabled for a concrete ReactOS
# framework dependency; adding a target before that contract exists would only
# produce an unusable binary.
set(_rpi3winsync_disabled_projects
    # Disabled: requires KMDF plus PortCls/WaveRT and mailbox/VCHIQ parity.
    drivers/audio/bcm2836/rpiwav/rpiwav.vcxproj
    # Disabled: requires KMDF and GpioClx compatibility.
    drivers/gpio/bcm2836/bcmgpio.vcxproj
    # Disabled: requires KMDF and SpbCx compatibility.
    drivers/i2c/bcm2836/bcmi2c.vcxproj
    # Disabled: requires KMDF and a validated VCHIQ user/kernel ABI.
    drivers/misc/vchiq/vchiq.vcxproj
    # Disabled: require KMDF and SDPORT parity; native sdbus backends are used.
    drivers/sd/bcm2836/bcm2836sdhc/bcm2836sdhc.vcxproj
    drivers/sd/bcm2836/rpisdhc/rpisdhc.vcxproj
    # Disabled: require KMDF and SpbCx compatibility.
    drivers/spi/bcm2836/bcmspi.vcxproj
    drivers/spi/bcmauxspi/bcmauxspi.vcxproj
    # Disabled: requires KMDF and SerCx2 compatibility.
    drivers/uart/bcm2836/serPL011/SerPL011.vcxproj)

set(_rpi3winsync_disabled_project_sources)
foreach(_rpi3winsync_project IN LISTS _rpi3winsync_disabled_projects)
    list(APPEND _rpi3winsync_disabled_project_sources
        "${_rpi3winsync_root}/${_rpi3winsync_project}")
endforeach()

add_custom_target(rpi3winsync SOURCES
    rpi3winsync.md
    ${_rpi3winsync_disabled_project_sources})
set_target_properties(rpi3winsync PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
