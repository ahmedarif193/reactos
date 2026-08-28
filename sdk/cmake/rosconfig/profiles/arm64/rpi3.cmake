# Copyright 2026 Ahmed Arif <arif193@gmail.com>

set(ROSCONFIG_PROFILE_PACKAGES
    sdbus
    usbdwc2
    smsc95xx
    cyw43455sdio
    cyw43455
    rpi3winsync)
set(ROSCONFIG_PROFILE_CONFIGS
    "RPI_SUPPORT:BOOL=ON"
    "RPI3_SUPPORT:BOOL=ON"
    "RPI5_SUPPORT:BOOL=OFF")
